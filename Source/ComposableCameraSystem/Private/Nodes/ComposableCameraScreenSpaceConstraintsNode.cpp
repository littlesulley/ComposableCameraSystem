// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraScreenSpaceConstraintsNode.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Nodes/ComposableCameraScreenSpacePivotNode.h"
#include "Utils/ComposableCameraViewportUtils.h"

#if !UE_BUILD_SHIPPING
#include "CanvasItem.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
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

	// Input: PivotActor
	PinDecl.PinName = TEXT("PivotActor");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "PivotActor", "Pivot Actor");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = true;
	PinDecl.DefaultValueString = FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpaceConstraintsNode", "PivotActorTip", "Actor to constrain in screen space.");
	OutPins.Add(PinDecl);

	// Input: Method — Translate vs Rotate strategy for keeping the pivot on-screen.
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
		"How to keep the pivot within the safe zone — Translate moves the camera, Rotate turns the camera.");
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

	auto [Pitch, Yaw] = CalibrateRotationOffsetNewton(
		DegTanHalfHor,
		AspectRatio,
		Direction,
		UKismetMathLibrary::MakeRotFromX(Direction),
		ScreenPositionX,
		ScreenPositionY);

	DesiredRotation.Yaw = Yaw;
	DesiredRotation.Pitch = Pitch;
	return (DesiredRotation - CurrentPose.Rotation).GetNormalized();
}

std::pair<float, float> UComposableCameraScreenSpaceConstraintsNode::CalibrateRotationOffsetNewton(float TanHalfHOR,
	float AspectRatio, FVector Direction, FRotator LookAtRotation, float ScreenX, float ScreenY)
{
	using Trig_T = double(*)(double);
	Trig_T Sin = &FMath::Sin;
	Trig_T Cos = &FMath::Cos;
	
	constexpr static int Steps = 10;
	const float TanHalfVOR = TanHalfHOR / AspectRatio;
	const float a = ScreenX;
	const float b = ScreenY;
	const float m = TanHalfHOR;
	const float n = TanHalfVOR;
	const float A = Direction.X;
	const float B = Direction.Y;
	const float C = Direction.Z;
	
	float X = LookAtRotation.Pitch - UKismetMathLibrary::DegAtan(2.0 * ScreenY * TanHalfVOR);
	float Y = LookAtRotation.Yaw   - UKismetMathLibrary::DegAtan(2.0 * ScreenX * TanHalfHOR);
	X = FMath::DegreesToRadians(X);
	Y = FMath::DegreesToRadians(Y);

	// Start with small damping.
	float OldError = FLT_MAX;
	
	uint32 Iteration = 0;
	for (Iteration = 0; Iteration < Steps; ++Iteration)
	{
		// Compute common terms.
		const float SinX = Sin(X);
		const float CosX = Cos(X);
		const float SinY = Sin(Y);
		const float CosY = Cos(Y);

		const float S = A * CosX * CosY + B * CosX * SinY + C * SinX;
		const float F1 = 2.f * a * m * S - (-A * SinY + B * CosY);
		const float F2 = 2.f * b * n * S - (-A * SinX * CosY - B * SinX * SinY + C * CosX);

		// Compute Jacobian.
		const float DSDX = -A * SinX * CosY - B * SinX * SinY + C * CosX;
		const float DSDY = CosX * (-A * SinY + B * CosY);

		const float DF1DX = 2.f * a * m * DSDX;
		const float DF1DY = 2.f * a * m * DSDY - (-A * CosY - B * SinY);
		const float DF2DX = 2.f * b * n * DSDX - (-A * CosX * CosY - B * CosX * SinY - C * SinX);
		const float DF2DY = 2.f * b * n * DSDY - (A * SinX * SinY - B * SinX * CosY);

		float Det = DF1DX * DF2DY - DF1DY * DF2DX;
	    if (FMath::Abs(Det) < 1e-3)
		{
			break;
		}

		float DX = (-F1 * DF2DY + F2 * DF1DY) / Det;
		float DY = (-DF1DX * F2 + DF2DX * F1) / Det;

		const float NewX = X + DX;
		const float NewY = Y + DY;

		// Evaluate new residual magnitude.
		const float SinXn = Sin(NewX);
		const float CosXn = Cos(NewX);
		const float SinYn = Sin(NewY);
		const float CosYn = Cos(NewY);

		const float Sn = A * CosXn * CosYn + B * CosXn * SinYn + C * SinXn;
		const float F1n = 2.f * a * m * Sn - (-A * SinYn + B * CosYn);
		const float F2n = 2.f * b * n * Sn - (-A * SinXn * CosYn - B * SinXn * SinYn + C * CosXn);

		const float NewError = FMath::Sqrt(F1n * F1n + F2n * F2n);
		const float OldErrorMag = FMath::Sqrt(F1 * F1 + F2 * F2);

		// Adaptive lambda adjustment.
		if (NewError < OldErrorMag)
		{
			// Accept update and decrease lambda (move toward Gauss–Newton).
			X = NewX;
			Y = NewY;
			OldError = NewError;
		}

		if (NewError < 1e-2f)
		{
			break;
		}
	}
	
	return { FMath::RadiansToDegrees(X), FMath::RadiansToDegrees(Y) };
}

std::pair<float, float> UComposableCameraScreenSpaceConstraintsNode::GetTanHalfHORAndAspectRatio(
	const FComposableCameraPose& OutCameraPose)
{
	// Viewport size is resolved through a general helper (PCM → GameViewport
	// → fallback) rather than hard-wiring a PCM deref — this lets the node
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
	// PivotActor is a pin-matched UPROPERTY — resolved by the base TickNode prologue.
	// Read from the current frame's resolved value.
	AActor* InPivotActor = PivotActor.Get();
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
	/** Filled translucent rect — Canvas equivalent of AHUD::DrawRect. */
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
