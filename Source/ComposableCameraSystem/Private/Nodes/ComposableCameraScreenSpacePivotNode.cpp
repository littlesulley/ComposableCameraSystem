// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraScreenSpacePivotNode.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Engine/Canvas.h"
#include "Engine/SceneCapture2D.h"
#include "GameFramework/HUD.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"

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

	auto [Pitch, Yaw] = CalibrateRotationOffset(
		DegTanHalfHOR,
		AspectRatio,
		Pivot - CameraPosition,
		LookAtRotation);
	
	LookAtRotation.Yaw = Yaw;
	LookAtRotation.Pitch = 90.f - Pitch;
	
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

std::pair<float, float> UComposableCameraScreenSpacePivotNode::CalibrateRotationOffset(double P, double Q, double A)
{
	constexpr static int Steps = 10;
	float X = 0, Y = 0;
	float OldX = X, OldY = Y;

	for (uint32 i = 0; i < Steps; ++i)
	{
		OldX = X, OldY = Y;

		Y = UKismetMathLibrary::DegAsin(-P / UKismetMathLibrary::DegSin(A));
		X = X -
			(UKismetMathLibrary::DegSin(A) * UKismetMathLibrary::DegCos(A + X) * UKismetMathLibrary::DegCos(Y) - UKismetMathLibrary::DegCos(A) * UKismetMathLibrary::DegSin(A + X) - Q)
		  / (-UKismetMathLibrary::DegSin(A) * UKismetMathLibrary::DegSin(A + X) * UKismetMathLibrary::DegCos(Y) - UKismetMathLibrary::DegCos(A) * UKismetMathLibrary::DegCos(A + X));

		if (FMath::Abs(X - OldX) < 1e-4 && FMath::Abs(Y - OldY) < 1e-4)
		{
			break;
		}
	}

	return { X, Y };
}

std::pair<float, float> UComposableCameraScreenSpacePivotNode::CalibrateRotationOffset(float TanHalfHOR,
	float AspectRatio, FVector Direction, FRotator LookAtRotation)
{
	using Trig_T = double(*)(double);
	Trig_T Sin = &FMath::Sin;
	Trig_T Cos = &FMath::Cos;
	
	constexpr static int Steps = 10;
	const float TanHalfVOR = TanHalfHOR / AspectRatio;
	const float a = SafeZoneCenter.X;
	const float b = SafeZoneCenter.Y;
	const float m = TanHalfHOR;
	const float n = TanHalfVOR;
	const float A = Direction.X;
	const float B = Direction.Y;
	const float C = Direction.Z;
	
	float X = LookAtRotation.Pitch - UKismetMathLibrary::DegAtan(2.0 * SafeZoneCenter.Y * TanHalfVOR);
	float Y = LookAtRotation.Yaw   - UKismetMathLibrary::DegAtan(2.0 * SafeZoneCenter.X * TanHalfHOR);
	X = FMath::DegreesToRadians(X);
	Y = FMath::DegreesToRadians(Y);
	float OldX = X, OldY = Y;

	float Lambda = 1e-2f;      // Start with small damping
	float OldError = FLT_MAX;
	
	uint32 Iteration = 0;
	for (Iteration = 0; Iteration < Steps; ++Iteration)
	{
		OldX = X, OldY = Y;

		// Compute common terms.
		const float SinX = Sin(X);
		const float CosX = Cos(X);
		const float SinY = Sin(Y);
		const float CosY = Cos(Y);

		const float S = A * SinX * CosY + B * SinX * SinY + C * CosY;
		const float D = A * CosY + B * SinY;

		const float F1 = 2.f * a * m * S - (-A * SinY + B * CosY);
		const float F2 = 2.f * b * n * S - (A * CosX * CosY + B * CosX * SinY - C * SinY);

		// Compute Jacobian.
		const float DSDX = CosX * D;
		const float DSDY = SinX * (-A * SinY + B * CosY) - C * SinY;

		const float DF1DX = 2.f * a * m * DSDX;
		const float DF1DY = 2.f * a * m * DSDY - (-A * CosY - B * SinY);
		const float DF2DX = 2.f * b * n * DSDX + SinX * D;
		const float DF2DY = 2.f * b * n * DSDY - CosX * (-A * SinY + B * CosY) + C * CosY;
		
		// Update.
		// const float Det = DF1DX * DF2DY - DF1DY * DF2DX;
		// if (FMath::Abs(Det) < 1e-4)
		// {
		// 	break;
		// }
		//
		// constexpr static float Lambda = 0.5f;
		// const float DX = (-F1 * DF2DY + F2 * DF1DY) / Det;
		// const float DY = (-DF1DX * F2 + DF2DX * F1) / Det;
		// X += Lambda * DX;
		// Y += Lambda * DY;
		//
		// if (FMath::Abs(X - OldX) < 1e-4 && FMath::Abs(Y - OldY) < 1e-4)
		// {
		// 	break;
		// }
		// Form J^T * J and J^T * F
		const float JTJ_00 = DF1DX * DF1DX + DF2DX * DF2DX;
		const float JTJ_01 = DF1DX * DF1DY + DF2DX * DF2DY;
		const float JTJ_11 = DF1DY * DF1DY + DF2DY * DF2DY;

		const float JTF_0 = DF1DX * F1 + DF2DX * F2;
		const float JTF_1 = DF1DY * F1 + DF2DY * F2;

		// Apply Levenberg–Marquardt damping
		const float JTJ_DAMP_00 = JTJ_00 + Lambda;
		const float JTJ_DAMP_11 = JTJ_11 + Lambda;
		const float Det = JTJ_DAMP_00 * JTJ_DAMP_11 - JTJ_01 * JTJ_01;
		if (FMath::Abs(Det) < 1e-8f)
			break;

		// Solve for Δ = -(JTJ + λI)⁻¹ * J^T * F
		const float DX = (-JTF_0 * JTJ_DAMP_11 + JTF_1 * JTJ_01) / Det;
		const float DY = (-JTF_1 * JTJ_DAMP_00 + JTF_0 * JTJ_01) / Det;

		const float NewX = X + DX;
		const float NewY = Y + DY;

		// Evaluate new residual magnitude
		const float SinXn = Sin(NewX);
		const float CosXn = Cos(NewX);
		const float SinYn = Sin(NewY);
		const float CosYn = Cos(NewY);

		const float Sn = A * SinXn * CosYn + B * SinXn * SinYn + C * CosYn;
		const float F1n = 2.f * a * m * Sn - (-A * SinYn + B * CosYn);
		const float F2n = 2.f * b * n * Sn - (A * CosXn * CosYn + B * CosXn * SinYn - C * SinYn);

		const float NewError = FMath::Sqrt(F1n * F1n + F2n * F2n);
		const float OldErrorMag = FMath::Sqrt(F1 * F1 + F2 * F2);

		// Adaptive lambda adjustment
		if (NewError < OldErrorMag)
		{
			// Accept update and decrease lambda (move toward Gauss–Newton)
			X = NewX;
			Y = NewY;
			Lambda *= 0.5f;
			OldError = NewError;
		}
		else
		{
			// Reject step and increase lambda (move toward gradient descent)
			Lambda *= 2.0f;
		}

		if (NewError < 1e-6f)
			break;
	}

	UKismetSystemLibrary::PrintString(this, "Error is: " + FString::SanitizeFloat(OldError));

	return { FMath::RadiansToDegrees(X), FMath::RadiansToDegrees(Y) };
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
