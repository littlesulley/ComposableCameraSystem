// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraImpulseBox.h"
#include "Components/BoxComponent.h"
#include "Kismet/KismetSystemLibrary.h"

AComposableCameraImpulseBox::AComposableCameraImpulseBox()
{
	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxComponent"));
	BoxComponent->SetGenerateOverlapEvents(true);
	BoxComponent->OnComponentBeginOverlap.AddDynamic(this, &IComposableCameraImpulseShapeInterface::BindToOnComponentBeginOverlap);
	BoxComponent->OnComponentEndOverlap.AddDynamic(this, &IComposableCameraImpulseShapeInterface::BindToOnComponentEndOverlap);
}

FVector AComposableCameraImpulseBox::GetForce(const FVector& CameraPosition)
{
	FVector Origin = GetOrigin(CameraPosition);
	FVector Direction = CameraPosition - Origin;
	float Distance = Direction.Length();
	Direction.Normalize();

	float ForceMag = 0.f;
	
	if (FRichCurve* InternalCurve = ForceCurve.GetRichCurve())
	{
		ForceMag = InternalCurve->Eval(Distance);
	}
	else if (UCurveFloat* ExternalCurve = ForceCurve.ExternalCurve)
	{
		ForceMag = ExternalCurve->GetFloatValue(Distance);
	}
	
	return Direction * ForceMag;
}

FVector AComposableCameraImpulseBox::GetOrigin()
{
	return BoxComponent->GetComponentLocation();
}

FVector AComposableCameraImpulseBox::GetOrigin(const FVector& CameraPosition)
{
	switch (DistanceType)
	{
	case EComposableCameraImpulseBoxDistanceType::BoxOrigin:
		{
			return BoxComponent->GetComponentLocation();
		}
	case EComposableCameraImpulseBoxDistanceType::XAxis:
		{
			FVector Origin = BoxComponent->GetComponentLocation();
			FVector Axis = BoxComponent->GetForwardVector();
			FVector Direction = CameraPosition - Origin;
			return Origin + Direction.ProjectOnToNormal(Axis);
		}
	case EComposableCameraImpulseBoxDistanceType::YAxis:
		{
			FVector Origin = BoxComponent->GetComponentLocation();
			FVector Axis = BoxComponent->GetRightVector();
			FVector Direction = CameraPosition - Origin;
			return Origin + Direction.ProjectOnToNormal(Axis);
		}
	case EComposableCameraImpulseBoxDistanceType::ZAxis:
		{
			FVector Origin = BoxComponent->GetComponentLocation();
			FVector Axis = BoxComponent->GetUpVector();
			FVector Direction = CameraPosition - Origin;
			
			return Origin + Direction.ProjectOnToNormal(Axis);
		}
	case EComposableCameraImpulseBoxDistanceType::XYPlane:
		{
			FVector Origin = BoxComponent->GetComponentLocation();
			FVector Normal = BoxComponent->GetUpVector();
			FVector Direction = CameraPosition - Origin;
			return Origin + FVector::VectorPlaneProject(Direction, Normal);
		}
	case EComposableCameraImpulseBoxDistanceType::XZPlane:
		{
			FVector Origin = BoxComponent->GetComponentLocation();
			FVector Normal = BoxComponent->GetRightVector();
			FVector Direction = CameraPosition - Origin;
			return Origin + FVector::VectorPlaneProject(Direction, Normal);
		}
	case EComposableCameraImpulseBoxDistanceType::YZPlane:
		{
			FVector Origin = BoxComponent->GetComponentLocation();
			FVector Normal = BoxComponent->GetForwardVector();
			FVector Direction = CameraPosition - Origin;
			return Origin + FVector::VectorPlaneProject(Direction, Normal);
		}
	}

	return GetOrigin();
}
