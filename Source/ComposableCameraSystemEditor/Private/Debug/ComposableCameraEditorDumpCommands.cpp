// Copyright 2026 Sulley. All Rights Reserved.
//
// Editor-side companion to the runtime CCS.Dump.* family
// (see ComposableCameraSystem/Private/Debug/ComposableCameraDebugDumpCommands.cpp).
//
// Adds a single console command, CCS.Editor.Dump.Graph, that serialises every
// currently-open Camera Type Asset editor's graph data (node templates,
// compute templates, pin connections, execution chains, exposed parameters,
// internal and exposed variables, variable-node records, default transitions)
// to LogComposableCameraSystemEditor at Display and also to the system
// clipboard. Matches the bug-report paste-in workflow the runtime dump
// commands enabled: instead of sending over a.uasset to reproduce a
// "graph saved wrong" report, the user copies the dump and pastes it into
// the ticket.
//
// Shipping builds strip the whole translation unit.

#if !UE_BUILD_SHIPPING

#include "ComposableCameraSystemEditorModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "Transitions/ComposableCameraTransitionBase.h"

#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/StringBuilder.h"
#include "Subsystems/AssetEditorSubsystem.h"

namespace
{
	// --- Output plumbing -------------------------------------------------
	//
	// Identical shape to the runtime LogAndClipboard so the user's mental
	// model is "same command family, editor namespace". Display verbosity
	// means the output appears in the user's Output Log by default regardless
	// of category verbosity overrides.

	static void LogAndClipboard(const FString& Text, const TCHAR* CommandName)
	{
		UE_LOG(LogComposableCameraSystemEditor, Display, TEXT("%s:\n%s"),
			CommandName, *Text);
		FPlatformApplicationMisc::ClipboardCopy(*Text);
		UE_LOG(LogComposableCameraSystemEditor, Display,
			TEXT("%s: %d chars copied to clipboard."), CommandName, Text.Len());
	}

	// --- Enum -> text helpers --------------------------------------------

	static const TCHAR* BuildStatusToString(EComposableCameraBuildStatus Status)
	{
		switch (Status)
		{
		case EComposableCameraBuildStatus::NotBuilt: return TEXT("NotBuilt");
		case EComposableCameraBuildStatus::Success: return TEXT("Success");
		case EComposableCameraBuildStatus::SuccessWithWarnings: return TEXT("SuccessWithWarnings");
		case EComposableCameraBuildStatus::Failed: return TEXT("Failed");
		}
		return TEXT("Unknown");
	}

	static FString NodeClassName(const UObject* Node)
	{
		return Node ? Node->GetClass()->GetName() : FString(TEXT("(null)"));
	}

	// --- Appenders -------------------------------------------------------

	static void AppendPosition(FStringBuilderBase& B, const FVector2D& Pos)
	{
		B.Appendf(TEXT("(%.0f, %.0f)"), Pos.X, Pos.Y);
	}

	template <typename TNodeBase>
	static void AppendTemplatesSection(FStringBuilderBase& B,
		const TCHAR* Label,
		const TArray<TObjectPtr<TNodeBase>>& Templates,
		const TArray<FVector2D>& Positions)
	{
		B.Appendf(TEXT("\n === %s (%d) ===\n"), Label, Templates.Num());
		for (int32 i = 0; i < Templates.Num(); ++i)
		{
			B.Appendf(TEXT(" [%d] %s"), i, *NodeClassName(Templates[i]));
			if (Positions.IsValidIndex(i))
			{
				B.Append(TEXT(" @"));
				AppendPosition(B, Positions[i]);
			}
			B.AppendChar(TEXT('\n'));
		}
	}

	static void AppendPinConnections(FStringBuilderBase& B,
		const TCHAR* Label,
		const TArray<FComposableCameraPinConnection>& Connections)
	{
		B.Appendf(TEXT("\n === %s (%d) ===\n"), Label, Connections.Num());
		for (int32 i = 0; i < Connections.Num(); ++i)
		{
			const FComposableCameraPinConnection& C = Connections[i];
			B.Appendf(TEXT(" [%d] %d.%s -> %d.%s\n"),
				i,
				C.SourceNodeIndex, *C.SourcePinName.ToString(),
				C.TargetNodeIndex, *C.TargetPinName.ToString());
		}
	}

	static void AppendExecutionOrder(FStringBuilderBase& B,
		const TCHAR* Label,
		const TArray<int32>& Order)
	{
		B.Appendf(TEXT("\n === %s (%d) ===\n"), Label, Order.Num());
		if (Order.Num() == 0)
		{
			return;
		}
		B.Append(TEXT(" "));
		for (int32 i = 0; i < Order.Num(); ++i)
		{
			if (i > 0)
			{
				B.Append(TEXT(" -> "));
			}
			B.Appendf(TEXT("%d"), Order[i]);
		}
		B.AppendChar(TEXT('\n'));
	}

	static void AppendFullExecChain(FStringBuilderBase& B,
		const TCHAR* Label,
		const TArray<FComposableCameraExecEntry>& Chain)
	{
		B.Appendf(TEXT("\n === %s (%d) ===\n"), Label, Chain.Num());
		for (int32 i = 0; i < Chain.Num(); ++i)
		{
			const FComposableCameraExecEntry& E = Chain[i];
			switch (E.EntryType)
			{
			case EComposableCameraExecEntryType::CameraNode:
				B.Appendf(TEXT(" [%d] CameraNode idx=%d\n"),
					i, E.CameraNodeIndex);
				break;
			case EComposableCameraExecEntryType::SetVariable:
				B.Appendf(TEXT(" [%d] SetVariable guid=%s name='%s' sourcePin='%s' size=%d (from node %d)\n"),
					i,
					*E.VariableGuid.ToString(EGuidFormats::DigitsWithHyphens),
					*E.VariableName.ToString(),
					*E.SourcePinName.ToString(),
					E.VariableSlotSize,
					E.CameraNodeIndex);
				break;
			}
		}
	}

	static void AppendExposedParameters(FStringBuilderBase& B,
		const TArray<FComposableCameraExposedParameter>& Params)
	{
		B.Appendf(TEXT("\n === Exposed Parameters (%d) ===\n"), Params.Num());
		for (int32 i = 0; i < Params.Num(); ++i)
		{
			const FComposableCameraExposedParameter& P = Params[i];
			B.Appendf(TEXT(" [%d] '%s' type=%d target=%d.%s required=%s\n"),
				i,
				*P.ParameterName.ToString(),
				static_cast<int32>(P.PinType),
				P.TargetNodeIndex,
				*P.TargetPinName.ToString(),
				P.bRequired ? TEXT("true") : TEXT("false"));
		}
	}

	static void AppendInternalVariables(FStringBuilderBase& B,
		const TCHAR* Label,
		const TArray<FComposableCameraInternalVariable>& Vars)
	{
		B.Appendf(TEXT("\n === %s (%d) ===\n"), Label, Vars.Num());
		for (int32 i = 0; i < Vars.Num(); ++i)
		{
			const FComposableCameraInternalVariable& V = Vars[i];
			B.Appendf(TEXT(" [%d] '%s' guid=%s type=%d init='%s' resetEveryFrame=%s\n"),
				i,
				*V.VariableName.ToString(),
				*V.VariableGuid.ToString(EGuidFormats::DigitsWithHyphens),
				static_cast<int32>(V.VariableType),
				*V.InitialValueString,
				V.bResetEveryFrame ? TEXT("true") : TEXT("false"));
		}
	}

	static void AppendVariableNodeRecords(FStringBuilderBase& B,
		const TArray<FComposableCameraVariableNodeRecord>& Records)
	{
		B.Appendf(TEXT("\n === Variable Node Records (%d) ===\n"), Records.Num());
		for (int32 i = 0; i < Records.Num(); ++i)
		{
			const FComposableCameraVariableNodeRecord& R = Records[i];
			B.Appendf(TEXT(" [%d] node=%s var='%s' (guid=%s) setter=%s computeChain=%s pos="),
				i,
				*R.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
				*R.VariableName.ToString(),
				*R.VariableGuid.ToString(EGuidFormats::DigitsWithHyphens),
				R.bIsSetter ? TEXT("true") : TEXT("false"),
				R.bIsComputeChain ? TEXT("true") : TEXT("false"));
			AppendPosition(B, R.Position);
			B.Appendf(TEXT(" connections=%d\n"), R.Connections.Num());
			for (const FComposableCameraVariablePinConnection& Conn: R.Connections)
			{
				B.Appendf(TEXT(" - %s %d.%s\n"),
					Conn.bIsComputeChain ? TEXT("compute") : TEXT("camera"),
					Conn.CameraNodeIndex,
					*Conn.CameraPinName.ToString());
			}
		}
	}

	static void AppendTransitions(FStringBuilderBase& B,
		const UComposableCameraTypeAsset& Asset)
	{
		B.Append(TEXT("\n === Default Transitions ===\n"));
		B.Appendf(TEXT(" Enter: %s\n"),
			Asset.EnterTransition
				? *Asset.EnterTransition->GetClass()->GetName()
				: TEXT("(none)"));
		B.Appendf(TEXT(" Exit: %s\n"),
			Asset.ExitTransition
				? *Asset.ExitTransition->GetClass()->GetName()
				: TEXT("(none)"));
	}

	// --- Per-asset orchestrator -----------------------------------------

	static void DumpAsset(FStringBuilderBase& B, const UComposableCameraTypeAsset& A)
	{
		B.Appendf(TEXT("Camera Type Asset: %s\n"), *A.GetName());
		B.Appendf(TEXT(" Package: %s\n"), *A.GetPathName());
		B.Appendf(TEXT(" Class: %s\n"), *A.GetClass()->GetName());
		B.Appendf(TEXT(" CameraTag: %s\n"), *A.CameraTag.ToString());
		B.Appendf(TEXT(" BuildStatus: %s (%d messages)\n"),
			BuildStatusToString(A.BuildStatus),
			A.BuildMessages.Num());

		AppendTemplatesSection(B, TEXT("Node Templates"),
			A.NodeTemplates, A.NodeTemplatePositions);
		AppendTemplatesSection(B, TEXT("Compute Node Templates"),
			A.ComputeNodeTemplates, A.ComputeNodeTemplatePositions);

		AppendPinConnections(B, TEXT("Pin Connections"), A.PinConnections);
		AppendPinConnections(B, TEXT("Compute Pin Connections"), A.ComputePinConnections);

		AppendExecutionOrder(B, TEXT("Execution Order"), A.ExecutionOrder);
		AppendFullExecChain(B, TEXT("Full Exec Chain"), A.FullExecChain);

		AppendExecutionOrder(B, TEXT("Compute Execution Order"), A.ComputeExecutionOrder);
		AppendFullExecChain(B, TEXT("Compute Full Exec Chain"), A.ComputeFullExecChain);

		AppendExposedParameters(B, A.ExposedParameters);
		AppendInternalVariables(B, TEXT("Internal Variables"), A.InternalVariables);
		AppendInternalVariables(B, TEXT("Exposed Variables"), A.ExposedVariables);
		AppendVariableNodeRecords(B, A.VariableNodes);
		AppendTransitions(B, A);
	}

	// --- Command ---------------------------------------------------------
	//
	// No arguments: dump every currently-open Camera Type Asset editor.
	// Walking the asset-editor subsystem covers the "edit multiple assets
	// at once" case - each asset appears in the output with its full path,
	// separated by a visible divider, so one command captures the state
	// the user has loaded into the editor right now without asking them
	// which asset to pick.

	static void CmdDumpGraph(const TArray<FString>& /*Args*/, UWorld* /*World*/)
	{
		UAssetEditorSubsystem* Subsystem =
			GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("CCS.Editor.Dump.Graph: no AssetEditorSubsystem (editor not running?)"));
			return;
		}

		TArray<UComposableCameraTypeAsset*> CameraAssets;
		for (UObject* Obj: Subsystem->GetAllEditedAssets())
		{
			if (UComposableCameraTypeAsset* A = Cast<UComposableCameraTypeAsset>(Obj))
			{
				CameraAssets.Add(A);
			}
		}
		if (CameraAssets.Num() == 0)
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("CCS.Editor.Dump.Graph: no Camera Type Asset editors are currently open."));
			return;
		}

		TStringBuilder<8192> Builder;
		for (int32 i = 0; i < CameraAssets.Num(); ++i)
		{
			if (i > 0)
			{
				Builder.Append(TEXT("\n--------------------------------------------------------------\n\n"));
			}
			DumpAsset(Builder, *CameraAssets[i]);
		}

		LogAndClipboard(FString(Builder), TEXT("CCS.Editor.Dump.Graph"));
	}

	// --- Registration ----------------------------------------------------

	static FAutoConsoleCommandWithWorldAndArgs GCmdDumpGraph(TEXT("CCS.Editor.Dump.Graph"),
		TEXT("Print every currently-open Camera Type Asset's graph data - node templates, ")
		TEXT("compute templates, pin connections, execution chains, exposed parameters, ")
		TEXT("internal / exposed variables, variable node records, default transitions - ")
		TEXT("to LogComposableCameraSystemEditor at Display and copy the same text to the ")
		TEXT("system clipboard. Bug-report-friendly plain text; no arguments."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CmdDumpGraph));

} // namespace

#endif // !UE_BUILD_SHIPPING
