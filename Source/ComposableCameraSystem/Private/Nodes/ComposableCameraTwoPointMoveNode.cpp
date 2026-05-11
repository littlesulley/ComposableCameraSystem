// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraTwoPointMoveNode.h"

#include "Curves/CurveFloat.h"

void UComposableCameraTwoPointMoveNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	ElapsedTime = 0.f;
}

void UComposableCameraTwoPointMoveNode::OnTickNode_Implementation(
	float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose,
	FComposableCameraPose& OutCameraPose)
{
	ElapsedTime += DeltaTime;

	const float NormalizedTime = Duration > SMALL_NUMBER
		? FMath::Clamp(ElapsedTime / Duration, 0.f, 1.f)
		: 1.f;

	const float Alpha = Curve
		? FMath::Clamp(Curve->GetFloatValue(NormalizedTime), 0.f, 1.f)
		: NormalizedTime;

	OutCameraPose.Position = FMath::Lerp(
		SourceTransform.GetLocation(),
		TargetTransform.GetLocation(),
		Alpha);

	const FQuat SourceRotation = SourceTransform.GetRotation();
	const FQuat TargetRotation = TargetTransform.GetRotation();
	OutCameraPose.Rotation = FQuat::Slerp(SourceRotation, TargetRotation, Alpha).Rotator();
}

void UComposableCameraTwoPointMoveNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("SourceTransform");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_SourceTransform", "Source Transform");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Transform;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_SourceTransform_Tip",
			"Transform used at normalized time 0.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("TargetTransform");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_TargetTransform", "Target Transform");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Transform;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_TargetTransform_Tip",
			"Transform used at normalized time 1 and held after Duration.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Curve");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_Curve", "Curve");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Object;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_Curve_Tip",
			"Float curve sampled with X in [0,1]. The sampled value is clamped to [0,1] and used as interpolation alpha.");
		OutPins.Add(Pin);
	}

	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = TEXT("Duration");
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_Duration", "Duration");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(Duration);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "TwoPointMove_Duration_Tip",
			"Seconds spent moving from SourceTransform to TargetTransform. If zero, the target transform is used immediately.");
		OutPins.Add(Pin);
	}
}
