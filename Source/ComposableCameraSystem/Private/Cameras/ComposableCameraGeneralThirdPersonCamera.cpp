// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraGeneralThirdPersonCamera.h"
#include "ComposableCameraSystemModule.h"
#include "Nodes/ComposableCameraPivotOffsetNode.h"
#include "Nodes/ComposableCameraControlRotateNode.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include "Nodes/ComposableCameraPivotDampingNode.h"
#include "Nodes/ComposableCameraApplyPivotOffsetNode.h"
#include "Nodes/ComposableCameraReceivePivotActorNode.h"
#include "Nodes/ComposableCameraTraceCollisionPushNode.h"
#include "Nodes/ComposableCameraSelfCollisionPushNode.h"

AComposableCameraGeneralThirdPersonCamera::AComposableCameraGeneralThirdPersonCamera(
	const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
{
	CameraNodes = {
		CreateDefaultSubobject<UComposableCameraControlRotateNode>(FName("ControlRotateNode")),
		CreateDefaultSubobject<UComposableCameraReceivePivotActorNode>(FName("ReceivePivotActorNode")),
		CreateDefaultSubobject<UComposableCameraPivotOffsetNode>(FName("PivotOffsetNode")),
		CreateDefaultSubobject<UComposableCameraPivotDampingNode>(FName("PivotDampingNode")),
		CreateDefaultSubobject<UComposableCameraApplyPivotOffsetNode>(FName("ApplyPivotOffsetNode")),
		CreateDefaultSubobject<UComposableCameraTraceCollisionPushNode>(FName("TraceCollisionPushNode")),
		CreateDefaultSubobject<UComposableCameraSelfCollisionPushNode>(FName("SelfCollisionPushNode")),
		CreateDefaultSubobject<UComposableCameraFieldOfViewNode>(FName("FieldOfViewNode")),
	};
}
