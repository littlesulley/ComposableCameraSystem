// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

struct COMPOSABLECAMERASYSTEM_API FComposableCameraLockOnAimPointState
{
	bool bInModify = false;
	bool bHasCurrentAddition = false;
	FVector CurrentAddition = FVector::ZeroVector;
	bool bIsBlendingOut = false;
	float BlendOutElapsedTime = 0.f;
	FVector BlendOutStartAddition = FVector::ZeroVector;
};

namespace ComposableCameraSystem
{
	COMPOSABLECAMERASYSTEM_API FVector ComputeLockOnAimPoint(
		const FVector& FollowPosition,
		const FVector& AimPosition,
		const FVector& CameraPosition,
		const FVector& CameraForward,
		float Radius,
		const FVector& Weights,
		const FVector2D& PitchRange,
		FComposableCameraLockOnAimPointState& State,
		float DeltaTime = 0.f,
		float BlendOutTime = 0.f);
}
