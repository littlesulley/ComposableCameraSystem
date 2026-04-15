// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraBlueprintLibrary.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"

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

AComposableCameraCameraBase* UComposableCameraBlueprintLibrary::ActivateComposableCameraFromDataTable(
	const UObject* WorldContextObject,
	int32 PlayerIndex,
	UDataTable* DataTable,
	FName RowName)
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
