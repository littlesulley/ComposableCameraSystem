// Copyright 2026 Sulley. All Rights Reserved.
#include "Nodes/ComposableCameraMathNodes.h"

#include "Curves/CurveFloat.h"

namespace ComposableCameraMathNodesPrivate
{
	const FName ResultPinName(TEXT("Result"));

	void AddPin(TArray<FComposableCameraNodePinDeclaration>& Pins, FName Name,
		EComposableCameraPinType Type, const FString& DefaultValue = FString(),
		EComposableCameraPinDirection Direction = EComposableCameraPinDirection::Input)
	{
		FComposableCameraNodePinDeclaration& Pin = Pins.AddDefaulted_GetRef();
		Pin.PinName = Name;
		Pin.DisplayName = FText::FromString(FName::NameToDisplayString(Name.ToString(), Type == EComposableCameraPinType::Bool));
		Pin.PinType = Type;
		Pin.Direction = Direction;
		Pin.DefaultValueString = DefaultValue;
	}

	void AddFloat(TArray<FComposableCameraNodePinDeclaration>& Pins, FName Name, float Value)
	{
		AddPin(Pins, Name, EComposableCameraPinType::Float, FString::SanitizeFloat(Value));
	}

	void AddBool(TArray<FComposableCameraNodePinDeclaration>& Pins, FName Name, bool Value)
	{
		AddPin(Pins, Name, EComposableCameraPinType::Bool, Value ? TEXT("true") : TEXT("false"));
	}
}

void UComposableCameraScalarNodeBase::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	ComposableCameraMathNodesPrivate::AddPin(OutPins, ComposableCameraMathNodesPrivate::ResultPinName,
		EComposableCameraPinType::Float, FString(), EComposableCameraPinDirection::Output);
}

void UComposableCameraScalarNodeBase::PublishResult(float Value)
{
	Result = FMath::IsFinite(Value) ? Value : 0.f;
	SetOutputPinValue<float>(ComposableCameraMathNodesPrivate::ResultPinName, Result);
}

void UComposableCameraScalarMathNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	float Value = 0.f;
	switch (Operation)
	{
	case EComposableCameraScalarOperation::Add: Value = A + B; break;
	case EComposableCameraScalarOperation::Subtract: Value = A - B; break;
	case EComposableCameraScalarOperation::Multiply: Value = A * B; break;
	case EComposableCameraScalarOperation::Divide: Value = FMath::Abs(B) <= UE_SMALL_NUMBER ? DivideByZeroValue : A / B; break;
	case EComposableCameraScalarOperation::Minimum: Value = FMath::IsFinite(A) && FMath::IsFinite(B) ? FMath::Min(A, B) : 0.f; break;
	case EComposableCameraScalarOperation::Maximum: Value = FMath::IsFinite(A) && FMath::IsFinite(B) ? FMath::Max(A, B) : 0.f; break;
	case EComposableCameraScalarOperation::Absolute: Value = FMath::Abs(A); break;
	default: break;
	}
	PublishResult(Value);
}

void UComposableCameraScalarMathNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	using namespace ComposableCameraMathNodesPrivate;
	Super::GetPinDeclarations_Implementation(OutPins);
	AddFloat(OutPins, TEXT("A"), A);
	AddFloat(OutPins, TEXT("B"), B);
	AddFloat(OutPins, TEXT("DivideByZeroValue"), DivideByZeroValue);
	AddPin(OutPins, TEXT("Operation"), EComposableCameraPinType::Enum,
		StaticEnum<EComposableCameraScalarOperation>()->GetNameStringByValue(static_cast<int64>(Operation)));
	OutPins.Last().EnumType = StaticEnum<EComposableCameraScalarOperation>();
	OutPins.Last().bDefaultAsPin = false;
}

void UComposableCameraClampNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	PublishResult(FMath::IsFinite(Value) && FMath::IsFinite(Minimum) && FMath::IsFinite(Maximum)
		? FMath::Clamp(Value, FMath::Min(Minimum, Maximum), FMath::Max(Minimum, Maximum)) : 0.f);
}

void UComposableCameraClampNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	using namespace ComposableCameraMathNodesPrivate;
	Super::GetPinDeclarations_Implementation(OutPins);
	AddFloat(OutPins, TEXT("Value"), Value);
	AddFloat(OutPins, TEXT("Minimum"), Minimum);
	AddFloat(OutPins, TEXT("Maximum"), Maximum);
}

void UComposableCameraRemapNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	if (!FMath::IsFinite(Value) || !FMath::IsFinite(InputMin) || !FMath::IsFinite(InputMax)
		|| !FMath::IsFinite(OutputMin) || !FMath::IsFinite(OutputMax))
	{
		PublishResult(0.f);
		return;
	}
	// Double intermediates prevent overflow when finite float endpoints have opposite signs.
	const double Width = static_cast<double>(InputMax) - InputMin;
	if (FMath::Abs(Width) <= UE_SMALL_NUMBER)
	{
		PublishResult(OutputMin);
		return;
	}
	double AlphaValue = (static_cast<double>(Value) - InputMin) / Width;
	if (bClampInput) AlphaValue = FMath::Clamp(AlphaValue, 0.0, 1.0);
	const double Mapped = FMath::Lerp(static_cast<double>(OutputMin), static_cast<double>(OutputMax), AlphaValue);
	PublishResult(FMath::IsFinite(Mapped) && FMath::Abs(Mapped) <= MAX_flt ? static_cast<float>(Mapped) : 0.f);
}

void UComposableCameraRemapNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	using namespace ComposableCameraMathNodesPrivate;
	Super::GetPinDeclarations_Implementation(OutPins);
	AddFloat(OutPins, TEXT("Value"), Value);
	AddFloat(OutPins, TEXT("InputMin"), InputMin);
	AddFloat(OutPins, TEXT("InputMax"), InputMax);
	AddFloat(OutPins, TEXT("OutputMin"), OutputMin);
	AddFloat(OutPins, TEXT("OutputMax"), OutputMax);
	AddBool(OutPins, TEXT("bClampInput"), bClampInput);
}

void UComposableCameraLerpNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	if (!FMath::IsFinite(A) || !FMath::IsFinite(B) || !FMath::IsFinite(Alpha))
	{
		PublishResult(0.f);
		return;
	}
	const double Weight = bClampAlpha ? FMath::Clamp(static_cast<double>(Alpha), 0.0, 1.0) : Alpha;
	const double Interpolated = FMath::Lerp(static_cast<double>(A), static_cast<double>(B), Weight);
	PublishResult(FMath::IsFinite(Interpolated) && FMath::Abs(Interpolated) <= MAX_flt ? static_cast<float>(Interpolated) : 0.f);
}

void UComposableCameraLerpNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	using namespace ComposableCameraMathNodesPrivate;
	Super::GetPinDeclarations_Implementation(OutPins);
	AddFloat(OutPins, TEXT("A"), A);
	AddFloat(OutPins, TEXT("B"), B);
	AddFloat(OutPins, TEXT("Alpha"), Alpha);
	AddBool(OutPins, TEXT("bClampAlpha"), bClampAlpha);
}

void UComposableCameraCurveSampleNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	PublishResult(IsValid(Curve) && FMath::IsFinite(Input) ? Curve->GetFloatValue(Input) : FallbackValue);
}

void UComposableCameraCurveSampleNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	using namespace ComposableCameraMathNodesPrivate;
	Super::GetPinDeclarations_Implementation(OutPins);
	AddFloat(OutPins, TEXT("Input"), Input);
	AddFloat(OutPins, TEXT("FallbackValue"), FallbackValue);
	AddPin(OutPins, TEXT("Curve"), EComposableCameraPinType::Object);
	OutPins.Last().bDefaultAsPin = false;
}

void UComposableCameraSelectNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	PublishResult(bCondition ? TrueValue : FalseValue);
}

void UComposableCameraSelectNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	using namespace ComposableCameraMathNodesPrivate;
	Super::GetPinDeclarations_Implementation(OutPins);
	AddBool(OutPins, TEXT("bCondition"), bCondition);
	AddFloat(OutPins, TEXT("TrueValue"), TrueValue);
	AddFloat(OutPins, TEXT("FalseValue"), FalseValue);
}

void UComposableCameraFloatToDoubleNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	Result = FMath::IsFinite(Value) ? static_cast<double>(Value) : 0.0;
	SetOutputPinValue<double>(ComposableCameraMathNodesPrivate::ResultPinName, Result);
}

void UComposableCameraFloatToDoubleNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	using namespace ComposableCameraMathNodesPrivate;
	AddFloat(OutPins, TEXT("Value"), Value);
	AddPin(OutPins, ResultPinName, EComposableCameraPinType::Double, FString(), EComposableCameraPinDirection::Output);
}

void UComposableCameraDoubleToFloatNode::OnTickNode_Implementation(float, const FComposableCameraPose&, FComposableCameraPose&)
{
	PublishResult(FMath::IsFinite(Value)
		? static_cast<float>(FMath::Clamp(Value, -static_cast<double>(MAX_flt), static_cast<double>(MAX_flt))) : 0.f);
}

void UComposableCameraDoubleToFloatNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	Super::GetPinDeclarations_Implementation(OutPins);
	ComposableCameraMathNodesPrivate::AddPin(OutPins, TEXT("Value"), EComposableCameraPinType::Double, FString::SanitizeFloat(Value));
}
