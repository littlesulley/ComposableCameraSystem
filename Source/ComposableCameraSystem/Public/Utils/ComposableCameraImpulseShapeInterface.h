// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Nodes/ComposableCameraImpulseResolutionNode.h"
#include "UObject/Interface.h"
#include "ComposableCameraImpulseShapeInterface.generated.h"

UINTERFACE()
class COMPOSABLECAMERASYSTEM_API UComposableCameraImpulseShapeInterface : public UInterface
{
	GENERATED_BODY()
};

class IComposableCameraImpulseShapeInterface
{
	GENERATED_BODY()

public:
	virtual FVector GetForce(const FVector& CameraPosition) = 0;
	virtual FVector GetOrigin() = 0;
	virtual AActor* GetSelf() = 0;

	UFUNCTION()
	virtual void BindToOnComponentBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult)
	{
		if (auto* Camera = Cast<AComposableCameraCameraBase>(OtherActor))
		{
			if (auto* Node = Cast<UComposableCameraImpulseResolutionNode>(Camera->GetNodeByClass(UComposableCameraImpulseResolutionNode::StaticClass())))
			{
				Node->AddImpulseShape(GetSelf());
			}
		}
	}

	UFUNCTION()
	virtual void BindToOnComponentEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex)
	{
		if (auto* Camera = Cast<AComposableCameraCameraBase>(OtherActor))
		{
			if (auto* Node = Cast<UComposableCameraImpulseResolutionNode>(Camera->GetNodeByClass(UComposableCameraImpulseResolutionNode::StaticClass())))
			{
				Node->RemoveImpulseShape(GetSelf());
			}
		}
	}
};