// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraImpulseResolutionNode.generated.h"

class UComposableCameraInterpolatorBase;
class USphereComponent;
class IComposableCameraImpulseShapeInterface;

/**
 * Node for resolving impulse shapes including impulse box and impulse sphere.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Resolves camera velocity based on impulse shapes with damping."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraImpulseResolutionNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraImpulseResolutionNode() { PaletteCategory = TEXT("Composition"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void BeginDestroy() override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	void AddImpulseShape(AActor* Shape)
	{
		ImpulseShapes.AddUnique(Shape);	
	}

	void RemoveImpulseShape(AActor* Shape)
	{
		ImpulseShapes.Remove(Shape);
	}

public:
	// Controls how fast the camera moves.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float VelocityDamping { 1.f };

	// Controls how fast the camera updates its velocity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters)
	UComposableCameraInterpolatorBase* Interpolator;
	
private:
	UPROPERTY()
	TObjectPtr<USphereComponent> Sphere;
	
	UPROPERTY()
	TArray<TScriptInterface<IComposableCameraImpulseShapeInterface>> ImpulseShapes;

	FVector OldVelocity { FVector::ZeroVector };
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FVector>>> Interpolator_T;
};
