// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraScreenSpacePivotNode.h"

#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/SceneCapture2D.h"
#include "GameFramework/HUD.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Math/ComposableCameraMath.h"
#include "Utils/ComposableCameraViewportUtils.h"

void UComposableCameraScreenSpacePivotNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	XInterpolator_T = TranslationParams.XInterpolator ? TranslationParams.XInterpolator->BuildDoubleInterpolator() : nullptr;
	YInterpolator_T = TranslationParams.YInterpolator ? TranslationParams.YInterpolator->BuildDoubleInterpolator() : nullptr;
	ZInterpolator_T = TranslationParams.ZInterpolator ? TranslationParams.ZInterpolator->BuildDoubleInterpolator() : nullptr;
	YawInterpolator_T = RotationParams.YawInterpolator ? RotationParams.YawInterpolator->BuildDoubleInterpolator() : nullptr;
	PitchInterpolator_T = RotationParams.PitchInterpolator ? RotationParams.PitchInterpolator->BuildDoubleInterpolator() : nullptr;

#if ENABLE_DRAW_DEBUG
	// PCM + PlayerController + HUD are all required for the debug draw lambda.
	// In the Level Sequence component path the PCM is null (see node's
	// GetLevelSequenceCompatibility() == RequiresPCM); fall through without
	// registering the debug-draw hook.
	if (OwningPlayerCameraManager)
	{
		if (APlayerController* PC = OwningPlayerCameraManager->GetOwningPlayerController())
		{
			if (AHUD* HUD = PC->GetHUD())
			{
				DrawDebugHandle = HUD->OnHUDPostRender.AddLambda([this](AHUD* HUD, UCanvas* Canvas)
				{
					DrawDebugInfo(HUD, Canvas);
				});
			}
		}
	}
#endif
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

#if ENABLE_DRAW_DEBUG
	if (OwningPlayerCameraManager)
	{
		AHUD* HUD = OwningPlayerCameraManager->GetOwningPlayerController()->GetHUD();
		HUD->OnHUDPostRender.Remove(DrawDebugHandle);
	}
#endif
}

void UComposableCameraScreenSpacePivotNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// NOTE: PivotSource and Method are declared as pins with bDefaultAsPin = false
	// so they render in the Details panel by default (design-time choice), but
	// remain individually promotable per-instance via the pin override toggle.
	// All pin names match their backing UPROPERTY FNames verbatim so the node
	// Details customization pairs them into single unified rows.

	// Input: PivotSource — selects WorldPosition vs ActorPosition pivot resolution.
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

	// Input: PivotWorldPosition — consumed when PivotSource == WorldPosition.
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

	// Input: PivotActor — consumed when PivotSource == ActorPosition.
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

	// Input: PivotWorldUpOffset — consumed when PivotSource == ActorPosition.
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

	// Input: Method — Translate vs Rotate strategy for keeping the pivot on-screen.
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
		"How to keep the pivot within the safe zone — Translate moves the camera, Rotate turns the camera.");
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
	const FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(CameraPosition, Pivot);
	
	// Get aspect ratio and tangent of half horizontal FOV.
	auto [DegTanHalfHOR, AspectRatio] =
		GetTanHalfHORAndAspectRatio(OutCameraPose);
	
	// Calibrate look-at rotation.
	auto [Pitch, Yaw] = CalibrateRotationOffsetNewton(
		DegTanHalfHOR,
		AspectRatio,
		Direction,
		LookAtRotation,
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

/** Using Levenberg–Marquardt's method to solve pitch (X) and yaw (Y).
 * The intuition is: we first assume X and Y, and get the desired camera space, defined by
 *
 * Forward Axis: [cos(X)cos(Y),  cos(X)sin(Y),  sin(X)]
 * Right Axis:   [-sin(Y),       cos(Y),        0     ]
 * Up Axis:      [-sin(X)cos(Y), -sin(X)sin(Y), cos(X)]
 *
 * Given a world-space Direction from camera to the pivot point, its desired camera space position should be
 *
 * Px = Forward * Direction
 * Py = Right * Direction
 * Pz = Up * Direction
 *
 * Then, use this information to match the desired screen space position, given by
 *
 * 2 * SafeZoneCenter.X = Py / (TanHalfHOR * Px)
 * 2 * SafeZoneCenter.Y = Pz / (TanHalfVOR * Px)
 *
 * We can then solve X and Y according to the above two nonlinear equations.
 *
 * WARNING: Do not exceed +- 90 degrees for pitch.
 *
 * I suspect there is some definite method to solve X and Y without iteration.
 */
std::pair<float, float> UComposableCameraScreenSpacePivotNode::CalibrateRotationOffsetLM(float TanHalfHOR,
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
	float Lambda = 1e-2f;    
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
		
		// Update using Levenberg–Marquardt (J^T*J+Lambda*I)Delta = -J^T*F.
		// Form J^T * J and J^T * F.
		const float JTJ_00 = DF1DX * DF1DX + DF2DX * DF2DX;
		const float JTJ_01 = DF1DX * DF1DY + DF2DX * DF2DY;
		const float JTJ_11 = DF1DY * DF1DY + DF2DY * DF2DY;

		const float JTF_0 = DF1DX * F1 + DF2DX * F2;
		const float JTF_1 = DF1DY * F1 + DF2DY * F2;

		// Apply Levenberg–Marquardt damping.
		const float JTJ_DAMP_00 = JTJ_00 + Lambda;
		const float JTJ_DAMP_11 = JTJ_11 + Lambda;
		const float Det = JTJ_DAMP_00 * JTJ_DAMP_11 - JTJ_01 * JTJ_01;
		if (FMath::Abs(Det) < 1e-3f)
		{
			break;
		}
		
		// Solve for Δ = -(JTJ + λI)⁻¹ * J^T * F.
		const float DX = (-JTF_0 * JTJ_DAMP_11 + JTF_1 * JTJ_01) / Det;
		const float DY = (-JTF_1 * JTJ_DAMP_00 + JTF_0 * JTJ_01) / Det;

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
			Lambda *= 0.5f;
			OldError = NewError;
		}
		else
		{
			// Reject step and increase lambda (move toward gradient descent).
			Lambda *= 2.0f;
		}

		if (NewError < 1e-2f)
		{
			break;
		}
	}
	
	return { FMath::RadiansToDegrees(X), FMath::RadiansToDegrees(Y) };
}

/** Using Newton's method to solve pitch and yaw. A little bit faster than LM.
 * Compiler Explorer: https://godbolt.org/z/o83sveY51
 */
std::pair<float, float> UComposableCameraScreenSpacePivotNode::CalibrateRotationOffsetNewton(float TanHalfHOR,
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

	auto [Pitch, Yaw] = CalibrateRotationOffsetNewton(
		DegTanHalfHor,
		AspectRatio,
		Direction,
		UKismetMathLibrary::MakeRotFromX(Direction),
		ScreenPositionX,
		ScreenPositionY);

	DesiredRotation.Yaw = Yaw;
	DesiredRotation.Pitch = Pitch;
	DeltaRotation = (DesiredRotation - CameraRotation).GetNormalized();
}

std::pair<float, float> UComposableCameraScreenSpacePivotNode::GetTanHalfHORAndAspectRatio(const FComposableCameraPose& OutCameraPose)
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

FVector UComposableCameraScreenSpacePivotNode::GetCurrentPivot()
{
	switch (PivotSource)
	{
	case EComposableCameraScreenSpacePivotSource::WorldPosition:
		return GetInputPinValue<FVector>("PivotWorldPosition");

	case EComposableCameraScreenSpacePivotSource::ActorPosition:
		{
			// PivotActor may be null if no default is authored and no upstream wire
			// resolved; fall back to the authored world-position default so screen-space
			// math doesn't pathologically target the world origin.
			AActor* Actor = GetInputPinValue<AActor*>("PivotActor");
			const float UpOffset = GetInputPinValue<float>("PivotWorldUpOffset");
			if (Actor)
			{
				return Actor->GetActorLocation() + FVector::UpVector * UpOffset;
			}
			return PivotWorldPosition;
		}
	}

	// Defensive: new enum case added without a branch above.
	return FVector::ZeroVector;
}

#if ENABLE_DRAW_DEBUG
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
#endif
