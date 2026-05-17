// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraScreenSpaceConstraintsNode.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"
#include "Nodes/ComposableCameraScreenSpacePivotNode.h"
#include "Utils/ComposableCameraViewportUtils.h"

#if !UE_BUILD_SHIPPING
#include "CanvasItem.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "HAL/IConsoleManager.h"
#include "SceneView.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowScreenSpaceConstraintsGizmo(
		TEXT("CCS.Debug.Viewport.ScreenSpaceConstraints"),
		0,
		TEXT("Show ScreenSpaceConstraintsNode debug: pink sphere at the constrained\n")
		TEXT("actor (3D) + 2D HUD overlay with the safe-zone rectangle, its center\n")
		TEXT("marker, and the projected actor marker. Requires `CCS.Debug.Viewport 1`.\n")
		TEXT("The 2D overlay only draws during PIE possessed play / standalone."),
		ECVF_Default);
}
#endif

void UComposableCameraScreenSpaceConstraintsNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();
}

void UComposableCameraScreenSpaceConstraintsNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector Pivot = GetCurrentPivot();

	// Get aspect ratio and tangent of half horizontal FOV.
	auto [DegTanHalfHOR, AspectRatio] =
		GetTanHalfHORAndAspectRatio(OutCameraPose);
	
	if (Method == EComposableCameraScreenSpaceMethod::Translate)
	{
		FVector TranslateAmount = EnsureWithinBoundsTranslation(Pivot, OutCameraPose, AspectRatio, DegTanHalfHOR);

		// Write into OutCameraPose
		OutCameraPose.Position += TranslateAmount;
	}
	else if (Method == EComposableCameraScreenSpaceMethod::Rotate)
	{
		FRotator RotateAmount = EnsureWithinBoundsRotation(Pivot, OutCameraPose, AspectRatio, DegTanHalfHOR);

		// Write into OutCameraPose
		OutCameraPose.Rotation = (OutCameraPose.Rotation + RotateAmount).GetNormalized();
	}
}

void UComposableCameraScreenSpaceConstraintsNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// Input: PivotActorSource
	PinDecl.PinName = TEXT("PivotActorSource");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "PivotActorSource", "Pivot Actor Source");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "PivotActorSourceTip", "Selects whether the constrained actor comes from the controller's controlled pawn or an explicit actor.");
	OutPins.Add(PinDecl);

	// Input: PivotActor
	PinDecl = {};
	PinDecl.PinName = TEXT("PivotActor");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "PivotActor", "Pivot Actor");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.DefaultValueString = FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "PivotActorTip", "Actor to constrain in screen space.");
	OutPins.Add(PinDecl);

	// Input: Method - Translate vs Rotate strategy for keeping the pivot on-screen.
	PinDecl = {};
	PinDecl.PinName = TEXT("Method");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "Method", "Method");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraScreenSpaceMethod>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(Method)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "MethodTip",
		"How to keep the pivot within the safe zone - Translate moves the camera, Rotate turns the camera.");
	OutPins.Add(PinDecl);

	// Input: SafeZoneCenter
	PinDecl = {};
	PinDecl.PinName = TEXT("SafeZoneCenter");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "SafeZoneCenter", "Safe Zone Center");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "SafeZoneCenterTip", "Screen space safe zone center.");
	OutPins.Add(PinDecl);

	// Input: SafeZoneWidth
	PinDecl.PinName = TEXT("SafeZoneWidth");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "SafeZoneWidth", "Safe Zone Width");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "SafeZoneWidthTip", "Safe zone width bounds.");
	OutPins.Add(PinDecl);

	// Input: SafeZoneHeight
	PinDecl.PinName = TEXT("SafeZoneHeight");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "SafeZoneHeight", "Safe Zone Height");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "SafeZoneHeightTip", "Safe zone height bounds.");
	OutPins.Add(PinDecl);
}


FVector UComposableCameraScreenSpaceConstraintsNode::EnsureWithinBoundsTranslation(const FVector& Pivot,
                                                                                   const FComposableCameraPose& CurrentPose, const float& AspectRatio, const float& TanHalfHOR)
{
	FVector CameraSpacePivotPosition = UKismetMathLibrary::LessLess_VectorRotator(Pivot - CurrentPose.Position, CurrentPose.Rotation);

	float Width = TanHalfHOR * CameraSpacePivotPosition.X * 2.0f;
	float LeftBound = (SafeZoneCenter.X + SafeZoneWidth.X) * Width;
	float RightBound = (SafeZoneCenter.X + SafeZoneWidth.Y) * Width;
	float BottomBound = (SafeZoneCenter.Y + SafeZoneHeight.X) * Width / AspectRatio;
	float TopBound = (SafeZoneCenter.Y + SafeZoneHeight.Y) * Width / AspectRatio;

	FVector MoveAmount { FVector::ZeroVector };
	
	if (CameraSpacePivotPosition.Y < LeftBound)   MoveAmount.Y = CameraSpacePivotPosition.Y - LeftBound;
	if (CameraSpacePivotPosition.Y > RightBound)  MoveAmount.Y = CameraSpacePivotPosition.Y - RightBound;
	if (CameraSpacePivotPosition.Z < BottomBound) MoveAmount.Z = CameraSpacePivotPosition.Z - BottomBound;
	if (CameraSpacePivotPosition.Z > TopBound)    MoveAmount.Z = CameraSpacePivotPosition.Z - TopBound;

	return UKismetMathLibrary::GreaterGreater_VectorRotator(MoveAmount, CurrentPose.Rotation);
}

FRotator UComposableCameraScreenSpaceConstraintsNode::EnsureWithinBoundsRotation(const FVector& Pivot,
	const FComposableCameraPose& CurrentPose, float AspectRatio, float DegTanHalfHor)
{
	FRotator DesiredRotation = CurrentPose.Rotation;
	FVector Direction = Pivot - CurrentPose.Position;
	
	FVector CameraSpaceDirection = DesiredRotation.UnrotateVector(Direction);
	float ScreenPositionX = CameraSpaceDirection.Y / (2.0f * DegTanHalfHor * CameraSpaceDirection.X);
	float ScreenPositionY = CameraSpaceDirection.Z / (2.0f * DegTanHalfHor / AspectRatio * CameraSpaceDirection.X);
	ScreenPositionX = FMath::Clamp(ScreenPositionX, SafeZoneCenter.X + SafeZoneWidth.X, SafeZoneCenter.X + SafeZoneWidth.Y);
	ScreenPositionY = FMath::Clamp(ScreenPositionY, SafeZoneCenter.Y + SafeZoneHeight.X, SafeZoneCenter.Y + SafeZoneHeight.Y);

	auto [Pitch, Yaw] = ComposableCameraSystem::SolveCameraRotationForScreenTarget(
		DegTanHalfHor,
		AspectRatio,
		Direction,
		ScreenPositionX,
		ScreenPositionY);

	DesiredRotation.Yaw = Yaw;
	DesiredRotation.Pitch = Pitch;
	return (DesiredRotation - CurrentPose.Rotation).GetNormalized();
}

std::pair<float, float> UComposableCameraScreenSpaceConstraintsNode::GetTanHalfHORAndAspectRatio(
	const FComposableCameraPose& OutCameraPose)
{
	// Viewport size is resolved through a general helper (PCM->GameViewport
	// ->fallback) rather than hard-wiring a PCM deref - this lets the node
	// evaluate correctly in the Level Sequence component path where there is
	// no PCM. See UE::ComposableCameras::TryGetEffectiveViewportSize.
	FIntPoint ViewportSize;
	UE::ComposableCameras::TryGetEffectiveViewportSize(OwningPlayerCameraManager, ViewportSize);
	const int32 ViewportX = ViewportSize.X;
	const int32 ViewportY = ViewportSize.Y;

	// Resolve effective FOV once so this math works whether the pose is in degrees-mode
	// (FieldOfView > 0) or physical-mode (FocalLength > 0).
	const float EffectiveFOV = static_cast<float>(OutCameraPose.GetEffectiveFieldOfView());

	float DegTanHalfHOR = UKismetMathLibrary::DegTan(EffectiveFOV / 2.0f);
	float AspectRatio = 1.0f * ViewportX / ViewportY;

	// Aspect ratio is different when bConstrainAspectRatio is true or false.
	// If bConstrainAspectRatio is false, ViewportX / ViewportY is the real aspect ratio, and FOV is computed according to AspectRatioAxisConstraint.
	// If bConstrainAspectRatio is true, camera's own aspect ratio is used, FOV is not modified, and black bars will be added to sides of the viewport.
	if (!OwningCamera->GetCameraComponent()->bConstrainAspectRatio)
	{
		if (OwningCamera->GetCameraComponent()->bOverrideAspectRatioAxisConstraint)
		{
			switch (OwningCamera->GetCameraComponent()->AspectRatioAxisConstraint)
			{
			case AspectRatio_MaintainXFOV:
				DegTanHalfHOR = UKismetMathLibrary::DegTan(EffectiveFOV / 2.0f);
				break;
			case AspectRatio_MaintainYFOV:
				DegTanHalfHOR = UKismetMathLibrary::DegTan(EffectiveFOV / 2.0f) / OwningCamera->GetCameraComponent()->AspectRatio * AspectRatio;
				break;
			case AspectRatio_MajorAxisFOV:
				if (ViewportX > ViewportY)
				{
					DegTanHalfHOR = UKismetMathLibrary::DegTan(EffectiveFOV / 2.0f);
				}
				else
				{
					DegTanHalfHOR = UKismetMathLibrary::DegTan(EffectiveFOV / 2.0f) / OwningCamera->GetCameraComponent()->AspectRatio * AspectRatio;
				}
				break;
			default:
				DegTanHalfHOR = UKismetMathLibrary::DegTan(EffectiveFOV / 2.0f);
				break;
			}
		}
	}
	else
	{
		AspectRatio = OwningCamera->GetCameraComponent()->AspectRatio;
	}

	return { DegTanHalfHOR, AspectRatio };
}

FVector UComposableCameraScreenSpaceConstraintsNode::GetCurrentPivot() const
{
	// PivotActor is a pin-matched UPROPERTY - resolved by the base TickNode prologue.
	// Read from the current frame's resolved value.
	AActor* InPivotActor = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
	if (IsValid(InPivotActor))
	{
		return InPivotActor->GetActorLocation();
	}
	return FVector::ZeroVector;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraScreenSpaceConstraintsNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowScreenSpaceConstraintsGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	// Sphere at the constrained actor's location (same resolution the tick
	// path uses via `GetCurrentPivot()`). Pink keeps it distinct from every
	// other gizmo hue in the palette.
	constexpr uint8 KForeground = 1;
	const FVector Pivot = GetCurrentPivot();
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, Pivot, /*Radius=*/8.f, FColor(255, 180, 220),
		/*Alpha=*/100, /*Segments=*/12, KForeground);
}

namespace
{
	/** Filled translucent rect - Canvas equivalent of AHUD::DrawRect. */
	void DrawCanvasRect(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

void UComposableCameraScreenSpaceConstraintsNode::DrawNodeDebug2D(UCanvas* Canvas, APlayerController* PC) const
{
	if (!Canvas) { return; }
	if (CVarShowScreenSpaceConstraintsGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	if (!IsValid(OwningCamera) || !OwningCamera->GetCameraComponent()) { return; }

	// Exact port of the original HUD-PostRender path, adapted to the
	// UCanvas* + PC* signature of DrawNodeDebug2D. The constrained-aspect-
	// ratio branch uses LocalPlayer projection data for the letterbox
	// offset; if you simplify to a single branch you get the safe-zone
	// drawn in the wrong place in windowed PIE with a pillar-boxed view.
	constexpr FLinearColor RectColor        (1.0f, 0.85f, 0.92f, 0.8f);
	constexpr FLinearColor CenterColor      (1.0f, 0.80f, 0.88f, 0.8f);
	constexpr FLinearColor PivotCenterColor (1.0f, 0.70f, 0.82f, 0.8f);
	constexpr float Radius = 5.0f;

	const int32 ScreenWidth  = Canvas->SizeX;
	const int32 ScreenHeight = Canvas->SizeY;

	const FVector Pivot = GetCurrentPivot();

	if (!OwningCamera->GetCameraComponent()->bConstrainAspectRatio)
	{
		const float ScreenX = (SafeZoneCenter.X + 0.5f + SafeZoneWidth.X)  * ScreenWidth;
		const float ScreenY = (-SafeZoneCenter.Y + 0.5f - SafeZoneHeight.Y) * ScreenHeight;
		const float ScreenW = (SafeZoneWidth.Y  - SafeZoneWidth.X)  * ScreenWidth;
		const float ScreenH = (SafeZoneHeight.Y - SafeZoneHeight.X) * ScreenHeight;
		DrawCanvasRect(Canvas, ScreenX, ScreenY, ScreenW, ScreenH, RectColor);

		const float CenterX = (SafeZoneCenter.X + 0.5f) * ScreenWidth;
		const float CenterY = (-SafeZoneCenter.Y + 0.5f) * ScreenHeight;
		DrawCanvasRect(Canvas, CenterX - Radius, CenterY - Radius, 2.f * Radius, 2.f * Radius, CenterColor);

		FVector2D ScreenPosition;
		if (PC && UGameplayStatics::ProjectWorldToScreen(PC, Pivot, ScreenPosition))
		{
			DrawCanvasRect(Canvas,
				ScreenPosition.X - Radius, ScreenPosition.Y - Radius,
				2.f * Radius, 2.f * Radius, PivotCenterColor);
		}
	}
	else
	{
		const float ViewportAspectRatio = 1.0f * ScreenWidth / ScreenHeight;
		const float AspectRatio = OwningCamera->GetCameraComponent()->AspectRatio;

		float ClampedScreenWidth  = ScreenWidth;
		float ClampedScreenHeight = ScreenHeight;
		if (ViewportAspectRatio > AspectRatio)
		{
			ClampedScreenWidth = ScreenHeight * AspectRatio;
		}
		else
		{
			ClampedScreenHeight = ScreenWidth / AspectRatio;
		}

		FVector2D ScreenOffset = FVector2D::ZeroVector;
		ULocalPlayer* const LP = PC ? PC->GetLocalPlayer() : nullptr;
		if (LP && LP->ViewportClient)
		{
			FSceneViewProjectionData ProjectionData;
			LP->GetProjectionData(LP->ViewportClient->Viewport, ProjectionData);
			ScreenOffset = FVector2D(ProjectionData.GetConstrainedViewRect().Min);
		}

		const float RatioX = ClampedScreenWidth  / ScreenWidth;
		const float RatioY = ClampedScreenHeight / ScreenHeight;

		const float ScreenX = (SafeZoneCenter.X * RatioX + 0.5f + SafeZoneWidth.X  * RatioX) * ScreenWidth  - ScreenOffset.X;
		const float ScreenY = (-SafeZoneCenter.Y * RatioY + 0.5f - SafeZoneHeight.Y * RatioY) * ScreenHeight - ScreenOffset.Y;
		const float ScreenW = (SafeZoneWidth.Y  * RatioX - SafeZoneWidth.X  * RatioX) * ScreenWidth;
		const float ScreenH = (SafeZoneHeight.Y * RatioY - SafeZoneHeight.X * RatioY) * ScreenHeight;
		DrawCanvasRect(Canvas, ScreenX, ScreenY, ScreenW, ScreenH, RectColor);

		const float CenterX = (SafeZoneCenter.X  * RatioX + 0.5f) * ScreenWidth  - ScreenOffset.X;
		const float CenterY = (-SafeZoneCenter.Y * RatioY + 0.5f) * ScreenHeight - ScreenOffset.Y;
		DrawCanvasRect(Canvas, CenterX - Radius, CenterY - Radius, 2.f * Radius, 2.f * Radius, CenterColor);

		FVector2D ScreenPosition;
		if (PC && UGameplayStatics::ProjectWorldToScreen(PC, Pivot, ScreenPosition, /*bPlayerViewportRelative=*/true))
		{
			DrawCanvasRect(Canvas,
				ScreenPosition.X - Radius, ScreenPosition.Y - Radius,
				2.f * Radius, 2.f * Radius, PivotCenterColor);
		}
	}
}
#endif
