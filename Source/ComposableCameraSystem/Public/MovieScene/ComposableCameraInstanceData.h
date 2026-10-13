// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShot.h"
#include "LevelSequence/ComposableCameraTypeAssetReference.h"
#include "MovieSceneObjectBindingID.h"
#include "ComposableCameraInstanceData.generated.h"

class UComposableCameraShotAsset;
struct FComposableCameraRuntimeDataBlock;

UENUM()
enum class EComposableCameraInstanceFieldType : uint8
{
	Number,
	Integer,
	Boolean,
	Object
};

/** Compiled against an evaluation copy. Never retain across a layout/array change. */
struct COMPOSABLECAMERASYSTEM_API FComposableCameraInstanceField
{
	FName Path;
	FProperty* Property = nullptr;
	void* Value = nullptr;
	EComposableCameraInstanceFieldType Type = EComposableCameraInstanceFieldType::Number;
	double ReadNumber() const;
	void WriteNumber(double Number) const;
	void WriteObject(UObject* Object) const;
};

USTRUCT()
struct COMPOSABLECAMERASYSTEM_API FComposableCameraInstanceBinding
{
	GENERATED_BODY()

	/** Parameters.Subject, Variables.Target, or Shot.Targets.<subject GUID>.Target.Actor. */
	UPROPERTY(EditAnywhere, Category = "Subject")
	FName Input;

	UPROPERTY(EditAnywhere, Category = "Subject")
	FMovieSceneObjectBindingID Binding;
};

/** Track-owned configuration. This is an inline object, never a Content Browser asset. */
UCLASS(EditInlineNew, DefaultToInstanced)
class COMPOSABLECAMERASYSTEM_API UComposableCameraInstanceData : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Camera")
	FComposableCameraTypeAssetReference Camera;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	bool bComposition = false;

	UPROPERTY(EditAnywhere, Category = "Composition", meta = (EditCondition = "bComposition", EditConditionHides))
	FComposableCameraShot Shot;

	UPROPERTY(EditAnywhere, Category = "Subjects")
	TArray<FComposableCameraInstanceBinding> Subjects;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UComposableCameraShotAsset> SourcePreset;

	void InitializeFromType(UComposableCameraTypeAsset* Type);
	void InitializeFromShot(const FComposableCameraShot& InShot, UComposableCameraShotAsset* Preset = nullptr);
	void CopyConfiguration(const UComposableCameraInstanceData& Source);
	/** Authoring copy owns its built-in type; evaluation copies share the immutable type. */
	void CopyForAuthoring(const UComposableCameraInstanceData& Source);
	void EnsureSubjectIdentities();
	void RefreshSubjectSlots();
	/** Reflection/path compilation is an authoring/activation operation, never a frame operation. */
	void GatherFields(TArray<FComposableCameraInstanceField>& OutFields);
	/** Reapply exposed inputs without rebuilding a temporary ParameterBlock each frame. */
	void ApplyInputs(FComposableCameraRuntimeDataBlock& Block) const;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
private:
	/** Inline built-in type belongs to this instance; ordinary types remain external references. */
	UPROPERTY(Instanced)
	TObjectPtr<UComposableCameraTypeAsset> BuiltInType;
};
