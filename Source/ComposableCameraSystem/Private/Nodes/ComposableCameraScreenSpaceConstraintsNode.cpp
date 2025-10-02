// Fill out your copyright notice in the Description page of Project Settings.


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
	double LeftBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.X + SafeZoneWidth.X) * DegTanHalfHor);
	double RightBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.X + SafeZoneWidth.Y) * DegTanHalfHor);
	double BottomBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.Y + SafeZoneHeight.X) * DegTanHalfHor / AspectRatio);
	double TopBound = UKismetMathLibrary::DegAtan(2.0 * (SafeZoneCenter.Y + SafeZoneHeight.Y) * DegTanHalfHor / AspectRatio);

	FRotator RotateAmount { FRotator::ZeroRotator };
	FRotator ResultRotationDiff = UKismetMathLibrary::NormalizedDeltaRotator(UKismetMathLibrary::FindLookAtRotation(CurrentPose.Position, Pivot), CurrentPose.Rotation);

	if (ResultRotationDiff.Yaw < LeftBound)     RotateAmount.Yaw = ResultRotationDiff.Yaw - LeftBound;
	if (ResultRotationDiff.Yaw > RightBound)    RotateAmount.Yaw = ResultRotationDiff.Yaw - RightBound;
	if (ResultRotationDiff.Pitch < BottomBound) RotateAmount.Pitch = ResultRotationDiff.Pitch - BottomBound;
	if (ResultRotationDiff.Pitch > TopBound)    RotateAmount.Pitch = ResultRotationDiff.Pitch - TopBound;

	return RotateAmount;
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
