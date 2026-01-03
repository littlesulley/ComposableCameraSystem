// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComposableCameraActionBase.generated.h"

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EComposableCameraActionExpirationType : uint8
{
	None = 0 UMETA(Hidden),
	
	// This action only persists for one frame.
	Instant = 1 << 0,
	
	// This action lasts for a fixed duration. Can also be removed manully or by condition.
	Duration = 1 << 1,
	
	// This action lasts indefinitive, must be removed manually. 
	Manual = 1 << 2,
	
	// This action expires according to some user-provided condition.
	Condition = 1 << 3
};
ENUM_CLASS_FLAGS(EComposableCameraActionExpirationType)


UENUM(BlueprintType)
enum class EComposableCameraActionExecutionType : uint8
{
	// This action is executed before camera is evaluated.
	PreCameraTick,
	
	// This action is executed before some node is evaluated. If no such node exists, this action is ignored.
	PreNodeTick,
	
	// This action is executed after some node is evaluated. If no such node exists, this action is ignored.
	PostNodeTick,
	
	// This action is executed after camera is evaluated.
	PostCameraTick
};

/**
 * A camera action is a hook where you can do anything before/after a camera is evaluated, or before/after some node is evaluated.
 */
UCLASS(Blueprintable, BlueprintType, DefaultToInstanced, EditInlineNew, CollapseCategories, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraActionBase : public UObject
{
	GENERATED_BODY()
	
public:
	// When will this action get executed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EComposableCameraActionExecutionType ExecutionType { EComposableCameraActionExecutionType::PreCameraTick };
	
	// How will this action get expired.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Bitmask, BitmaskEnum = EComposableCameraActionExpirationType))
	uint8 ExpirationType { static_cast<uint8>(EComposableCameraActionExpirationType::Duration | EComposableCameraActionExpirationType::Condition) };
	
	// Duration if this action expiration type is Duration. If <=0, this action will not be added.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "ExpirationType & \"/Script/ComposableCameraSystem.EComposableCameraActionExpirationType::Duration", EditConditionHides))
	float Duration { 1.f };

public:
	
	// Predicate checking whether this action can be executed. If false, this action will get expired. Only get called when ExpirationType has Condition on.
	UFUNCTION(BlueprintNativeEvent, DisplayName = "CanExecute", Category = "ComposableCameraSystem|Action")
	bool CanExecute(float DeltaTime, const FComposableCameraPose& CurrentCameraPose);
	virtual bool CanExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose) { return true; }
	
	// Do something for this action.
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnExecute", Category = "ComposableCameraSystem|Action")
	void OnExecute(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	virtual void OnExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) {}
};
