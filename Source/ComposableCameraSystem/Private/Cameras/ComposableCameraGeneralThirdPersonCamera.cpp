// Copyright 2026 Sulley. All Rights Reserved.

#include "Cameras/ComposableCameraGeneralThirdPersonCamera.h"
#include "ComposableCameraSystemModule.h"
#include "Nodes/ComposableCameraPivotOffsetNode.h"
#include "Nodes/ComposableCameraControlRotateNode.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include "Nodes/ComposableCameraPivotDampingNode.h"
#include "Nodes/ComposableCameraCameraOffsetNode.h"
#include "Nodes/ComposableCameraReceivePivotActorNode.h"
#include "Nodes/ComposableCameraCollisionPushNode.h"
#include "Nodes/ComposableCameraRotationConstraints.h"

AComposableCameraGeneralThirdPersonCamera::AComposableCameraGeneralThirdPersonCamera(
	const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
{
	CameraNodes = {
		CreateDefaultSubobject<UComposableCameraControlRotateNode>(FName("ControlRotateNode")),
		CreateDefaultSubobject<UComposableCameraRotationConstraints>(FName("RotationConstraintsNode")),
		CreateDefaultSubobject<UComposableCameraReceivePivotActorNode>(FName("ReceivePivotActorNode")),
		CreateDefaultSubobject<UComposableCameraPivotOffsetNode>(FName("PivotOffsetNode")),
		CreateDefaultSubobject<UComposableCameraPivotDampingNode>(FName("PivotDampingNode")),
		CreateDefaultSubobject<UComposableCameraCameraOffsetNode>(FName("ApplyPivotOffsetNode")),
		CreateDefaultSubobject<UComposableCameraCollisionPushNode>(FName("SelfCollisionPushNode")),
		CreateDefaultSubobject<UComposableCameraFieldOfViewNode>(FName("FieldOfViewNode")),
	};
}
