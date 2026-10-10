// Copyright 2026 Sulley. All Rights Reserved.
#include "Editors/ComposableCameraShotTemplates.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotTemplates"
namespace ComposableCameraSystem::ShotAuthoring
{
FText TemplateLabel(EComposableShotTemplate Template)
{
	switch (Template)
	{
	case EComposableShotTemplate::CloseUp: return LOCTEXT("Close", "Single / Close-up");
	case EComposableShotTemplate::Medium: return LOCTEXT("Medium", "Single / Medium");
	case EComposableShotTemplate::FullBody: return LOCTEXT("Full", "Single / Full body");
	case EComposableShotTemplate::TwoShot: return LOCTEXT("Two", "Pair / Two-shot");
	case EComposableShotTemplate::ShoulderLeft: return LOCTEXT("Left", "Pair / Left shoulder");
	case EComposableShotTemplate::ShoulderRight: return LOCTEXT("Right", "Pair / Right shoulder");
	case EComposableShotTemplate::ReverseShoulder: return LOCTEXT("Reverse", "Pair / Reverse shoulder");
	case EComposableShotTemplate::Group: return LOCTEXT("Group", "Group / Wide");
	}
	return FText::GetEmpty();
}

bool CanUseTemplate(EComposableShotTemplate Template, int32 Count)
{
	if (Template > EComposableShotTemplate::Group) return false;
	if (Template <= EComposableShotTemplate::FullBody) return Count == 1;
	if (Template == EComposableShotTemplate::Group) return Count >= 2;
	return Count == 2;
}

FComposableCameraShotTarget MakeTarget(AActor* Actor)
{
	FComposableCameraShotTarget Result;
	Result.Target.Actor = Actor;
	Result.BoundsShape = EShotTargetBoundsShape::AutoFromComponentBounds;
	Result.BoundsCachePolicy = EBoundsCachePolicy::Live;
	if (Actor)
	{
		Result.RefreshAutoBoundsCache();
		const UPrimitiveComponent* Mesh = Result.CachedBoundsMeshComponent.Get();
		const FBox Box = Mesh ? Mesh->Bounds.GetBox() : Actor->GetComponentsBoundingBox(true);
		const FVector Center = Box.IsValid ? Box.GetCenter() : Actor->GetActorLocation();
		Result.Target.Offset = Actor->GetActorQuat().UnrotateVector(Center - Actor->GetActorLocation());
		Result.Target.bOffsetInLocalSpace = true;
		Result.RefreshAutoBoundsCache();
	}
	return Result;
}

bool BuildTemplate(EComposableShotTemplate Template,
	TConstArrayView<FComposableCameraShotTarget> Targets, FComposableCameraShot& OutShot)
{
	if (!CanUseTemplate(Template, Targets.Num())) return false;
	FComposableCameraShot Shot;
	Shot.Targets.Append(Targets.GetData(), Targets.Num());
	// Templates derive a fresh pivot from current bounds instead of accumulating the previous template's offset.
	for (FComposableCameraShotTarget& Target : Shot.Targets)
	{
		if (AActor* Actor = Target.Target.Actor.Get(); Actor && !Target.Target.bUseBoneAsPivot)
		{
			Target.RefreshAutoBoundsCache();
			const UPrimitiveComponent* Mesh = Target.CachedBoundsMeshComponent.Get();
			const FVector Base = !Target.Target.ComponentName.IsNone() && Mesh ? Mesh->GetComponentLocation() : Actor->GetActorLocation();
			const FQuat Basis = !Target.Target.ComponentName.IsNone() && Mesh ? Mesh->GetComponentQuat() : Actor->GetActorQuat();
			const FVector Center = Mesh ? Mesh->Bounds.Origin : Base;
			Target.Target.Offset = Basis.UnrotateVector(Center - Base);
			Target.Target.bOffsetInLocalSpace = true;
		}
#if WITH_EDITORONLY_DATA
		else if (!Target.Target.Actor.IsValid() && !Target.Target.bUseBoneAsPivot)
		{
			if (const USkeletalMesh* Mesh = Target.Target.EditorPreviewMesh.LoadSynchronous())
			{
				const FTransform MeshWorld = Target.Target.EditorPreviewMeshRelativeTransform * Target.Target.EditorPreviewTransform;
				const FTransform Pivot = Target.Target.ComponentName.IsNone() ? Target.Target.EditorPreviewTransform : MeshWorld;
				const auto Bounds = Mesh->GetBounds().TransformBy(MeshWorld);
				Target.Target.Offset = Pivot.GetRotation().UnrotateVector(Bounds.Origin - Pivot.GetLocation());
				Target.Target.bOffsetInLocalSpace = true;
				if (Target.BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds) Target.CachedAutoBoundsExtent = Bounds.BoxExtent;
			}
		}
#endif
	}
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::InheritFromActor;
	Shot.Placement.LocalCameraDirection = FVector2D(0.f, 5.f);
	Shot.Lens.ManualFOV = 40.f;
	Shot.Focus.Mode = EShotFocusMode::FollowAimAnchor;
	Shot.Lens.Aperture = 2.8f;
	const FVector Extent = Shot.Targets[0].GetEffectiveBoundsExtent();
	const float Height = FMath::Max(static_cast<float>(Extent.Z * 2.0), 160.f);
	Shot.Placement.Distance = FMath::Clamp(Height * 2.f, FShotPlacement::MinDistance, FShotPlacement::MaxDistance);
	if (Targets.Num() == 1)
	{
		const float Size = Template == EComposableShotTemplate::CloseUp ? .32f
			: Template == EComposableShotTemplate::Medium ? .75f : 1.6f;
		Shot.Placement.Distance *= Size;
		// The selection helper authors a bounds-center pivot. Raise it toward the face for close shots.
		if (!Shot.Targets[0].Target.bUseBoneAsPivot)
		{
			const auto& Target = Shot.Targets[0].Target;
			FQuat PivotBasis = FQuat::Identity;
			bool bHasPivot = false;
			if (AActor* Actor = Target.Actor.Get())
			{
				PivotBasis = Actor->GetActorQuat(); bHasPivot = true;
				if (!Target.ComponentName.IsNone()) for (UActorComponent* Component : Actor->GetComponents())
					if (auto* Scene = Cast<USceneComponent>(Component); Scene && Scene->GetFName() == Target.ComponentName) { PivotBasis = Scene->GetComponentQuat(); break; }
			}
#if WITH_EDITORONLY_DATA
			else if (Target.EditorPreviewMesh.IsValid())
			{
				PivotBasis = (Target.ComponentName.IsNone() ? Target.EditorPreviewTransform : Target.EditorPreviewMeshRelativeTransform * Target.EditorPreviewTransform).GetRotation();
				bHasPivot = true;
			}
#endif
			if (bHasPivot)
				Shot.Targets[0].Target.Offset += PivotBasis.UnrotateVector(FVector(0, 0, Height * (Template == EComposableShotTemplate::CloseUp ? .35f
					: Template == EComposableShotTemplate::Medium ? .18f : 0.f)));
		}
	}
	else
	{
		Shot.Placement.BasisFrame = EShotPlacementBasisFrame::TwoTargetAxis;
		Shot.Placement.BasisActorIndex = 0;
		Shot.Placement.BasisSecondaryTargetIndex = 1;
		const bool bShoulder = Template != EComposableShotTemplate::TwoShot && Template != EComposableShotTemplate::Group;
		if (bShoulder)
		{
			const bool bReverse = Template == EComposableShotTemplate::ReverseShoulder;
			Shot.Placement.PlacementAnchor.TargetIndex = bReverse ? 1 : 0;
			Shot.Aim.AimAnchor.TargetIndex = bReverse ? 0 : 1;
			Shot.Placement.BasisActorIndex = Shot.Placement.PlacementAnchor.TargetIndex;
			Shot.Placement.BasisSecondaryTargetIndex = Shot.Aim.AimAnchor.TargetIndex;
			Shot.Placement.LocalCameraDirection = FVector2D(
				Template == EComposableShotTemplate::ShoulderLeft ? 160.f : 200.f, 7.f);
			Shot.Placement.Distance = Height * .8f;
			Shot.Aim.ScreenPosition.X = Template == EComposableShotTemplate::ShoulderLeft ? .12f : -.12f;
		}
		else
		{
			Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::WeightedWorldCentroid;
			FBox GroupBox(ForceInit);
			for (int32 Index = 0; Index < Targets.Num(); ++Index)
			{
				FComposableCameraAnchorTargetWeight& Weight = Shot.Placement.PlacementAnchor.WeightedTargets.AddDefaulted_GetRef();
				Weight.TargetIndex = Index;
				FVector Point;
				if (Shot.Targets[Index].Target.ResolveWorldPoint(Point)) GroupBox += Point;
#if WITH_EDITORONLY_DATA
				else if (!Targets[Index].Target.EditorPreviewMesh.IsNull()) GroupBox += Targets[Index].Target.EditorPreviewTransform.GetLocation();
#endif
			}
			Shot.Aim.AimAnchor = Shot.Placement.PlacementAnchor;
			Shot.Placement.LocalCameraDirection = FVector2D(90.f, 8.f);
			Shot.Placement.Distance = FMath::Max(Shot.Placement.Distance,
				GroupBox.IsValid ? static_cast<float>(GroupBox.GetExtent().Size() * 3.0) : 0.f);
			Shot.Lens.FOVMode = EShotFOVMode::SolvedFromBoundsFit;
			Shot.Lens.DesiredViewportFillRatio = .8f;
		}
	}
	Shot.Placement.Distance = FMath::Clamp(Shot.Placement.Distance, FShotPlacement::MinDistance, FShotPlacement::MaxDistance);
	OutShot = MoveTemp(Shot);
	return true;
}

bool MoveTarget(FComposableCameraShot& Shot, int32 From, int32 To)
{
	if (!Shot.Targets.IsValidIndex(From) || !Shot.Targets.IsValidIndex(To) || From == To) return false;
	FComposableCameraShotTarget Target = MoveTemp(Shot.Targets[From]);
	Shot.Targets.RemoveAt(From);
	Shot.Targets.Insert(MoveTemp(Target), To);
	const auto Remap = [From, To](int32& Index)
	{
		if (Index == From) Index = To;
		else if (From < To && Index > From && Index <= To) --Index;
		else if (To < From && Index >= To && Index < From) ++Index;
	};
	const auto RemapAnchor = [&Remap](FComposableCameraAnchorSpec& Anchor)
	{
		Remap(Anchor.TargetIndex);
		for (FComposableCameraAnchorTargetWeight& Weight : Anchor.WeightedTargets) Remap(Weight.TargetIndex);
	};
	RemapAnchor(Shot.Placement.PlacementAnchor);
	RemapAnchor(Shot.Aim.AimAnchor);
	RemapAnchor(Shot.Focus.FocusAnchor);
	Remap(Shot.Placement.BasisActorIndex);
	Remap(Shot.Placement.BasisSecondaryTargetIndex);
	return true;
}

bool RemoveTarget(FComposableCameraShot& Shot, int32 Index)
{
	const int32 OldCount = Shot.Targets.Num();
	if (!Shot.Targets.IsValidIndex(Index)) return false;
	Shot.Targets.RemoveAt(Index);
	const auto Remap = [Index, OldCount](int32& Reference)
	{
		if (Reference == Index) Reference = INDEX_NONE;
		else if (Reference > Index && Reference < OldCount) --Reference;
	};
	const auto RemapAnchor = [&Remap, Index](FComposableCameraAnchorSpec& Anchor)
	{
		Remap(Anchor.TargetIndex);
		Anchor.WeightedTargets.RemoveAll([Index](const auto& Entry) { return Entry.TargetIndex == Index; });
		for (auto& Entry : Anchor.WeightedTargets) Remap(Entry.TargetIndex);
	};
	RemapAnchor(Shot.Placement.PlacementAnchor);
	RemapAnchor(Shot.Aim.AimAnchor);
	RemapAnchor(Shot.Focus.FocusAnchor);
	Remap(Shot.Placement.BasisActorIndex);
	Remap(Shot.Placement.BasisSecondaryTargetIndex);
	return true;
}
}
#undef LOCTEXT_NAMESPACE
