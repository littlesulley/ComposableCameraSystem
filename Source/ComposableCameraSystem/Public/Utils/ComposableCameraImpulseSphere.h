// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraImpulseShapeInterface.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "ComposableCameraImpulseSphere.generated.h"

class USphereComponent;

/**
 * This actor can be placed into levels or spawned dynamically to provide an impulse mechanism for cameras. \n
 * When camera enters the sphere, it will receive consistent impulse force initiated by this sphere. \n
 * The force direction is from the sphere origin to the camera position.
 */
UCLASS(ClassGroup = ComposableCameraSystem, Placeable, NotBlueprintable)
class COMPOSABLECAMERASYSTEM_API AComposableCameraImpulseSphere
	: public AActor
	, public IComposableCameraImpulseShapeInterface
{
	GENERATED_BODY()

public:
	AComposableCameraImpulseSphere();

	//~~ IComposableCameraImpulseShapeInterface
	virtual FVector GetForce(const FVector& CameraPosition) override;
	virtual FVector GetOrigin() override;
	virtual AActor* GetSelf() override { return this; }
	//~~

public:
	// Radius of this impulse sphere. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComposableCameraImpulseSphere")
	float Radius { 100.f };

	// Force curve along the distance to sphere center. Practically, F(0) should be the largest, F(Radius) should be zero. Set it to a relatively large value, e.g., 1000.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComposableCameraImpulseSphere")
	FRuntimeFloatCurve ForceCurve;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> SphereComponent;
};
