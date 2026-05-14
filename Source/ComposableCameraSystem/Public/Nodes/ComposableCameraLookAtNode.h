// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraLookAtNode.generated.h"

class UComposableCameraInterpolatorBase;
class USkeletalMeshComponent;

UENUM()
enum class EComposableCameraLookAtType : uint8
{
	// Target position.
	ByPosition,

	// Target actor.
	ByActor
};

UENUM()
enum class EComposableCameraLookAtConstraintType : uint8
{
	// Hard look at: player cannot control camera.
	Hard,

	// Soft look at: player can control camera around the look at direction.
	Soft
};

/**
 * Node for looking at some target position.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Orients the camera to look at a target position or actor with optional soft constraints."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraLookAtNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraLookAtNode() { PaletteCategory = TEXT("Rotation"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

protected:

public:
	// Look at type, to look at a specified position or by an actor's position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraLookAtType LookAtType;

	// Target look-at position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtType == EComposableCameraLookAtType::ByPosition", EditConditionHides))
	FVector LookAtPosition;

	// Selects whether the look-at actor comes from the controller's controlled
	// pawn or from the explicitly supplied LookAtActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtType == EComposableCameraLookAtType::ByActor", EditConditionHides))
	EComposableCameraActorInputSource LookAtActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Explicit target look-at actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtType == EComposableCameraLookAtType::ByActor && LookAtActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> LookAtActor;

	// Target look-at socket when the actor has a SkeletalMeshComponent. If no such component exists, will use LookAtActor's position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtType == EComposableCameraLookAtType::ByActor", EditConditionHides))
	FName LookAtSocket;

	// Look-at constraint type, hard look at or soft look at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraLookAtConstraintType LookAtConstraintType;

	// Soft look at range in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft", EditConditionHides))
	float SoftLookAtRange { 20.f };
	
	// Soft look at weight. The larger it is, the harder the camera will look at the target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft", EditConditionHides))
	float SoftLookAtWeight { 0.2f };

	// Soft look at interpolator when resuming to the look-at direction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = InputParameters, meta = (EditCondition = "LookAtConstraintType == EComposableCameraLookAtConstraintType::Soft", EditConditionHides))
	UComposableCameraInterpolatorBase* SoftLookAtInterpolator { nullptr };

private:
	// Cached SkeletalMeshComponent on the currently-resolved LookAtActor.
	// Stored as TWeakObjectPtr (not raw, not UPROPERTY): LookAtActor can be
	// driven by an input pin and CHANGE every frame, and the SkelMesh
	// component on that actor can be destroyed / re-spawned independently
	// of this node - Tick must IsValid()-check before deref. Resolution
	// happens lazily in Tick when the active LookAtActor differs from the
	// last actor we resolved against (`LastResolvedLookAtActor`); that
	// avoids the per-frame `GetComponentByClass` walk while still picking
	// up actor swaps and component churn.
	TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponentForLookAtActor;

	// Identity of the actor that produced the cached SkelMesh component.
	// When LookAtActor changes (driven by input pin) this no longer matches
	// the current LookAtActor and Tick re-resolves the SkelMesh component.
	TWeakObjectPtr<AActor> LastResolvedLookAtActor;

	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> Interpolator_T;
};
