// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraMixingCameraNode.h"

#include <algorithm>

#include "Algo/MaxElement.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Math/ComposableCameraMath.h"


void UComposableCameraMixingCameraNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Mixing cameras depend on the PCM to spawn and manage their child camera
	// instances. There is no PCM-less equivalent for CreateNewCamera. The
	// Level Sequence path uses GetLevelSequenceCompatibility() == RequiresPCM
	// and will warn in the Details panel before activation; this guard is the
	// safety net in case a TypeAsset with a MixingCamera node is still evaluated
	// via the LS component path.
	if (!OwningPlayerCameraManager)
	{
		return;
	}

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
	// as a degrees-mode pose (FocalLength cleared). Same invariant as BlendBy().
	OutCameraPose.SetFieldOfViewDegrees(GetMixedFieldOfView(Poses, Weights));
}

void UComposableCameraMixingCameraNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Cameras (TArray of struct) is intentionally not exposed as a pin. It is consumed once at
	// OnInitialize. The four enum/float knobs below ARE per-frame relevant and follow the same
	// Details-only-by-default convention as the other nodes (bDefaultAsPin = false).
	// OnReceiveMixingCameraWeights is exposed as a delegate pin so callers can bind it at
	// activation time through the K2 ActivateComposableCamera node.
	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("MixMode");
		PinDecl.DisplayName = NSLOCTEXT("ComposableCameraMixingCameraNode", "MixMode", "Mix Mode");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraMixingCameraMode>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(MixMode)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("ComposableCameraMixingCameraNode", "MixModeTip",
			"Selects whether to mix position only, rotation only, or both. Note: when promoted to a context parameter, the value can change per frame regardless of the editor EditCondition cascade.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("WeightNormalizationMethod");
		PinDecl.DisplayName = NSLOCTEXT("ComposableCameraMixingCameraNode", "WeightNormalizationMethod", "Weight Normalization Method");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraMixingCameraWeightNormalizationMethod>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(WeightNormalizationMethod)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("ComposableCameraMixingCameraNode", "WeightNormalizationMethodTip",
			"Method used to normalize the per-camera weights before mixing -L1, L2, or SoftMax.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("MixRotationMethod");
		PinDecl.DisplayName = NSLOCTEXT("ComposableCameraMixingCameraNode", "MixRotationMethod", "Mix Rotation Method");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Enum;
		PinDecl.EnumType = StaticEnum<EComposableCameraMixingCameraRotationMethod>();
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(MixRotationMethod)) : FString();
		PinDecl.Tooltip = NSLOCTEXT("ComposableCameraMixingCameraNode", "MixRotationMethodTip",
			"Algorithm used to average rotations across mixed cameras. Only consulted when Mix Mode is RotationOnly or Both.");
		OutPins.Add(PinDecl);
	}

	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("CircularInterpEpsilon");
		PinDecl.DisplayName = NSLOCTEXT("ComposableCameraMixingCameraNode", "CircularInterpEpsilon", "Circular Interp Epsilon");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Float;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = false;
		PinDecl.DefaultValueString = FString::SanitizeFloat(CircularInterpEpsilon);
		PinDecl.Tooltip = NSLOCTEXT("ComposableCameraMixingCameraNode", "CircularInterpEpsilonTip",
			"Epsilon used by the Circular Interpolation rotation method. Only consulted when Mix Rotation Method is CircularInterp.");
		OutPins.Add(PinDecl);
	}

	// Delegate pin: OnReceiveMixingCameraWeights. Bound at activation time via
	// ApplyDelegateBindings. The signature function is retrieved from the
	// FDelegateProperty on this node's class; it carries the return type
	// (TArray<float>) that the K2 schema needs to validate wiring.
	{
		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = TEXT("OnReceiveMixingCameraWeights");
		PinDecl.DisplayName = NSLOCTEXT("ComposableCameraMixingCameraNode", "OnReceiveWeights", "On Receive Mixing Camera Weights");
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = EComposableCameraPinType::Delegate;
		PinDecl.bRequired = false;
		PinDecl.bDefaultAsPin = true; // Delegates are meaningfully bindable only as pins, not from the Details panel.
		PinDecl.Tooltip = NSLOCTEXT("ComposableCameraMixingCameraNode", "OnReceiveWeightsTip",
			"Delegate called each tick to provide per-camera mixing weights. Must return a TArray<float> with one entry per camera.");

		// Extract the signature UFunction from the FDelegateProperty via reflection.
		if (const FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(
				GetClass()->FindPropertyByName(TEXT("OnReceiveMixingCameraWeights"))))
		{
			PinDecl.SignatureFunction = DelegateProp->SignatureFunction;
		}
		OutPins.Add(PinDecl);
	}
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
	if (Weights.IsEmpty())
	{
		return;
	}

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
