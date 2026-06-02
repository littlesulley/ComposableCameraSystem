// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraVolumeConstraintNode.h"

#include "Components/BoxComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SphereComponent.h"
#include "ComposableCameraSystemModule.h"
#include "GameFramework/Actor.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowVolumeConstraintGizmo(
		TEXT("CCS.Debug.Viewport.VolumeConstraint"),
		0,
		TEXT("Show VolumeConstraintNode gizmo (box/sphere wireframe, green when camera is inside, red when being clamped; small marker at the clamped point).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Works in both possessed play and F8 eject."),
		ECVF_Default);
}
#endif

void UComposableCameraVolumeConstraintNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Build three independent 1D interpolator instances from the shared
	// template, so each axis retains its own filter state (a spring
	// overshoot on X must not bleed into Y / Z). Any of these may be null
	// if the user left ClampInterpolator unset or the interpolator class
	// does not support BuildDoubleInterpolator. Guarded at call sites.
	ClampInterpolatorX_T = IsValid(ClampInterpolator) ? ClampInterpolator->BuildDoubleInterpolator() : nullptr;
	ClampInterpolatorY_T = IsValid(ClampInterpolator) ? ClampInterpolator->BuildDoubleInterpolator() : nullptr;
	ClampInterpolatorZ_T = IsValid(ClampInterpolator) ? ClampInterpolator->BuildDoubleInterpolator() : nullptr;

	bHasSeededSmoothing = false;
	LastSmoothedPosition = FVector::ZeroVector;
}

void UComposableCameraVolumeConstraintNode::OnTickNode_Implementation(
	float DeltaTime, const FComposableCameraPose& /*CurrentCameraPose*/, FComposableCameraPose& OutCameraPose)
{
	const FVector UpstreamPos = OutCameraPose.Position;

	// Seed the smoothing state on the very first tick so the first frame
	// doesn't visibly drift from origin up to the actual camera position.
	if (!bHasSeededSmoothing)
	{
		LastSmoothedPosition = UpstreamPos;
		bHasSeededSmoothing = true;
	}

	FResolvedVolume Volume;
	if (!ResolveVolume(Volume))
	{
		// Volume unusable. Keep smoothing state coherent with the current
		// pass-through output so re-enabling the constraint later doesn't
		// cause a catch-up snap.
		LastSmoothedPosition = UpstreamPos;
#if !UE_BUILD_SHIPPING
		DebugHasResolvedVolume = false;
		DebugIsClamping = false;
#endif
		return;
	}

	bool bAlreadyInside = false;
	const FVector Target = NearestPointInVolume(Volume, UpstreamPos, bAlreadyInside);

	// Apply per-axis smoothing when an interpolator is configured. Each
	// axis uses its own filter instance. Reset(current, target) updates
	// the filter's goal, Run(DT) advances one step. Without an interpolator
	// the output is the hard clamp (or pass-through when already inside).
	FVector Output;
	const bool bHasInterpolators =
		ClampInterpolatorX_T && ClampInterpolatorY_T && ClampInterpolatorZ_T;

	if (bHasInterpolators)
	{
		ClampInterpolatorX_T->Reset(LastSmoothedPosition.X, Target.X);
		ClampInterpolatorY_T->Reset(LastSmoothedPosition.Y, Target.Y);
		ClampInterpolatorZ_T->Reset(LastSmoothedPosition.Z, Target.Z);

		Output.X = ClampInterpolatorX_T->Run(DeltaTime);
		Output.Y = ClampInterpolatorY_T->Run(DeltaTime);
		Output.Z = ClampInterpolatorZ_T->Run(DeltaTime);
	}
	else
	{
		Output = Target;
	}

	OutCameraPose.Position = Output;
	LastSmoothedPosition = Output;

#if !UE_BUILD_SHIPPING
	DebugResolvedVolume = Volume;
	DebugHasResolvedVolume = true;
	DebugIsClamping = !bAlreadyInside;
	DebugClampedPosition = Target;
	DebugUpstreamPosition = UpstreamPos;
#endif
}

bool UComposableCameraVolumeConstraintNode::ResolveVolume(FResolvedVolume& OutVolume) const
{
	switch (VolumeSource)
	{
	case EComposableCameraVolumeSource::FromActor:
		{
			if (!VolumeActor)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("VolumeConstraintNode: VolumeSource=FromActor but VolumeActor is null; pass-through this tick."));
				return false;
			}

			// TInlineComponentArray keeps the traversal stack-allocated for the
			// common single-shape case. Zero heap churn on the hot path.
			TInlineComponentArray<UShapeComponent*> Shapes;
			VolumeActor->GetComponents(Shapes);
			if (Shapes.Num() == 0)
			{
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("VolumeConstraintNode: VolumeActor '%s' has no UShapeComponent; pass-through this tick."),
					*VolumeActor->GetName());
				return false;
			}

			UShapeComponent* First = Shapes[0];
			if (UBoxComponent* Box = Cast<UBoxComponent>(First))
			{
				OutVolume.Shape = EComposableCameraVolumeShape::Box;
				OutVolume.Center = Box->GetComponentLocation();
				OutVolume.Rotation = Box->GetComponentRotation();
				OutVolume.BoxExtents = Box->GetScaledBoxExtent();
				return true;
			}
			if (USphereComponent* Sphere = Cast<USphereComponent>(First))
			{
				OutVolume.Shape = EComposableCameraVolumeShape::Sphere;
				OutVolume.Center = Sphere->GetComponentLocation();
				OutVolume.Rotation = FRotator::ZeroRotator;
				OutVolume.SphereRadius = Sphere->GetScaledSphereRadius();
				return true;
			}

			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("VolumeConstraintNode: VolumeActor '%s' first UShapeComponent '%s' is an unsupported subclass (only Box / Sphere are supported)."),
				*VolumeActor->GetName(), *First->GetName());
			return false;
		}

	case EComposableCameraVolumeSource::Inline:
		OutVolume.Shape = Shape;
		OutVolume.Center = VolumeCenter;
		OutVolume.Rotation = VolumeRotation;
		OutVolume.BoxExtents = BoxExtents;
		OutVolume.SphereRadius = SphereRadius;
		return true;
	}

	return false;
}

FVector UComposableCameraVolumeConstraintNode::NearestPointInVolume(
	const FResolvedVolume& Volume, const FVector& WorldPos, bool& OutIsAlreadyInside)
{
	switch (Volume.Shape)
	{
	case EComposableCameraVolumeShape::Box:
		{
			// Transform into the OBB's local space (so the "box" is AABB
			// aligned to local axes), clamp per-axis, transform back.
			const FVector Local = Volume.Rotation.UnrotateVector(WorldPos - Volume.Center);
			const FVector HalfExtents = Volume.BoxExtents.ComponentMax(FVector::ZeroVector);

			const FVector ClampedLocal(
				FMath::Clamp(Local.X, -HalfExtents.X, HalfExtents.X),
				FMath::Clamp(Local.Y, -HalfExtents.Y, HalfExtents.Y),
				FMath::Clamp(Local.Z, -HalfExtents.Z, HalfExtents.Z));

			OutIsAlreadyInside = ClampedLocal.Equals(Local);
			return Volume.Rotation.RotateVector(ClampedLocal) + Volume.Center;
		}

	case EComposableCameraVolumeShape::Sphere:
		{
			const FVector Delta = WorldPos - Volume.Center;
			const float Radius = FMath::Max(Volume.SphereRadius, 0.f);
			const float DistSq = Delta.SizeSquared();

			OutIsAlreadyInside = DistSq <= Radius * Radius;
			if (OutIsAlreadyInside)
			{
				return WorldPos;
			}

			// Handle the degenerate "WorldPos == Center, Radius > 0" case so
			// SafeNormal doesn't silently return ZeroVector and plant the
			// camera exactly at center (it's technically valid. Any point on
			// the sphere is "nearest". But picking world-forward gives a
			// predictable, non-NaN result).
			const FVector Direction = Delta.IsNearlyZero()
				? FVector::ForwardVector
				: Delta.GetSafeNormal();
			return Volume.Center + Direction * Radius;
		}
	}

	OutIsAlreadyInside = true;
	return WorldPos;
}

void UComposableCameraVolumeConstraintNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: volume actor (FromActor mode. Most commonly wired at runtime
	// from a context parameter naming the room / arena / etc.).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "VolumeActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "VolConstr_VolumeActor", "Volume Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "VolConstr_VolumeActor_Tip",
			"Actor with a UBoxComponent or USphereComponent defining the constraint volume (FromActor mode).");
		OutPins.Add(Pin);
	}

	// Input: inline sphere radius. Commonly driven dynamically in Inline
	// mode ("shrink the arena radius over time" etc.).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "SphereRadius";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "VolConstr_SphereRadius", "Sphere Radius");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(SphereRadius);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "VolConstr_SphereRadius_Tip",
			"Radius of the Inline-mode sphere volume, in world units.");
		OutPins.Add(Pin);
	}

	// Input: inline box extents. Same reasoning as SphereRadius.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "BoxExtents";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "VolConstr_BoxExtents", "Box Extents");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = BoxExtents.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "VolConstr_BoxExtents_Tip",
			"Half-extents of the Inline-mode box volume in its local space (pre-rotation).");
		OutPins.Add(Pin);
	}

	// Input: inline volume center. Allows repositioning the Inline volume
	// at runtime without swapping cameras.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "VolumeCenter";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "VolConstr_Center", "Volume Center");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Vector3D;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = VolumeCenter.ToString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "VolConstr_Center_Tip",
			"Center of the Inline-mode volume in world space.");
		OutPins.Add(Pin);
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraVolumeConstraintNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowVolumeConstraintGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }

	if (!DebugHasResolvedVolume) { return; }

	// Green wireframe when the camera is happily inside; red when we had to
	// clamp this tick. Volume edges sit out in the world around the player,
	// not at the camera itself, no F8 gate needed.
	const FColor VolumeColor = DebugIsClamping ? FColor(255, 90, 90) : FColor(90, 255, 120);

	switch (DebugResolvedVolume.Shape)
	{
	case EComposableCameraVolumeShape::Box:
		DrawDebugBox(World, DebugResolvedVolume.Center,
			DebugResolvedVolume.BoxExtents,
			DebugResolvedVolume.Rotation.Quaternion(),
			VolumeColor,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/0, /*Thickness=*/1.5f);
		break;

	case EComposableCameraVolumeShape::Sphere:
		DrawDebugSphere(World, DebugResolvedVolume.Center,
			DebugResolvedVolume.SphereRadius,
			/*Segments=*/24, VolumeColor,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f,
			/*DepthPriority=*/0, /*Thickness=*/1.5f);
		break;
	}

	// Small sphere at the projected-back clamped position when active, so
	// the author can see exactly where the camera got pulled to.
	if (DebugIsClamping)
	{
		FComposableCameraViewportDebug::DrawSolidDebugSphere(
			World, DebugClampedPosition, /*Radius=*/8.f, VolumeColor,
			/*Alpha=*/160, /*Segments=*/12, /*DepthPriority=*/0);
	}
}
#endif
