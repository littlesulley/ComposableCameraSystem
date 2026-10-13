// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotEditorDiagnostics.h"
#include "Editors/ComposableCameraShotSubjectLabel.h"
#include "DataAssets/ComposableCameraShot.h"

#define LOCTEXT_NAMESPACE "ShotEditorDiagnostics"
namespace
{
int32 FailedAnchorTarget(const FComposableCameraAnchorSpec& Anchor, const FComposableCameraShot& Shot)
{
	if (Anchor.Mode == EShotAnchorMode::SingleTarget) return Anchor.TargetIndex;
	if (Anchor.Mode == EShotAnchorMode::WeightedWorldCentroid)
		for (const auto& Entry : Anchor.WeightedTargets)
		{
			FVector Point;
			if (Entry.Weight > 0.f && (!Shot.Targets.IsValidIndex(Entry.TargetIndex)
				|| !Shot.Targets[Entry.TargetIndex].Target.ResolveWorldPoint(Point))) return Entry.TargetIndex;
		}
	return INDEX_NONE;
}
}

FShotEditorDiagnostic DiagnoseShotPreview(const FComposableCameraShot& Shot, bool bPoseValid,
	bool bPoseHeld, float SolvedFOV, bool bNativeOutput)
{
	using EIssue = EShotEditorDiagnosticIssue;
	using EPage = EShotEditorDiagnosticPage;
	FVector Follow, Aim;
	const bool bUsesFollow = Shot.Placement.Mode != EShotPlacementMode::FixedWorldPosition;
	const bool bUsesAim = Shot.Aim.Mode == EShotAimMode::LookAtAnchor;
	if (bUsesFollow && !Shot.Placement.PlacementAnchor.ResolveWorldPosition(Shot.Targets, Follow))
		return { EIssue::FollowAnchor, EPage::Follow, FailedAnchorTarget(Shot.Placement.PlacementAnchor, Shot), bPoseHeld };
	if (bUsesAim && !Shot.Aim.AimAnchor.ResolveWorldPosition(Shot.Targets, Aim))
		return { EIssue::AimAnchor, EPage::Aim, FailedAnchorTarget(Shot.Aim.AimAnchor, Shot), bPoseHeld };
	if (!bNativeOutput && !bPoseValid)
	{
		if (Shot.Placement.Mode == EShotPlacementMode::AnchorAtScreen && bUsesAim && Follow.Equals(Aim))
			return { EIssue::CoincidentAnchors, EPage::Follow, INDEX_NONE, bPoseHeld };
		return { EIssue::PoseUnavailable, EPage::Follow, INDEX_NONE, bPoseHeld };
	}
	if (Shot.Placement.Mode == EShotPlacementMode::AnchorOrbit)
	{
		const auto& P = Shot.Placement;
		if (P.BasisFrame == EShotPlacementBasisFrame::InheritFromActor)
		{
			FQuat Basis;
			if (!Shot.Targets.IsValidIndex(P.BasisActorIndex) || !Shot.Targets[P.BasisActorIndex].Target.ResolveBasisQuat(Basis))
				return { EIssue::BasisFallback, EPage::Follow, P.BasisActorIndex };
		}
		else if (P.BasisFrame == EShotPlacementBasisFrame::TwoTargetAxis)
		{
			FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
			for (int32 Index : { P.BasisActorIndex, P.BasisSecondaryTargetIndex })
			{
				FVector Point;
				if (!Shot.Targets.IsValidIndex(Index) || !Shot.Targets[Index].Target.ResolveWorldPoint(Point))
					return { EIssue::BasisFallback, EPage::Follow, Index };
				if (Index == P.BasisActorIndex) A = Point;
				else B = Point;
			}
			// Equal indices also represent a degenerate pair.
			if (P.BasisActorIndex == P.BasisSecondaryTargetIndex || FVector2D(A.X - B.X, A.Y - B.Y).IsNearlyZero())
				return { EIssue::BasisFallback, EPage::Follow };
		}
	}
	const FComposableCameraAnchorSpec* Focus = nullptr;
	switch (Shot.Focus.Mode)
	{
	case EShotFocusMode::Manual: break;
	case EShotFocusMode::FollowPlacementAnchor: Focus = &Shot.Placement.PlacementAnchor; break;
	case EShotFocusMode::FollowAimAnchor: Focus = &Shot.Aim.AimAnchor; break;
	case EShotFocusMode::FollowCustomAnchor: Focus = &Shot.Focus.FocusAnchor; break;
	}
	FVector FocusPoint;
	if (Focus && !Focus->ResolveWorldPosition(Shot.Targets, FocusPoint))
		return { EIssue::FocusAnchor, EPage::Lens, FailedAnchorTarget(*Focus, Shot) };
	// Native output includes blends/patches; its FOV is not the source Shot's lens solve.
	if (!bNativeOutput && Shot.Lens.FOVMode == EShotFOVMode::SolvedFromBoundsFit)
	{
		bool bHasBounds = false;
		for (const auto& Target : Shot.Targets)
		{
			FVector Point;
			if (Target.BoundsContributionWeight > 0.f && !Target.GetEffectiveBoundsExtent().IsZero()
				&& Target.Target.ResolveWorldPoint(Point)) { bHasBounds = true; break; }
		}
		if (!bHasBounds) return { EIssue::NoFramingBounds, EPage::Subjects };
		if (FMath::IsNearlyEqual(SolvedFOV, Shot.Lens.FOVClamp.Min, .01f)) return { EIssue::FOVMinimum, EPage::Lens };
		if (FMath::IsNearlyEqual(SolvedFOV, Shot.Lens.FOVClamp.Max, .01f)) return { EIssue::FOVMaximum, EPage::Lens };
	}
	return {};
}

const TCHAR* FShotEditorDiagnostic::PropertyPath() const
{
	switch (Issue)
	{
	case EShotEditorDiagnosticIssue::FollowAnchor: return TEXT("Placement.PlacementAnchor");
	case EShotEditorDiagnosticIssue::AimAnchor: return TEXT("Aim.AimAnchor");
	case EShotEditorDiagnosticIssue::BasisFallback: return TEXT("Placement.BasisFrame");
	case EShotEditorDiagnosticIssue::FocusAnchor: return TEXT("Focus.Mode");
	case EShotEditorDiagnosticIssue::FOVMinimum:
	case EShotEditorDiagnosticIssue::FOVMaximum: return TEXT("Lens.FOVClamp");
	case EShotEditorDiagnosticIssue::None:
	case EShotEditorDiagnosticIssue::SourceUnavailable:
	case EShotEditorDiagnosticIssue::CoincidentAnchors:
	case EShotEditorDiagnosticIssue::PoseUnavailable: return TEXT("Placement.Mode");
	case EShotEditorDiagnosticIssue::NoFramingBounds: return TEXT("");
	}
	return TEXT("");
}

FText FShotEditorDiagnostic::ToText() const
{
	FText Reason;
	switch (Issue)
	{
	case EShotEditorDiagnosticIssue::None: return FText::GetEmpty();
	case EShotEditorDiagnosticIssue::SourceUnavailable: Reason = LOCTEXT("Source", "Shot source unavailable. Check the section's source asset."); break;
	case EShotEditorDiagnosticIssue::FollowAnchor: Reason = LOCTEXT("Follow", "Follow anchor unresolved. Check its subject index, actor/component and anchor weights."); break;
	case EShotEditorDiagnosticIssue::AimAnchor: Reason = LOCTEXT("Aim", "Aim anchor unresolved. Check its subject index, actor/component and anchor weights."); break;
	case EShotEditorDiagnosticIssue::CoincidentAnchors: Reason = LOCTEXT("Coincident", "Anchor At Screen cannot seed coincident Follow/Aim anchors. Separate the anchors or use Anchor Orbit."); break;
	case EShotEditorDiagnosticIssue::PoseUnavailable: Reason = LOCTEXT("Pose", "Shot pose unavailable. Check Follow/Aim modes, anchor geometry and screen positions."); break;
	case EShotEditorDiagnosticIssue::BasisFallback: Reason = LOCTEXT("Basis", "Orbit basis falls back to World. Check basis subjects and the horizontal A-to-B separation."); break;
	case EShotEditorDiagnosticIssue::FocusAnchor: Reason = LOCTEXT("Focus", "Focus anchor unresolved. Manual focus distance is used as fallback."); break;
	case EShotEditorDiagnosticIssue::NoFramingBounds: Reason = LOCTEXT("Bounds", "Bounds-fit has no resolved contributing bounds. Check subject bounds and weights."); break;
	case EShotEditorDiagnosticIssue::FOVMinimum: Reason = LOCTEXT("FOVMin", "Bounds-fit FOV reached its minimum. Check FOV Clamp, fill ratio or follow distance."); break;
	case EShotEditorDiagnosticIssue::FOVMaximum: Reason = LOCTEXT("FOVMax", "Bounds-fit FOV reached its maximum. Check FOV Clamp, fill ratio or follow distance."); break;
	}
	if (TargetIndex != INDEX_NONE)
	{
		const FText Subject = ComposableCameraSystem::ShotSubjectLabels::ToText(TargetIndex);
		Reason = FText::Format(LOCTEXT("WithSubject", "{0} {1}"), Subject, Reason);
	}
	return bPoseHeld ? FText::Format(LOCTEXT("HeldPose", "{0} Camera pose not updated; last view retained."), Reason) : Reason;
}
#undef LOCTEXT_NAMESPACE
