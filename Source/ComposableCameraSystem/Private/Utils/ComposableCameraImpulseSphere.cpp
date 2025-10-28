// Copyright Sulley. All rights reserved.

#include "Utils/ComposableCameraImpulseSphere.h"

#include "Components/SphereComponent.h"


AComposableCameraImpulseSphere::AComposableCameraImpulseSphere()
{
	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComponent"));
	SphereComponent->InitSphereRadius(Radius);
	SphereComponent->SetSphereRadius(Radius);
	SphereComponent->SetGenerateOverlapEvents(true);
	SphereComponent->OnComponentBeginOverlap.AddDynamic(this, &IComposableCameraImpulseShapeInterface::BindToOnComponentBeginOverlap);
	SphereComponent->OnComponentEndOverlap.AddDynamic(this, &IComposableCameraImpulseShapeInterface::BindToOnComponentEndOverlap);
}

FVector AComposableCameraImpulseSphere::GetForce(const FVector& CameraPosition)
{
	FVector Origin = GetOrigin();
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

FVector AComposableCameraImpulseSphere::GetOrigin()
{
	return SphereComponent->GetComponentLocation();
}

#if WITH_EDITOR
void AComposableCameraImpulseSphere::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		const FName PropertyName(PropertyChangedEvent.Property->GetName());
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AComposableCameraImpulseSphere, Radius))
		{
			SphereComponent->SetSphereRadius(Radius);
		}
	}
}
#endif

