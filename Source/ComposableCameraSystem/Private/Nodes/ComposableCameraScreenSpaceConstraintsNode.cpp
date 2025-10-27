// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraScreenSpaceConstraintsNode.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Nodes/ComposableCameraScreenSpacePivotNode.h"

void UComposableCameraScreenSpaceConstraintsNode::OnBeginPlayNode_Implementation(
	const FComposableCameraPose& CurrentCameraPose)
{
	AHUD* HUD = OwningPlayerCameraManager->GetOwningPlayerController()->GetHUD();
	DrawDebugHandle = HUD->OnHUDPostRender.AddLambda([this](AHUD* HUD, UCanvas* Canvas)
	{
		DrawDebugInfo(HUD, Canvas);
	});
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

void UComposableCameraScreenSpaceConstraintsNode::BeginDestroy()
{
	Super::BeginDestroy();

	if (OwningPlayerCameraManager)
	{
		AHUD* HUD = OwningPlayerCameraManager->GetOwningPlayerController()->GetHUD();
		HUD->OnHUDPostRender.Remove(DrawDebugHandle);
	}
}

void UComposableCameraScreenSpaceConstraintsNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraScreenSpaceConstraintsNode* CastedInitializer = Cast<UComposableCameraScreenSpaceConstraintsNode>(Initializer))
	{
		Method = CastedInitializer->Method;
		SafeZoneCenter = CastedInitializer->SafeZoneCenter;
		SafeZoneWidth = CastedInitializer->SafeZoneWidth;
		SafeZoneHeight = CastedInitializer->SafeZoneHeight;
	}
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

FVector UComposableCameraScreenSpaceConstraintsNode::GetCurrentPivot()
{
	if (ContextPivotActor.Variable && ContextPivotActor.Variable->RuntimeValue)
	{
		return ContextPivotActor.Variable->RuntimeValue->GetActorLocation();
	}
	else if (ContextPivotActor.Value)
	{
		return ContextPivotActor.Value->GetActorLocation();
	}

	return FVector::ZeroVector;
}

void UComposableCameraScreenSpaceConstraintsNode::DrawDebugInfo(AHUD* HUD, UCanvas* Canvas)
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
