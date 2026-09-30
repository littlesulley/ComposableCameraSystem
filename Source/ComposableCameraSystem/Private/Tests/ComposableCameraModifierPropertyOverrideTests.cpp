// Copyright 2026 Sulley. All Rights Reserved.

#include "Modifiers/ComposableCameraModifierBase.h"
#include "Modifiers/ComposableCameraModifierTransition.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraModifierManager.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"
#include "Curves/CurveFloat.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Interpolator/ComposableCameraSimpleSpringInterpolator.h"
#include "Nodes/ComposableCameraCameraOffsetNode.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include "Nodes/ComposableCameraPivotRotateNode.h"
#include "Tests/ComposableCameraTestObjects.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ComposableCameraModifierPropertyOverrideTests"

namespace ComposableCameraModifierTransitionTest
{
	struct FScopedWorld
	{
		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World)
			{
				FWorldContext& WorldContext =
					GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		~FScopedWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}

		UWorld* World = nullptr;
	};

	struct FCameraAndNode
	{
		AComposableCameraCameraBase* Camera = nullptr;
		UComposableCameraModifierTestNode* Node = nullptr;
	};

	struct FModifierSpec
	{
		UComposableCameraNodeModifierDataAsset* Asset = nullptr;
		UComposableCameraModifierBase* Modifier = nullptr;
		UComposableCameraModifierTestNode* Template = nullptr;
	};

	static FCameraAndNode SpawnCameraAndNode(UWorld* World)
	{
		FCameraAndNode Result;
		if (!World)
		{
			return Result;
		}

		Result.Camera = World->SpawnActor<AComposableCameraCameraBase>(
			AComposableCameraCameraBase::StaticClass(),
			FTransform::Identity);
		if (!Result.Camera)
		{
			return Result;
		}

		Result.Node =
			NewObject<UComposableCameraModifierTestNode>(Result.Camera);
		Result.Camera->CameraNodes.Add(Result.Node);
		Result.Camera->Initialize(nullptr);
		return Result;
	}

	static FModifierSpec MakeModifier(AComposableCameraCameraBase* Camera)
	{
		FModifierSpec Result;
		if (!Camera)
		{
			return Result;
		}

		Result.Asset =
			NewObject<UComposableCameraNodeModifierDataAsset>(Camera);
		Result.Asset->ApplyMode =
			EComposableCameraModifierApplyMode::ModifyExistingInstance;
		Result.Modifier =
			NewObject<UComposableCameraModifierBase>(Result.Asset);
		Result.Template =
			NewObject<UComposableCameraModifierTestNode>(Result.Modifier);
		Result.Modifier->NodeTemplate = Result.Template;
		Result.Asset->Modifiers.Add(Result.Modifier);
		return Result;
	}

	static T_NodeModifier MakeEffective(const FModifierSpec& Spec)
	{
		T_NodeModifier Effective;
		if (Spec.Modifier && Spec.Asset)
		{
			Effective.Add(
				UComposableCameraModifierTestNode::StaticClass(),
				FModifierEntry { Spec.Modifier, Spec.Asset });
		}
		return Effective;
	}

	static UComposableCameraModifierTransitionBase* MakeLinearTransition(
		UObject* Outer,
		float Duration = 1.f,
		float DiscreteSwitchWeight = 1.f)
	{
		UComposableCameraModifierTransitionBase* Transition =
			NewObject<UComposableCameraModifierTransitionBase>(Outer);
		Transition->Duration = Duration;
		Transition->BlendFunction =
			EComposableCameraModifierBlendFunction::Linear;
		Transition->DiscreteSwitchWeight = DiscreteSwitchWeight;
		return Transition;
	}

	static void TickOnce(
		AComposableCameraCameraBase* Camera,
		float DeltaTime)
	{
		if (Camera)
		{
			Camera->InvalidateTickCache();
			(void)Camera->TickCamera(DeltaTime);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierCopiesOnlyCheckedPropertiesTest,
	"System.Engine.ComposableCameraSystem.Modifiers.PropertyOverride.CopiesOnlyCheckedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierCopiesOnlyCheckedPropertiesTest::RunTest(const FString& Parameters)
{
	UComposableCameraModifierBase* Modifier = NewObject<UComposableCameraModifierBase>();
	UComposableCameraCameraOffsetNode* Template = NewObject<UComposableCameraCameraOffsetNode>(Modifier);
	UComposableCameraCameraOffsetNode* Target = NewObject<UComposableCameraCameraOffsetNode>();

	Modifier->NodeTemplate = Template;
	Template->PivotPosition = FVector(10.f, 20.f, 30.f);
	Template->CameraOffset = FVector(100.f, 200.f, 300.f);
	Template->PaletteCategory = TEXT("TemplateMetadata");
	Modifier->OverrideProperties.Add(GET_MEMBER_NAME_CHECKED(UComposableCameraCameraOffsetNode, CameraOffset));
	Modifier->OverrideProperties.Add(GET_MEMBER_NAME_CHECKED(UComposableCameraCameraNodeBase, PaletteCategory));

	Target->PivotPosition = FVector(-10.f, -20.f, -30.f);
	Target->CameraOffset = FVector::ZeroVector;
	Target->PaletteCategory = TEXT("RuntimeMetadata");

	Modifier->ApplyModifierToNode(Target);

	UTEST_TRUE("Checked CameraOffset copies from the template",
		Target->CameraOffset.Equals(Template->CameraOffset, KINDA_SMALL_NUMBER));
	UTEST_TRUE("Unchecked PivotPosition remains authored on the runtime node",
		Target->PivotPosition.Equals(FVector(-10.f, -20.f, -30.f), KINDA_SMALL_NUMBER));
	UTEST_EQUAL("PaletteCategory remains metadata, never a runtime override",
		Target->PaletteCategory, FName(TEXT("RuntimeMetadata")));

	const FProperty* PaletteCategory = FindFProperty<FProperty>(
		UComposableCameraCameraNodeBase::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UComposableCameraCameraNodeBase, PaletteCategory));
	UTEST_FALSE("PaletteCategory is not eligible for a modifier checkbox",
		UComposableCameraModifierBase::IsNodePropertyOverridable(PaletteCategory));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierBeatsInputPinResolutionTest,
	"System.Engine.ComposableCameraSystem.Modifiers.PropertyOverride.BeatsInputPinResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierBeatsInputPinResolutionTest::RunTest(const FString& Parameters)
{
	FComposableCameraRuntimeDataBlock RuntimeData;
	RuntimeData.Storage.SetNumZeroed(sizeof(FVector));
	RuntimeData.TotalSize = sizeof(FVector);
	FComposableCameraRuntimeDataBlock::FSlotShape Shape;
	Shape.PinType = EComposableCameraPinType::Vector3D;
	Shape.Size = sizeof(FVector);
	RuntimeData.SlotShapes.Add(0, Shape);
	RuntimeData.ExposedInputPinOffsets.Add(
		FComposableCameraPinKey{ 0, GET_MEMBER_NAME_CHECKED(UComposableCameraCameraOffsetNode, CameraOffset) }, 0);
	RuntimeData.WriteValue<FVector>(0, FVector(1.f, 2.f, 3.f));

	UComposableCameraModifierBase* Modifier = NewObject<UComposableCameraModifierBase>();
	UComposableCameraCameraOffsetNode* Template = NewObject<UComposableCameraCameraOffsetNode>(Modifier);
	UComposableCameraCameraOffsetNode* Target = NewObject<UComposableCameraCameraOffsetNode>();
	Template->CameraOffset = FVector(100.f, 200.f, 300.f);
	Modifier->NodeTemplate = Template;
	Modifier->OverrideProperties.Add(GET_MEMBER_NAME_CHECKED(UComposableCameraCameraOffsetNode, CameraOffset));

	Target->SetRuntimeDataBlock(&RuntimeData, 0);
	Target->Initialize(nullptr, nullptr);
	UTEST_TRUE("Input pin resolves before modifier application",
		Target->CameraOffset.Equals(FVector(1.f, 2.f, 3.f), KINDA_SMALL_NUMBER));

	Modifier->ApplyModifierToNode(Target);
	Target->ResolveAllInputPins();
	UTEST_TRUE("Modifier value survives later input-pin resolution",
		Target->CameraOffset.Equals(Template->CameraOffset, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraCustomModifierPreservesChangedFovTest,
	"System.Engine.ComposableCameraSystem.Modifiers.Custom.PreservesChangedFov",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraCustomModifierPreservesChangedFovTest::RunTest(const FString& Parameters)
{
	constexpr int32 FloatSize = static_cast<int32>(sizeof(float));
	FComposableCameraRuntimeDataBlock RuntimeData;
	RuntimeData.Storage.SetNumZeroed(2 * FloatSize);
	RuntimeData.TotalSize = 2 * FloatSize;
	FComposableCameraRuntimeDataBlock::FSlotShape Shape;
	Shape.PinType = EComposableCameraPinType::Float;
	Shape.Size = FloatSize;
	RuntimeData.SlotShapes.Add(0, Shape);
	RuntimeData.SlotShapes.Add(FloatSize, Shape);
	RuntimeData.ExposedInputPinOffsets.Add(
		FComposableCameraPinKey{ 0, GET_MEMBER_NAME_CHECKED(UComposableCameraFieldOfViewNode, FieldOfView) }, 0);
	RuntimeData.ExposedInputPinOffsets.Add(
		FComposableCameraPinKey{ 0, GET_MEMBER_NAME_CHECKED(UComposableCameraFieldOfViewNode, MinFoV) }, FloatSize);
	RuntimeData.WriteValue<float>(0, 120.f);
	RuntimeData.WriteValue<float>(FloatSize, 50.f);

	UComposableCameraFieldOfViewNode* ExposedNode =
		NewObject<UComposableCameraFieldOfViewNode>();
	ExposedNode->SetRuntimeDataBlock(&RuntimeData, 0);
	ExposedNode->Initialize(nullptr, nullptr);
	UTEST_TRUE("Activation input resolves before the custom callback",
		FMath::IsNearlyEqual(ExposedNode->FieldOfView, 120.f));
	int32 CallbackCount = 0;
	ExposedNode->ApplyCustomModifierWithPinOwnership(
		[ExposedNode, &CallbackCount]()
		{
			++CallbackCount;
			ExposedNode->FieldOfView += 20.f;
		});

	RuntimeData.WriteValue<float>(0, 130.f);
	RuntimeData.WriteValue<float>(FloatSize, 60.f);
	FComposableCameraPose ExposedPose;
	ExposedNode->TickNode(0.016f, ExposedPose, ExposedPose);
	UTEST_TRUE("One-shot FOV addition survives the first pin refresh",
		FMath::IsNearlyEqual(ExposedPose.GetEffectiveFieldOfView(), 140.0));
	UTEST_TRUE("Explicit pin readers observe the owned FOV",
		FMath::IsNearlyEqual(
			ExposedNode->GetInputPinValue<float>(TEXT("FieldOfView")), 140.f));
	UTEST_TRUE("Untouched input pin remains live",
		FMath::IsNearlyEqual(ExposedNode->MinFoV, 60.f));
	RuntimeData.WriteValue<float>(0, 150.f);
	ExposedNode->TickNode(0.016f, ExposedPose, ExposedPose);
	UTEST_TRUE("Later input changes do not recompute the one-shot addition",
		FMath::IsNearlyEqual(ExposedPose.GetEffectiveFieldOfView(), 140.0));
	UTEST_EQUAL("Custom callback runs once", CallbackCount, 1);

	UComposableCameraFieldOfViewNode* UnchangedNode =
		NewObject<UComposableCameraFieldOfViewNode>();
	UnchangedNode->SetRuntimeDataBlock(&RuntimeData, 0);
	UnchangedNode->Initialize(nullptr, nullptr);
	UnchangedNode->ApplyCustomModifierWithPinOwnership([]() {});
	RuntimeData.WriteValue<float>(0, 135.f);
	FComposableCameraPose UnchangedPose;
	UnchangedNode->TickNode(0.016f, UnchangedPose, UnchangedPose);
	UTEST_TRUE("Callback that changes no field does not freeze FOV",
		FMath::IsNearlyEqual(UnchangedPose.GetEffectiveFieldOfView(), 135.0));

	RuntimeData.InputPinSourceOffsets.Add(
		FComposableCameraPinKey{ 1, GET_MEMBER_NAME_CHECKED(UComposableCameraFieldOfViewNode, FieldOfView) }, 0);
	UComposableCameraFieldOfViewNode* WiredNode =
		NewObject<UComposableCameraFieldOfViewNode>();
	WiredNode->SetRuntimeDataBlock(&RuntimeData, 1);
	WiredNode->Initialize(nullptr, nullptr);
	WiredNode->ApplyCustomModifierWithPinOwnership(
		[WiredNode]() { WiredNode->FieldOfView += 20.f; });
	RuntimeData.WriteValue<float>(0, 145.f);
	FComposableCameraPose WiredPose;
	WiredNode->TickNode(0.016f, WiredPose, WiredPose);
	UTEST_TRUE("Wired inputs are not frozen before their source runs",
		FMath::IsNearlyEqual(WiredPose.GetEffectiveFieldOfView(), 145.0));

	UComposableCameraFieldOfViewNode* DefaultNode =
		NewObject<UComposableCameraFieldOfViewNode>();
	DefaultNode->FieldOfView = 120.f;
	DefaultNode->ApplyCustomModifierWithPinOwnership(
		[DefaultNode]() { DefaultNode->FieldOfView += 20.f; });
	FComposableCameraPose DefaultPose;
	DefaultNode->TickNode(0.016f, DefaultPose, DefaultPose);
	UTEST_TRUE("Node default and K2 input both receive the addition",
		FMath::IsNearlyEqual(DefaultPose.GetEffectiveFieldOfView(), 140.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierDuplicatesInstancedSubobjectsTest,
	"System.Engine.ComposableCameraSystem.Modifiers.PropertyOverride.DuplicatesInstancedSubobjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierDuplicatesInstancedSubobjectsTest::RunTest(const FString& Parameters)
{
	UComposableCameraModifierBase* Modifier = NewObject<UComposableCameraModifierBase>();
	UComposableCameraPivotRotateNode* Template = NewObject<UComposableCameraPivotRotateNode>(Modifier);
	UComposableCameraPivotRotateNode* Target = NewObject<UComposableCameraPivotRotateNode>();
	UComposableCameraSimpleSpringInterpolator* TemplateInterpolator =
		NewObject<UComposableCameraSimpleSpringInterpolator>(Template);
	TemplateInterpolator->DampTime = 0.25f;
	Template->Interpolator = TemplateInterpolator;
	Modifier->NodeTemplate = Template;
	Modifier->OverrideProperties.Add(GET_MEMBER_NAME_CHECKED(UComposableCameraPivotRotateNode, Interpolator));

	Target->Interpolator = NewObject<UComposableCameraSimpleSpringInterpolator>(Target);
	Modifier->ApplyModifierToNode(Target);

	UComposableCameraSimpleSpringInterpolator* AppliedInterpolator =
		Cast<UComposableCameraSimpleSpringInterpolator>(Target->Interpolator.Get());
	UTEST_TRUE("Instanced interpolator is copied", AppliedInterpolator != nullptr);
	UTEST_TRUE("Runtime node owns a duplicate instead of the asset subobject",
		AppliedInterpolator != TemplateInterpolator);
	if (AppliedInterpolator)
	{
		UTEST_EQUAL("Duplicated interpolator outer is the runtime node", AppliedInterpolator->GetOuter(),
			static_cast<UObject*>(Target));
		UTEST_TRUE("Duplicated interpolator retains its authored value",
			FMath::IsNearlyEqual(AppliedInterpolator->DampTime, 0.25f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierWrapperSelectsActiveBranchTest,
	"System.Engine.ComposableCameraSystem.Modifiers.Wrapper.SelectsActiveBranch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierWrapperSelectsActiveBranchTest::RunTest(const FString& Parameters)
{
	UComposableCameraModifierBase* Wrapper = NewObject<UComposableCameraModifierBase>();
	UComposableCameraCameraOffsetNode* GenericTemplate =
		NewObject<UComposableCameraCameraOffsetNode>(Wrapper);
	GenericTemplate->CameraOffset = FVector(10.f, 20.f, 30.f);
	Wrapper->NodeTemplate = GenericTemplate;
	Wrapper->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(UComposableCameraCameraOffsetNode, CameraOffset));

	UComposableCameraModifierBase* CustomModifier =
		NewObject<UComposableCameraModifierBase>(Wrapper);
	CustomModifier->NodeClass = UComposableCameraCameraOffsetNode::StaticClass();
	Wrapper->CustomModifier = CustomModifier;

	UComposableCameraCameraOffsetNode* Target = NewObject<UComposableCameraCameraOffsetNode>();
	Wrapper->bUseCustomModifierClass = false;
	Wrapper->ApplyModifierToNode(Target);
	TestTrue("Node Type mode applies the wrapper template",
		Target->CameraOffset.Equals(GenericTemplate->CameraOffset, KINDA_SMALL_NUMBER));

	Target->CameraOffset = FVector::ZeroVector;
	Wrapper->bUseCustomModifierClass = true;
	TestEqual("Custom mode delegates its target node class",
		Wrapper->GetTargetNodeClass().Get(), UComposableCameraCameraOffsetNode::StaticClass());
	TestFalse("Custom modifier callbacks retain post-initialize timing",
		Wrapper->UsesNodeTemplateOverride());
	Wrapper->ApplyModifierToNode(Target);
	TestTrue("Custom mode ignores the wrapper's generic template",
		Target->CameraOffset.IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierCameraTagQueryTest,
	"System.Engine.ComposableCameraSystem.Modifiers.CameraTagQuery.FiltersCameraTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierCameraTagQueryTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AdsTag = FGameplayTag::RequestGameplayTag(TEXT("Camera.ADS"));
	const FGameplayTag ThirdPersonTag = FGameplayTag::RequestGameplayTag(TEXT("Camera.ThirdPerson"));
	const FGameplayTag UiTag = FGameplayTag::RequestGameplayTag(TEXT("Camera.ContextStack.UI"));
	UTEST_TRUE("ADS test tag exists", AdsTag.IsValid());
	UTEST_TRUE("Third-person test tag exists", ThirdPersonTag.IsValid());
	UTEST_TRUE("UI test tag exists", UiTag.IsValid());

	UComposableCameraNodeModifierDataAsset* QueryProbe =
		NewObject<UComposableCameraNodeModifierDataAsset>();
	FGameplayTagContainer AdsCameraTags(AdsTag);
	FGameplayTagContainer AdsUiCameraTags(AdsTag);
	AdsUiCameraTags.AddTag(UiTag);
	TestTrue("Empty query matches every camera", QueryProbe->MatchesCameraTags(AdsCameraTags));

	FGameplayTagQueryExpression AnyGameplayCamera;
	AnyGameplayCamera.AnyTagsMatch().AddTag(AdsTag).AddTag(ThirdPersonTag);
	FGameplayTagQueryExpression NoUi;
	NoUi.NoTagsMatch().AddTag(UiTag);
	FGameplayTagQueryExpression RootExpression;
	RootExpression.AllExprMatch().AddExpr(AnyGameplayCamera).AddExpr(NoUi);
	QueryProbe->CameraTagQuery = FGameplayTagQuery::BuildQuery(RootExpression);
	TestTrue("Nested ALL(ANY, NONE) query accepts ADS", QueryProbe->MatchesCameraTags(AdsCameraTags));
	TestFalse("Nested ALL(ANY, NONE) query rejects UI", QueryProbe->MatchesCameraTags(AdsUiCameraTags));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	auto SpawnCameraWithOffsetNode = [World](FGameplayTag CameraTag)
	{
		AComposableCameraCameraBase* Camera =
			World->SpawnActor<AComposableCameraCameraBase>(
				AComposableCameraCameraBase::StaticClass(), FTransform::Identity);
		if (Camera)
		{
			Camera->CameraTags.AddTag(CameraTag);
			Camera->CameraNodes.Add(NewObject<UComposableCameraCameraOffsetNode>(Camera));
		}
		return Camera;
	};

	AComposableCameraCameraBase* AdsCamera = SpawnCameraWithOffsetNode(AdsTag);
	AComposableCameraCameraBase* ThirdPersonCamera = SpawnCameraWithOffsetNode(ThirdPersonTag);
	TestNotNull("ADS camera spawned", AdsCamera);
	TestNotNull("Third-person camera spawned", ThirdPersonCamera);

	UComposableCameraModifierManager* Manager = NewObject<UComposableCameraModifierManager>();
	const TSubclassOf<UComposableCameraCameraNodeBase> OffsetNodeClass =
		UComposableCameraCameraOffsetNode::StaticClass();

	UComposableCameraNodeModifierDataAsset* AllCamerasAsset =
		NewObject<UComposableCameraNodeModifierDataAsset>(Manager);
	UComposableCameraModifierBase* AllCamerasModifier =
		NewObject<UComposableCameraModifierBase>(AllCamerasAsset);
	AllCamerasModifier->NodeClass = OffsetNodeClass;
	AllCamerasAsset->Modifiers.Add(AllCamerasModifier);
	AllCamerasAsset->Priority = 10;

	UComposableCameraNodeModifierDataAsset* AdsAsset =
		NewObject<UComposableCameraNodeModifierDataAsset>(Manager);
	UComposableCameraModifierBase* AdsModifier = NewObject<UComposableCameraModifierBase>(AdsAsset);
	AdsModifier->NodeClass = OffsetNodeClass;
	AdsAsset->Modifiers.Add(AdsModifier);
	AdsAsset->CameraTagQuery = FGameplayTagQuery::MakeQuery_MatchTag(AdsTag);
	AdsAsset->Priority = 20;

	Manager->AddModifier(AllCamerasAsset);
	Manager->AddModifier(AdsAsset);

	const auto& ModifierData = Manager->GetModifierData();
	const TArray<FModifierEntry>* RegisteredCandidates = ModifierData.ModifierData.Find(OffsetNodeClass);
	TestTrue("All query candidates share one node-class bucket",
		RegisteredCandidates && RegisteredCandidates->Num() == 2);

	if (AdsCamera)
	{
		Manager->GetModifierData().UpdateEffectiveModifiers(AdsCamera);
		const T_PropertyModifier* EffectiveProperties =
			Manager->GetModifierData().EffectiveModifiers.Find(OffsetNodeClass);
		const FModifierEntry* Effective =
			EffectiveProperties
				? EffectiveProperties->Find(NAME_None)
				: nullptr;
		TestTrue("Higher-priority matching query wins", Effective && Effective->Asset == AdsAsset);
	}

	if (ThirdPersonCamera)
	{
		Manager->GetModifierData().UpdateEffectiveModifiers(ThirdPersonCamera);
		const T_PropertyModifier* EffectiveProperties =
			Manager->GetModifierData().EffectiveModifiers.Find(OffsetNodeClass);
		const FModifierEntry* Effective =
			EffectiveProperties
				? EffectiveProperties->Find(NAME_None)
				: nullptr;
		TestTrue("Empty query applies to an unrelated camera tag",
			Effective && Effective->Asset == AllCamerasAsset);

		Manager->RemoveModifier(AllCamerasAsset);
		Manager->GetModifierData().UpdateEffectiveModifiers(ThirdPersonCamera);
		TestTrue("Non-matching ADS query does not affect the third-person camera",
			Manager->GetModifierData().EffectiveModifiers.IsEmpty());
	}

	AComposableCameraCameraBase* TypeAssetCamera =
		World->SpawnActor<AComposableCameraCameraBase>(
			AComposableCameraCameraBase::StaticClass(), FTransform::Identity);
	UComposableCameraTypeAsset* TypeAsset = NewObject<UComposableCameraTypeAsset>(Manager);
	TypeAsset->CameraTags.AddTag(AdsTag);
	if (TypeAssetCamera)
	{
		UE::ComposableCameras::ConstructCameraFromTypeAsset(
			TypeAssetCamera, TypeAsset, FComposableCameraParameterBlock());
		TestTrue("Type-asset construction copies the full camera tag container",
			TypeAssetCamera->CameraTags.HasTagExact(AdsTag));
		TestEqual("Type-asset construction refreshes the cached trace label",
			TypeAssetCamera->CameraTagsTraceName, TypeAssetCamera->CameraTags.ToStringSimple());
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierPerPropertySelectionTest,
	"System.Engine.ComposableCameraSystem.Modifiers.Selection.PerPropertyWinners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierPerPropertySelectionTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	const FCameraAndNode CameraAndNode =
		SpawnCameraAndNode(ScopedWorld.World);
	TestNotNull("Camera spawned", CameraAndNode.Camera);
	TestNotNull("Modifier test node created", CameraAndNode.Node);
	if (!CameraAndNode.Camera || !CameraAndNode.Node)
	{
		return false;
	}

	const FName FloatProperty = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierTestNode, FloatValue);
	const FName VectorProperty = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierTestNode, Vector2DValue);
	const FName NonPinProperty = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierTestNode, NonPinFloatValue);

	const FModifierSpec ModifierA = MakeModifier(CameraAndNode.Camera);
	ModifierA.Asset->Priority = 10;
	ModifierA.Template->FloatValue = 100.f;
	ModifierA.Template->NonPinFloatValue = 30.f;
	ModifierA.Modifier->OverrideProperties.Add(FloatProperty);
	ModifierA.Modifier->OverrideProperties.Add(NonPinProperty);

	const FModifierSpec ModifierB = MakeModifier(CameraAndNode.Camera);
	ModifierB.Asset->Priority = 20;
	ModifierB.Template->FloatValue = 200.f;
	ModifierB.Template->Vector2DValue = FVector2D(40.f, 50.f);
	ModifierB.Modifier->OverrideProperties.Add(FloatProperty);
	ModifierB.Modifier->OverrideProperties.Add(VectorProperty);

	UComposableCameraModifierManager* Manager =
		NewObject<UComposableCameraModifierManager>(CameraAndNode.Camera);
	Manager->AddModifier(ModifierA.Asset);
	Manager->AddModifier(ModifierB.Asset);
	const FComposableCameraModifierUpdateResult InitialResult =
		Manager->GetModifierData().UpdateEffectiveModifiers(
			CameraAndNode.Camera);
	TestTrue("Per-property in-place selection avoids camera reactivation",
		InitialResult.bChanged
			&& !InitialResult.bRequiresCameraReactivation);

	const T_PropertyModifier* EffectiveProperties =
		Manager->GetModifierData().EffectiveModifiers.Find(
			UComposableCameraModifierTestNode::StaticClass());
	TestNotNull("Node class has effective property winners",
		EffectiveProperties);
	if (!EffectiveProperties)
	{
		return false;
	}

	const FModifierEntry* FloatWinner =
		EffectiveProperties->Find(FloatProperty);
	const FModifierEntry* VectorWinner =
		EffectiveProperties->Find(VectorProperty);
	const FModifierEntry* NonPinWinner =
		EffectiveProperties->Find(NonPinProperty);
	TestTrue("Higher priority wins overlapping property",
		FloatWinner && FloatWinner->Asset == ModifierB.Asset);
	TestTrue("Higher-priority modifier keeps its disjoint property",
		VectorWinner && VectorWinner->Asset == ModifierB.Asset);
	TestTrue("Lower-priority modifier still owns its disjoint property",
		NonPinWinner && NonPinWinner->Asset == ModifierA.Asset);
	TestEqual("Exactly three property winners are selected",
		EffectiveProperties->Num(), 3);

	CameraAndNode.Camera->ApplyEffectiveModifiers(
		Manager->GetModifierData().EffectiveModifiers);
	TestTrue("Different same-class modifiers compose on one runtime node",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 200.f)
			&& CameraAndNode.Node->Vector2DValue.Equals(
				FVector2D(40.f, 50.f), KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				CameraAndNode.Node->NonPinFloatValue, 30.f));

	Manager->RemoveModifier(ModifierB.Asset);
	const FComposableCameraModifierUpdateResult RemoveResult =
		Manager->GetModifierData().UpdateEffectiveModifiers(
			CameraAndNode.Camera);
	TestTrue("Removing one contributor remains in-place",
		RemoveResult.bChanged
			&& !RemoveResult.bRequiresCameraReactivation);
	CameraAndNode.Camera->ReconcileInPlaceEffectiveModifiersFromAssets(
		Manager->GetModifierData().EffectiveModifiers);
	TickOnce(CameraAndNode.Camera, 0.f);
	TestTrue("Removed properties fall back while remaining winner survives",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 100.f)
			&& CameraAndNode.Node->Vector2DValue.Equals(
				FVector2D::ZeroVector, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				CameraAndNode.Node->NonPinFloatValue, 30.f));

	const FModifierSpec ModifierC = MakeModifier(CameraAndNode.Camera);
	ModifierC.Asset->Priority = ModifierA.Asset->Priority;
	ModifierC.Template->FloatValue = 300.f;
	ModifierC.Modifier->OverrideProperties.Add(FloatProperty);
	Manager->AddModifier(ModifierC.Asset);
	Manager->GetModifierData().UpdateEffectiveModifiers(
		CameraAndNode.Camera);
	EffectiveProperties =
		Manager->GetModifierData().EffectiveModifiers.Find(
			UComposableCameraModifierTestNode::StaticClass());
	FloatWinner = EffectiveProperties
		? EffectiveProperties->Find(FloatProperty)
		: nullptr;
	TestTrue("Later registration wins an equal-priority overlap",
		FloatWinner && FloatWinner->Asset == ModifierC.Asset);

	UComposableCameraNodeModifierDataAsset* CustomAsset =
		NewObject<UComposableCameraNodeModifierDataAsset>(Manager);
	CustomAsset->ApplyMode =
		EComposableCameraModifierApplyMode::ModifyExistingInstance;
	CustomAsset->Priority = ModifierC.Asset->Priority;
	CustomAsset->OverrideExitValueTransition =
		MakeLinearTransition(CustomAsset, 2.f);
	ModifierC.Asset->OverrideEnterValueTransition =
		MakeLinearTransition(ModifierC.Asset, 3.f);
	UComposableCameraModifierBase* CustomModifier =
		NewObject<UComposableCameraModifierBase>(CustomAsset);
	CustomModifier->NodeClass =
		UComposableCameraModifierTestNode::StaticClass();
	CustomAsset->Modifiers.Add(CustomModifier);
	Manager->AddModifier(CustomAsset);
	Manager->GetModifierData().UpdateEffectiveModifiers(
		CameraAndNode.Camera);
	EffectiveProperties =
		Manager->GetModifierData().EffectiveModifiers.Find(
			UComposableCameraModifierTestNode::StaticClass());
	const FModifierEntry* WholeNodeWinner =
		EffectiveProperties
			? EffectiveProperties->Find(NAME_None)
			: nullptr;
	TestTrue("Winning Custom Modifier preserves whole-node compatibility",
		EffectiveProperties
			&& EffectiveProperties->Num() == 1
			&& WholeNodeWinner
			&& WholeNodeWinner->Asset == CustomAsset);

	Manager->RemoveModifier(CustomAsset);
	const FComposableCameraModifierUpdateResult CustomRemovalResult =
		Manager->GetModifierData().UpdateEffectiveModifiers(
			CameraAndNode.Camera);
	TestTrue("Equal-priority Custom removal prefers desired property Enter",
		!CustomRemovalResult.bRequiresCameraReactivation
			&& CustomRemovalResult.ModifierTransition
				== ModifierC.Asset->OverrideEnterValueTransition.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierPerPropertyConstructionTest,
	"System.Engine.ComposableCameraSystem.Modifiers.Selection.PerPropertyConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierPerPropertyConstructionTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	const FCameraAndNode CameraAndNode =
		SpawnCameraAndNode(ScopedWorld.World);
	if (!CameraAndNode.Camera || !CameraAndNode.Node)
	{
		AddError(TEXT("Camera or test node was not created."));
		return false;
	}

	const FModifierSpec FloatModifier =
		MakeModifier(CameraAndNode.Camera);
	FloatModifier.Asset->ApplyMode =
		EComposableCameraModifierApplyMode::ReactivateCamera;
	FloatModifier.Asset->Priority = 10;
	FloatModifier.Template->FloatValue = 25.f;
	FloatModifier.Modifier->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, FloatValue));

	const FModifierSpec VectorModifier =
		MakeModifier(CameraAndNode.Camera);
	VectorModifier.Asset->ApplyMode =
		EComposableCameraModifierApplyMode::ReactivateCamera;
	VectorModifier.Asset->Priority = 20;
	VectorModifier.Template->Vector2DValue = FVector2D(60.f, 70.f);
	VectorModifier.Modifier->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, Vector2DValue));

	UComposableCameraModifierManager* Manager =
		NewObject<UComposableCameraModifierManager>(CameraAndNode.Camera);
	Manager->AddModifier(FloatModifier.Asset);
	Manager->AddModifier(VectorModifier.Asset);
	const FComposableCameraModifierUpdateResult Result =
		Manager->GetModifierData().UpdateEffectiveModifiers(
			CameraAndNode.Camera);
	TestTrue("Reactivate property winners request normal camera lifecycle",
		Result.bChanged && Result.bRequiresCameraReactivation);

	CameraAndNode.Camera->ApplyEffectiveModifiers(
		Manager->GetModifierData().EffectiveModifiers,
		/* bApplyNodeTemplateModifiers = */ true,
		/* bApplyLegacyBlueprintModifiers = */ false);
	TestTrue("Construction applies disjoint winners from both assets",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 25.f)
			&& CameraAndNode.Node->Vector2DValue.Equals(
				FVector2D(60.f, 70.f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraInPlaceModifierTransitionsLivePinValuesTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionsLivePinValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraInPlaceModifierTransitionsLivePinValuesTest::RunTest(
	const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AComposableCameraCameraBase* Camera =
		World->SpawnActor<AComposableCameraCameraBase>(
			AComposableCameraCameraBase::StaticClass(), FTransform::Identity);
	UComposableCameraCameraOffsetNode* Node =
		NewObject<UComposableCameraCameraOffsetNode>(Camera);
	Camera->CameraNodes.Add(Node);

	FComposableCameraRuntimeDataBlock RuntimeData;
	RuntimeData.Storage.SetNumZeroed(sizeof(FVector));
	RuntimeData.TotalSize = sizeof(FVector);
	FComposableCameraRuntimeDataBlock::FSlotShape Shape;
	Shape.PinType = EComposableCameraPinType::Vector3D;
	Shape.Size = sizeof(FVector);
	RuntimeData.SlotShapes.Add(0, Shape);
	RuntimeData.ExposedInputPinOffsets.Add(
		FComposableCameraPinKey{
			0,
			GET_MEMBER_NAME_CHECKED(UComposableCameraCameraOffsetNode, CameraOffset) },
		0);
	RuntimeData.WriteValue<FVector>(0, FVector(10.f, 0.f, 0.f));
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Camera->Initialize(nullptr);

	UComposableCameraNodeModifierDataAsset* Asset =
		NewObject<UComposableCameraNodeModifierDataAsset>(Camera);
	Asset->ApplyMode = EComposableCameraModifierApplyMode::ModifyExistingInstance;
	UComposableCameraModifierBase* Modifier =
		NewObject<UComposableCameraModifierBase>(Asset);
	UComposableCameraCameraOffsetNode* Template =
		NewObject<UComposableCameraCameraOffsetNode>(Modifier);
	Template->CameraOffset = FVector(110.f, 0.f, 0.f);
	Modifier->NodeTemplate = Template;
	Modifier->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(UComposableCameraCameraOffsetNode, CameraOffset));
	Asset->Modifiers.Add(Modifier);

	UComposableCameraModifierTransitionBase* Transition =
		NewObject<UComposableCameraModifierTransitionBase>(Asset);
	Transition->Duration = 1.f;
	Transition->BlendFunction = EComposableCameraModifierBlendFunction::Linear;

	T_NodeModifier Effective;
	Effective.Add(
		UComposableCameraCameraOffsetNode::StaticClass(),
		FModifierEntry { Modifier, Asset });
	Camera->ReconcileInPlaceModifiers(Effective, Transition);
	Camera->InvalidateTickCache();
	(void)Camera->TickCamera(0.5f);
	UTEST_TRUE("Enter blends from the live exposed value",
		Node->CameraOffset.Equals(FVector(60.f, 0.f, 0.f), KINDA_SMALL_NUMBER));
	UTEST_TRUE("Explicit pin readers observe the same in-place overlay",
		Node->GetInputPinValue<FVector>(TEXT("CameraOffset")).Equals(
			FVector(60.f, 0.f, 0.f), KINDA_SMALL_NUMBER));

	RuntimeData.WriteValue<FVector>(0, FVector(20.f, 0.f, 0.f));
	Camera->ReconcileInPlaceModifiers(T_NodeModifier {}, Transition);
	Camera->InvalidateTickCache();
	(void)Camera->TickCamera(0.5f);
	UTEST_TRUE("Exit blends current effective value toward the live lower layer",
		Node->CameraOffset.Equals(FVector(40.f, 0.f, 0.f), KINDA_SMALL_NUMBER));

	RuntimeData.WriteValue<FVector>(0, FVector(30.f, 0.f, 0.f));
	Camera->InvalidateTickCache();
	(void)Camera->TickCamera(0.5f);
	UTEST_TRUE("Exit releases ownership to the current lower value, not the template",
		Node->CameraOffset.Equals(FVector(30.f, 0.f, 0.f), KINDA_SMALL_NUMBER));
	UTEST_TRUE("Explicit pin readers return to the live lower layer after release",
		Node->GetInputPinValue<FVector>(TEXT("CameraOffset")).Equals(
			FVector(30.f, 0.f, 0.f), KINDA_SMALL_NUMBER));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraInPlaceModifierAppliesToEveryMatchingNodeTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.AppliesToEveryMatchingNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraInPlaceModifierAppliesToEveryMatchingNodeTest::RunTest(
	const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AComposableCameraCameraBase* Camera =
		World->SpawnActor<AComposableCameraCameraBase>(
			AComposableCameraCameraBase::StaticClass(), FTransform::Identity);
	UComposableCameraCameraOffsetNode* NodeA =
		NewObject<UComposableCameraCameraOffsetNode>(Camera);
	UComposableCameraCameraOffsetNode* NodeB =
		NewObject<UComposableCameraCameraOffsetNode>(Camera);
	UComposableCameraPivotRotateNode* PivotNode =
		NewObject<UComposableCameraPivotRotateNode>(Camera);
	Camera->CameraNodes.Add(NodeA);
	Camera->CameraNodes.Add(NodeB);
	Camera->CameraNodes.Add(PivotNode);

	UComposableCameraNodeModifierDataAsset* Asset =
		NewObject<UComposableCameraNodeModifierDataAsset>(Camera);
	Asset->ApplyMode = EComposableCameraModifierApplyMode::ModifyExistingInstance;
	UComposableCameraModifierBase* Modifier =
		NewObject<UComposableCameraModifierBase>(Asset);
	UComposableCameraCameraOffsetNode* Template =
		NewObject<UComposableCameraCameraOffsetNode>(Modifier);
	Template->CameraOffset = FVector(100.f, 200.f, 300.f);
	Modifier->NodeTemplate = Template;
	Modifier->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(UComposableCameraCameraOffsetNode, CameraOffset));
	Asset->Modifiers.Add(Modifier);

	UComposableCameraModifierManager* Manager =
		NewObject<UComposableCameraModifierManager>(Camera);
	Manager->AddModifier(Asset);
	const FComposableCameraModifierUpdateResult InPlaceAddResult =
		Manager->GetModifierData().UpdateEffectiveModifiers(Camera);
	UTEST_TRUE("In-place selection does not request camera reactivation",
		InPlaceAddResult.bChanged
			&& !InPlaceAddResult.bRequiresCameraReactivation);
	Camera->ApplyEffectiveModifiers(
		Manager->GetModifierData().EffectiveModifiers);

	UTEST_TRUE("First matching node receives the in-place value",
		NodeA->CameraOffset.Equals(Template->CameraOffset, KINDA_SMALL_NUMBER));
	UTEST_TRUE("Second matching node receives an independent in-place value",
		NodeB->CameraOffset.Equals(Template->CameraOffset, KINDA_SMALL_NUMBER));

	UComposableCameraNodeModifierDataAsset* LegacyAsset =
		NewObject<UComposableCameraNodeModifierDataAsset>(Manager);
	UComposableCameraModifierBase* LegacyModifier =
		NewObject<UComposableCameraModifierBase>(LegacyAsset);
	LegacyModifier->NodeTemplate =
		NewObject<UComposableCameraPivotRotateNode>(LegacyModifier);
	LegacyModifier->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(UComposableCameraPivotRotateNode, RotationOffset));
	LegacyAsset->Modifiers.Add(LegacyModifier);
	Manager->AddModifier(LegacyAsset);
	const FComposableCameraModifierUpdateResult LegacyAddResult =
		Manager->GetModifierData().UpdateEffectiveModifiers(Camera);
	UTEST_TRUE("A mixed in-place and legacy selection requests camera reactivation",
		LegacyAddResult.bChanged
			&& LegacyAddResult.bRequiresCameraReactivation);

	Manager->RemoveModifier(LegacyAsset);
	const FComposableCameraModifierUpdateResult LegacyRemoveResult =
		Manager->GetModifierData().UpdateEffectiveModifiers(Camera);
	UTEST_TRUE("Removing the legacy half of a mixed selection also reactivates",
		LegacyRemoveResult.bChanged
			&& LegacyRemoveResult.bRequiresCameraReactivation);

	Manager->RemoveModifier(Asset);
	const FComposableCameraModifierUpdateResult InPlaceRemoveResult =
		Manager->GetModifierData().UpdateEffectiveModifiers(Camera);
	UTEST_TRUE("Pure in-place removal avoids camera reactivation",
		InPlaceRemoveResult.bChanged
			&& !InPlaceRemoveResult.bRequiresCameraReactivation);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierApplyModeCompatibilityTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.ApplyModeCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierApplyModeCompatibilityTest::RunTest(
	const FString& Parameters)
{
	UTEST_FALSE("Null properties are not continuously blendable",
		UComposableCameraModifierBase::IsNodePropertyContinuouslyBlendable(nullptr));

	UComposableCameraNodeModifierDataAsset* LegacyAsset =
		NewObject<UComposableCameraNodeModifierDataAsset>();
	UTEST_TRUE("Existing/default assets retain camera reactivation",
		LegacyAsset->ApplyMode
			== EComposableCameraModifierApplyMode::ReactivateCamera);

	UComposableCameraNodeModifierDataAsset* InPlaceAsset =
		NewObject<UComposableCameraNodeModifierDataAsset>();
	InPlaceAsset->ApplyMode =
		EComposableCameraModifierApplyMode::ModifyExistingInstance;
	UTEST_TRUE("In-place mode remains explicit opt-in",
		InPlaceAsset->ApplyMode
			== EComposableCameraModifierApplyMode::ModifyExistingInstance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierInterruptedTransitionMatrixTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.InterruptedEnterExitReenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierInterruptedTransitionMatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	const FCameraAndNode CameraAndNode =
		SpawnCameraAndNode(ScopedWorld.World);
	TestNotNull("Camera spawned", CameraAndNode.Camera);
	TestNotNull("Modifier test node created", CameraAndNode.Node);
	if (!CameraAndNode.Camera || !CameraAndNode.Node)
	{
		return false;
	}

	CameraAndNode.Node->FloatValue = 0.f;
	const FModifierSpec Modifier = MakeModifier(CameraAndNode.Camera);
	Modifier.Template->FloatValue = 100.f;
	Modifier.Modifier->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, FloatValue));
	UComposableCameraModifierTransitionBase* Transition =
		MakeLinearTransition(CameraAndNode.Camera);

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(Modifier), Transition);
	TickOnce(CameraAndNode.Camera, 0.25f);
	TestTrue("Enter reaches 25%",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 25.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		T_NodeModifier {}, Transition);
	TickOnce(CameraAndNode.Camera, 0.25f);
	TestTrue("Interrupted exit starts from current effective value",
		FMath::IsNearlyEqual(
			CameraAndNode.Node->FloatValue, 18.75f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(Modifier), Transition);
	TickOnce(CameraAndNode.Camera, 0.25f);
	TestTrue("Re-enter starts from interrupted exit value",
		FMath::IsNearlyEqual(
			CameraAndNode.Node->FloatValue, 39.0625f));

	TickOnce(CameraAndNode.Camera, 0.75f);
	TestTrue("Re-enter completes at Modifier target",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 100.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		T_NodeModifier {}, Transition);
	TickOnce(CameraAndNode.Camera, 1.f);
	TestTrue("Final exit restores baseline",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierReplacementTransitionMatrixTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.Replacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierReplacementTransitionMatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	const FCameraAndNode CameraAndNode =
		SpawnCameraAndNode(ScopedWorld.World);
	TestNotNull("Camera spawned", CameraAndNode.Camera);
	TestNotNull("Modifier test node created", CameraAndNode.Node);
	if (!CameraAndNode.Camera || !CameraAndNode.Node)
	{
		return false;
	}

	CameraAndNode.Node->FloatValue = 0.f;
	const FModifierSpec ModifierA = MakeModifier(CameraAndNode.Camera);
	const FModifierSpec ModifierB = MakeModifier(CameraAndNode.Camera);
	ModifierA.Template->FloatValue = 100.f;
	ModifierB.Template->FloatValue = 200.f;
	const FName FloatProperty = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierTestNode, FloatValue);
	ModifierA.Modifier->OverrideProperties.Add(FloatProperty);
	ModifierB.Modifier->OverrideProperties.Add(FloatProperty);
	UComposableCameraModifierTransitionBase* Transition =
		MakeLinearTransition(CameraAndNode.Camera);

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(ModifierA), Transition);
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("A enters halfway",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 50.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(ModifierB), Transition);
	TestTrue("Replacement reconcile causes no synchronous jump",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 50.f));
	TickOnce(CameraAndNode.Camera, 0.25f);
	TestTrue("A to B replacement starts from current value",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 87.5f));
	TickOnce(CameraAndNode.Camera, 0.75f);
	TestTrue("A to B replacement completes at B",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 200.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(ModifierA), Transition);
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("B to A replacement uses B as its source",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 150.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierPartialPropertySetTransitionMatrixTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.PartialPropertySets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierPartialPropertySetTransitionMatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	const FCameraAndNode CameraAndNode =
		SpawnCameraAndNode(ScopedWorld.World);
	TestNotNull("Camera spawned", CameraAndNode.Camera);
	TestNotNull("Modifier test node created", CameraAndNode.Node);
	if (!CameraAndNode.Camera || !CameraAndNode.Node)
	{
		return false;
	}

	const FModifierSpec ModifierA = MakeModifier(CameraAndNode.Camera);
	const FModifierSpec ModifierB = MakeModifier(CameraAndNode.Camera);
	ModifierA.Asset->Priority = 20;
	ModifierB.Asset->Priority = 10;
	const FName FloatProperty = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierTestNode, FloatValue);
	const FName VectorProperty = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierTestNode, Vector2DValue);
	const FName NonPinProperty = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierTestNode, NonPinFloatValue);

	ModifierA.Template->FloatValue = 100.f;
	ModifierA.Template->Vector2DValue = FVector2D(100.f, 100.f);
	ModifierA.Modifier->OverrideProperties.Add(FloatProperty);
	ModifierA.Modifier->OverrideProperties.Add(VectorProperty);
	ModifierA.Asset->OverrideEnterValueTransition =
		MakeLinearTransition(ModifierA.Asset, 2.f);
	ModifierA.Asset->OverrideExitValueTransition =
		MakeLinearTransition(ModifierA.Asset, 4.f);

	ModifierB.Template->Vector2DValue = FVector2D(200.f, 200.f);
	ModifierB.Template->NonPinFloatValue = 80.f;
	ModifierB.Modifier->OverrideProperties.Add(VectorProperty);
	ModifierB.Modifier->OverrideProperties.Add(NonPinProperty);
	ModifierB.Asset->OverrideEnterValueTransition =
		MakeLinearTransition(ModifierB.Asset, 2.f);
	ModifierB.Asset->OverrideReplaceValueTransition =
		MakeLinearTransition(ModifierB.Asset, 1.f);
	ModifierB.Asset->OverrideExitValueTransition =
		MakeLinearTransition(ModifierB.Asset, 2.f);

	// Seed A synchronously through the compatibility overload. Subsequent
	// selection edges use the production asset-resolved transition path.
	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(ModifierA), nullptr);
	TickOnce(CameraAndNode.Camera, 0.f);
	TestTrue("A starts from its authored values",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 100.f)
		&& CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(100.f, 100.f), KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 0.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiersFromAssets(
		MakeEffective(ModifierB));
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("A-only property uses A Exit",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 87.5f));
	TestTrue("Shared property uses B Replace",
		CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(150.f, 150.f), KINDA_SMALL_NUMBER));
	TestTrue("B-only property uses B Enter",
		FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 20.f));

	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Independent property clocks keep their own durations",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 75.f)
		&& CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(200.f, 200.f), KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 40.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiersFromAssets(
		T_NodeModifier {});
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Unowned A property continues its original Exit",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 62.5f));
	TestTrue("B properties use B Exit",
		CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(150.f, 150.f), KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 30.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiersFromAssets(
		MakeEffective(ModifierA));
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Re-entered A-only property uses A Enter",
		FMath::IsNearlyEqual(
			CameraAndNode.Node->FloatValue, 71.875f));
	TestTrue("Re-entered shared property uses A Enter, not Replace",
		CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(137.5f, 137.5f), KINDA_SMALL_NUMBER));
	TestTrue("Still-unowned B property continues B Exit",
		FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 20.f));

	TickOnce(CameraAndNode.Camera, 1.5f);
	ModifierB.Asset->OverrideReplaceValueTransition = nullptr;
	CameraAndNode.Camera->ReconcileInPlaceModifiersFromAssets(
		MakeEffective(ModifierB));
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Null Replace preserves higher-priority A Exit",
		CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(112.5f, 112.5f), KINDA_SMALL_NUMBER));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(ModifierA), nullptr);
	TickOnce(CameraAndNode.Camera, 0.f);
	ModifierB.Asset->Priority = 30;
	CameraAndNode.Camera->ReconcileInPlaceModifiersFromAssets(
		MakeEffective(ModifierB));
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Null Replace preserves higher-priority B Enter",
		CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(125.f, 125.f), KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierDiscreteThresholdMatrixTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.DiscreteThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierDiscreteThresholdMatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	auto RunEnterThresholdCase =
		[this, World = ScopedWorld.World](
			const TCHAR* Label,
			float SwitchWeight,
			float BeforeDelta,
			float AtDelta)
		{
			const FCameraAndNode CameraAndNode =
				SpawnCameraAndNode(World);
			if (!CameraAndNode.Camera || !CameraAndNode.Node)
			{
				AddError(FString::Printf(
					TEXT("%s: failed to create camera fixture"), Label));
				return;
			}

			CameraAndNode.Node->DiscreteValue = false;
			const FModifierSpec Modifier =
				MakeModifier(CameraAndNode.Camera);
			Modifier.Template->DiscreteValue = true;
			Modifier.Modifier->OverrideProperties.Add(
				GET_MEMBER_NAME_CHECKED(
					UComposableCameraModifierTestNode, DiscreteValue));
			UComposableCameraModifierTransitionBase* Transition =
				MakeLinearTransition(
					CameraAndNode.Camera, 1.f, SwitchWeight);

			CameraAndNode.Camera->ReconcileInPlaceModifiers(
				MakeEffective(Modifier), Transition);
			TickOnce(CameraAndNode.Camera, BeforeDelta);
			TestFalse(
				*FString::Printf(TEXT("%s stays at source before threshold"), Label),
				CameraAndNode.Node->DiscreteValue);
			TickOnce(CameraAndNode.Camera, AtDelta);
			TestTrue(
				*FString::Printf(TEXT("%s switches at threshold"), Label),
				CameraAndNode.Node->DiscreteValue);
		};

	{
		const FCameraAndNode CameraAndNode =
			SpawnCameraAndNode(ScopedWorld.World);
		if (!CameraAndNode.Camera || !CameraAndNode.Node)
		{
			AddError(TEXT("AtStart: failed to create camera fixture"));
			return false;
		}
		const FModifierSpec Modifier =
			MakeModifier(CameraAndNode.Camera);
		Modifier.Template->DiscreteValue = true;
		Modifier.Modifier->OverrideProperties.Add(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, DiscreteValue));
		UComposableCameraModifierTransitionBase* Transition =
			MakeLinearTransition(CameraAndNode.Camera, 1.f, 0.f);
		CameraAndNode.Camera->ReconcileInPlaceModifiers(
			MakeEffective(Modifier), Transition);
		TickOnce(CameraAndNode.Camera, 0.f);
		TestTrue("AtStart switches on first evaluation",
			CameraAndNode.Node->DiscreteValue);
	}

	RunEnterThresholdCase(TEXT("AtHalf"), 0.5f, 0.25f, 0.25f);
	RunEnterThresholdCase(TEXT("AtEnd"), 1.f, 0.5f, 0.5f);

	{
		const FCameraAndNode CameraAndNode =
			SpawnCameraAndNode(ScopedWorld.World);
		if (!CameraAndNode.Camera || !CameraAndNode.Node)
		{
			AddError(TEXT("Exit threshold: failed to create camera fixture"));
			return false;
		}
		const FModifierSpec Modifier =
			MakeModifier(CameraAndNode.Camera);
		Modifier.Template->DiscreteValue = true;
		Modifier.Modifier->OverrideProperties.Add(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, DiscreteValue));
		UComposableCameraModifierTransitionBase* Transition =
			MakeLinearTransition(CameraAndNode.Camera, 1.f, 0.5f);
		CameraAndNode.Camera->ReconcileInPlaceModifiers(
			MakeEffective(Modifier), nullptr);
		TickOnce(CameraAndNode.Camera, 0.f);
		TestTrue("Discrete Modifier activated before exit",
			CameraAndNode.Node->DiscreteValue);

		CameraAndNode.Camera->ReconcileInPlaceModifiers(
			T_NodeModifier {}, Transition);
		TickOnce(CameraAndNode.Camera, 0.25f);
		TestTrue("Exit retains source before threshold",
			CameraAndNode.Node->DiscreteValue);
		TickOnce(CameraAndNode.Camera, 0.25f);
		TestFalse("Exit releases baseline at threshold",
			CameraAndNode.Node->DiscreteValue);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierImmediateTransitionMatrixTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.ImmediateTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierImmediateTransitionMatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	const FCameraAndNode CameraAndNode =
		SpawnCameraAndNode(ScopedWorld.World);
	TestNotNull("Camera spawned", CameraAndNode.Camera);
	TestNotNull("Modifier test node created", CameraAndNode.Node);
	if (!CameraAndNode.Camera || !CameraAndNode.Node)
	{
		return false;
	}

	const FModifierSpec Modifier = MakeModifier(CameraAndNode.Camera);
	Modifier.Template->FloatValue = 100.f;
	Modifier.Modifier->OverrideProperties.Add(
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, FloatValue));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(Modifier), nullptr);
	TestTrue("Null transition waits for normal node evaluation",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 0.f));
	TickOnce(CameraAndNode.Camera, 0.f);
	TestTrue("Null transition enters on first evaluation",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 100.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		T_NodeModifier {}, nullptr);
	TickOnce(CameraAndNode.Camera, 0.f);
	TestTrue("Null transition exits on first evaluation",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 0.f));

	UComposableCameraModifierTransitionBase* ZeroDuration =
		MakeLinearTransition(CameraAndNode.Camera, 0.f);
	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(Modifier), ZeroDuration);
	TickOnce(CameraAndNode.Camera, 0.f);
	TestTrue("Zero-duration transition enters immediately",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 100.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		T_NodeModifier {}, ZeroDuration);
	TickOnce(CameraAndNode.Camera, 0.f);
	TestTrue("Zero-duration transition exits immediately",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierContinuousTypeMatrixTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.ContinuousTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierContinuousTypeMatrixTest::RunTest(
	const FString& Parameters)
{
	using namespace ComposableCameraModifierTransitionTest;

	FScopedWorld ScopedWorld;
	const FCameraAndNode CameraAndNode =
		SpawnCameraAndNode(ScopedWorld.World);
	TestNotNull("Camera spawned", CameraAndNode.Camera);
	TestNotNull("Modifier test node created", CameraAndNode.Node);
	if (!CameraAndNode.Camera || !CameraAndNode.Node)
	{
		return false;
	}

	const FModifierSpec Modifier = MakeModifier(CameraAndNode.Camera);
	Modifier.Template->FloatValue = 10.f;
	Modifier.Template->DoubleValue = 20.0;
	Modifier.Template->Vector2DValue = FVector2D(2.f, 4.f);
	Modifier.Template->Vector3DValue = FVector(2.f, 4.f, 6.f);
	Modifier.Template->Vector4Value = FVector4(2.f, 4.f, 6.f, 8.f);
	Modifier.Template->RotatorValue = FRotator(0.f, 90.f, 0.f);
	Modifier.Template->TransformValue = FTransform(
		FRotator(0.f, 90.f, 0.f).Quaternion(),
		FVector(10.f, 20.f, 30.f),
		FVector(3.f, 3.f, 3.f));
	Modifier.Template->LinearColorValue =
		FLinearColor(1.f, 0.5f, 0.25f, 1.f);
	Modifier.Template->NonPinFloatValue = 8.f;

	const FName ContinuousProperties[] = {
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, FloatValue),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, DoubleValue),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, Vector2DValue),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, Vector3DValue),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, Vector4Value),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, RotatorValue),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, TransformValue),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, LinearColorValue),
		GET_MEMBER_NAME_CHECKED(
			UComposableCameraModifierTestNode, NonPinFloatValue)
	};
	for (const FName PropertyName : ContinuousProperties)
	{
		Modifier.Modifier->OverrideProperties.Add(PropertyName);
	}

	UComposableCameraModifierTransitionBase* Transition =
		MakeLinearTransition(CameraAndNode.Camera);
	CameraAndNode.Node->ResetModifierNotifications();
	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		MakeEffective(Modifier), Transition);
	TickOnce(CameraAndNode.Camera, 0.5f);

	TestTrue("Float interpolates",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 5.f));
	TestTrue("Double interpolates",
		FMath::IsNearlyEqual(CameraAndNode.Node->DoubleValue, 10.0));
	TestTrue("Vector2D interpolates",
		CameraAndNode.Node->Vector2DValue.Equals(
			FVector2D(1.f, 2.f), KINDA_SMALL_NUMBER));
	TestTrue("Vector3D interpolates",
		CameraAndNode.Node->Vector3DValue.Equals(
			FVector(1.f, 2.f, 3.f), KINDA_SMALL_NUMBER));
	TestTrue("Vector4 interpolates",
		CameraAndNode.Node->Vector4Value.Equals(
			FVector4(1.f, 2.f, 3.f, 4.f), KINDA_SMALL_NUMBER));
	TestTrue("Rotator uses quaternion interpolation",
		CameraAndNode.Node->RotatorValue.Equals(
			FRotator(0.f, 45.f, 0.f), KINDA_SMALL_NUMBER));
	TestTrue("Transform location interpolates",
		CameraAndNode.Node->TransformValue.GetLocation().Equals(
			FVector(5.f, 10.f, 15.f), KINDA_SMALL_NUMBER));
	TestTrue("Transform scale interpolates",
		CameraAndNode.Node->TransformValue.GetScale3D().Equals(
			FVector(2.f, 2.f, 2.f), KINDA_SMALL_NUMBER));
	TestTrue("Transform rotation interpolates",
		CameraAndNode.Node->TransformValue.GetRotation().Rotator().Equals(
			FRotator(0.f, 45.f, 0.f), KINDA_SMALL_NUMBER));
	TestTrue("LinearColor interpolates through non-pin opt-in",
		CameraAndNode.Node->LinearColorValue.Equals(
			FLinearColor(0.5f, 0.25f, 0.125f, 0.5f)));
	TestTrue("Non-pin float interpolates",
		FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 4.f));
	TestTrue("Non-pin LinearColor receives change notification",
		CameraAndNode.Node->WasModifierPropertyNotified(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, LinearColorValue)));
	TestTrue("Non-pin float receives change notification",
		CameraAndNode.Node->WasModifierPropertyNotified(
			GET_MEMBER_NAME_CHECKED(
				UComposableCameraModifierTestNode, NonPinFloatValue)));

	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Continuous matrix reaches target",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 10.f)
		&& CameraAndNode.Node->RotatorValue.Equals(
			FRotator(0.f, 90.f, 0.f), KINDA_SMALL_NUMBER)
		&& CameraAndNode.Node->TransformValue.GetLocation().Equals(
			FVector(10.f, 20.f, 30.f), KINDA_SMALL_NUMBER)
		&& CameraAndNode.Node->LinearColorValue.Equals(
			FLinearColor(1.f, 0.5f, 0.25f, 1.f))
		&& FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 8.f));

	CameraAndNode.Camera->ReconcileInPlaceModifiers(
		T_NodeModifier {}, Transition);
	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Continuous matrix exits through midpoint",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 5.f)
		&& CameraAndNode.Node->RotatorValue.Equals(
			FRotator(0.f, 45.f, 0.f), KINDA_SMALL_NUMBER)
		&& CameraAndNode.Node->TransformValue.GetLocation().Equals(
			FVector(5.f, 10.f, 15.f), KINDA_SMALL_NUMBER)
		&& CameraAndNode.Node->LinearColorValue.Equals(
			FLinearColor(0.5f, 0.25f, 0.125f, 0.5f))
		&& FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 4.f));

	TickOnce(CameraAndNode.Camera, 0.5f);
	TestTrue("Continuous matrix restores baseline",
		FMath::IsNearlyEqual(CameraAndNode.Node->FloatValue, 0.f)
		&& CameraAndNode.Node->RotatorValue.IsNearlyZero()
		&& CameraAndNode.Node->TransformValue.Equals(
			FTransform::Identity)
		&& CameraAndNode.Node->LinearColorValue.Equals(
			FLinearColor::Transparent)
		&& FMath::IsNearlyEqual(
			CameraAndNode.Node->NonPinFloatValue, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraModifierBlendFunctionMatrixTest,
	"System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.BlendFunctions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraModifierBlendFunctionMatrixTest::RunTest(
	const FString& Parameters)
{
	UComposableCameraModifierTransitionBase* Transition =
		NewObject<UComposableCameraModifierTransitionBase>();
	Transition->Duration = 1.f;
	Transition->BlendExponent = 2.f;

	auto TestWeight =
		[this, Transition](
			const TCHAR* Label,
			EComposableCameraModifierBlendFunction BlendFunction,
			float ElapsedTime,
			float Expected)
		{
			Transition->BlendFunction = BlendFunction;
			TestTrue(Label, FMath::IsNearlyEqual(
				Transition->EvaluateWeight(ElapsedTime), Expected));
		};

	TestWeight(
		TEXT("Linear"),
		EComposableCameraModifierBlendFunction::Linear,
		0.25f, 0.25f);
	TestWeight(
		TEXT("SmoothStep"),
		EComposableCameraModifierBlendFunction::SmoothStep,
		0.25f, 0.15625f);
	TestWeight(
		TEXT("SmootherStep"),
		EComposableCameraModifierBlendFunction::SmootherStep,
		0.25f, 0.103515625f);
	TestWeight(
		TEXT("EaseIn"),
		EComposableCameraModifierBlendFunction::EaseIn,
		0.25f, 0.0625f);
	TestWeight(
		TEXT("EaseOut"),
		EComposableCameraModifierBlendFunction::EaseOut,
		0.25f, 0.4375f);
	TestWeight(
		TEXT("EaseInOut first half"),
		EComposableCameraModifierBlendFunction::EaseInOut,
		0.25f, 0.125f);
	TestWeight(
		TEXT("EaseInOut second half"),
		EComposableCameraModifierBlendFunction::EaseInOut,
		0.75f, 0.875f);

	UCurveFloat* CustomCurve = NewObject<UCurveFloat>(Transition);
	CustomCurve->FloatCurve.UpdateOrAddKey(0.f, 0.f);
	CustomCurve->FloatCurve.UpdateOrAddKey(0.5f, 1.5f);
	CustomCurve->FloatCurve.UpdateOrAddKey(1.f, 1.f);
	Transition->CustomCurve = CustomCurve;
	TestWeight(
		TEXT("CustomCurve clamps output"),
		EComposableCameraModifierBlendFunction::CustomCurve,
		0.5f, 1.f);

	Transition->CustomCurve = nullptr;
	TestWeight(
		TEXT("Missing CustomCurve falls back to linear"),
		EComposableCameraModifierBlendFunction::CustomCurve,
		0.25f, 0.25f);

	Transition->BlendFunction =
		EComposableCameraModifierBlendFunction::Linear;
	TestTrue("Negative elapsed time clamps to zero",
		FMath::IsNearlyEqual(Transition->EvaluateWeight(-1.f), 0.f));
	TestTrue("Elapsed time beyond duration clamps to one",
		FMath::IsNearlyEqual(Transition->EvaluateWeight(2.f), 1.f));
	Transition->Duration = 0.f;
	TestTrue("Zero duration returns one",
		FMath::IsNearlyEqual(Transition->EvaluateWeight(0.f), 1.f));
	return true;
}

#undef LOCTEXT_NAMESPACE
