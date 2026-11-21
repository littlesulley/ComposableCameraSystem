// Copyright 2026 Sulley. All Rights Reserved.
#include "Nodes/ComposableCameraMathNodes.h"

#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Curves/CurveFloat.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSMathBoundaryTest,
	"ComposableCameraSystem.Math.Boundaries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSMathBoundaryTest::RunTest(const FString&)
{
	FComposableCameraPose Pose;
	Pose.Position = FVector(10, 20, 30);
	Pose.Rotation = FRotator(5, 10, 15);
	Pose.SetFieldOfViewDegrees(73.f);
	FComposableCameraPose Output = Pose;
	auto* Math = NewObject<UComposableCameraScalarMathNode>();
	Math->A = -8.f; Math->B = 2.f;
	const EComposableCameraScalarOperation Operations[] = {
		EComposableCameraScalarOperation::Add, EComposableCameraScalarOperation::Subtract,
		EComposableCameraScalarOperation::Multiply, EComposableCameraScalarOperation::Divide,
		EComposableCameraScalarOperation::Minimum, EComposableCameraScalarOperation::Maximum,
		EComposableCameraScalarOperation::Absolute };
	const float Expected[] = { -6.f, -10.f, -16.f, -4.f, -8.f, 2.f, 8.f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Operations); ++Index)
	{
		Math->Operation = Operations[Index];
		Math->OnTickNode_Implementation(1.f / 60.f, Pose, Output);
		TestEqual(TEXT("Arithmetic operation"), Math->Result, Expected[Index]);
	}
	Math->Operation = EComposableCameraScalarOperation::Divide;
	Math->B = 0.f; Math->DivideByZeroValue = 7.f;
	Math->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Division by zero has an explicit fallback"), Math->Result, 7.f);
	Math->A = std::numeric_limits<float>::quiet_NaN(); Math->B = 1.f;
	Math->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Non-finite results cannot poison downstream pose"), Math->Result, 0.f);

	auto* Clamp = NewObject<UComposableCameraClampNode>();
	Clamp->Value = 20.f; Clamp->Minimum = 10.f; Clamp->Maximum = -10.f;
	Clamp->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Reversed clamp bounds are supported"), Clamp->Result, 10.f);

	auto* Remap = NewObject<UComposableCameraRemapNode>();
	Remap->Value = 25.f; Remap->InputMin = 100.f; Remap->InputMax = 0.f;
	Remap->OutputMin = 60.f; Remap->OutputMax = 100.f;
	Remap->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Reversed input range preserves its direction"), Remap->Result, 90.f);
	Remap->Value = -100.f;
	Remap->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Remap clamps by default"), Remap->Result, 100.f);
	Remap->bClampInput = false;
	Remap->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Unclamped remap extrapolates"), Remap->Result, 140.f);
	Remap->InputMax = Remap->InputMin;
	Remap->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Zero-width range uses OutputMin"), Remap->Result, 60.f);

	auto* Lerp = NewObject<UComposableCameraLerpNode>();
	Lerp->A = 10.f; Lerp->B = 20.f; Lerp->Alpha = 1.5f;
	Lerp->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Lerp clamps by default"), Lerp->Result, 20.f);
	Lerp->bClampAlpha = false;
	Lerp->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Unclamped lerp extrapolates"), Lerp->Result, 25.f);

	auto* Select = NewObject<UComposableCameraSelectNode>();
	Select->TrueValue = 8.f; Select->FalseValue = 3.f;
	Select->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("False selects false input"), Select->Result, 3.f);
	Select->bCondition = true;
	Select->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Selection can change on the next sample"), Select->Result, 8.f);
	TestTrue(TEXT("Math nodes preserve position and rotation"), Output.Position == Pose.Position && Output.Rotation == Pose.Rotation);
	TestEqual(TEXT("Math nodes preserve lens"), Output.GetEffectiveFieldOfView(), Pose.GetEffectiveFieldOfView());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSMathCurveAndConversionTest,
	"ComposableCameraSystem.Math.CurveAndConversion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSMathCurveAndConversionTest::RunTest(const FString&)
{
	FComposableCameraPose Pose, Output;
	auto* Sample = NewObject<UComposableCameraCurveSampleNode>();
	Sample->FallbackValue = 12.f;
	Sample->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Missing curve publishes fallback"), Sample->Result, 12.f);
	Sample->Curve = NewObject<UCurveFloat>(Sample);
	Sample->Curve->FloatCurve.AddKey(0.f, 0.f);
	Sample->Curve->FloatCurve.AddKey(1.f, 10.f);
	Sample->Input = .5f;
	Sample->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Curve is sampled at the supplied input"), Sample->Result, 5.f);
	Sample->Input = std::numeric_limits<float>::infinity();
	Sample->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Invalid sample input uses fallback"), Sample->Result, 12.f);
	Sample->Curve = nullptr;
	Sample->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Clearing curve does not retain stale output"), Sample->Result, 12.f);

	auto* Widen = NewObject<UComposableCameraFloatToDoubleNode>();
	Widen->Value = 12.5f; Widen->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Float conversion"), Widen->Result, 12.5);
	auto* Narrow = NewObject<UComposableCameraDoubleToFloatNode>();
	Narrow->Value = std::numeric_limits<double>::max();
	Narrow->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Finite double saturates to float range"), Narrow->Result, MAX_flt);
	Narrow->Value = std::numeric_limits<double>::quiet_NaN();
	Narrow->OnTickNode_Implementation(0.f, Pose, Output);
	TestEqual(TEXT("Non-finite conversion is deterministic"), Narrow->Result, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSMathWiredEvaluationTest,
	"ComposableCameraSystem.Math.WiredEvaluationAndIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSMathWiredEvaluationTest::RunTest(const FString&)
{
	auto* Asset = NewObject<UComposableCameraTypeAsset>();
	auto* RemapTemplate = NewObject<UComposableCameraRemapNode>(Asset);
	RemapTemplate->InputMax = 600.f; RemapTemplate->OutputMin = 60.f; RemapTemplate->OutputMax = 120.f;
	auto* FOVTemplate = NewObject<UComposableCameraFieldOfViewNode>(Asset);
	Asset->NodeTemplates = { RemapTemplate, FOVTemplate };
	auto& Wire = Asset->PinConnections.AddDefaulted_GetRef();
	Wire.SourceNodeIndex = 0; Wire.SourcePinName = TEXT("Result");
	Wire.TargetNodeIndex = 1; Wire.TargetPinName = TEXT("FieldOfView");
	auto& Speed = Asset->ExposedParameters.AddDefaulted_GetRef();
	Speed.ParameterName = TEXT("Speed"); Speed.PinType = EComposableCameraPinType::Float;
	Speed.TargetNodeIndex = 0; Speed.TargetPinName = TEXT("Value");

	FComposableCameraRuntimeDataBlock First = Asset->BuildRuntimeDataLayout();
	FComposableCameraRuntimeDataBlock Second = Asset->BuildRuntimeDataLayout();
	auto* FirstRemap = DuplicateObject<UComposableCameraRemapNode>(RemapTemplate, GetTransientPackage());
	auto* SecondRemap = DuplicateObject<UComposableCameraRemapNode>(RemapTemplate, GetTransientPackage());
	auto* FirstFOV = DuplicateObject<UComposableCameraFieldOfViewNode>(FOVTemplate, GetTransientPackage());
	FirstRemap->SetRuntimeDataBlock(&First, 0); FirstRemap->Initialize(nullptr, nullptr);
	SecondRemap->SetRuntimeDataBlock(&Second, 0); SecondRemap->Initialize(nullptr, nullptr);
	FirstFOV->SetRuntimeDataBlock(&First, 1); FirstFOV->Initialize(nullptr, nullptr);
	const int32* FirstOffset = First.ExposedParameterOffsets.Find(TEXT("Speed"));
	const int32* SecondOffset = Second.ExposedParameterOffsets.Find(TEXT("Speed"));
	if (!TestNotNull(TEXT("First exposed input"), FirstOffset) || !TestNotNull(TEXT("Second exposed input"), SecondOffset)) return false;
	First.WriteValue<float>(*FirstOffset, 100.f); Second.WriteValue<float>(*SecondOffset, 300.f);
	FComposableCameraPose Pose;
	FirstRemap->TickNode(1.f / 60.f, Pose, Pose);
	FirstFOV->TickNode(1.f / 60.f, Pose, Pose);
	TestEqual(TEXT("Downstream FOV receives current sample's remap"), Pose.GetEffectiveFieldOfView(), 70.0);
	SecondRemap->TickNode(1.f / 60.f, Pose, Pose);
	TestEqual(TEXT("Second instance has independent input and output"), Second.ReadOutputPin<float>(0, TEXT("Result")), 90.f);
	First.WriteValue<float>(*FirstOffset, 500.f);
	FirstRemap->TickNode(1.f / 30.f, Pose, Pose);
	FirstFOV->TickNode(1.f / 30.f, Pose, Pose);
	TestEqual(TEXT("Next sample refreshes inputs without reinitialization"), Pose.GetEffectiveFieldOfView(), 110.0);
	TestEqual(TEXT("First sample update does not affect second instance"), Second.ReadOutputPin<float>(0, TEXT("Result")), 90.f);
	TestEqual(TEXT("Authored template is not modified by evaluation"), RemapTemplate->Value, 0.f);
	return true;
}
#endif
