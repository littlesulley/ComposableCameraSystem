// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Customizations/ComposableCameraMeshProfileCustomization.h"

#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

namespace
{
	void GatherMeshProfileDetailNodes(
		const TSharedRef<IDetailTreeNode>& Node,
		TArray<TSharedRef<IDetailTreeNode>>& OutNodes)
	{
		OutNodes.Add(Node);

		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			GatherMeshProfileDetailNodes(Child, OutNodes);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshProfileDetailsLayoutTest,
	"ComposableCameraSystem.Editor.MeshCamera.ProfileDetailsLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileDetailsLayoutTest::RunTest(
	const FString& /*Parameters*/)
{
	FPropertyEditorModule& PropertyEditorModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	const TSharedRef<IPropertyRowGenerator> RowGenerator =
		PropertyEditorModule.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
	RowGenerator->RegisterInstancedCustomPropertyLayout(
		UComposableCameraMeshProfile::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(
			&FComposableCameraMeshProfileCustomization::MakeInstance));

	UComposableCameraMeshProfile* Profile =
		NewObject<UComposableCameraMeshProfile>(GetTransientPackage());
	RowGenerator->SetObjects({ Profile });

	TArray<TSharedRef<IDetailTreeNode>> DetailNodes;
	for (const TSharedRef<IDetailTreeNode>& RootNode : RowGenerator->GetRootTreeNodes())
	{
		GatherMeshProfileDetailNodes(RootNode, DetailNodes);
	}

	int32 VisibleCameraParentRows = 0;
	int32 VisibleContextNameRows = 0;
	TMap<FName, int32> VisibleCameraChildCounts;
	const TSet<FName> ExpectedCameraChildren =
	{
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, CameraType),
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, TransitionOverride),
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, ActivationParams),
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, Parameters)
	};

	for (const TSharedRef<IDetailTreeNode>& DetailNode : DetailNodes)
	{
		const TSharedPtr<IPropertyHandle> PropertyHandle =
			DetailNode->CreatePropertyHandle();
		if (!PropertyHandle.IsValid() || !PropertyHandle->IsValidHandle())
		{
			continue;
		}

		const FProperty* Property = PropertyHandle->GetProperty();
		if (!Property)
		{
			continue;
		}

		if (Property->GetOwnerStruct() == UComposableCameraMeshProfile::StaticClass()
			&& Property->GetFName()
				== GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Camera))
		{
			++VisibleCameraParentRows;
		}
		else if (Property->GetOwnerStruct()
			== FComposableCameraParameterTableRow::StaticStruct()
			&& ExpectedCameraChildren.Contains(Property->GetFName()))
		{
			++VisibleCameraChildCounts.FindOrAdd(Property->GetFName());
		}
		else if (Property->GetOwnerStruct()
			== FComposableCameraParameterTableRow::StaticStruct()
			&& Property->GetFName()
				== GET_MEMBER_NAME_CHECKED(
					FComposableCameraParameterTableRow,
					ContextName))
		{
			++VisibleContextNameRows;
		}
	}

	TestEqual(
		TEXT("Mesh Profile hides the redundant Camera parent row"),
		VisibleCameraParentRows,
		0);
	TestEqual(
		TEXT("Mesh Profile exposes every expected Camera child"),
		VisibleCameraChildCounts.Num(),
		ExpectedCameraChildren.Num());
	for (const FName ExpectedChild : ExpectedCameraChildren)
	{
		TestEqual(
			*FString::Printf(
				TEXT("Camera child '%s' appears exactly once"),
				*ExpectedChild.ToString()),
			VisibleCameraChildCounts.FindRef(ExpectedChild),
			1);
	}
	TestEqual(
		TEXT("Mesh Profile hides Context Name because each Layer owns an automatic temporary Context"),
		VisibleContextNameRows,
		0);
	return true;
}

#endif
