// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComposableCameraActionBase.generated.h"

class AComposableCameraPlayerCameraManager;
class UComposableCameraCameraNodeBase;

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EComposableCameraActionExpirationType : uint8
{
	None = 0 UMETA(Hidden),
	
	// This action only persists for one frame.
	Instant = 1 << 0,
	
	// This action lasts for a fixed duration. Can also be removed manully or by condition.
	Duration = 1 << 1,
	
	// This action lasts indefinitive, must be removed manually through node ExpireAction. 
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
	
	// This action is executed before the node whose class matches TargetNodeClass is evaluated.
	// If TargetNodeClass is unset or no matching node exists on the camera, this action is ignored.
	PreNodeTick,

	// This action is executed after the node whose class matches TargetNodeClass is evaluated.
	// If TargetNodeClass is unset or no matching node exists on the camera, this action is ignored.
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

	// Target node class for node-scoped execution. Exact class match, matching the Modifier system.
	// Only meaningful when ExecutionType is PreNodeTick or PostNodeTick.
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		meta = (EditCondition = "ExecutionType == EComposableCameraActionExecutionType::PreNodeTick || ExecutionType == EComposableCameraActionExecutionType::PostNodeTick",
				EditConditionHides))
	TSubclassOf<UComposableCameraCameraNodeBase> TargetNodeClass;

	// How will this action get expired.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Bitmask, BitmaskEnum = EComposableCameraActionExpirationType))
	uint8 ExpirationType { static_cast<uint8>(EComposableCameraActionExpirationType::Duration | EComposableCameraActionExpirationType::Condition) };
	
	// Duration if this action expiration type is Duration. If <=0, this action will not be added.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "ExpirationType & \"/Script/ComposableCameraSystem.EComposableCameraActionExpirationType::Duration", EditConditionHides))
	float Duration { 1.f };

public:
	bool OnCanExecute(float DeltaTime, const FComposableCameraPose& CurrentCameraPose);
	
	// Predicate checking whether this action can be executed. If false, this action will get expired. Only get called when ExpirationType has Condition on.
	UFUNCTION(BlueprintNativeEvent, DisplayName = "CanExecute", Category = "ComposableCameraSystem|Action")
	bool CanExecute(float DeltaTime, const FComposableCameraPose& CurrentCameraPose);
	virtual bool CanExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose) { return true; }
	
	// Do something for this action.
	UFUNCTION(BlueprintNativeEvent, DisplayName = "OnExecute", Category = "ComposableCameraSystem|Action")
	void OnExecute(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	virtual void OnExecute_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) {}

	// Manually expire this action.
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Action")
	void ExpireAction()
	{
		bCanExecuteManual = false;
	}

public:
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	bool bOnlyForCurrentCamera { true };

	UPROPERTY(BlueprintReadOnly)
	AComposableCameraPlayerCameraManager* PlayerCameraManager{};

	/** Time since this action began ticking, in seconds. Only meaningful when
	 *  ExpirationType has the Duration bit set (Duration expiration fires
	 *  when ElapsedTime >= Duration). Exposed for debug tooling (the
	 *  Actions panel region); gameplay code should not poll it — use the
	 *  lifecycle hooks instead. */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Action")
	float GetElapsedTime() const { return ElapsedTime; }

private:
	bool bCanExecuteInstant { true };
	bool bCanExecuteDuration { true };
	bool bCanExecuteManual { true };
	bool bCanExecuteCondition { true };

	float ElapsedTime { 0.f };
};
