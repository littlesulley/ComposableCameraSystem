// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraBlueprintLibrary.h"

#include "AsyncActions/AsyncPlayCutsceneSequence.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Patches/ComposableCameraPatchHandle.h"
#include "Patches/ComposableCameraPatchInstance.h"
#include "Patches/ComposableCameraPatchManager.h"

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::ActivateComposableCameraFromTypeAsset(
	const UObject* WorldContextObject,
	int32 PlayerIndex,
	UComposableCameraTypeAsset* CameraTypeAsset,
	FName ContextName,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	FComposableCameraParameterBlock Parameters,
	FComposableCameraActivateParams ActivationParams)
{
	AComposableCameraPlayerCameraManager* PCM = GetComposableCameraPlayerCameraManager(WorldContextObject, PlayerIndex);
	if (PCM && CameraTypeAsset)
	{
		return PCM->ActivateNewCameraFromTypeAsset(
			CameraTypeAsset, TransitionOverride, ActivationParams, Parameters, ContextName);
	}

	return nullptr;
}

UAsyncPlayCutsceneSequence* UComposableCameraBlueprintLibrary::PlayCutsceneSequence(
	UObject* WorldContextObject,
	ULevelSequence* InLevelSequence,
	FName ContextName,
	UComposableCameraTransitionDataAsset* EnterTransition,
	FMovieSceneSequencePlaybackSettings PlaybackSettings)
{
	return UAsyncPlayCutsceneSequence::Create(
		WorldContextObject, InLevelSequence, ContextName, EnterTransition, PlaybackSettings);
}

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::ActivateComposableCameraFromDataTable(
	const UObject* WorldContextObject,
	int32 PlayerIndex,
	UDataTable* DataTable,
	FName RowName,
	FComposableCameraParameterBlock OverrideParameters)
{
	if (!DataTable)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ActivateComposableCameraFromDataTable: DataTable is null."));
		return nullptr;
	}

	if (DataTable->GetRowStruct() != FComposableCameraParameterTableRow::StaticStruct())
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ActivateComposableCameraFromDataTable: DataTable '%s' has row struct '%s', expected FComposableCameraParameterTableRow."),
			*DataTable->GetName(),
			DataTable->GetRowStruct() ? *DataTable->GetRowStruct()->GetName() : TEXT("null"));
		return nullptr;
	}

	const FComposableCameraParameterTableRow* Row = DataTable->FindRow<FComposableCameraParameterTableRow>(
		RowName, TEXT("ActivateComposableCameraFromDataTable"));
	if (!Row)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ActivateComposableCameraFromDataTable: Row '%s' not found in DataTable '%s'."),
			*RowName.ToString(), *DataTable->GetName());
		return nullptr;
	}

	// Sync-load the camera type. DataTable paths are by definition synchronous
	// (we're inside a BP call) — callers that want async loading should use the
	// streamable manager directly and then call ActivateComposableCameraFromTypeAsset.
	UComposableCameraTypeAsset* TypeAsset = Row->CameraType.LoadSynchronous();
	if (!TypeAsset)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("ActivateComposableCameraFromDataTable: Row '%s' has no valid CameraType."),
			*RowName.ToString());
		return nullptr;
	}

	// Build the parameter block from the row's string map, using the type
	// asset's exposed parameters AND exposed variables as the type-info
	// source. Both share the same ParameterBlock keyspace at activation time
	// (see UComposableCameraTypeAsset::ApplyParameterBlock), so the row can
	// supply values for either category and the runtime routes them
	// correctly. Unknown keys are logged as orphans below.
	FComposableCameraParameterBlock Params;
	const TArray<FComposableCameraExposedParameter>& Exposed = TypeAsset->GetExposedParameters();
	const TArray<FComposableCameraInternalVariable>& ExposedVars = TypeAsset->ExposedVariables;

	TSet<FName> KnownNames;
	KnownNames.Reserve(Exposed.Num() + ExposedVars.Num());

	for (const FComposableCameraExposedParameter& Param : Exposed)
	{
		KnownNames.Add(Param.ParameterName);

		const FString* RowValuePtr = Row->Parameters.Values.Find(Param.ParameterName);
		const FString ParamDefault = TypeAsset->GetExposedParameterDefaultValue(Param);
		const FString& ValueString = RowValuePtr ? *RowValuePtr : ParamDefault;

		if (ValueString.IsEmpty())
		{
			// Nothing to write — the runtime data block will be zero-initialized
			// for this slot and ApplyParameterBlock will log a warning if the
			// parameter was bRequired.
			continue;
		}

		FString ParseError;
		const bool bOk = FComposableCameraParameterBlock::ApplyStringValue(
			Params, Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType, ValueString, &ParseError);

		if (!bOk)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ActivateComposableCameraFromDataTable: Row '%s' parameter '%s' parse failed (%s)."
					 " Falling back to node pin default."),
				*RowName.ToString(), *Param.ParameterName.ToString(), *ParseError);

			// One-shot fallback: if the row value was bad but the node pin
			// has a sane default, try that next. This covers the case where
			// a row has a typo but the node authored a valid default.
			if (RowValuePtr && !ParamDefault.IsEmpty()
				&& !ParamDefault.Equals(ValueString))
			{
				FString FallbackError;
				FComposableCameraParameterBlock::ApplyStringValue(
					Params, Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType,
					ParamDefault, &FallbackError);
			}
		}
	}

	// Exposed variables: same parsing flow as exposed parameters, but the
	// type-side default comes from InitialValueString (the variable's
	// author-time initial value).
	// If the row omits this variable entirely AND InitialValueString is
	// empty, we intentionally leave it out of the ParameterBlock — the
	// runtime will zero-initialize the slot and log nothing (exposed
	// variables have no "required" flag).
	for (const FComposableCameraInternalVariable& Var : ExposedVars)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}

		KnownNames.Add(Var.VariableName);

		const FString* RowValuePtr = Row->Parameters.Values.Find(Var.VariableName);
		const FString& ValueString = RowValuePtr ? *RowValuePtr : Var.InitialValueString;

		if (ValueString.IsEmpty())
		{
			continue;
		}

		FString ParseError;
		const bool bOk = FComposableCameraParameterBlock::ApplyStringValue(
			Params, Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType, ValueString, &ParseError);

		if (!bOk)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ActivateComposableCameraFromDataTable: Row '%s' exposed variable '%s' parse failed (%s)."
					 " Falling back to InitialValueString."),
				*RowName.ToString(), *Var.VariableName.ToString(), *ParseError);

			if (RowValuePtr && !Var.InitialValueString.IsEmpty()
				&& !Var.InitialValueString.Equals(ValueString))
			{
				FString FallbackError;
				FComposableCameraParameterBlock::ApplyStringValue(
					Params, Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType,
					Var.InitialValueString, &FallbackError);
			}
		}
	}

	// Flag orphaned row entries (keys the current type asset no longer exposes
	// as either a parameter OR a variable) exactly once per call so designers
	// can clean up after a CameraType swap.
	for (const TPair<FName, FString>& Entry : Row->Parameters.Values)
	{
		if (!KnownNames.Contains(Entry.Key))
		{
			UE_LOG(LogComposableCameraSystem, Verbose,
				TEXT("ActivateComposableCameraFromDataTable: Row '%s' has orphaned entry '%s' not present on CameraType '%s'."),
				*RowName.ToString(), *Entry.Key.ToString(), *TypeAsset->GetName());
		}
	}

	// Merge per-call-site overrides on top of the row-parsed values. An override
	// entry for a given name replaces the row value entirely — this is how the
	// K2 node's "Add Override Pin" feature works: the row provides the base
	// configuration, and the override block carries per-call-site adjustments.
	for (TPair<FName, FComposableCameraParameterValue>& Entry : OverrideParameters.Values)
	{
		Params.Values.Add(Entry.Key, MoveTemp(Entry.Value));
	}
	for (TPair<FName, FScriptDelegate>& Entry : OverrideParameters.DelegateValues)
	{
		Params.DelegateValues.Add(Entry.Key, MoveTemp(Entry.Value));
	}

	// Resolve the transition override. Sync-load only if a path is set so we
	// don't stall on a null slot.
	UComposableCameraTransitionDataAsset* TransitionOverride = nullptr;
	if (!Row->TransitionOverride.IsNull())
	{
		TransitionOverride = Row->TransitionOverride.LoadSynchronous();
	}

	return ActivateComposableCameraFromTypeAsset(
		WorldContextObject,
		PlayerIndex,
		TypeAsset,
		Row->ContextName,
		TransitionOverride,
		Params,
		Row->ActivationParams);
}

void UComposableCameraBlueprintLibrary::TerminateCurrentCamera(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	FComposableCameraActivateParams ActivationParams)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->TerminateCurrentCamera(TransitionOverride, ActivationParams);
	}
}

void UComposableCameraBlueprintLibrary::PopCameraContext(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager,
	FName ContextName,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	FComposableCameraActivateParams ActivationParams)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->PopCameraContext(ContextName, TransitionOverride, ActivationParams);
	}
}

int32 UComposableCameraBlueprintLibrary::GetCameraContextStackDepth(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager)
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->GetContextStackDepth();
	}
	return 0;
}

FName UComposableCameraBlueprintLibrary::GetActiveContextName(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager)
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->GetActiveContextName();
	}
	return NAME_None;
}

void UComposableCameraBlueprintLibrary::AddModifier(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->AddModifier(ModifierAsset);
	}
}

void UComposableCameraBlueprintLibrary::RemoveModifier(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->RemoveModifier(ModifierAsset);
	}
}

UComposableCameraActionBase* UComposableCameraBlueprintLibrary::AddAction(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, TSubclassOf<UComposableCameraActionBase> ActionClass, bool bOnlyForCurrentCamera)
{
	if (PlayerCameraManager)
	{
		return PlayerCameraManager->AddCameraAction(ActionClass, bOnlyForCurrentCamera);
	}

	return nullptr;
}

void UComposableCameraBlueprintLibrary::ExpireAction(const UObject* WorldContextObject,
	AComposableCameraPlayerCameraManager* PlayerCameraManager, TSubclassOf<UComposableCameraActionBase> ActionClass)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ExpireCameraAction(ActionClass);
	}
}

AComposableCameraPlayerCameraManager* UComposableCameraBlueprintLibrary::GetComposableCameraPlayerCameraManager(
	const UObject* WorldContextObject, int Index)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, Index);
	return Cast<AComposableCameraPlayerCameraManager>(PC->PlayerCameraManager);
}

FVector UComposableCameraBlueprintLibrary::MakeLiteralVector(FVector Value)
{
	return Value;
}

FVector4 UComposableCameraBlueprintLibrary::MakeLiteralVector4(FVector4 Value)
{
	return Value;
}

FVector2D UComposableCameraBlueprintLibrary::MakeLiteralVector2D(FVector2D Value)
{
	return Value;
}

FRotator UComposableCameraBlueprintLibrary::MakeLiteralRotator(FRotator Value)
{
	return Value;
}

FTransform UComposableCameraBlueprintLibrary::MakeLiteralTransform(FTransform Value)
{
	return Value;
}

UObject* UComposableCameraBlueprintLibrary::MakeLiteralObject(UObject* Value)
{
	return Value;
}

FName UComposableCameraBlueprintLibrary::MakeLiteralName(FName Value)
{
	return Value;
}

uint8 UComposableCameraBlueprintLibrary::MakeLiteralByte(uint8 Value)
{
	return Value;
}

// ─── Camera Patch (Stage 2 minimal surface) ─────────────────────────────────

namespace
{
	// Shared drill helper: PCM → ContextStack → (ActiveDirector | named-context Director)
	// → PatchManager. NAME_None on ContextName means "active context", matching
	// AddModifier / AddCameraAction's implicit targeting. A non-None name that
	// isn't currently on the stack returns nullptr — the BP entry points log
	// and fail the activation.
	UComposableCameraPatchManager* ResolvePatchManager(
		AComposableCameraPlayerCameraManager* PCM, FName ContextName)
	{
		if (!PCM)
		{
			return nullptr;
		}
		const UComposableCameraContextStack* Stack = PCM->GetContextStack();
		if (!Stack)
		{
			return nullptr;
		}
		UComposableCameraDirector* Director = ContextName.IsNone()
			? Stack->GetActiveDirector()
			: Stack->GetDirectorForContext(ContextName);
		return Director ? Director->GetPatchManager() : nullptr;
	}
}

UComposableCameraPatchHandle* UComposableCameraBlueprintLibrary::AddCameraPatch(
	const UObject* WorldContextObject,
	int32 PlayerIndex,
	UComposableCameraPatchTypeAsset* PatchAsset,
	FName ContextName,
	FComposableCameraPatchActivateParams Params,
	FComposableCameraParameterBlock Parameters)
{
	if (!PatchAsset)
	{
		return nullptr;
	}
	AComposableCameraPlayerCameraManager* PlayerCameraManager =
		GetComposableCameraPlayerCameraManager(WorldContextObject, PlayerIndex);
	UComposableCameraPatchManager* Manager = ResolvePatchManager(PlayerCameraManager, ContextName);
	if (!Manager)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("AddCameraPatch: no PatchManager reachable (PlayerIndex=%d, ContextName='%s'). "
			     "Either the player has no PCM, or the named context is not on the stack."),
			PlayerIndex,
			ContextName.IsNone() ? TEXT("<active>") : *ContextName.ToString());
		return nullptr;
	}
	return Manager->AddPatch(PatchAsset, Params, Parameters);
}

void UComposableCameraBlueprintLibrary::ExpireCameraPatch(
	UComposableCameraPatchHandle* Handle, float ExitDurationOverride)
{
	if (!Handle)
	{
		return;
	}
	UComposableCameraPatchInstance* Instance = Handle->GetInstance();
	if (!Instance)
	{
		return;
	}
	// PatchManager is the instance's outer (NewObject<...>(this) in AddPatch).
	UComposableCameraPatchManager* Manager = Instance->GetTypedOuter<UComposableCameraPatchManager>();
	if (!Manager)
	{
		return;
	}
	Manager->ExpirePatch(Handle, ExitDurationOverride);
}

void UComposableCameraBlueprintLibrary::ExpireAllPatchesOnContext(
	const UObject* WorldContextObject,
	int32 PlayerIndex,
	FName ContextName,
	float ExitDurationOverride)
{
	AComposableCameraPlayerCameraManager* PlayerCameraManager =
		GetComposableCameraPlayerCameraManager(WorldContextObject, PlayerIndex);
	UComposableCameraPatchManager* Manager = ResolvePatchManager(PlayerCameraManager, ContextName);
	if (!Manager)
	{
		// Quiet on the "no patches anywhere yet" path (Manager not yet built),
		// but log when the context name was an explicit miss — that's likely
		// a typo or a stale context reference.
		if (!ContextName.IsNone())
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ExpireAllPatchesOnContext: context '%s' not on the stack (PlayerIndex=%d)."),
				*ContextName.ToString(), PlayerIndex);
		}
		return;
	}
	Manager->ExpireAll(ExitDurationOverride);
}

bool UComposableCameraBlueprintLibrary::IsPatchActive(const UComposableCameraPatchHandle* Handle)
{
	return Handle && Handle->IsActive();
}

EComposableCameraPatchPhase UComposableCameraBlueprintLibrary::GetPatchPhase(
	const UComposableCameraPatchHandle* Handle)
{
	return Handle ? Handle->GetPhase() : EComposableCameraPatchPhase::Expired;
}

float UComposableCameraBlueprintLibrary::GetPatchAlpha(const UComposableCameraPatchHandle* Handle)
{
	return Handle ? Handle->GetAlpha() : 0.f;
}

float UComposableCameraBlueprintLibrary::GetPatchElapsedTime(const UComposableCameraPatchHandle* Handle)
{
	return Handle ? Handle->GetElapsedTime() : 0.f;
}
