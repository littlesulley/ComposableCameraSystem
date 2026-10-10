// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraShot.h"

enum class EComposableShotTemplate : uint8
{
	CloseUp, Medium, FullBody, TwoShot, ShoulderLeft, ShoulderRight, ReverseShoulder, Group
};

namespace ComposableCameraSystem::ShotAuthoring
{
	FText TemplateLabel(EComposableShotTemplate Template);
	bool CanUseTemplate(EComposableShotTemplate Template, int32 TargetCount);
	FComposableCameraShotTarget MakeTarget(AActor* Actor);
	bool BuildTemplate(EComposableShotTemplate Template,
		TConstArrayView<FComposableCameraShotTarget> Targets, FComposableCameraShot& OutShot);
	/** Move identities and all index-based references together. Section bindings are remapped by the caller. */
	bool MoveTarget(FComposableCameraShot& Shot, int32 From, int32 To);
	/** Remove one slot; preserve surviving identities and invalidate references to the deleted slot. */
	bool RemoveTarget(FComposableCameraShot& Shot, int32 Index);
}
