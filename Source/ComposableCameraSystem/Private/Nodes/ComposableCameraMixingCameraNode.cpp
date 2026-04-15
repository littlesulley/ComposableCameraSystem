// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraMixingCameraNode.h"

#include <algorithm>

#include "Algo/MaxElement.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Math/ComposableCameraMath.h"


void UComposableCameraMixingCameraNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	for (const FComposableCameraMixingCameraNodeCameraDefinition& Definition : Cameras)
	{
		FComposableCameraActivateParams ActivationParams (
			Definition.ActivationParams.bPreserveCameraPose,
			Definition.ActivationParams.InitialTransform,
			Definition.ActivationParams.bUseInitialTransformRotation,
			false,
			0.f
		);
		AComposableCameraCameraBase* CameraInstance = OwningPlayerCameraManager->CreateNewCamera(
			Definition.CameraClass, ActivationParams);

		CameraInstances.Add(CameraInstance);
	}
}

void UComposableCameraMixingCameraNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	TArray<float> Weights = OnReceiveMixingCameraWeights.IsBound() ? OnReceiveMixingCameraWeights.Execute() : TArray<float>();

	if (Weights.Num() != CameraInstances.Num())
	{
		UE_LOG(LogComposableCameraSystem, Error, TEXT("The number of weights (%d) is not equal to the number of cameras (%d), will not proceed."), Weights.Num(), CameraInstances.Num())
		return;
	}

	TArray<FComposableCameraPose> Poses;
	TickCameras(Poses, DeltaTime);
	NormalizeWeights(Weights);

	switch (MixMode)
	{
	case EComposableCameraMixingCameraMode::PositionOnly:
		OutCameraPose.Position = GetMixedPosition(Poses, Weights);
		break;
	case EComposableCameraMixingCameraMode::RotationOnly:
		OutCameraPose.Rotation = GetMixedRotation(Poses, Weights);
		break;
	case EComposableCameraMixingCameraMode::Both:
		OutCameraPose.Position = GetMixedPosition(Poses, Weights);
		OutCameraPose.Rotation = GetMixedRotation(Poses, Weights);
		break;
	}

	// Mix FOV through effective-degrees so each mixed pose contributes its resolved FOV
	// regardless of whether it was expressed via FieldOfView or FocalLength. Emit the result
	// as a degrees-mode pose (FocalLength cleared) — same invariant as BlendBy().
	OutCameraPose.SetFieldOfViewDegrees(GetMixedFieldOfView(Poses, Weights));
}

void UComposableCameraMixingCameraNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// MixingCamera node uses a delegate for weights and Cameras array as UPROPERTY.
	// These are runtime-configured and don't map cleanly to the pin system.
	// Return empty array.
}


void UComposableCameraMixingCameraNode::BeginDestroy()
{
	Super::BeginDestroy();

	for (AComposableCameraCameraBase* CameraInstance : CameraInstances)
	{
		CameraInstance->Destroy();
	}
}

void UComposableCameraMixingCameraNode::SetUpdateWeights(FOnReceiveMixingCameraWeights OnUpdateMixingCameraWeights)
{
	OnReceiveMixingCameraWeights = OnUpdateMixingCameraWeights;
}

void UComposableCameraMixingCameraNode::NormalizeWeights(TArray<float>& Weights)
{
	float Accumulation = 0.f;
	const float MaxWeight = *Algo::MaxElement(Weights);
	
	for (uint32 Index = 0; AComposableCameraCameraBase* Camera : CameraInstances)
	{
		float CurrentWeight = 0.f;

		switch (WeightNormalizationMethod)
		{
		case EComposableCameraMixingCameraWeightNormalizationMethod::L1:
			CurrentWeight = FMath::Abs(Weights[Index]);
			break;
		case EComposableCameraMixingCameraWeightNormalizationMethod::L2:
			CurrentWeight = Weights[Index] * Weights[Index];
			break;
		case EComposableCameraMixingCameraWeightNormalizationMethod::SoftMax:
			CurrentWeight = FMath::Exp(Weights[Index] - MaxWeight);
			break;
		}
		
		if (!Camera)
		{
			CurrentWeight = 0.f;
		}

		Weights[Index] = CurrentWeight;
		Accumulation += CurrentWeight; 
		Index++;
	}

	for (int32 Index = 0; Index < Weights.Num(); Index++)
	{
		Weights[Index] /= (Accumulation + UE_KINDA_SMALL_NUMBER);
	}
}

void UComposableCameraMixingCameraNode::TickCameras(TArray<FComposableCameraPose>& Poses, float DeltaTime)
{
	for (int32 Index = 0; Index < CameraInstances.Num(); Index++)
	{
		if (CameraInstances[Index])
		{
			Poses.Add(CameraInstances[Index]->TickCamera(DeltaTime));
		}
		else
		{
			Poses.Add(FComposableCameraPose());
		}
	}
}

FVector UComposableCameraMixingCameraNode::GetMixedPosition(const TArray<FComposableCameraPose>& Poses,
	const TArray<float>& Weights)
{
	FVector OutPosition = FVector::ZeroVector;

	for (int32 Index = 0; Index < Poses.Num(); Index++)
	{
		OutPosition += Weights[Index] * Poses[Index].Position;
	}

	return OutPosition;
}

FRotator UComposableCameraMixingCameraNode::GetMixedRotation(const TArray<FComposableCameraPose>& Poses,
	const TArray<float>& Weights)
{
	TArray<FRotator> Rotations;
	Rotations.SetNum(Poses.Num());
	std::transform(Poses.GetData(), Poses.GetData() + Poses.Num(), Rotations.GetData(),
		[](const FComposableCameraPose& Pose){
		return Pose.Rotation;
	});

	switch (MixRotationMethod)
	{
	case EComposableCameraMixingCameraRotationMethod::MatrixInterp: {
		auto [Rotation, EigenVector] = ComposableCameraSystem::MatrixInterpRotation(Rotations, Weights, InitialEigenVector);
		InitialEigenVector = EigenVector;
		return Rotation;
	}
	case EComposableCameraMixingCameraRotationMethod::CircularInterp:
		return ComposableCameraSystem::CircularInterpRotation(Rotations, Weights, CircularInterpEpsilon);
	case EComposableCameraMixingCameraRotationMethod::QuaternionInterpolation:
		return ComposableCameraSystem::QuaternionInterpRotation(Rotations, Weights);
	case EComposableCameraMixingCameraRotationMethod::AngleInterpolation:
		return ComposableCameraSystem::AngleInterpRotation(Rotations, Weights);
	}

	return FRotator{};
}

double UComposableCameraMixingCameraNode::GetMixedFieldOfView(const TArray<FComposableCameraPose>& Poses,
	const TArray<float>& Weights)
{
	double OutFieldOfView = 0.f;

	for (int32 Index = 0; Index < Poses.Num(); Index++)
	{
		// Resolve each pose's effective FOV (handles both degrees-mode and physical-mode poses)
		// before weighting, so mixing correctly handles a mix of physical and non-physical cameras.
		OutFieldOfView += Weights[Index] * Poses[Index].GetEffectiveFieldOfView();
	}

	return OutFieldOfView;
}
