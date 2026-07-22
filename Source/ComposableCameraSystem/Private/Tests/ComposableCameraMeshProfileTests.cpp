// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "MeshCamera/ComposableCameraMeshProfileState.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshProfileCameraSchemaTest,
	"ComposableCameraSystem.MeshCamera.ProfileCameraSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileCameraSchemaTest::RunTest(
	const FString& /*Parameters*/)
{
	const FStructProperty* CameraProperty =
		FindFProperty<FStructProperty>(
			UComposableCameraMeshProfile::StaticClass(),
			GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Camera));
	TestNotNull(TEXT("Mesh Profile exposes a Camera section property"), CameraProperty);
	if (!CameraProperty)
	{
		return false;
	}

	TestTrue(
		TEXT("Mesh Profile Camera reuses the ParameterTableRow schema"),
		CameraProperty->Struct == FComposableCameraParameterTableRow::StaticStruct());
	TestTrue(
		TEXT("Mesh Profile Camera fields render directly inside the Camera section"),
		CameraProperty->HasMetaData(TEXT("ShowOnlyInnerProperties")));
	TestTrue(
		TEXT("Mesh Profile Camera property uses the Camera category"),
		CameraProperty->GetMetaData(TEXT("Category")) == TEXT("Camera"));

	const FArrayProperty* ModifierProperty =
		FindFProperty<FArrayProperty>(
			UComposableCameraMeshProfile::StaticClass(),
			GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, ModifierAssets));
	TestNotNull(TEXT("Mesh Profile preserves ModifierAssets"), ModifierProperty);
	if (ModifierProperty)
	{
		TestTrue(
			TEXT("ModifierAssets uses the Modifier category"),
			ModifierProperty->GetMetaData(TEXT("Category")) == TEXT("Modifier"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshProfileParameterBlockTest,
	"ComposableCameraSystem.MeshCamera.ProfileParameterBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileParameterBlockTest::RunTest(
	const FString& /*Parameters*/)
{
	UComposableCameraTypeAsset* TypeAsset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());

	FComposableCameraExposedParameter DistanceParameter;
	DistanceParameter.ParameterName = TEXT("Distance");
	DistanceParameter.PinType = EComposableCameraPinType::Float;
	TypeAsset->ExposedParameters.Add(DistanceParameter);

	FComposableCameraInternalVariable ModeVariable;
	ModeVariable.VariableName = TEXT("ModeIndex");
	ModeVariable.VariableType = EComposableCameraPinType::Int32;
	ModeVariable.InitialValueString = TEXT("3");
	TypeAsset->ExposedVariables.Add(ModeVariable);

	FComposableCameraParameterTableRow CameraConfig;
	CameraConfig.Parameters.Values.Add(TEXT("Distance"), TEXT("420.5"));
	CameraConfig.Parameters.Values.Add(TEXT("ModeIndex"), TEXT("7"));

	FComposableCameraParameterBlock ParameterBlock;
	CameraConfig.BuildParameterBlock(
		*TypeAsset,
		ParameterBlock,
		TEXT("Mesh Profile automation test"));

	float Distance = 0.f;
	int32 ModeIndex = 0;
	TestTrue(
		TEXT("Mesh Profile parses exposed Camera parameter overrides"),
		ParameterBlock.Get(TEXT("Distance"), Distance));
	TestEqual(TEXT("Parsed Camera parameter value"), Distance, 420.5f);
	TestTrue(
		TEXT("Mesh Profile parses exposed Camera variable overrides"),
		ParameterBlock.Get(TEXT("ModeIndex"), ModeIndex));
	TestEqual(TEXT("Parsed Camera variable value"), ModeIndex, 7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshExpiredProfileCleanupGuardTest,
	"ComposableCameraSystem.MeshCamera.ExpiredCameraOnlyProfileCleanupGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshExpiredProfileCleanupGuardTest::RunTest(
	const FString& /*Parameters*/)
{
	using UE::ComposableCameras::Mesh::ShouldCreateTemporaryCameraContext;

	TestTrue(
		TEXT("Camera-only Layer still creates an owned Context cleanup handle"),
		ShouldCreateTemporaryCameraContext(true, false));
	TestFalse(
		TEXT("Existing owned Context is not duplicated while the Layer remains active"),
		ShouldCreateTemporaryCameraContext(true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshProfileCameraContextOwnershipPolicyTest,
	"ComposableCameraSystem.MeshCamera.LayerCameraContextOwnershipPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileCameraContextOwnershipPolicyTest::RunTest(
	const FString& /*Parameters*/)
{
	using UE::ComposableCameras::Mesh::ShouldCreateTemporaryCameraContext;

	TestTrue(
		TEXT("First Camera Layer creates a temporary Context"),
		ShouldCreateTemporaryCameraContext(true, false));
	TestTrue(
		TEXT("Nested Camera Layer creates its own temporary Context"),
		ShouldCreateTemporaryCameraContext(true, false));
	TestFalse(
		TEXT("An already-entered Layer does not create a second Context"),
		ShouldCreateTemporaryCameraContext(true, true));
	TestFalse(
		TEXT("Modifier-only Layer does not create a camera Context"),
		ShouldCreateTemporaryCameraContext(false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerExitModifierRefreshPolicyTest,
	"ComposableCameraSystem.MeshCamera.LayerExitModifierRefreshPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerExitModifierRefreshPolicyTest::RunTest(
	const FString& /*Parameters*/)
{
	using UE::ComposableCameras::Mesh::ShouldRefreshModifiersAfterLayerExit;
	using UE::ComposableCameras::Mesh::ShouldSyncModifierSelectionAfterLayerExit;

	TestFalse(
		TEXT("Active Camera Layer pop resumes a lower camera with the correct older Modifier set"),
		ShouldRefreshModifiersAfterLayerExit(true, true));
	TestTrue(
		TEXT("Non-active Layer exit refreshes the still-active camera"),
		ShouldRefreshModifiersAfterLayerExit(true, false));
	TestFalse(
		TEXT("Layer exit without Modifiers does not reactivate the camera"),
		ShouldRefreshModifiersAfterLayerExit(false, false));
	TestTrue(
		TEXT("Active Camera Layer pop synchronizes selection without rebuilding the lower camera"),
		ShouldSyncModifierSelectionAfterLayerExit(true, true));
	TestFalse(
		TEXT("Non-active Layer exit uses the full camera refresh path instead"),
		ShouldSyncModifierSelectionAfterLayerExit(true, false));
	return true;
}

#endif
