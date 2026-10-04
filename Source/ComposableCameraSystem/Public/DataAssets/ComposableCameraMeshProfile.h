// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "Engine/DataAsset.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "ComposableCameraMeshProfile.generated.h"

class UComposableCameraNodeModifierDataAsset;
class UComposableCameraActionTypeAsset;
class UComposableCameraPatchTypeAsset;
class AComposableCameraPlayerCameraManager;
class AComposableCameraMeshSurfaceStorageActor;
class APlayerController;

UENUM(BlueprintType)
enum class EComposableCameraMeshProfileType : uint8
{
	CameraType UMETA(DisplayName = "Camera Type"),
	Modifier,
	Action,
	Patch
};

/** Per-local-player source for inputs that would be wired on a K2 node. */
UENUM(BlueprintType)
enum class EComposableCameraMeshParameterSource : uint8
{
	None,
	Pawn,
	PlayerController,
	CameraManager,
	StorageActor,
	RunningCamera
};

USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshParameterBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EComposableCameraMeshParameterSource Source = EComposableCameraMeshParameterSource::Pawn;

	/** Delegate only. Function must match the selected Action property's signature. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName FunctionName;
};

USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshActionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Action")
	TSoftObjectPtr<UComposableCameraActionTypeAsset> ActionAsset;

	UPROPERTY(EditAnywhere, Category = "Action")
	bool bOnlyForCurrentCamera = false;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (DisplayName = "Parameters"))
	FComposableCameraExposedParameterValues Parameters;

	/** Typed runtime bindings. Edited through the generated parameter rows. */
	UPROPERTY(EditAnywhere, Category = "Action")
	TMap<FName, FComposableCameraMeshParameterBinding> Bindings;

	void BuildParameterBlock(const UComposableCameraActionTypeAsset& Asset,
		APlayerController* PlayerController, AComposableCameraPlayerCameraManager* CameraManager,
		AComposableCameraMeshSurfaceStorageActor* StorageActor,
		FComposableCameraParameterBlock& OutParameters) const;
};

USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraMeshPatchConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Patch")
	TSoftObjectPtr<UComposableCameraPatchTypeAsset> PatchAsset;

	UPROPERTY(EditAnywhere, Category = "Patch")
	FComposableCameraPatchActivateParams ActivationParams;

	UPROPERTY(EditAnywhere, Category = "Patch", meta = (DisplayName = "Exposed Parameters"))
	FComposableCameraExposedParameterValues Parameters;

	void BuildParameterBlock(const UComposableCameraPatchTypeAsset& Asset,
		FComposableCameraParameterBlock& OutParameters) const;
};

/**
 * Camera behavior invoked when a player enters a mesh surface layer.
 *
 * Exactly one selected effect family executes. Inactive configurations remain
 * serialized so changing Type does not destroy authoring data.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraMeshProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Profile")
	EComposableCameraMeshProfileType Type = EComposableCameraMeshProfileType::CameraType;

	/** Optional Camera Type activation performed once when this Profile becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ShowOnlyInnerProperties, EditCondition = "Type == EComposableCameraMeshProfileType::CameraType", EditConditionHides))
	FComposableCameraParameterTableRow Camera;

	/** Modifier templates installed for the lifetime of this active Profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (EditCondition = "Type == EComposableCameraMeshProfileType::Modifier", EditConditionHides))
	TArray<TObjectPtr<UComposableCameraNodeModifierDataAsset>> ModifierAssets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (EditCondition = "Type == EComposableCameraMeshProfileType::Action", EditConditionHides))
	FComposableCameraMeshActionConfig Action;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch", meta = (EditCondition = "Type == EComposableCameraMeshProfileType::Patch", EditConditionHides))
	FComposableCameraMeshPatchConfig Patch;

	bool NeedsTypeSelection() const { return bNeedsTypeSelection; }
	/** Explicitly acknowledge the selected family when converting a legacy mixed Profile. */
	void ConfirmTypeSelection();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	friend class FComposableCameraMeshProfileMigrationTest;
	void MigrateLegacyProfile();

	UPROPERTY()
	bool bNeedsTypeSelection = false;

	int32 LoadedSchemaVersion = 1;
};
