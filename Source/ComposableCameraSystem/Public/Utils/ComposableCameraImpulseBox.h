// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraImpulseShapeInterface.h"
#include "GameFramework/Actor.h"
#include "ComposableCameraImpulseBox.generated.h"

class UBoxComponent;

UENUM()
enum class EComposableCameraImpulseBoxDistanceType : uint8
{
	// Use box's center as the origin (for calculating the force).
	BoxOrigin,
	
	// The origin is the point projected onto the X axis from camera position.
	XAxis,

	// The origin is the point projected onto the Y axis from camera position.
	YAxis,

	// The origin is the point projected onto the Z axis from camera position.
	ZAxis,

	// The origin is the point projected onto the XY plane from camera position.
	XYPlane,

	// The origin is the point projected onto the XZ plane from camera position.
	XZPlane,

	// The origin is the point projected onto the YZ plane from camera position.
	YZPlane
};

UCLASS(ClassGroup = ComposableCameraSystem, Placeable, NotBlueprintable)
class COMPOSABLECAMERASYSTEM_API AComposableCameraImpulseBox
	: public AActor
	, public IComposableCameraImpulseShapeInterface
{
	GENERATED_BODY()

public:
	AComposableCameraImpulseBox();

	//~~ IComposableCameraImpulseShapeInterface
	virtual FVector GetForce(const FVector& CameraPosition) override;
	virtual FVector GetOrigin() override;
	virtual AActor* GetSelf() override { return this; }
	//~~

	FVector GetOrigin(const FVector& CameraPosition);

public:
	// How to compute the distance when the camera is in the box. Set it to a relatively large value, e.g., 1000.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComposableCameraImpulseBox")
	EComposableCameraImpulseBoxDistanceType DistanceType { EComposableCameraImpulseBoxDistanceType::BoxOrigin };
	
	// Force curve along the distance. Practically, F(0) should be the largest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComposableCameraImpulseBox")
	FRuntimeFloatCurve ForceCurve;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> BoxComponent;
};
