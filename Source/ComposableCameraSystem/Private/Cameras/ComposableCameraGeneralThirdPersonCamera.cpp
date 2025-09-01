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

UComposableCameraGeneralThirdPersonCamera::UComposableCameraGeneralThirdPersonCamera(
	const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
{
	CameraNodes = {
		CreateDefaultSubobject<UComposableCameraReceivePivotActorNode>(FName("ReceivePivotActorNode")),
		CreateDefaultSubobject<UComposableCameraControlRotateNode>(FName("ControlRotateNode")),
		CreateDefaultSubobject<UComposableCameraPivotOffsetNode>(FName("PivotOffsetNode")),
		CreateDefaultSubobject<UComposableCameraPivotDampingNode>(FName("PivotDampingNode")),
		CreateDefaultSubobject<UComposableCameraApplyPivotOffsetNode>(FName("ApplyPivotOffsetNode")),
		CreateDefaultSubobject<UComposableCameraTraceCollisionPushNode>(FName("TraceCollisionPushNode")),
		CreateDefaultSubobject<UComposableCameraSelfCollisionPushNode>(FName("SelfCollisionPushNode")),
		CreateDefaultSubobject<UComposableCameraFieldOfViewNode>(FName("FieldOfViewNode")),
	};
}

FComposableCameraPose UComposableCameraGeneralThirdPersonCamera::OnTickCamera_Implementation(float DeltaTime)
{
	UE_LOG(LogWindows, Warning, TEXT("HAHAHA"))
	return CameraPose;
}

void UComposableCameraGeneralThirdPersonCamera::OnBeginPlayCamera_Implementation()
{
	
}
