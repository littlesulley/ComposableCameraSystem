// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraPivotDampingNode.h"

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/KismetMathLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowPivotDampingGizmo(
		TEXT("CCS.Debug.Viewport.PivotDamping"),
		0,
		TEXT("Show PivotDampingNode gizmo (green sphere at the damped pivot. Lags behind raw pivot under motion).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. Works in both possessed play and F8 eject."),
		ECVF_Default);
}
#endif

void UComposableCameraPivotDampingNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	UpwardInterpolator_T    = IsValid(UpwardInterpolator)    ? UpwardInterpolator->BuildDoubleInterpolator()    : nullptr;
	DownwardInterpolator_T  = IsValid(DownwardInterpolator)  ? DownwardInterpolator->BuildDoubleInterpolator()  : nullptr;
	LeftwardInterpolator_T  = IsValid(LeftwardInterpolator)  ? LeftwardInterpolator->BuildDoubleInterpolator()  : nullptr;
	RightwardInterpolator_T = IsValid(RightwardInterpolator) ? RightwardInterpolator->BuildDoubleInterpolator() : nullptr;
	ForwardInterpolator_T   = IsValid(ForwardInterpolator)   ? ForwardInterpolator->BuildDoubleInterpolator()   : nullptr;
	BackwardInterpolator_T  = IsValid(BackwardInterpolator)  ? BackwardInterpolator->BuildDoubleInterpolator()  : nullptr;
}

void UComposableCameraPivotDampingNode::OnFirstTickNode_Implementation()
{
	// PivotPosition is already resolved by the base-class ResolveAllInputPins()
	// prologue at this point. Seed LastPivotPosition to the actual runtime
	// position so the first frame produces zero delta and no damping artifact.
	LastPivotPosition = PivotPosition;
}

void UComposableCameraPivotDampingNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// PivotPosition and bMaintainCameraSpacePivotPosition are pin-matched UPROPERTYs -	// already resolved by the base TickNode prologue. Read PivotPosition directly.
	const FVector Pivot = PivotPosition;

	// The rotation must be determined before this node. 
	// If you are using the ControlRotation node, place it before this node.
	FRotator CameraRotation = OutCameraPose.Rotation;
	
	if (bMaintainCameraSpacePivotPosition)
	{
		FRotator LastCameraRotation = OwningCamera->CameraPose.Rotation;
		FVector FakeCameraSpacePivotDirection = UKismetMathLibrary::LessLess_VectorRotator(
			Pivot - LastPivotPosition, LastCameraRotation);
		LastPivotPosition = Pivot - UKismetMathLibrary::GreaterGreater_VectorRotator(FakeCameraSpacePivotDirection, CameraRotation);
	}

	CameraRotation.Pitch = 0;
	
	FVector WorldSpacePivotDirection = Pivot - LastPivotPosition;
	FVector CameraSpacePivotDirection =
		UKismetMathLibrary::LessLess_VectorRotator(WorldSpacePivotDirection, CameraRotation);
	
	FVector CameraSpaceDampedPivotDirection = CameraSpacePivotDirection;
	
	if (CameraSpacePivotDirection.X > 0)
	{
		double NewX = CameraSpacePivotDirection.X;
		if (ForwardInterpolator_T)
		{
			ForwardInterpolator_T->Reset(0, CameraSpacePivotDirection.X);
			NewX = ForwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.X = NewX;
	}
	else
	{
		double NewX = CameraSpacePivotDirection.X;
		if (BackwardInterpolator_T)
		{
			BackwardInterpolator_T->Reset(0, CameraSpacePivotDirection.X);
			NewX = BackwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.X = NewX;
	}

	if (CameraSpacePivotDirection.Y > 0)
	{
		double NewY = CameraSpacePivotDirection.Y;
		if (RightwardInterpolator_T)
		{
			RightwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Y);
			NewY = RightwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Y = NewY;
	}
	else
	{
		double NewY = CameraSpacePivotDirection.Y;
		if (LeftwardInterpolator_T)
		{
			LeftwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Y);
			NewY = LeftwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Y = NewY;
	}

	if (CameraSpacePivotDirection.Z > 0)
	{
		double NewZ = CameraSpacePivotDirection.Z;
		if (UpwardInterpolator_T)
		{
			UpwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Z);
			NewZ= UpwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Z = NewZ;
	}
	else
	{
		double NewZ = CameraSpacePivotDirection.Z;
		if (DownwardInterpolator_T)
		{
			DownwardInterpolator_T->Reset(0, CameraSpacePivotDirection.Z);
			NewZ= DownwardInterpolator_T->Run(DeltaTime);
		}
		CameraSpaceDampedPivotDirection.Z = NewZ;
	}
	
	FVector WorldSpaceDampedPivotPosition = UKismetMathLibrary::GreaterGreater_VectorRotator(
		CameraSpaceDampedPivotDirection, CameraRotation) + LastPivotPosition;
	
	SetOutputPinValue<FVector>("PivotPosition", WorldSpaceDampedPivotPosition);

	LastPivotPosition = WorldSpaceDampedPivotPosition;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraPivotDampingNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowPivotDampingGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	// Damped pivot sits at the same character / world target location as the
	// raw pivot. Not on the camera. Occlusion gate doesn't apply.
	// Magenta to stay distinct from the green CollisionPush trace (same hue
	// family would blur together when both nodes are enabled at once).
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, LastPivotPosition, /*Radius=*/10.f, FColor(255, 0, 255),
		/*Alpha=*/100, /*Segments=*/12, /*DepthPriority=*/0);
}
#endif

void UComposableCameraPivotDampingNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// PivotPosition Input
	PinDecl.PinName = TEXT("PivotPosition");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraPivotDampingNode", "PivotPosition", "Pivot Position");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector3D;
	PinDecl.bRequired = true;
	PinDecl.DefaultValueString = PivotPosition.ToString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraPivotDampingNode", "PivotPositionTip", "Pivot position to damp.");
	OutPins.Add(PinDecl);

	// bMaintainCameraSpacePivotPosition Input
	PinDecl = {};
	PinDecl.PinName = TEXT("bMaintainCameraSpacePivotPosition");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraPivotDampingNode", "MaintainCSP", "Maintain Camera Space Pivot Position");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Bool;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = bMaintainCameraSpacePivotPosition ? TEXT("true") : TEXT("false");
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraPivotDampingNode", "MaintainCSPTip",
		"When true, keeps the pivot's camera-space position stable across camera-rotation changes before damping.");
	OutPins.Add(PinDecl);

	// PivotPosition Output
	PinDecl = {};
	PinDecl.PinName = TEXT("PivotPosition");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraPivotDampingNode", "PivotPositionOutput", "Pivot Position");
	PinDecl.Direction = EComposableCameraPinDirection::Output;
	PinDecl.PinType = EComposableCameraPinType::Vector3D;
	PinDecl.bRequired = false;
	PinDecl.DefaultValueString = FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraPivotDampingNode", "PivotPositionOutputTip", "Damped pivot position output.");
	OutPins.Add(PinDecl);
}

