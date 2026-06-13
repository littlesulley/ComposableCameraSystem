// Copyright 2026 Sulley. All Rights Reserved.
//
// Console commands that dump the current CCS state as plain text: to the log
// (at Display verbosity, so it shows up without raising LogComposableCameraSystem
// verbosity) AND to the system clipboard. Bug reports become paste-ins rather
// than screenshots, and two timestamped dumps can be diffed to see what changed.
//
// All three commands consume the same `BuildDebugSnapshot` pipeline that backs
// the 2D panel and `showdebug camera`, so they stay in lockstep with the rest
// of the debug surface for free. Formatting reuses
// `ComposableCameraDebug::AppendTreeNodeLine` + the typed-value appenders.
//
// Shipping builds strip the whole translation unit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "Misc/StringBuilder.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Patches/ComposableCameraPatchManager.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "UObject/UObjectIterator.h"
#include "Utils/ComposableCameraDebugFormatUtils.h"

namespace
{
	// --- PCM discovery -----------------------------------------------
	// Mirror the viewport-debug ticker's resolution order. The command-
	// context World (when the console is invoked from PIE) is preferred;
	// fall back to scanning GEngine's world contexts for the first
	// Game/PIE world with a CCS PCM. Editor-world invocations (main
	// viewport without PIE running) return nullptr and the command
	// emits a warning.
	static AComposableCameraPlayerCameraManager* ResolveDumpCommandPCM(UWorld* World)
	{
		auto TryWorld = [](UWorld* W) ->AComposableCameraPlayerCameraManager*
		{
			if (!W) { return nullptr; }
			APlayerController* PC = W->GetFirstPlayerController();
			if (!PC) { return nullptr; }
			return Cast<AComposableCameraPlayerCameraManager>(PC->PlayerCameraManager);
		};

		if (auto* PCM = TryWorld(World))
		{
			return PCM;
		}

		if (!GEngine) { return nullptr; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) { continue; }
			if (auto* PCM = TryWorld(Ctx.World()))
			{
				return PCM;
			}
		}
		return nullptr;
	}

	// Single writeback point: log at Display + copy to clipboard. Log is
	// kept at Display (not Verbose / Log) so the dump appears in the user's
	// Output Log by default, regardless of LogComposableCameraSystem's
	// verbosity setting. The second UE_LOG line reports the clipboard copy
	// so the user knows the paste is ready.
	static void LogAndClipboard(const FString& Text, const TCHAR* CommandName)
	{
		UE_LOG(LogComposableCameraSystem, Display, TEXT("%s:\n%s"), CommandName, *Text);
		FPlatformApplicationMisc::ClipboardCopy(*Text);
		UE_LOG(LogComposableCameraSystem, Display,
			TEXT("%s: %d chars copied to clipboard."), CommandName, Text.Len());
	}

	// --- Snapshot ->text helpers -------------------------------------

	// Compact phase glyph for the in-line dump rows.
	static const TCHAR* PhaseShortName(int8 Phase)
	{
		switch (Phase)
		{
		case 0: return TEXT("Entering");
		case 1: return TEXT("Active  ");
		case 2: return TEXT("Exiting ");
		case 3: return TEXT("Expired ");
		default: return TEXT("???     ");
		}
	}

	// Compact bitmask glyph: D = Duration, M = Manual, C = Condition.
	// Returns up to 3 chars + null. "-" for empty mask.
	static FString ExpirationMaskGlyph(uint8 Mask)
	{
		FString Out;
		if (Mask & static_cast<uint8>(EComposableCameraPatchExpirationType::Duration))  Out += TEXT("D");
		if (Mask & static_cast<uint8>(EComposableCameraPatchExpirationType::Manual))    Out += TEXT("M");
		if (Mask & static_cast<uint8>(EComposableCameraPatchExpirationType::Condition)) Out += TEXT("C");
		return Out.IsEmpty() ? FString(TEXT("-")) : Out;
	}

	// One row per Patch. Caller supplies the column-0 indent so this can be reused
	// from CCS.Dump.Patches (no indent), CCS.Dump.Tree (small indent), and
	// CCS.Dump.Stack (deeper indent for per-context sub-tree alignment).
	static void AppendPatchesDump(
		FStringBuilderBase& B,
		const TArray<FComposableCameraPatchSnapshot>& Patches,
		int32 IndentSpaces)
	{
		FString Indent;
		Indent.Reserve(IndentSpaces);
		for (int32 i = 0; i < IndentSpaces; ++i) Indent += TEXT(' ');

		if (Patches.Num() == 0)
		{
			B.Appendf(TEXT("%sPatches: (none)\n"), *Indent);
			return;
		}
		B.Appendf(TEXT("%sPatches (%d):\n"), *Indent, Patches.Num());
		for (const FComposableCameraPatchSnapshot& P : Patches)
		{
			// Phase-specific timing column: Entering/Exiting show ramp progress;
			// Active shows ElapsedTimeActive vs Duration when Duration channel is on.
			FString Timing;
			if (P.Phase == 0 && P.EnterDuration > 0.f) // Entering
			{
				Timing = FString::Printf(TEXT("enter %.2f/%.2fs"), P.ElapsedInPhase, P.EnterDuration);
			}
			else if (P.Phase == 2 && P.ExitDuration > 0.f) // Exiting
			{
				Timing = FString::Printf(TEXT("exit  %.2f/%.2fs"), P.ElapsedInPhase, P.ExitDuration);
			}
			else if (P.Phase == 1 && (P.ExpirationType & static_cast<uint8>(EComposableCameraPatchExpirationType::Duration)) && P.Duration > 0.f)
			{
				Timing = FString::Printf(TEXT("active %.2f/%.2fs"), P.ElapsedTimeActive, P.Duration);
			}
			else if (P.Phase == 1)
			{
				Timing = FString::Printf(TEXT("active %.2fs"), P.ElapsedTimeActive);
			}

			B.Appendf(
				TEXT("%s  [layer=%d] %s%-24s | %s | a=%.2f | %s | exp=%s%s%s\n"),
				*Indent,
				P.LayerIndex,
				P.Source == EComposableCameraPatchSource::Sequencer ? TEXT("[Seq] ") : TEXT(""),
				*P.AssetName,
				PhaseShortName(P.Phase),
				P.Alpha,
				*Timing,
				*ExpirationMaskGlyph(P.ExpirationType),
				P.bExpireOnCameraChange ? TEXT("+CamChange") : TEXT(""),
				P.Source == EComposableCameraPatchSource::Sequencer && !P.HostActorName.IsEmpty()
					? *FString::Printf(TEXT(" on %s"), *P.HostActorName)
					: TEXT(""));
		}
	}

	static void AppendStackDump(FStringBuilderBase& B, const UComposableCameraContextStack* Stack)
	{
		FComposableCameraContextStackSnapshot Snap;
		Stack->BuildDebugSnapshot(Snap);

		B.Appendf(TEXT("Context Stack (depth: %d, pending destroy: %d)\n"),
			Snap.LiveStackDepth, Snap.PendingDestroyCount);

		// Walk live contexts first (top->base; BuildDebugSnapshot emits
		// them in that order), then pending-destroy entries. LiveIdx
		// decrements as we walk live entries so the numeric labels mirror
		// the PCM DisplayDebug output.
		int32 LiveIdx = Snap.LiveStackDepth - 1;
		for (const FComposableCameraContextSnapshot& Ctx : Snap.Contexts)
		{
			if (Ctx.bIsPendingDestroy)
			{
				B.Appendf(TEXT("   [pending] %s\n"), *Ctx.ContextName.ToString());
				continue;
			}

			B.Appendf(TEXT("%s[%d] %s%s\n"),
				Ctx.bIsActive ? TEXT("-> ") : TEXT("   "),
				LiveIdx--,
				*Ctx.ContextName.ToString(),
				Ctx.bIsBase ? TEXT(" [base]") : TEXT(""));
			B.Appendf(TEXT("      Running Camera: %s\n"), *Ctx.RunningCameraDisplay);
			B.Appendf(TEXT("      Last Pose: %s  Rot: %s  FOV: %.1f\n"),
				*Ctx.LastPose.Position.ToCompactString(),
				*Ctx.LastPose.Rotation.ToCompactString(),
				Ctx.LastPose.GetEffectiveFieldOfView());

			if (Ctx.TreeNodes.Num() == 0)
			{
				B.Append(TEXT("      Evaluation Tree: (empty)\n"));
			}
			else
			{
				B.Append(TEXT("      Evaluation Tree:\n"));
				for (const FComposableCameraTreeNodeSnapshot& TN : Ctx.TreeNodes)
				{
					ComposableCameraDebug::AppendTreeNodeLine(B, TN, /*BaseIndentCols=*/8);
					B.AppendChar(TEXT('\n'));
				}
			}

			AppendPatchesDump(B, Ctx.Patches, /*IndentSpaces=*/6);
		}
	}

	static void AppendTreeDump(FStringBuilderBase& B, const UComposableCameraDirector* Director)
	{
		FComposableCameraContextSnapshot Ctx;
		Director->BuildDebugSnapshot(Ctx);

		B.Appendf(TEXT("Running Camera: %s\n"), *Ctx.RunningCameraDisplay);
		B.Appendf(TEXT("Last Pose: %s  Rot: %s  FOV: %.1f\n"),
			*Ctx.LastPose.Position.ToCompactString(),
			*Ctx.LastPose.Rotation.ToCompactString(),
			Ctx.LastPose.GetEffectiveFieldOfView());

		if (Ctx.TreeNodes.Num() == 0)
		{
			B.Append(TEXT("Evaluation Tree: (empty)\n"));
		}
		else
		{
			B.Append(TEXT("Evaluation Tree:\n"));
			for (const FComposableCameraTreeNodeSnapshot& TN : Ctx.TreeNodes)
			{
				ComposableCameraDebug::AppendTreeNodeLine(B, TN, /*BaseIndentCols=*/2);
				B.AppendChar(TEXT('\n'));
			}
		}

		AppendPatchesDump(B, Ctx.Patches, /*IndentSpaces=*/0);
	}

	static void AppendCameraDump(FStringBuilderBase& B, AComposableCameraCameraBase* Camera)
	{
		B.Appendf(TEXT("Camera: %s\n"), *Camera->GetName());
		B.Appendf(TEXT("  Class: %s\n"), *Camera->GetClass()->GetName());
		B.Appendf(TEXT("  Tag:   %s\n"), *Camera->CameraTag.ToString());
		B.Appendf(TEXT("  Pose:  pos=%s  rot=%s  fov=%.1f\n"),
			*Camera->CameraPose.Position.ToCompactString(),
			*Camera->CameraPose.Rotation.ToCompactString(),
			Camera->CameraPose.GetEffectiveFieldOfView());

		if (Camera->IsTransient())
		{
			B.Appendf(TEXT("  Lifetime: %.1f / %.1fs\n"),
				Camera->GetLifeTime() - Camera->GetRemainingLifeTime(),
				Camera->GetLifeTime());
		}

		// Nodes with per-pin output values. Use the same exec-chain walk
		// the panel uses so step numbers reflect actual tick order.
		const FComposableCameraRuntimeDataBlock* DB = Camera->OwnedRuntimeDataBlock.Get();
		const bool bHasDB = DB && DB->IsValid();

		auto EmitNode = [&](int32 Step, int32 NodeArrayIdx, const UComposableCameraCameraNodeBase* Node)
		{
			B.Appendf(TEXT("  [%d] %s\n"), Step, *Node->GetClass()->GetName());
			if (!bHasDB) { return; }

			TArray<FComposableCameraNodePinDeclaration> Pins;
			const_cast<UComposableCameraCameraNodeBase*>(Node)->GatherAllPinDeclarations(Pins);
			for (const FComposableCameraNodePinDeclaration& Pin : Pins)
			{
				if (Pin.Direction != EComposableCameraPinDirection::Output) { continue; }
				B.Appendf(TEXT("         %-20s = "), *Pin.PinName.ToString());
				ComposableCameraDebug::AppendOutputPinValue(
					B, *DB, NodeArrayIdx, Pin.PinName, Pin.PinType, Pin.EnumType);
				B.AppendChar(TEXT('\n'));
			}
		};

		if (Camera->FullExecChain.Num() > 0)
		{
			B.Appendf(TEXT("  Nodes (%d, exec order):\n"), Camera->FullExecChain.Num());
			int32 Step = 0;
			for (const FComposableCameraExecEntry& Entry : Camera->FullExecChain)
			{
				switch (Entry.EntryType)
				{
				case EComposableCameraExecEntryType::CameraNode:
				{
					if (!Camera->CameraNodes.IsValidIndex(Entry.CameraNodeIndex)) { break; }
					const UComposableCameraCameraNodeBase* Node = Camera->CameraNodes[Entry.CameraNodeIndex];
					if (!Node) { break; }
					EmitNode(++Step, Entry.CameraNodeIndex, Node);
					break;
				}
				case EComposableCameraExecEntryType::SetVariable:
					B.Appendf(TEXT("       -> set %s = <%s>\n"),
						*Entry.VariableName.ToString(), *Entry.SourcePinName.ToString());
					break;
				}
			}
		}
		else
		{
			B.Append(TEXT("  Nodes (array order):\n"));
			int32 Step = 0;
			for (int32 i = 0; i < Camera->CameraNodes.Num(); ++i)
			{
				const UComposableCameraCameraNodeBase* Node = Camera->CameraNodes[i];
				if (!Node) { continue; }
				EmitNode(++Step, i, Node);
			}
		}

		// Parameters + variables (from the runtime data block).
		const UComposableCameraTypeAsset* TypeAsset = Camera->SourceTypeAsset.Get();
		if (TypeAsset && bHasDB)
		{
			const TArray<FComposableCameraExposedParameter>& Params = TypeAsset->GetExposedParameters();
			if (Params.Num() > 0)
			{
				B.Appendf(TEXT("  Parameters (%d):\n"), Params.Num());
				for (const FComposableCameraExposedParameter& P : Params)
				{
					B.Appendf(TEXT("    %-24s = "), *P.ParameterName.ToString());
					if (const int32* Offset = DB->ExposedParameterOffsets.Find(P.ParameterName))
					{
						ComposableCameraDebug::AppendTypedValue(B, *DB, *Offset, P.PinType, P.EnumType);
					}
					else
					{
						B.Append(TEXT("(unresolved)"));
					}
					B.AppendChar(TEXT('\n'));
				}
			}

			if (DB->InternalVariableOffsets.Num() > 0)
			{
				// Build pin-type lookup for the variable slots (same pattern
				// the panel + DisplayDebug use).
				struct FVarTypeInfo { EComposableCameraPinType PinType; const UEnum* EnumType; };
				TMap<FName, FVarTypeInfo> VarTypes;
				VarTypes.Reserve(TypeAsset->InternalVariables.Num() + TypeAsset->ExposedVariables.Num());
				for (const FComposableCameraInternalVariable& Var : TypeAsset->InternalVariables)
				{
					VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
				}
				for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
				{
					VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
				}

				B.Appendf(TEXT("  Variables (%d):\n"), DB->InternalVariableOffsets.Num());
				for (const auto& Pair : DB->InternalVariableOffsets)
				{
					EComposableCameraPinType PinType = EComposableCameraPinType::Float;
					const UEnum* EnumType = nullptr;
					if (const FVarTypeInfo* Found = VarTypes.Find(Pair.Key))
					{
						PinType  = Found->PinType;
						EnumType = Found->EnumType;
					}
					B.Appendf(TEXT("    %-24s = "), *Pair.Key.ToString());
					ComposableCameraDebug::AppendTypedValue(B, *DB, Pair.Value, PinType, EnumType);
					B.AppendChar(TEXT('\n'));
				}
			}
		}

		// Data block memory summary (matches the panel's subsection).
		if (bHasDB)
		{
			B.Appendf(TEXT("  Data Block: %d B (%d pins, %d params, %d vars, %d defaults)\n"),
				DB->Storage.Num(),
				DB->OutputPinOffsets.Num(),
				DB->ExposedParameterOffsets.Num(),
				DB->InternalVariableOffsets.Num(),
				DB->DefaultValueOffsets.Num());
		}
	}

	// --- Command implementations -------------------------------------

	static void CmdDumpStack(const TArray<FString>& /*Args*/, UWorld* World)
	{
		AComposableCameraPlayerCameraManager* PCM = ResolveDumpCommandPCM(World);
		if (!PCM || !PCM->GetContextStack())
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("CCS.Dump.Stack: no CCS PCM found (is PIE running?)"));
			return;
		}
		TStringBuilder<2048> B;
		AppendStackDump(B, PCM->GetContextStack());
		LogAndClipboard(FString(B), TEXT("CCS.Dump.Stack"));
	}

	static void CmdDumpTree(const TArray<FString>& /*Args*/, UWorld* World)
	{
		AComposableCameraPlayerCameraManager* PCM = ResolveDumpCommandPCM(World);
		if (!PCM) { UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("CCS.Dump.Tree: no CCS PCM found.")); return; }

		const UComposableCameraContextStack* Stack = PCM->GetContextStack();
		UComposableCameraDirector* Director = Stack ? Stack->GetActiveDirector() : nullptr;
		if (!Director)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("CCS.Dump.Tree: no active director (context stack empty?)"));
			return;
		}
		TStringBuilder<2048> B;
		AppendTreeDump(B, Director);
		LogAndClipboard(FString(B), TEXT("CCS.Dump.Tree"));
	}

	// `CCS.Dump.Patches`
	//   No args. Dumps active patches from BOTH paths into one merged list:
	//   - BP path  : active context's PatchManager (one row per BP-driven patch).
	//   - Sequencer: every UComposableCameraLevelSequenceComponent's overlay map
	//                (one row per Sequencer-driven section overlay, marked [Seq]
	//                in the dump output).
	static void CmdDumpPatches(const TArray<FString>& /*Args*/, UWorld* World)
	{
		AComposableCameraPlayerCameraManager* PCM = ResolveDumpCommandPCM(World);
		if (!PCM) { UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("CCS.Dump.Patches: no CCS PCM found.")); return; }

		TArray<FComposableCameraPatchSnapshot> Patches;

		// Source 1: PatchManager (BP path).
		const UComposableCameraContextStack* Stack = PCM->GetContextStack();
		UComposableCameraDirector* Director = Stack ? Stack->GetActiveDirector() : nullptr;
		if (const UComposableCameraPatchManager* Manager = Director ? Director->GetPatchManager() : nullptr)
		{
			Manager->BuildDebugSnapshot(Patches);
		}

		// Source 2: LS Component overlays (Sequencer path). Walk every LS Component
		// in the same world and merge in. Mirrors the panel's BuildPatchesLines
		// logic so the dump and the on-screen panel always agree on what's active.
		if (UWorld* CmdWorld = PCM->GetWorld())
		{
			for (TObjectIterator<UComposableCameraLevelSequenceComponent> It; It; ++It)
			{
				UComposableCameraLevelSequenceComponent* LSComp = *It;
				if (!LSComp || !IsValid(LSComp) || LSComp->GetWorld() != CmdWorld)
				{
					continue;
				}
				LSComp->BuildSequencerPatchSnapshot(Patches);
			}
		}

		TStringBuilder<2048> B;
		AppendPatchesDump(B, Patches, /*IndentSpaces=*/0);
		LogAndClipboard(FString(B), TEXT("CCS.Dump.Patches"));
	}

	// `CCS.Dump.Camera [tag]`
	//   No arg ->active context's RunningCamera.
	//   With arg ->scan each live context's RunningCamera for a CameraTag
	//              whose string matches the arg; first match wins.
	// Source-side cameras in mid-transition aren't searched (they live
	// inside eval-tree leaves, not on `Director::RunningCamera`). That's
	// acceptable for the diagnostic use case -"what's this camera doing
	// right now" is almost always about the top/target side of any blend.
	static void CmdDumpCamera(const TArray<FString>& Args, UWorld* World)
	{
		AComposableCameraPlayerCameraManager* PCM = ResolveDumpCommandPCM(World);
		if (!PCM) { UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("CCS.Dump.Camera: no CCS PCM found.")); return; }

		const UComposableCameraContextStack* Stack = PCM->GetContextStack();
		if (!Stack) { UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("CCS.Dump.Camera: no context stack.")); return; }

		AComposableCameraCameraBase* Camera = nullptr;
		if (Args.Num() == 0)
		{
			Camera = Stack->GetRunningCamera();
		}
		else
		{
			const FString& TargetTag = Args[0];
			FComposableCameraContextStackSnapshot Snap;
			Stack->BuildDebugSnapshot(Snap);
			for (const FComposableCameraContextSnapshot& Ctx : Snap.Contexts)
			{
				if (Ctx.bIsPendingDestroy) { continue; }
				UComposableCameraDirector* Director = Stack->GetDirectorForContext(Ctx.ContextName);
				if (!Director) { continue; }
				AComposableCameraCameraBase* Candidate = Director->GetRunningCamera();
				if (!IsValid(Candidate)) { continue; }
				if (Candidate->CameraTag.ToString().Equals(TargetTag, ESearchCase::IgnoreCase))
				{
					Camera = Candidate;
					break;
				}
			}
		}

		if (!Camera)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("CCS.Dump.Camera: no matching camera (%s)"),
				Args.Num() == 0 ? TEXT("active context has no running camera") : *Args[0]);
			return;
		}

		TStringBuilder<4096> B;
		AppendCameraDump(B, Camera);
		LogAndClipboard(FString(B), TEXT("CCS.Dump.Camera"));
	}

	// --- Registration -----------------------------------------------
	// FAutoConsoleCommand*WithWorld constructors register at static init
	// time and deregister at shutdown. Plain function pointers are OK; no
	// per-frame cost when the command isn't invoked.

	static FAutoConsoleCommandWithWorldAndArgs GCmdDumpStack(
		TEXT("CCS.Dump.Stack"),
		TEXT("Print the full CCS context stack (depth, each context's running camera, last pose, evaluation tree) to LogComposableCameraSystem at Display and copy the same text to the system clipboard. Bug-report-friendly plain text; no arguments."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdDumpStack));

	static FAutoConsoleCommandWithWorldAndArgs GCmdDumpTree(
		TEXT("CCS.Dump.Tree"),
		TEXT("Print the active context's evaluation tree (running camera, last pose, flattened tree nodes with transition progress) to LogComposableCameraSystem at Display and copy to clipboard. No arguments."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdDumpTree));

	static FAutoConsoleCommandWithWorldAndArgs GCmdDumpCamera(
		TEXT("CCS.Dump.Camera"),
		TEXT("Print a camera's full state (nodes, per-pin output values, exposed parameters, internal variables, data block summary) to LogComposableCameraSystem at Display and copy to clipboard.\n")
		TEXT("Usage: CCS.Dump.Camera             . Dumps the active context's running camera\n")
		TEXT("       CCS.Dump.Camera <CameraTag> . Scans each context's running camera for a matching tag (case-insensitive, first match)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdDumpCamera));

	static FAutoConsoleCommandWithWorldAndArgs GCmdDumpPatches(
		TEXT("CCS.Dump.Patches"),
		TEXT("Print the active context's PatchManager state (one row per active Camera Patch with layer / phase / alpha / timing / expiration channels) to LogComposableCameraSystem at Display and copy to clipboard. No arguments."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdDumpPatches));

} // namespace

#endif // !UE_BUILD_SHIPPING
