// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "DataAssets/ComposableCameraShotTarget.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"

namespace ComposableCameraSystem::ShotSubjectGizmo
{
struct FSubjectGeometry
{
	FVector Base = FVector::ZeroVector;
	FVector Pivot = FVector::ZeroVector;
	FQuat OffsetRotation = FQuat::Identity;
	FQuat BasisRotation = FQuat::Identity;
	bool bUsedBone = false;
	bool bHasBasis = false;
};

inline bool Resolve(const FComposableCameraTargetInfo& Target, FSubjectGeometry& Out)
{
	Out = {};
	FTransform Frame;
	if (!Target.ResolvePivotTransform(Frame, &Out.bUsedBone)) return false;
	Out.Base = Frame.GetLocation();
	Out.OffsetRotation = Target.bOffsetInLocalSpace ? Frame.GetRotation() : FQuat::Identity;
	Out.Pivot = Out.Base + Out.OffsetRotation.RotateVector(Target.Offset);
	Out.bHasBasis = Target.ResolveBasisQuat(Out.BasisRotation);
	return !Out.Base.ContainsNaN() && !Out.Pivot.ContainsNaN();
}

/** Non-owning identity snapshot. Avoid copying soft-path strings for every painted handle. */
struct FSourceIdentity
{
	TWeakObjectPtr<AActor> Actor;
	FName Component, Bone;
	bool bBone = false, bLocal = false, bMeshBasis = false;
#if WITH_EDITORONLY_DATA
	TWeakObjectPtr<USkeletalMesh> PreviewMesh;
	FTransform PreviewTransform, MeshRelativeTransform;
#endif
	void Capture(const FComposableCameraTargetInfo& Target)
	{
		Actor = Target.Actor.Get(); Component = Target.ComponentName; Bone = Target.BoneName;
		bBone = Target.bUseBoneAsPivot; bLocal = Target.bOffsetInLocalSpace; bMeshBasis = Target.bUseSkeletalMeshForwardAsBasis;
#if WITH_EDITORONLY_DATA
		PreviewMesh = Target.EditorPreviewMesh.Get(); PreviewTransform = Target.EditorPreviewTransform;
		MeshRelativeTransform = Target.EditorPreviewMeshRelativeTransform;
#endif
	}
	bool Matches(const FComposableCameraTargetInfo& Target) const
	{
		return Actor.Get() == Target.Actor.Get() && Component == Target.ComponentName && Bone == Target.BoneName
			&& bBone == Target.bUseBoneAsPivot && bLocal == Target.bOffsetInLocalSpace && bMeshBasis == Target.bUseSkeletalMeshForwardAsBasis
#if WITH_EDITORONLY_DATA
			&& PreviewMesh.Get() == Target.EditorPreviewMesh.Get() && PreviewTransform.Equals(Target.EditorPreviewTransform)
			&& MeshRelativeTransform.Equals(Target.EditorPreviewMeshRelativeTransform)
#endif
			;
	}
};

inline FVector Axis(int32 Index)
{
	return Index == 0 ? FVector::ForwardVector : Index == 1 ? FVector::RightVector : FVector::UpVector;
}

/** A camera-facing axis has no reliable screen motion; display it without a hit area. */
inline bool CanDrag(const FVector2D& PixelsPerUnit)
{
	return !PixelsPerUnit.ContainsNaN() && PixelsPerUnit.SquaredLength() >= .0001;
}

inline double DragDelta(const FVector2D& MouseDelta, const FVector2D& PixelsPerUnit, double Speed = 1.)
{
	return CanDrag(PixelsPerUnit) ? FVector2D::DotProduct(MouseDelta, PixelsPerUnit) / PixelsPerUnit.SquaredLength() * Speed : 0.;
}

inline bool HasBounds(const FComposableCameraShotTarget& Target)
{
	const FVector Extent = Target.GetEffectiveBoundsExtent();
	return Target.BoundsShape != EShotTargetBoundsShape::None && !Extent.ContainsNaN()
		&& !Extent.IsZero() && Extent.X >= 0. && Extent.Y >= 0. && Extent.Z >= 0.;
}
}
