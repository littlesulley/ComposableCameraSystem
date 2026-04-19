// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraPostProcessNode.h"

#include "Engine/PostProcessUtils.h"

void UComposableCameraPostProcessNode::OnTickNode_Implementation(float DeltaTime,
                                                                 const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Apply this node's post-process settings onto the pose using OverrideChanged
	// semantics: only properties whose bOverride_* flag is true in our
	// PostProcessSettings are written into the pose. All other properties on
	// the pose (from the component baseline or earlier nodes) pass through
	// untouched. Multiple PostProcess nodes in the same camera compose in
	// execution order — later overrides win for the same property.
	FPostProcessUtils::OverridePostProcessSettings(OutCameraPose.PostProcessSettings, PostProcessSettings);
}
