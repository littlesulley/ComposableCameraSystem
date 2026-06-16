// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraScreenSpacePivotNode.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"
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
	static TAutoConsoleVariable<int32> CVarShowScreenSpacePivotGizmo(
		TEXT("CCS.Debug.Viewport.ScreenSpacePivot"),
		0,
		TEXT("Show ScreenSpacePivotNode debug: teal sphere at world pivot (3D) +\n")
		TEXT("2D HUD overlay with the safe-zone rectangle, its center marker, and\n")
		TEXT("the projected pivot marker. Requires `CCS.Debug.Viewport 1`. The\n")
		TEXT("2D overlay only draws during PIE possessed play / standalone (the\n")
		TEXT("HUD pass doesn't fire during F8 eject); 3D sphere works everywhere."),
		ECVF_Default);
}
#endif

void UComposableCameraScreenSpacePivotNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	XInterpolator_T = TranslationParams.XInterpolator ? TranslationParams.XInterpolator->BuildDoubleInterpolator() : nullptr;
	YInterpolator_T = TranslationParams.YInterpolator ? TranslationParams.YInterpolator->BuildDoubleInterpolator() : nullptr;
	ZInterpolator_T = TranslationParams.ZInterpolator ? TranslationParams.ZInterpolator->BuildDoubleInterpolator() : nullptr;
	YawInterpolator_T = RotationParams.YawInterpolator ? RotationParams.YawInterpolator->BuildDoubleInterpolator() : nullptr;
	PitchInterpolator_T = RotationParams.PitchInterpolator ? RotationParams.PitchInterpolator->BuildDoubleInterpolator() : nullptr;
}

void UComposableCameraScreenSpacePivotNode::OnTickNode_Implementation(float DeltaTime,
                                                                      const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector Pivot = GetCurrentPivot();
	
	if (Method == EComposableCameraScreenSpaceMethod::Translate)
	{
		FVector TranslateAmount = GetScreenSpaceTranslateAmount(Pivot, OutCameraPose, DeltaTime);

		// Write into OutCameraPose
		OutCameraPose.Position = GetOwningCamera()->GetCameraPose().Position + TranslateAmount;
	}
	else if (Method == EComposableCameraScreenSpaceMethod::Rotate)
	{
		FRotator RotateAmount = GetScreenSpaceRotateAmount(Pivot, OutCameraPose, DeltaTime);

		// Write into OutCameraPose
		OutCameraPose.Rotation = (OutCameraPose.Rotation + RotateAmount).GetNormalized();
	}
}

void UComposableCameraScreenSpacePivotNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// NOTE: PivotSource and Method are declared as pins with bDefaultAsPin = false
	// so they render in the Details panel by default (design-time choice), but
	// remain individually promotable per-instance via the pin override toggle.
	// All pin names match their backing UPROPERTY FNames verbatim so the node
	// Details customization pairs them into single unified rows.

	// Input: PivotSource - selects WorldPosition vs ActorPosition pivot resolution.
	PinDecl = {};
	PinDecl.PinName = TEXT("PivotSource");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotSource", "Pivot Source");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraScreenSpacePivotSource>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(PivotSource)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotSourceTip",
		"Selects whether the pivot is read from PivotWorldPosition (WorldPosition) or derived from PivotActor + PivotWorldUpOffset (ActorPosition).");
	OutPins.Add(PinDecl);

	// Input: PivotActorSource - consumed when PivotSource == ActorPosition.
	PinDecl = {};
	PinDecl.PinName = TEXT("PivotActorSource");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotActorSource", "Pivot Actor Source");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotActorSourceTip",
		"Selects whether PivotActor comes from the controller's controlled pawn or an explicit actor when PivotSource is ActorPosition.");
	OutPins.Add(PinDecl);

	// Input: PivotWorldPosition - consumed when PivotSource == WorldPosition.
	PinDecl = {};
	PinDecl.PinName = TEXT("PivotWorldPosition");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotWorldPosition", "Pivot World Position");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector3D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PivotWorldPosition.ToString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotWorldPositionTip",
		"Pivot in world space. Used when PivotSource == WorldPosition.");
	OutPins.Add(PinDecl);

	// Input: PivotActor - consumed when PivotSource == ActorPosition.
	PinDecl.PinName = TEXT("PivotActor");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotActor", "Pivot Actor");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotActorTip",
		"Actor whose world location supplies the pivot. Used when PivotSource == ActorPosition.");
	OutPins.Add(PinDecl);

	// Input: PivotWorldUpOffset - consumed when PivotSource == ActorPosition.
	PinDecl.PinName = TEXT("PivotWorldUpOffset");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotWorldUpOffset", "Pivot World Up Offset");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Float;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = FString::SanitizeFloat(PivotWorldUpOffset);
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "PivotWorldUpOffsetTip",
		"World-up offset added to PivotActor->GetActorLocation(). Used when PivotSource == ActorPosition.");
	OutPins.Add(PinDecl);

	// Input: Method - Translate vs Rotate strategy for keeping the pivot on-screen.
	PinDecl = {};
	PinDecl.PinName = TEXT("Method");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "Method", "Method");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraScreenSpaceMethod>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(Method)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "MethodTip",
		"How to keep the pivot within the safe zone - Translate moves the camera, Rotate turns the camera.");
	OutPins.Add(PinDecl);

	// Input: SafeZoneCenter
	PinDecl = {};
	PinDecl.PinName = TEXT("SafeZoneCenter");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "SafeZoneCenter", "Safe Zone Center");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "SafeZoneCenterTip", "Screen space safe zone center.");
	OutPins.Add(PinDecl);

	// Input: SafeZoneWidth
	PinDecl.PinName = TEXT("SafeZoneWidth");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "SafeZoneWidth", "Safe Zone Width");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "SafeZoneWidthTip", "Safe zone width bounds.");
	OutPins.Add(PinDecl);

	// Input: SafeZoneHeight
	PinDecl.PinName = TEXT("SafeZoneHeight");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "SafeZoneHeight", "Safe Zone Height");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Vector2D;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraScreenSpacePivotNode", "SafeZoneHeightTip", "Safe zone height bounds.");
	OutPins.Add(PinDecl);
}


FVector UComposableCameraScreenSpacePivotNode::GetScreenSpaceTranslateAmount(const FVector& Pivot,
                                                                             const FComposableCameraPose& OutCameraPose, float DeltaTime)
{
	double CameraDistance = TranslationParams.CameraDistance;
	FVector CameraPosition = OutCameraPose.Position;
	FRotator CameraRotation = OutCameraPose.Rotation;
	FVector CameraSpacePivotPosition = UKismetMathLibrary::LessLess_VectorRotator(Pivot - CameraPosition, CameraRotation);
	
	// Calculate camera space damped X axis (camera distance) offset.
	double CameraSpaceDampedXOffset = CameraSpacePivotPosition.X - CameraDistance;

	if (XInterpolator_T)
	{
		XInterpolator_T->Reset(0, CameraSpaceDampedXOffset);
		CameraSpaceDampedXOffset = XInterpolator_T->Run(DeltaTime);
	}
	
	// Get aspect ratio and tangent of half horizontal FOV.
	auto [DegTanHalfHOR, AspectRatio] =
		GetTanHalfHORAndAspectRatio(OutCameraPose);

	// Get the expected camera distance.
	CameraDistance = CameraSpacePivotPosition.X - CameraSpaceDampedXOffset;

	if (CameraDistance < 0)
	{
		CameraDistance = -CameraDistance;
	}

	// Calculate camera space damped Y/Z axis (camera distance) offset.
	float W = DegTanHalfHOR * CameraDistance * 2.0f;
	float DesiredCameraSpaceY = W * SafeZoneCenter.X;
	float DesiredCameraSpaceZ = W / AspectRatio * SafeZoneCenter.Y;
	
	double CameraSpaceDampedYOffset = CameraSpacePivotPosition.Y - DesiredCameraSpaceY;
	double CameraSpaceDampedZOffset = CameraSpacePivotPosition.Z - DesiredCameraSpaceZ;

	if (YInterpolator_T)
	{
		YInterpolator_T->Reset(0, CameraSpaceDampedYOffset);
		CameraSpaceDampedYOffset = YInterpolator_T->Run(DeltaTime);
	}
	if (ZInterpolator_T)
	{
		ZInterpolator_T->Reset(0, CameraSpaceDampedZOffset);
		CameraSpaceDampedZOffset = ZInterpolator_T->Run(DeltaTime);
	}

	// Ensure the pivot is within safe zone.
	FVector CameraSpaceDampedOffset { CameraSpaceDampedXOffset, CameraSpaceDampedYOffset, CameraSpaceDampedZOffset };
	EnsureWithinBoundsTranslation(CameraSpacePivotPosition, CameraSpaceDampedOffset, AspectRatio, DegTanHalfHOR, CameraDistance);

	// Convert to world space offset.
	return UKismetMathLibrary::GreaterGreater_VectorRotator(CameraSpaceDampedOffset, CameraRotation);
}

FRotator UComposableCameraScreenSpacePivotNode::GetScreenSpaceRotateAmount(const FVector& Pivot,
                                                                           const FComposableCameraPose& OutCameraPose, float DeltaTime)
{
	FRotator CameraRotation = OutCameraPose.Rotation;
	FVector CameraPosition = OutCameraPose.Position;

	const FVector Direction = Pivot - CameraPosition;

	// Get aspect ratio and tangent of half horizontal FOV.
	auto [DegTanHalfHOR, AspectRatio] =
		GetTanHalfHORAndAspectRatio(OutCameraPose);

	// Solve for the (Pitch, Yaw) that places the pivot at SafeZoneCenter.
	auto [Pitch, Yaw] = ComposableCameraSystem::SolveCameraRotationForScreenTarget(
		DegTanHalfHOR,
		AspectRatio,
		Direction,
		SafeZoneCenter.X,
		SafeZoneCenter.Y);

	FRotator DesiredRotation = { Pitch, Yaw, 0.f };
	FRotator DeltaRotation = UKismetMathLibrary::NormalizedDeltaRotator(DesiredRotation, CameraRotation);
	
	// Calculate damped delta rotation.
	if (YawInterpolator_T)
	{
		YawInterpolator_T->Reset(0, DeltaRotation.Yaw);
		DeltaRotation.Yaw = YawInterpolator_T->Run(DeltaTime);
	}
	if (PitchInterpolator_T)
	{
		PitchInterpolator_T->Reset(0, DeltaRotation.Pitch);
		DeltaRotation.Pitch = PitchInterpolator_T->Run(DeltaTime);
	}

	EnsureWithinBoundsRotation(CameraRotation, Direction, DeltaRotation, AspectRatio, DegTanHalfHOR);

	return DeltaRotation;
}

void UComposableCameraScreenSpacePivotNode::EnsureWithinBoundsTranslation(const FVector& CameraSpacePivotPosition,
                                                                          FVector& CameraSpaceDampedOffset, const float& AspectRatio, const float& TanHalfHOR, const float& CameraDistance)
{
	FVector DesiredCameraSpacePivotPosition = CameraSpacePivotPosition - CameraSpaceDampedOffset;

	float Width = TanHalfHOR * CameraDistance * 2.0f;
	float LeftBound = (SafeZoneCenter.X + SafeZoneWidth.X) * Width;
	float RightBound = (SafeZoneCenter.X + SafeZoneWidth.Y) * Width;
	float BottomBound = (SafeZoneCenter.Y + SafeZoneHeight.X) * Width / AspectRatio;
	float TopBound = (SafeZoneCenter.Y + SafeZoneHeight.Y) * Width / AspectRatio;
	
	if (DesiredCameraSpacePivotPosition.Y < LeftBound)   CameraSpaceDampedOffset.Y += DesiredCameraSpacePivotPosition.Y - LeftBound;
	if (DesiredCameraSpacePivotPosition.Y > RightBound)  CameraSpaceDampedOffset.Y += DesiredCameraSpacePivotPosition.Y - RightBound;
	if (DesiredCameraSpacePivotPosition.Z < BottomBound) CameraSpaceDampedOffset.Z += DesiredCameraSpacePivotPosition.Z - BottomBound;
	if (DesiredCameraSpacePivotPosition.Z > TopBound)    CameraSpaceDampedOffset.Z += DesiredCameraSpacePivotPosition.Z - TopBound;
}

void UComposableCameraScreenSpacePivotNode::EnsureWithinBoundsRotation(const FRotator& CameraRotation, const FVector& Direction, FRotator& DeltaRotation,
	float AspectRatio, float DegTanHalfHor)
{
	FRotator DesiredRotation = (CameraRotation + DeltaRotation).GetNormalized();

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
	DeltaRotation = (DesiredRotation - CameraRotation).GetNormalized();
}

std::pair<float, float> UComposableCameraScreenSpacePivotNode::GetTanHalfHORAndAspectRatio(const FComposableCameraPose& OutCameraPose)
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

FVector UComposableCameraScreenSpacePivotNode::GetCurrentPivot() const
{
	switch (PivotSource)
	{
	case EComposableCameraScreenSpacePivotSource::WorldPosition:
		return PivotWorldPosition;

	case EComposableCameraScreenSpacePivotSource::ActorPosition:
		{
			// Pin-matched UPROPERTYs are resolved by the base TickNode prologue.
			// Read the members here so Details-only defaults work the same as
			// promoted / wired pins.
			AActor* Actor = ComposableCameraSystem::ResolveActorInput(
				PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
			if (IsValid(Actor))
			{
				return Actor->GetActorLocation() + FVector::UpVector * PivotWorldUpOffset;
			}
			return PivotWorldPosition;
		}
	}

	// Defensive: new enum case added without a branch above.
	return FVector::ZeroVector;
}

#if !UE_BUILD_SHIPPING
void UComposableCameraScreenSpacePivotNode::DrawNodeDebug(UWorld* World, bool /*bViewerIsOutsideCamera*/) const
{
	if (!World) { return; }
	if (CVarShowScreenSpacePivotGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	// Sphere at the resolved world pivot. `GetCurrentPivot()` returns either
	// `PivotWorldPosition` or `PivotActor->GetActorLocation() + up-offset`
	// depending on `PivotSource` - same resolution the tick path uses.
	constexpr uint8 KForeground = 1;
	const FVector Pivot = GetCurrentPivot();
	FComposableCameraViewportDebug::DrawSolidDebugSphere(
		World, Pivot, /*Radius=*/8.f, FComposableCameraViewportDebugColors::ScreenSpacePivot(),
		/*Alpha=*/100, /*Segments=*/12, KForeground, TEXT("ScreenSpacePivot"));
}

namespace
{
	/** Filled translucent rect - Canvas equivalent of AHUD::DrawRect. */
	void DrawScreenSpacePivotCanvasRect(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

void UComposableCameraScreenSpacePivotNode::DrawNodeDebug2D(UCanvas* Canvas, APlayerController* PC) const
{
	if (!Canvas) { return; }
	if (CVarShowScreenSpacePivotGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }
	if (!IsValid(OwningCamera) || !OwningCamera->GetCameraComponent()) { return; }

	// Preserves the exact math of the original HUD-PostRender implementation.
	// The constrained-aspect-ratio branch in particular uses the LocalPlayer's
	// ProjectionData to account for the letterboxed viewport offset - if you
	// simplify to a single branch you get the safe-zone drawn in the wrong
	// place in letterboxed PIE sessions. Don't collapse the two cases.
	constexpr FLinearColor RectColor        (0.8f, 0.9f, 1.0f, 0.8f);
	constexpr FLinearColor CenterColor      (0.7f, 0.9f, 1.0f, 0.8f);
	constexpr FLinearColor PivotCenterColor (0.6f, 0.78f, 1.0f, 0.8f);
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
		DrawScreenSpacePivotCanvasRect(Canvas, ScreenX, ScreenY, ScreenW, ScreenH, RectColor);

		const float CenterX = (SafeZoneCenter.X + 0.5f) * ScreenWidth;
		const float CenterY = (-SafeZoneCenter.Y + 0.5f) * ScreenHeight;
		DrawScreenSpacePivotCanvasRect(Canvas, CenterX - Radius, CenterY - Radius, 2.f * Radius, 2.f * Radius, CenterColor);

		FVector2D ScreenPosition;
		if (PC && UGameplayStatics::ProjectWorldToScreen(PC, Pivot, ScreenPosition))
		{
			DrawScreenSpacePivotCanvasRect(Canvas,
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

		// Read the letterboxed-viewport offset via the LocalPlayer's projection
		// data - this is the step that keeps the rect aligned when PIE runs
		// windowed with a pillar-boxed game area.
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
		DrawScreenSpacePivotCanvasRect(Canvas, ScreenX, ScreenY, ScreenW, ScreenH, RectColor);

		const float CenterX = (SafeZoneCenter.X  * RatioX + 0.5f) * ScreenWidth  - ScreenOffset.X;
		const float CenterY = (-SafeZoneCenter.Y * RatioY + 0.5f) * ScreenHeight - ScreenOffset.Y;
		DrawScreenSpacePivotCanvasRect(Canvas, CenterX - Radius, CenterY - Radius, 2.f * Radius, 2.f * Radius, CenterColor);

		FVector2D ScreenPosition;
		if (PC && UGameplayStatics::ProjectWorldToScreen(PC, Pivot, ScreenPosition, /*bPlayerViewportRelative=*/true))
		{
			DrawScreenSpacePivotCanvasRect(Canvas,
				ScreenPosition.X - Radius, ScreenPosition.Y - Radius,
				2.f * Radius, 2.f * Radius, PivotCenterColor);
		}
	}
}
#endif
