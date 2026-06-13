// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShot.h"

namespace ComposableCameraSystem::ShotDetailsVisibility
{
inline bool IsAnchorFieldVisible(EShotAnchorMode Mode, FName PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, Mode))
	{
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, TargetIndex))
	{
		return Mode == EShotAnchorMode::SingleTarget;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, WeightedTargets))
	{
		return Mode == EShotAnchorMode::WeightedWorldCentroid;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FComposableCameraAnchorSpec, WorldPosition))
	{
		return Mode == EShotAnchorMode::FixedWorldPosition;
	}
	return true;
}

inline bool IsPlacementFieldVisible(EShotPlacementMode Mode,
	EShotPlacementBasisFrame BasisFrame,
	FName PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, Mode))
	{
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementAnchor))
	{
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisFrame)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, LocalCameraDirection))
	{
		return Mode == EShotPlacementMode::AnchorOrbit;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, BasisActorIndex))
	{
		return Mode == EShotPlacementMode::AnchorOrbit
			&& BasisFrame == EShotPlacementBasisFrame::InheritFromActor;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, Distance)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, DistanceSpeed))
	{
		return Mode == EShotPlacementMode::AnchorOrbit
			|| Mode == EShotPlacementMode::AnchorAtScreen;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, ScreenPosition)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementZones))
	{
		return Mode == EShotPlacementMode::AnchorAtScreen;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotPlacement, FixedWorldPosition))
	{
		return Mode == EShotPlacementMode::FixedWorldPosition;
	}
	return true;
}

inline bool IsAimFieldVisible(EShotAimMode Mode, FName PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotAim, Mode))
	{
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotAim, AimAnchor))
	{
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotAim, ScreenPosition)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FShotAim, AimZones))
	{
		return Mode == EShotAimMode::LookAtAnchor;
	}
	return true;
}

inline bool IsLensFieldVisible(EShotFOVMode Mode, FName PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotLens, FOVMode)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FShotLens, Aperture)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FShotLens, FOVSpeed))
	{
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotLens, ManualFOV))
	{
		return Mode == EShotFOVMode::Manual;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotLens, DesiredViewportFillRatio)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FShotLens, FOVClamp))
	{
		return Mode == EShotFOVMode::SolvedFromBoundsFit;
	}
	return true;
}

inline bool IsFocusFieldVisible(EShotFocusMode Mode, FName PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotFocus, Mode))
	{
		return true;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotFocus, ManualDistance))
	{
		return Mode == EShotFocusMode::Manual;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FShotFocus, FocusAnchor))
	{
		return Mode == EShotFocusMode::FollowCustomAnchor;
	}
	return true;
}
} // namespace ComposableCameraSystem::ShotDetailsVisibility
