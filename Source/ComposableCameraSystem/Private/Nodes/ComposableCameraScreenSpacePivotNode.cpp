// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraScreenSpacePivotNode.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

void UComposableCameraScreenSpacePivotNode::OnBeginPlayNode_Implementation(
	const FComposableCameraPose& CurrentCameraPose)
{
	XInterpolator_T = TranslationParams.XInterpolator ? TranslationParams.XInterpolator->BuildDoubleInterpolator() : nullptr;
	YInterpolator_T = TranslationParams.YInterpolator ? TranslationParams.YInterpolator->BuildDoubleInterpolator() : nullptr;
	ZInterpolator_T = TranslationParams.ZInterpolator ? TranslationParams.ZInterpolator->BuildDoubleInterpolator() : nullptr;
	YawInterpolator_T = RotationParams.YawInterpolator ? RotationParams.YawInterpolator->BuildDoubleInterpolator() : nullptr;
	PitchInterpolator_T = RotationParams.PitchInterpolator ? RotationParams.PitchInterpolator->BuildDoubleInterpolator() : nullptr;
	
	AHUD* HUD = OwningPlayerCameraManager->GetOwningPlayerController()->GetHUD();
	DrawDebugHandle = HUD->OnHUDPostRender.AddLambda([this](AHUD* HUD, UCanvas* Canvas)
	{
		DrawDebugInfo(HUD, Canvas);
	});
}

void UComposableCameraScreenSpacePivotNode::OnTickNode_Implementation(float DeltaTime,
                                                                      const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FVector Pivot = GetCurrentPivot();
	
	if (Method == EComposableCameraScreenSpaceMethod::Translate)
	{
		FVector TranslateAmount = GetScreenSpaceTranslateAmount(Pivot, OutCameraPose, DeltaTime);

		// Write into OutCameraPose
		OutCameraPose.Position = GetOwningPlayerCameraManager()->GetCameraLocation() + TranslateAmount;
	}
	else if (Method == EComposableCameraScreenSpaceMethod::Rotate)
	{
		FRotator RotateAmount = GetScreenSpaceRotateAmount(Pivot, OutCameraPose, DeltaTime);

		// Write into OutCameraPose
		FQuat CurrentCameraRotation = OutCameraPose.Rotation.GetNormalized().Quaternion();
		FQuat LocalRotationPitch = FRotator(RotateAmount.Pitch, 0, 0).Quaternion();
		FQuat WorldRotationYaw = FRotator(0,  RotateAmount.Yaw, 0).Quaternion();
		FQuat NewCameraRotation = WorldRotationYaw * CurrentCameraRotation * LocalRotationPitch;
		OutCameraPose.Rotation = NewCameraRotation.GetNormalized().Rotator();
	}
}

void UComposableCameraScreenSpacePivotNode::BeginDestroy()
{
	Super::BeginDestroy();

	if (OwningPlayerCameraManager)
	{
		AHUD* HUD = OwningPlayerCameraManager->GetOwningPlayerController()->GetHUD();
		HUD->OnHUDPostRender.Remove(DrawDebugHandle);
	}
}

FVector UComposableCameraScreenSpacePivotNode::GetScreenSpaceTranslateAmount(const FVector& Pivot,
                                                                             const FComposableCameraPose& OutCameraPose, float DeltaTime)
{
	double CameraDistance = TranslationParams.CameraDistance;
	FVector CameraPosition = GetOwningPlayerCameraManager()->GetCameraLocation();
	FRotator CameraRotation = OutCameraPose.Rotation;
	FVector CameraSpacePivotPosition = UKismetMathLibrary::LessLess_VectorRotator(Pivot - CameraPosition, CameraRotation);
	
	// Calculate camera space damped X axis (camera distance) offset.
	double CameraSpaceDampedXOffset = CameraSpacePivotPosition.X - CameraDistance;

	if (XInterpolator_T)
	{
		XInterpolator_T->Reset(0, CameraSpaceDampedXOffset);
		CameraSpaceDampedXOffset = XInterpolator_T->Run(DeltaTime);
	}

	// Get aspect ratio.
	int32 ViewportX, ViewportY;
	OwningPlayerCameraManager->GetOwningPlayerController()->GetViewportSize(ViewportX, ViewportY);
	float AspectRatio = 1.0f * ViewportX / ViewportY;

	UKismetSystemLibrary::PrintString(this, FString::SanitizeFloat(ViewportX) + " " + FString::SanitizeFloat(ViewportY));
	//AspectRatio = OwningCamera->GetCameraComponent()->AspectRatio;

	// Get the expected camera distance.
	CameraDistance = CameraSpacePivotPosition.X - CameraSpaceDampedXOffset;

	// Calculate camera space damped Y/Z axis (camera distance) offset.
	float W = UKismetMathLibrary::DegTan(OutCameraPose.FieldOfView / 2.0f) * CameraDistance * 2.0f;
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
	EnsureWithinBounds(CameraSpacePivotPosition, CameraSpaceDampedOffset, AspectRatio, OutCameraPose.FieldOfView, CameraDistance);

	// Convert to world space offset.
	return UKismetMathLibrary::GreaterGreater_VectorRotator(CameraSpaceDampedOffset, CameraRotation);
	
}

void UComposableCameraScreenSpacePivotNode::EnsureWithinBounds(const FVector& CameraSpacePivotPosition,
	FVector& CameraSpaceDampedOffset, const float& AspectRatio, const float& FieldOfView, const float& CameraDistance)
{
	FVector DesiredCameraSpacePivotPosition = CameraSpacePivotPosition - CameraSpaceDampedOffset;

	float Width = UKismetMathLibrary::DegTan(FieldOfView / 2.0f) * CameraDistance * 2.0f;
	float LeftBound = (SafeZoneCenter.X + SafeZoneWidth.X) * Width;
	float RightBound = (SafeZoneCenter.X + SafeZoneWidth.Y) * Width;
	float BottomBound = (SafeZoneCenter.Y + SafeZoneHeight.X) * Width / AspectRatio;
	float TopBound = (SafeZoneCenter.Y + SafeZoneHeight.Y) * Width / AspectRatio;
	
	if (DesiredCameraSpacePivotPosition.Y < LeftBound)   CameraSpaceDampedOffset.Y += DesiredCameraSpacePivotPosition.Y - LeftBound;
	if (DesiredCameraSpacePivotPosition.Y > RightBound)  CameraSpaceDampedOffset.Y += DesiredCameraSpacePivotPosition.Y - RightBound;
	if (DesiredCameraSpacePivotPosition.Z < BottomBound) CameraSpaceDampedOffset.Z += DesiredCameraSpacePivotPosition.Z - BottomBound;
	if (DesiredCameraSpacePivotPosition.Z > TopBound)    CameraSpaceDampedOffset.Z += DesiredCameraSpacePivotPosition.Z - TopBound;
}

FRotator UComposableCameraScreenSpacePivotNode::GetScreenSpaceRotateAmount(const FVector& Pivot,
	const FComposableCameraPose& OutCameraPose, float DeltaTime)
{
	return FRotator{};
}

FVector UComposableCameraScreenSpacePivotNode::GetCurrentPivot()
{
	if (ContextPivotPosition.Variable)
	{
		return ContextPivotPosition.Variable->RuntimeValue;
	}
	else
	{
		return ContextPivotPosition.Value;
	}
}

void UComposableCameraScreenSpacePivotNode::DrawDebugInfo(AHUD* HUD, UCanvas* Canvas)
{
	if (OwningPlayerCameraManager && OwningPlayerCameraManager->bDrawDebugInformation)
	{
		constexpr FLinearColor RectColor = FLinearColor(0.8f, 0.9f, 1.0f, 0.8f);
		constexpr FLinearColor CenterColor = FLinearColor(0.7f, 0.9f, 1.0f, 0.8f);
		constexpr FLinearColor PivotCenterColor = FLinearColor(0.6f, 0.78f, 1.0f, 0.8f);
		constexpr float Radius = 5.0f;
		
		int32 ScreenWidth = Canvas->SizeX;
		int32 ScreenHeight = Canvas->SizeY;
		
		float ScreenX = (SafeZoneCenter.X + 0.5f + SafeZoneWidth.X) * ScreenWidth;
		float ScreenY = (-SafeZoneCenter.Y + 0.5f - SafeZoneHeight.Y) * ScreenHeight;
		float ScreenW = (SafeZoneWidth.Y - SafeZoneWidth.X) * ScreenWidth;
		float ScreenH = (SafeZoneHeight.Y - SafeZoneHeight.X) * ScreenHeight;
		HUD->DrawRect(RectColor, ScreenX, ScreenY, ScreenW, ScreenH);

		float CenterX = (SafeZoneCenter.X + 0.5f) * ScreenWidth;
		float CenterY = (-SafeZoneCenter.Y + 0.5f) * ScreenHeight;
		HUD->DrawRect(CenterColor, CenterX - Radius, CenterY - Radius, 2 * Radius, 2 * Radius);
	
		FVector2D ScreenPosition;
		FVector Pivot = GetCurrentPivot();
		UGameplayStatics::ProjectWorldToScreen(OwningPlayerCameraManager->GetOwningPlayerController(), Pivot, ScreenPosition);
		HUD->DrawRect(PivotCenterColor, ScreenPosition.X - Radius, ScreenPosition.Y - Radius, 2 * Radius, 2 * Radius);
	}
} 
