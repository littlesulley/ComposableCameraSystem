// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraSetRotationNode.generated.h"

UENUM(BlueprintType)
enum class EComposableCameraSetRotationSource : uint8
{
	FromActor,
	FromVector,
	FromRotator,
	FromTwoActors UMETA(DisplayName = "From Two Actors")
};

/**
 * Node for replacing the current camera rotation from an actor forward vector,
 * an explicit forward vector, a literal rotator, or the direction between two actors.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Set Rotation", ToolTip = "Sets camera rotation from an actor, vector, rotator, or two-actor direction."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraSetRotationNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraSetRotationNode() { PaletteCategory = TEXT("Rotation"); }

public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	// Selects where the replacement rotation comes from.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSetRotationSource RotationSource { EComposableCameraSetRotationSource::FromRotator };

	// Selects whether the actor source is explicit or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromActor", EditConditionHides))
	EComposableCameraActorInputSource RotationActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Actor whose forward vector defines the replacement rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromActor && RotationActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> RotationActor { nullptr };

	// Selects whether FirstActor comes from an explicit actor or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors", EditConditionHides))
	EComposableCameraActorInputSource FirstActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// First endpoint for FromTwoActors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors && FirstActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> FirstActor { nullptr };

	// Selects whether SecondActor comes from an explicit actor or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors", EditConditionHides))
	EComposableCameraActorInputSource SecondActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Second endpoint for FromTwoActors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors && SecondActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> SecondActor { nullptr };

	// Forward vector used to build the replacement rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromVector", EditConditionHides))
	FVector RotationVector { FVector::ForwardVector };

	// Literal replacement rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromRotator", EditConditionHides))
	FRotator Rotation { FRotator::ZeroRotator };

	// Offset applied after the base rotation is resolved. Yaw is world-space;
	// pitch and roll are local-space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FRotator RotationOffset { FRotator::ZeroRotator };
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Begin Play: Set Rotation", ToolTip = "Sets the initial camera rotation from an actor, vector, rotator, or two-actor direction during BeginPlay."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraBeginPlaySetRotationNode : public UComposableCameraComputeNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraBeginPlaySetRotationNode() { PaletteCategory = TEXT("Rotation"); }

public:
	virtual void ExecuteBeginPlay() override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	// Selects where the replacement rotation comes from.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraSetRotationSource RotationSource { EComposableCameraSetRotationSource::FromRotator };

	// Selects whether the actor source is explicit or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromActor", EditConditionHides))
	EComposableCameraActorInputSource RotationActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Actor whose forward vector defines the replacement rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromActor && RotationActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> RotationActor { nullptr };

	// Selects whether FirstActor comes from an explicit actor or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors", EditConditionHides))
	EComposableCameraActorInputSource FirstActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// First endpoint for FromTwoActors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors && FirstActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> FirstActor { nullptr };

	// Selects whether SecondActor comes from an explicit actor or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors", EditConditionHides))
	EComposableCameraActorInputSource SecondActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Second endpoint for FromTwoActors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromTwoActors && SecondActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> SecondActor { nullptr };

	// Forward vector used to build the replacement rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromVector", EditConditionHides))
	FVector RotationVector { FVector::ForwardVector };

	// Literal replacement rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "RotationSource == EComposableCameraSetRotationSource::FromRotator", EditConditionHides))
	FRotator Rotation { FRotator::ZeroRotator };

	// Offset applied after the base rotation is resolved. Yaw is world-space;
	// pitch and roll are local-space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FRotator RotationOffset { FRotator::ZeroRotator };
};
