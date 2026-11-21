// Copyright 2026 Sulley. All Rights Reserved.
#include "Tests/ComposableCameraAssetValidationTestTypes.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#endif
#include "Actions/ComposableCameraActionBase.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraShotTemplateLibrary.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "DataAssets/ComposableCameraTransitionTableDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Math/ComposableCameraShotSolver.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "Nodes/ComposableCameraMathNodes.h"
#include "Nodes/ComposableCameraRelativeFixedPoseNode.h"
#include "Transitions/ComposableCameraLinearTransition.h"
#include "UObject/Package.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSAssetValidationReadOnlyTest,
	"ComposableCameraSystem.Validation.ReadOnlyTypeAndPatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSAssetValidationReadOnlyTest::RunTest(const FString&)
{
	auto* Asset = NewObject<UComposableCameraTypeAsset>();
	Asset->NodeTemplates.Add(NewObject<UComposableCameraClampNode>(Asset));
	Asset->Build(false);
	const uint32 Revision = Asset->GetEvaluationRevision();
	const auto Status = Asset->BuildStatus;
	const int32 MessageCount = Asset->BuildMessages.Num();
	const bool bDirty = Asset->GetOutermost()->IsDirty();
	FDataValidationContext Context;
	TestTrue(TEXT("Valid TypeAsset reports Valid"), Asset->IsDataValid(Context) == EDataValidationResult::Valid);
	TestEqual(TEXT("Validation does not restart preview evaluation"), Asset->GetEvaluationRevision(), Revision);
	TestTrue(TEXT("Validation leaves Build status unchanged"), Asset->BuildStatus == Status);
	TestEqual(TEXT("Validation leaves Build messages unchanged"), Asset->BuildMessages.Num(), MessageCount);
	TestEqual(TEXT("Validation does not dirty the package"), Asset->GetOutermost()->IsDirty(), bDirty);

	Asset->NodeTemplates.Add(nullptr);
	FDataValidationContext Invalid;
	TestTrue(TEXT("Errors are recomputed from current data"), Asset->IsDataValid(Invalid) == EDataValidationResult::Invalid);
	TestEqual(TEXT("Invalid validation also leaves revision unchanged"), Asset->GetEvaluationRevision(), Revision);
	TestTrue(TEXT("Invalid validation does not replace last Build status"), Asset->BuildStatus == Status);

	auto* Patch = NewObject<UComposableCameraPatchTypeAsset>();
	Patch->NodeTemplates.Add(NewObject<UComposableCameraRelativeFixedPoseNode>(Patch));
	FDataValidationContext PatchContext;
	TestTrue(TEXT("Inherited native validation executes Patch-specific compatibility checks"), Patch->IsDataValid(PatchContext) == EDataValidationResult::Invalid);
	Patch->NodeTemplates.Reset(); Patch->NodeTemplates.Add(NewObject<UComposableCameraClampNode>(Patch));
	Patch->DefaultExitDuration = -1.f;
	FDataValidationContext DurationContext;
	TestTrue(TEXT("Invalid Patch envelope is caught before activation"), Patch->IsDataValid(DurationContext) == EDataValidationResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSAssetValidationConnectionTest,
	"ComposableCameraSystem.Validation.PinStorageAndObjectClasses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSAssetValidationConnectionTest::RunTest(const FString&)
{
	auto* Asset = NewObject<UComposableCameraTypeAsset>();
	Asset->NodeTemplates = { NewObject<UComposableCameraClampNode>(Asset), NewObject<UComposableCameraDoubleToFloatNode>(Asset) };
	auto& Wire = Asset->PinConnections.AddDefaulted_GetRef();
	Wire.SourceNodeIndex = 0; Wire.SourcePinName = TEXT("Result");
	Wire.TargetNodeIndex = 1; Wire.TargetPinName = TEXT("Value");
	FDataValidationContext Mismatch;
	TestTrue(TEXT("Persisted float-to-double wire is invalid without a conversion node"), Asset->IsDataValid(Mismatch) == EDataValidationResult::Invalid);
	Asset->Build(false);
	TestTrue(TEXT("Editor Build reports the same storage error"), Asset->BuildStatus == EComposableCameraBuildStatus::Failed);
	Asset->NodeTemplates[1] = NewObject<UComposableCameraFloatToDoubleNode>(Asset);
	FDataValidationContext Converted;
	TestTrue(TEXT("Explicit conversion accepts its correctly typed input"), Asset->IsDataValid(Converted) == EDataValidationResult::Valid);
	Wire.TargetPinName = TEXT("RemovedPin");
	FDataValidationContext Stale;
	TestTrue(TEXT("Stale pin names are diagnosed"), Asset->IsDataValid(Stale) == EDataValidationResult::Invalid);

	Asset->NodeTemplates = { GetMutableDefault<UComposableCameraValidationObjectSource>(), NewObject<UComposableCameraCurveSampleNode>(Asset) };
	Wire.SourcePinName = TEXT("VectorCurve"); Wire.TargetPinName = TEXT("Curve");
	FDataValidationContext WrongClass;
	TestTrue(TEXT("Sibling object classes cannot be wired"), Asset->IsDataValid(WrongClass) == EDataValidationResult::Invalid);
	Wire.SourcePinName = TEXT("FloatCurve");
	FDataValidationContext CorrectClass;
	TestTrue(TEXT("Matching object class is valid"), Asset->IsDataValid(CorrectClass) == EDataValidationResult::Valid);
	Wire.SourcePinName = TEXT("GenericObject");
	FDataValidationContext DynamicClass;
	TestTrue(TEXT("Potential runtime downcast remains legal"), Asset->IsDataValid(DynamicClass) == EDataValidationResult::Valid);
	TestTrue(TEXT("Runtime class requirement is surfaced as a warning"), DynamicClass.GetNumWarnings() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSShotAssetValidationTest,
	"ComposableCameraSystem.Validation.ShotTemplates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSShotAssetValidationTest::RunTest(const FString&)
{
	auto* Asset = NewObject<UComposableCameraShotAsset>();
	Asset->Shot.Targets.AddDefaulted(); // Deliberately no level actor: reusable template.
	FDataValidationContext Unbound;
	TestTrue(TEXT("Unbound reusable Shot target is valid"), Asset->IsDataValid(Unbound) == EDataValidationResult::Valid);
	Asset->Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Asset->Shot.Placement.BasisActorIndex = 99;
	FDataValidationContext UnusedBasis;
	TestTrue(TEXT("Screen placement ignores orbit-only basis fields"), Asset->IsDataValid(UnusedBasis) == EDataValidationResult::Valid);
	Asset->Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
	Asset->Shot.Placement.BasisActorIndex = 0;
	Asset->Shot.Aim.AimAnchor.TargetIndex = 5;
	FDataValidationContext BadIndex;
	TestTrue(TEXT("Invalid consumed anchor index is rejected"), Asset->IsDataValid(BadIndex) == EDataValidationResult::Invalid);
	Asset->Shot.Aim.AimAnchor.TargetIndex = 0;
	Asset->Shot.Lens.FOVMode = EShotFOVMode::SolvedFromBoundsFit;
	Asset->Shot.Lens.FOVClamp.Min = 100.f; Asset->Shot.Lens.FOVClamp.Max = 30.f;
	FDataValidationContext BadRange;
	TestTrue(TEXT("Reversed FOV limits are rejected"), Asset->IsDataValid(BadRange) == EDataValidationResult::Invalid);
	Asset->Shot.Lens.FOVClamp.Min = 30.f; Asset->Shot.Lens.FOVClamp.Max = 100.f;
	FDataValidationContext NoBounds;
	TestTrue(TEXT("Missing framing bounds are a fallback warning"), Asset->IsDataValid(NoBounds) == EDataValidationResult::Valid);
	TestTrue(TEXT("Bounds warning is emitted"), NoBounds.GetNumWarnings() > 0);
	Asset->Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
	Asset->Shot.Aim.Mode = EShotAimMode::NoOp;
	Asset->Shot.Focus.Mode = EShotFocusMode::Manual;
	Asset->Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Asset->Shot.Targets.Reset();
	FDataValidationContext Fixed;
	TestTrue(TEXT("World-fixed camera does not require unused anchors"), Asset->IsDataValid(Fixed) == EDataValidationResult::Valid);

	auto* Library = NewObject<UComposableCameraShotTemplateLibrary>();
	auto& First = Library->Shots.AddDefaulted_GetRef(); First.Name = TEXT("CloseUp"); First.Shot = Asset;
	FDataValidationContext LibraryValid;
	TestTrue(TEXT("Named Shot reference is valid"), Library->IsDataValid(LibraryValid) == EDataValidationResult::Valid);
	const FComposableCameraShotTemplate DuplicateEntry = Library->Shots[0];
	Library->Shots.Add(DuplicateEntry);
	FDataValidationContext Duplicate;
	TestTrue(TEXT("Duplicate template names are rejected"), Library->IsDataValid(Duplicate) == EDataValidationResult::Invalid);
	return true;
}

// CR regression: the solver supports this combination; validation must agree.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSAnchorAtScreenNoOpValidationTest,
	"ComposableCameraSystem.Validation.AnchorAtScreenNoOp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSAnchorAtScreenNoOpValidationTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSolver;
	auto* ShotAsset = NewObject<UComposableCameraShotAsset>();
	auto& Shot = ShotAsset->Shot;
	Shot.Placement.Mode = EShotPlacementMode::AnchorAtScreen;
	Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::FixedWorldPosition;
	Shot.Placement.PlacementAnchor.WorldPosition = FVector(500, 0, 0);
	Shot.Placement.Distance = 250.f;
	Shot.Placement.ScreenPosition = FVector2D(0.2, -0.1);
	Shot.Aim.Mode = EShotAimMode::NoOp;
	Shot.Focus.Mode = EShotFocusMode::Manual;
	Shot.Lens.FOVMode = EShotFOVMode::Manual;
	Shot.Lens.ManualFOV = 90.f;
	FShotSolveContext SolveContext;
	SolveContext.ViewportAspectRatio = 1.f;
	const FShotSolveResult Solved = SolveShot(Shot, SolveContext);
	TestTrue(TEXT("AnchorAtScreen with NoOp aim produces a valid runtime pose"), Solved.bValid);
	TestTrue(TEXT("NoOp retains identity rotation with zero roll"), Solved.CameraRotation.Equals(FRotator::ZeroRotator));
	FDataValidationContext ShotContext;
	TestTrue(TEXT("Supported Shot combination passes native validation"), ShotAsset->IsDataValid(ShotContext) == EDataValidationResult::Valid);
	TestEqual(TEXT("Supported Shot combination has no validation errors"), ShotContext.GetNumErrors(), 0u);

	auto* TypeAsset = NewObject<UComposableCameraTypeAsset>();
	auto* Framing = NewObject<UComposableCameraCompositionFramingNode>(TypeAsset);
	Framing->Shot = Shot;
	TypeAsset->NodeTemplates.Add(Framing);
	FDataValidationContext TypeContext;
	TestTrue(TEXT("The same inline Shot passes CameraType validation"), TypeAsset->IsDataValid(TypeContext) == EDataValidationResult::Valid);
	TypeAsset->Build(false);
	TestTrue(TEXT("Supported inline Shot does not fail graph Build"), TypeAsset->BuildStatus != EComposableCameraBuildStatus::Failed);

	Shot.Aim.Mode = static_cast<EShotAimMode>(255);
	FDataValidationContext UnknownAim;
	TestTrue(TEXT("Unknown Aim mode is still rejected"), ShotAsset->IsDataValid(UnknownAim) == EDataValidationResult::Invalid);
	Shot.Aim.Mode = EShotAimMode::NoOp;
	Shot.Focus.Mode = EShotFocusMode::FollowAimAnchor;
	FDataValidationContext FocusNeedsAnchor;
	TestTrue(TEXT("NoOp does not bypass an aim anchor consumed by Focus"), ShotAsset->IsDataValid(FocusNeedsAnchor) == EDataValidationResult::Invalid);
	Shot.Focus.Mode = EShotFocusMode::Manual;
	Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::SingleTarget;
	FDataValidationContext BadPlacement;
	TestTrue(TEXT("AnchorAtScreen still requires a valid placement target index"), ShotAsset->IsDataValid(BadPlacement) == EDataValidationResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSProfileAndTemplateValidationTest,
	"ComposableCameraSystem.Validation.ProfilesAndEffectTemplates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSProfileAndTemplateValidationTest::RunTest(const FString&)
{
	auto* Profile = NewObject<UComposableCameraMeshProfile>();
	Profile->Type = EComposableCameraMeshProfileType::Action;
	FDataValidationContext EmptyAction;
	TestTrue(TEXT("Selected effect family requires its asset"), Profile->IsDataValid(EmptyAction) == EDataValidationResult::Invalid);
	Profile->Action.ActionAsset = NewObject<UComposableCameraActionTypeAsset>();
	FDataValidationContext Selected;
	TestTrue(TEXT("Selected soft reference is valid; inactive camera/patch fields are ignored"),
		Profile->IsDataValid(Selected) == EDataValidationResult::Valid);
	Profile->Action.ActionAsset = TSoftObjectPtr<UComposableCameraActionTypeAsset>(FSoftObjectPath(TEXT("/Game/CCSValidationMissingAsset.CCSValidationMissingAsset")));
	FDataValidationContext MissingAsset;
	const bool bDiscoveryInProgress = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().IsLoadingAssets();
	const EDataValidationResult MissingResult = Profile->IsDataValid(MissingAsset);
	if (!bDiscoveryInProgress)
		TestTrue(TEXT("Broken soft reference is rejected after discovery completes"), MissingResult == EDataValidationResult::Invalid);
	else
		TestTrue(TEXT("Incomplete discovery is reported rather than silently accepted"), MissingAsset.GetNumWarnings() > 0 || MissingAsset.GetNumErrors() > 0);
	Profile->Type = EComposableCameraMeshProfileType::Modifier;
	FDataValidationContext EmptyModifiers;
	TestTrue(TEXT("Empty Modifier family warns without failing"), Profile->IsDataValid(EmptyModifiers) == EDataValidationResult::Valid);
	TestTrue(TEXT("Empty family warning"), EmptyModifiers.GetNumWarnings() > 0);

	auto* Action = NewObject<UComposableCameraActionTypeAsset>();
	Action->ActionTemplate = NewObject<UComposableCameraActionBase>(Action);
	Action->ActionTemplate->Duration = 0.f;
	FDataValidationContext Duration;
	TestTrue(TEXT("Duration-enabled Action cannot have zero duration"), Action->IsDataValid(Duration) == EDataValidationResult::Invalid);
	Action->ActionTemplate->ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Manual);
	FDataValidationContext Manual;
	TestTrue(TEXT("Manual Action ignores unused duration"), Action->IsDataValid(Manual) == EDataValidationResult::Valid);

	auto* Modifier = NewObject<UComposableCameraNodeModifierDataAsset>();
	auto* Entry = NewObject<UComposableCameraModifierBase>(Modifier);
	Modifier->Modifiers.Add(Entry); Entry->NodeTemplate = NewObject<UComposableCameraClampNode>(Entry);
	Entry->OverrideProperties.Add(TEXT("Value"));
	FDataValidationContext Override;
	TestTrue(TEXT("Existing editable override is valid"), Modifier->IsDataValid(Override) == EDataValidationResult::Valid);
	Entry->OverrideProperties.Add(TEXT("DeletedProperty"));
	FDataValidationContext StaleOverride;
	TestTrue(TEXT("Stale property override is rejected"), Modifier->IsDataValid(StaleOverride) == EDataValidationResult::Invalid);

	auto* Transition = NewObject<UComposableCameraTransitionDataAsset>();
	FDataValidationContext EmptyTransition;
	TestTrue(TEXT("Missing transition template is rejected"), Transition->IsDataValid(EmptyTransition) == EDataValidationResult::Invalid);
	Transition->Transition = NewObject<UComposableCameraLinearTransition>(Transition);
	FDataValidationContext TransitionValid;
	TestTrue(TEXT("Transition template is valid"), Transition->IsDataValid(TransitionValid) == EDataValidationResult::Valid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSEmptyAuthoringValidationTest,
	"ComposableCameraSystem.Validation.EmptyAuthoringWarnings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSEmptyAuthoringValidationTest::RunTest(const FString&)
{
	auto* EmptyCamera = NewObject<UComposableCameraTypeAsset>();
	FDataValidationContext EmptyCameraContext;
	TestTrue(TEXT("Empty CameraType is valid with a warning"), EmptyCamera->IsDataValid(EmptyCameraContext) == EDataValidationResult::Valid);
	TestEqual(TEXT("Empty CameraType does not produce errors"), EmptyCameraContext.GetNumErrors(), 0u);
	TestTrue(TEXT("Empty CameraType still reports missing nodes"), EmptyCameraContext.GetNumWarnings() > 0);

	auto* Modifier = NewObject<UComposableCameraNodeModifierDataAsset>();
	Modifier->Modifiers.Add(NewObject<UComposableCameraModifierBase>(Modifier));
	Modifier->Modifiers.Add(NewObject<UComposableCameraModifierBase>(Modifier));
	FDataValidationContext EmptyEntries;
	TestTrue(TEXT("Unconfigured Node Type entries do not fail validation"), Modifier->IsDataValid(EmptyEntries) == EDataValidationResult::Valid);
	TestEqual(TEXT("Unconfigured entries produce no errors"), EmptyEntries.GetNumErrors(), 0u);
	TestEqual(TEXT("Each unconfigured entry produces a warning"), EmptyEntries.GetNumWarnings(), 2u);

	Modifier->Modifiers[0]->NodeClass = UComposableCameraClampNode::StaticClass();
	FDataValidationContext LegacyEntry;
	TestTrue(TEXT("Legacy callback target remains valid"), Modifier->IsDataValid(LegacyEntry) == EDataValidationResult::Valid);
	TestEqual(TEXT("Configured legacy entry adds no missing-type warning"), LegacyEntry.GetNumWarnings(), 1u);

	auto* InvalidOverride = NewObject<UComposableCameraModifierBase>(Modifier);
	InvalidOverride->NodeTemplate = NewObject<UComposableCameraClampNode>(InvalidOverride);
	InvalidOverride->OverrideProperties.Add(TEXT("DeletedProperty"));
	Modifier->Modifiers.Add(InvalidOverride);
	FDataValidationContext MixedIssues;
	TestTrue(TEXT("A warning does not hide a stale override error"), Modifier->IsDataValid(MixedIssues) == EDataValidationResult::Invalid);
	TestTrue(TEXT("Stale override remains an error"), MixedIssues.GetNumErrors() > 0);
	TestEqual(TEXT("Empty entry warning survives mixed results"), MixedIssues.GetNumWarnings(), 1u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSTransitionTableValidationResultTest,
	"ComposableCameraSystem.Validation.TransitionTableResult", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSTransitionTableValidationResultTest::RunTest(const FString&)
{
	auto* Table = NewObject<UComposableCameraTransitionTableDataAsset>();
	FDataValidationContext Empty;
	TestTrue(TEXT("Successfully checked empty table is Valid, not NotValidated"), Table->IsDataValid(Empty) == EDataValidationResult::Valid);
	auto& Entry = Table->Entries.AddDefaulted_GetRef();
	FDataValidationContext Missing;
	TestTrue(TEXT("Incomplete entry is Invalid"), Table->IsDataValid(Missing) == EDataValidationResult::Invalid);
	Entry.SourceTypeAsset = NewObject<UComposableCameraTypeAsset>(); Entry.TargetTypeAsset = NewObject<UComposableCameraTypeAsset>();
	FDataValidationContext Warning;
	TestTrue(TEXT("Warning-only table is Valid"), Table->IsDataValid(Warning) == EDataValidationResult::Valid);
	TestEqual(TEXT("Missing optional transition warning is preserved"), Warning.GetNumWarnings(), 1u);
	return true;
}
#endif
