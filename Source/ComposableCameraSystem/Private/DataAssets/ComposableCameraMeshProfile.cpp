// Copyright 2026 Sulley. All Rights Reserved.

#include "DataAssets/ComposableCameraMeshProfile.h"

#include "Actions/ComposableCameraActionBase.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Serialization/CustomVersion.h"
#include "UObject/UnrealType.h"

namespace
{
	const FGuid MeshProfileVersionGuid(0x8665D5C8, 0x480C4F9E, 0xA5C2125A, 0x290D7B70);
	const FCustomVersionRegistration MeshProfileVersion(MeshProfileVersionGuid, 1, TEXT("CCSMeshProfile"));

	UObject* ResolveSource(EComposableCameraMeshParameterSource Source, APlayerController* PC,
		AComposableCameraPlayerCameraManager* PCM, AComposableCameraMeshSurfaceStorageActor* Storage)
	{
		switch (Source)
		{
		case EComposableCameraMeshParameterSource::None: return nullptr;
		case EComposableCameraMeshParameterSource::Pawn: return PC ? PC->GetPawn() : nullptr;
		case EComposableCameraMeshParameterSource::PlayerController: return PC;
		case EComposableCameraMeshParameterSource::CameraManager: return PCM;
		case EComposableCameraMeshParameterSource::StorageActor: return Storage;
		case EComposableCameraMeshParameterSource::RunningCamera: return PCM ? PCM->GetRunningCamera() : nullptr;
		default: return nullptr;
		}
	}
}

void UComposableCameraMeshProfile::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(MeshProfileVersionGuid);
	Super::Serialize(Ar);
	if (Ar.IsLoading())
	{
		LoadedSchemaVersion = Ar.CustomVer(MeshProfileVersionGuid);
	}
}

void UComposableCameraMeshProfile::PostLoad()
{
	Super::PostLoad();
	if (LoadedSchemaVersion < 1)
	{
		MigrateLegacyProfile();
	}
}

void UComposableCameraMeshProfile::MigrateLegacyProfile()
{
	const bool bHasCamera = !Camera.CameraType.IsNull();
	const bool bHasModifiers = ModifierAssets.ContainsByPredicate(
		[](const UComposableCameraNodeModifierDataAsset* Asset) { return Asset != nullptr; });
	Type = !bHasCamera && bHasModifiers
		? EComposableCameraMeshProfileType::Modifier : EComposableCameraMeshProfileType::CameraType;
	bNeedsTypeSelection = bHasCamera && bHasModifiers;
	if (bNeedsTypeSelection)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Mesh Profile '%s' contains legacy Camera + Modifier settings. Select and confirm one Type before use; both configurations are retained."), *GetPathName());
	}
}

void UComposableCameraMeshProfile::ConfirmTypeSelection()
{
	bNeedsTypeSelection = false;
}

#if WITH_EDITOR
void UComposableCameraMeshProfile::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Type))
	{
		ConfirmTypeSelection();
	}
	Super::PostEditChangeProperty(Event);
}
#endif

void FComposableCameraMeshPatchConfig::BuildParameterBlock(
	const UComposableCameraPatchTypeAsset& Asset, FComposableCameraParameterBlock& OutParameters) const
{
	FComposableCameraParameterTableRow Row;
	Row.Parameters = Parameters;
	Row.BuildParameterBlock(Asset, OutParameters, TEXT("Mesh Patch Profile"));
}

void FComposableCameraMeshActionConfig::BuildParameterBlock(
	const UComposableCameraActionTypeAsset& Asset, APlayerController* PC,
	AComposableCameraPlayerCameraManager* PCM, AComposableCameraMeshSurfaceStorageActor* Storage,
	FComposableCameraParameterBlock& OutParameters) const
{
	OutParameters = FComposableCameraParameterBlock();
	const UComposableCameraActionBase* Template = Asset.ActionTemplate;
	if (!IsValid(Template))
	{
		return;
	}
	// Layer entry only. Match K2 exposure and leave unspecified defaults on the template.
	for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
	{
		const FProperty* Property = *It;
		if (!UComposableCameraActionTypeAsset::IsExposableProperty(Property))
		{
			continue;
		}
		EComposableCameraPinType PinType;
		UScriptStruct* StructType = nullptr;
		UEnum* EnumType = nullptr;
		UFunction* Signature = nullptr;
		if (!TryMapPropertyToPinType(Property, PinType, StructType, EnumType, &Signature))
		{
			continue;
		}
		const FName Name = Property->GetFName();
		if (PinType == EComposableCameraPinType::Actor || PinType == EComposableCameraPinType::Delegate)
		{
			const FComposableCameraMeshParameterBinding* Binding = Bindings.Find(Name);
			if (!Binding)
			{
				continue;
			}
			UObject* Source = ResolveSource(Binding->Source, PC, PCM, Storage);
			if (PinType == EComposableCameraPinType::Actor)
			{
				const FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(Property);
				if (!Source || Source->IsA(ObjectProperty->PropertyClass))
				{
					OutParameters.SetActor(Name, Cast<AActor>(Source));
					continue;
				}
			}
			else
			{
				FScriptDelegate Delegate;
				if (Binding->Source == EComposableCameraMeshParameterSource::None)
				{
					OutParameters.SetDelegate(Name, Delegate);
					continue;
				}
				const UFunction* Function = IsValid(Source) ? Source->FindFunction(Binding->FunctionName) : nullptr;
				if (Function && Signature && Function->IsSignatureCompatibleWith(Signature))
				{
					Delegate.BindUFunction(Source, Binding->FunctionName);
					OutParameters.SetDelegate(Name, Delegate);
					continue;
				}
			}
			UE_LOG(LogComposableCameraSystem, Warning, TEXT("Mesh Action parameter '%s' has an incompatible runtime binding; keeping asset default."), *Name.ToString());
			continue;
		}
		if (const FString* Value = Parameters.Values.Find(Name))
		{
			FString Error;
			if (!FComposableCameraParameterBlock::ApplyStringValue(
				OutParameters, Name, PinType, StructType, EnumType, *Value, &Error))
			{
				UE_LOG(LogComposableCameraSystem, Warning, TEXT("Mesh Action parameter '%s': %s; keeping asset default."), *Name.ToString(), *Error);
			}
		}
	}
}
