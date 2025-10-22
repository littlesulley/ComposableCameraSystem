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
		OutCameraPose.Position = GetOwningCamera()->GetCameraPose().Position + TranslateAmount;
	}
	else if (Method == EComposableCameraScreenSpaceMethod::Rotate)
	{
		FRotator RotateAmount = GetScreenSpaceRotateAmount(Pivot, OutCameraPose, DeltaTime);

		// Write into OutCameraPose
		OutCameraPose.Rotation = (OutCameraPose.Rotation + RotateAmount).GetNormalized();
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

void UComposableCameraScreenSpacePivotNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraScreenSpacePivotNode* CastedInitializer = Cast<UComposableCameraScreenSpacePivotNode>(Initializer))
	{
		Method = CastedInitializer->Method;
		TranslationParams = CastedInitializer->TranslationParams;
		RotationParams = CastedInitializer->RotationParams;
		SafeZoneCenter = CastedInitializer->SafeZoneCenter;
		SafeZoneWidth = CastedInitializer->SafeZoneWidth;
		SafeZoneHeight = CastedInitializer->SafeZoneHeight;
	}
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
	
	FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(CameraPosition, Pivot);
	
	// Get aspect ratio and tangent of half horizontal FOV.
	auto [DegTanHalfHOR, AspectRatio] =
		GetTanHalfHORAndAspectRatio(OutCameraPose);
	
	// Calibrate look-at rotation.
	float VerticalAngle = UKismetMathLibrary::DegAtan(2.0 * SafeZoneCenter.Y * DegTanHalfHOR / AspectRatio);
	float HorizontalAngle = UKismetMathLibrary::DegAtan(2.0 * SafeZoneCenter.X * DegTanHalfHOR);

	// auto [PitchOffset, YawOffset] = CalibrateRotationOffset(
	// 	UKismetMathLibrary::DegTan(HorizontalAngle),
	// 	UKismetMathLibrary::DegTan(VerticalAngle),
	// 	90 - LookAtRotation.Pitch);
	//
	// LookAtRotation.Yaw += YawOffset;
	// LookAtRotation.Pitch += PitchOffset;
	// float YawOffset = UKismetMathLibrary::DegAsin(UKismetMathLibrary::DegSin(HorizontalAngle) / UKismetMathLibrary::DegSin(90.f - LookAtRotation.Pitch));
	// LookAtRotation.Yaw -= YawOffset;
	// LookAtRotation.Pitch -= UKismetMathLibrary::DegAsin(UKismetMathLibrary::DegSin(90.f - LookAtRotation.Pitch) * UKismetMathLibrary::DegCos(90.f - LookAtRotation.Pitch) * (UKismetMathLibrary::DegCos(YawOffset) - 1.f));

	auto [Pitch, Yaw] = CalibrateRotationOffset(DegTanHalfHOR, AspectRatio, Pivot - CameraPosition);
	//Pitch = 90 - Pitch;

	LookAtRotation = FRotator { Pitch, Yaw, 0 };
	
	FRotator DeltaRotation = UKismetMathLibrary::NormalizedDeltaRotator(LookAtRotation, CameraRotation);
	
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

	//EnsureWithinBoundsRotation(CameraRotation, LookAtRotation, DeltaRotation, AspectRatio, DegTanHalfHOR);

	return DeltaRotation;
}

// This function iteratively solves the following equations for x and y using Newton's method:
// P(sin(A+x)*sin(A)*cos(y)+cos(A+x)*cos(A)) = -sin(A)*sin(y)
// Q(sin(A+x)*sin(A)*cos(y)+cos(A+x)*cos(A)) = sin(A)*cos(A+x)*cos(y)-cos(A)*sin(A+x)
// The default iteration step is 5.
std::pair<float, float> UComposableCameraScreenSpacePivotNode::CalibrateRotationOffset(double P, double Q, double A)
{
	constexpr static int Steps = 10;
	float X = 0, Y = 0;
	float OldX = X, OldY = Y;

	for (uint32 i = 0; i < Steps; ++i)
	{
		OldX = X, OldY = Y;

		// Y = UKismetMathLibrary::DegAsin(-P / UKismetMathLibrary::DegSin(A));
		// X = X -
		// 	(UKismetMathLibrary::DegSin(A) * UKismetMathLibrary::DegCos(A + X) * UKismetMathLibrary::DegCos(Y) - UKismetMathLibrary::DegCos(A) * UKismetMathLibrary::DegSin(A + X) - Q)
		//   / (-UKismetMathLibrary::DegSin(A) * UKismetMathLibrary::DegSin(A + X) * UKismetMathLibrary::DegCos(Y) - UKismetMathLibrary::DegCos(A) * UKismetMathLibrary::DegCos(A + X));

		if (FMath::Abs(X - OldX) < 1e-4 && FMath::Abs(Y - OldY) < 1e-4)
		{
			break;
		}
	}

	return { X, Y };
}

std::pair<float, float> UComposableCameraScreenSpacePivotNode::CalibrateRotationOffset(double TanHalfHOR, double AspectRatio, FVector Direction)
{
	using Trig_T = double(*)(double);
	Trig_T Sin = &UKismetMathLibrary::DegSin;
	Trig_T Cos = &UKismetMathLibrary::DegCos;
	
	constexpr static int Steps = 20;
	const float A = Direction.X;
	const float B = Direction.Y;
	const float C = Direction.Z;
	const float TanHalfVOR = TanHalfHOR / AspectRatio;

	FRotator InitialRotation = UKismetMathLibrary::MakeRotFromX(Direction);
	float P = InitialRotation.Pitch, Y = InitialRotation.Yaw;
	float OldP = P, OldY = Y;

	uint32 Iteration = 0;
	for (Iteration = 0; Iteration < Steps; ++Iteration)
	{
		OldP = P, OldY = Y;

		float Y1 = 2 * SafeZoneCenter.X * TanHalfHOR * (A * Sin(P) * Cos(Y) + B * Sin(P) * Sin(Y) + C * Cos(P)) - (-A * Sin(Y) + B * Cos(Y));
		float Y2 = 2 * SafeZoneCenter.X * TanHalfHOR * (-A * Sin(P) * Sin(Y) + B * Sin(P) * Cos(Y))             - (-A * Cos(Y) - B * Sin(Y));
		Y = Y - Y1 / Y2;

		float P1 = 2 * SafeZoneCenter.Y * TanHalfVOR * (A * Sin(P) * Cos(Y) + B * Sin(P) * Sin(Y) + C * Cos(P)) - (A * Cos(P) * Cos(Y) + B * Cos(P) * Sin(Y) - C * Sin(P));
		float P2 = 2 * SafeZoneCenter.Y * TanHalfVOR * (A * Cos(P) * Cos(Y) + B * Cos(P) * Sin(Y) - C * Sin(P)) - (-A * Sin(P) * Cos(Y) - B * Sin(P) * Sin(Y) - C * Cos(P));
		P = P - P1 / P2;

		if (FMath::Abs(P - OldP) < 1e-4 && FMath::Abs(Y - OldY) < 1e-4)
		{
			break;
		}
	}
	
	UKismetSystemLibrary::PrintString(this, "Iteration is " + FString::FromInt(Iteration));
	UKismetSystemLibrary::PrintString(this, "OldP - P is " + FString::SanitizeFloat(OldP - P));
	UKismetSystemLibrary::PrintString(this, "OldY - Y is " + FString::SanitizeFloat(OldY - Y));
	
	UKismetSystemLibrary::PrintString(this, "P is " + FString::SanitizeFloat(P));
	UKismetSystemLibrary::PrintString(this, "Y is " + FString::SanitizeFloat(Y));

	return { P, Y };
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

void UComposableCameraScreenSpacePivotNode::EnsureWithinBoundsRotation(const FRotator& CameraRotation, const FRotator& LookAtRotation, FRotator& DeltaRotation,
	float AspectRatio, float DegTanHalfHor)
{
	double LeftBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.X + SafeZoneWidth.X) * DegTanHalfHor); 
	double RightBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.X + SafeZoneWidth.Y) * DegTanHalfHor); 
	double BottomBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.Y + SafeZoneHeight.X) * DegTanHalfHor / AspectRatio);
	double TopBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.Y + SafeZoneHeight.Y) * DegTanHalfHor / AspectRatio);

	FRotator DesiredRotation = (CameraRotation + DeltaRotation).GetNormalized();
	FRotator ResultRotationDiff = UKismetMathLibrary::NormalizedDeltaRotator(LookAtRotation, DesiredRotation);

	// Calibrate bounding angles according to current camera pitch.
	LeftBound = UKismetMathLibrary::DegAsin(UKismetMathLibrary::DegSin(LeftBound) / UKismetMathLibrary::DegSin(90.f - LookAtRotation.Pitch));
	RightBound = UKismetMathLibrary::DegAsin(UKismetMathLibrary::DegSin(RightBound) / UKismetMathLibrary::DegSin(90.f - LookAtRotation.Pitch));
	
	if (ResultRotationDiff.Yaw < LeftBound) DeltaRotation.Yaw += ResultRotationDiff.Yaw - LeftBound;
	if (ResultRotationDiff.Yaw > RightBound) DeltaRotation.Yaw += ResultRotationDiff.Yaw - RightBound;

	BottomBound += UKismetMathLibrary::DegAsin(UKismetMathLibrary::DegSin(90.f - LookAtRotation.Pitch) * UKismetMathLibrary::DegCos(90.f - LookAtRotation.Pitch) * (UKismetMathLibrary::DegCos(ResultRotationDiff.Yaw) - 1.f));
	TopBound += UKismetMathLibrary::DegAsin(UKismetMathLibrary::DegSin(90.f - LookAtRotation.Pitch) * UKismetMathLibrary::DegCos(90.f - LookAtRotation.Pitch) * (UKismetMathLibrary::DegCos(ResultRotationDiff.Yaw) - 1.f));

	if (ResultRotationDiff.Pitch < BottomBound) DeltaRotation.Pitch += ResultRotationDiff.Pitch - BottomBound;
	if (ResultRotationDiff.Pitch > TopBound) DeltaRotation.Pitch += ResultRotationDiff.Pitch - TopBound;
}

std::pair<float, float> UComposableCameraScreenSpacePivotNode::GetTanHalfHORAndAspectRatio(const FComposableCameraPose& OutCameraPose)
{
	int32 ViewportX, ViewportY;
	OwningPlayerCameraManager->GetOwningPlayerController()->GetViewportSize(ViewportX, ViewportY);

	float DegTanHalfHOR = UKismetMathLibrary::DegTan(OutCameraPose.FieldOfView / 2.0f);;
	float AspectRatio = 1.0f * ViewportX / ViewportY;;

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
				DegTanHalfHOR = UKismetMathLibrary::DegTan(OutCameraPose.FieldOfView / 2.0f);
				break;
			case AspectRatio_MaintainYFOV:
				DegTanHalfHOR = UKismetMathLibrary::DegTan(OutCameraPose.FieldOfView / 2.0f) / OwningCamera->GetCameraComponent()->AspectRatio * AspectRatio;
				break;
			case AspectRatio_MajorAxisFOV:
				if (ViewportX > ViewportY)
				{
					DegTanHalfHOR = UKismetMathLibrary::DegTan(OutCameraPose.FieldOfView / 2.0f);
				}
				else
				{
					DegTanHalfHOR = UKismetMathLibrary::DegTan(OutCameraPose.FieldOfView / 2.0f) / OwningCamera->GetCameraComponent()->AspectRatio * AspectRatio;
				}
				break;
			default:
				DegTanHalfHOR = UKismetMathLibrary::DegTan(OutCameraPose.FieldOfView / 2.0f);
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
		
		if (!OwningCamera->GetCameraComponent()->bConstrainAspectRatio)
		{
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
		else
		{
			float ViewportAspectRatio = 1.0f * ScreenWidth / ScreenHeight;
			float AspectRatio = OwningCamera->GetCameraComponent()->AspectRatio;
			
			float ClampedScreenWidth = ScreenWidth;
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
			ULocalPlayer* const LP = OwningPlayerCameraManager->GetOwningPlayerController() ? OwningPlayerCameraManager->GetOwningPlayerController()->GetLocalPlayer() : nullptr;
			
			if (LP && LP->ViewportClient)
			{
				FSceneViewProjectionData ProjectionData;
				LP->GetProjectionData(LP->ViewportClient->Viewport, ProjectionData);
				ScreenOffset = FVector2D(ProjectionData.GetConstrainedViewRect().Min);
			}
			
			float RatioX = ClampedScreenWidth / ScreenWidth;
			float RatioY = ClampedScreenHeight / ScreenHeight;
			
			float ScreenX = (SafeZoneCenter.X * RatioX + 0.5f + SafeZoneWidth.X * RatioX) * ScreenWidth - ScreenOffset.X;
			float ScreenY = (-SafeZoneCenter.Y * RatioY + 0.5f - SafeZoneHeight.Y * RatioY) * ScreenHeight - ScreenOffset.Y;
			float ScreenW = (SafeZoneWidth.Y * RatioX - SafeZoneWidth.X * RatioX) * ScreenWidth;
			float ScreenH = (SafeZoneHeight.Y * RatioY - SafeZoneHeight.X * RatioY) * ScreenHeight;
			HUD->DrawRect(RectColor, ScreenX, ScreenY, ScreenW, ScreenH);
			
			float CenterX = (SafeZoneCenter.X * RatioX + 0.5f) * ScreenWidth - ScreenOffset.X;
			float CenterY = (-SafeZoneCenter.Y * RatioY + 0.5f) * ScreenHeight - ScreenOffset.Y;
			HUD->DrawRect(CenterColor, CenterX - Radius, CenterY - Radius, 2 * Radius, 2 * Radius);
	
			FVector2D ScreenPosition;
			FVector Pivot = GetCurrentPivot();
			UGameplayStatics::ProjectWorldToScreen(OwningPlayerCameraManager->GetOwningPlayerController(), Pivot, ScreenPosition, true);
			HUD->DrawRect(PivotCenterColor, ScreenPosition.X - Radius, ScreenPosition.Y - Radius, 2 * Radius, 2 * Radius);
		}
	}
} 
