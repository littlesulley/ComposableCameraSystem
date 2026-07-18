// Copyright 2026 Sulley. All Rights Reserved.

#include "Modifiers/ComposableCameraModifierBase.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraModifierManager.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Interpolator/ComposableCameraSimpleSpringInterpolator.h"
#include "Nodes/ComposableCameraCameraOffsetNode.h"
#include "Nodes/ComposableCameraPivotRotateNode.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ComposableCameraModifierPropertyOverrideTests"

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
		const FModifierEntry* Effective =
			Manager->GetModifierData().EffectiveModifiers.Find(OffsetNodeClass);
		TestTrue("Higher-priority matching query wins", Effective && Effective->Asset == AdsAsset);
	}

	if (ThirdPersonCamera)
	{
		Manager->GetModifierData().UpdateEffectiveModifiers(ThirdPersonCamera);
		const FModifierEntry* Effective =
			Manager->GetModifierData().EffectiveModifiers.Find(OffsetNodeClass);
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

#undef LOCTEXT_NAMESPACE
