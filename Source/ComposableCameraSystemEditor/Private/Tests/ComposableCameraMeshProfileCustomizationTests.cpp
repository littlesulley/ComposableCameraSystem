// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Customizations/ComposableCameraMeshProfileCustomization.h"
#include "Customizations/ComposableCameraParameterTableRowCustomization.h"

#include "Actions/ComposableCameraMoveToAction.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Actions/ComposableCameraActionBase.h"
#include "DetailLayoutBuilder.h"
#include "HAL/PlatformTime.h"
#include "IDetailsView.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "PropertyPath.h"
#include "UObject/GCObject.h"
#include "Layout/Children.h"
#include "Widgets/SWidget.h"

namespace
{
	void GatherMeshProfileDetailNodes(
		const TSharedRef<IDetailTreeNode>& Node,
		TArray<TSharedRef<IDetailTreeNode>>& OutNodes)
	{
		OutNodes.Add(Node);

		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children, true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			GatherMeshProfileDetailNodes(Child, OutNodes);
		}
	}

	TSharedRef<IPropertyRowGenerator> MakeMeshProfileRowGenerator()
	{
		FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		TSharedRef<IPropertyRowGenerator> Generator = Module.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Generator->RegisterInstancedCustomPropertyLayout(UComposableCameraMeshProfile::StaticClass(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FComposableCameraMeshProfileCustomization::MakeInstance));
		Generator->RegisterInstancedCustomPropertyTypeLayout(FComposableCameraExposedParameterValues::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FComposableCameraParameterTableRowCustomization::MakeInstance));
		return Generator;
	}

	TArray<TSharedRef<IDetailTreeNode>> GatherMeshProfileRows(const TSharedRef<IPropertyRowGenerator>& Generator)
	{
		TArray<TSharedRef<IDetailTreeNode>> Nodes;
		for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes()) { GatherMeshProfileDetailNodes(Root, Nodes); }
		return Nodes;
	}

	bool ContainsMeshObjectPicker(const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == FName(TEXT("SObjectPropertyEntryBox"))) { return true; }
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (ContainsMeshObjectPicker(Children->GetChildAt(Index))) { return true; }
		}
		return false;
	}

	struct FMeshProfileDetailsRefreshState : FGCObject
	{
		TObjectPtr<UComposableCameraMeshProfile> Profile = NewObject<UComposableCameraMeshProfile>();
		TSharedPtr<IDetailsView> View;
		TSharedPtr<IPropertyHandle> TypeHandle;
		int32 LayoutBuilds = 0;

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			Collector.AddReferencedObject(Profile);
		}
		virtual FString GetReferencerName() const override { return TEXT("FMeshProfileDetailsRefreshState"); }
	};

	class FMeshProfileRefreshTestCustomization : public IDetailCustomization
	{
	public:
		explicit FMeshProfileRefreshTestCustomization(TWeakPtr<FMeshProfileDetailsRefreshState> InState)
			: State(InState), Customization(FComposableCameraMeshProfileCustomization::MakeInstance()) {}

		virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override
		{
			if (TSharedPtr<FMeshProfileDetailsRefreshState> Pinned = State.Pin())
			{
				++Pinned->LayoutBuilds;
				Pinned->TypeHandle = Builder.GetProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraMeshProfile, Type));
			}
			Customization->CustomizeDetails(Builder);
		}

	private:
		TWeakPtr<FMeshProfileDetailsRefreshState> State;
		TSharedRef<IDetailCustomization> Customization;
	};

	class FMeshProfileSwitchTypeCommand : public IAutomationLatentCommand
	{
	public:
		FMeshProfileSwitchTypeCommand(FAutomationTestBase* InTest,
			TSharedRef<FMeshProfileDetailsRefreshState> InState, EComposableCameraMeshProfileType InType)
			: Test(InTest), State(InState), Type(InType) {}

		virtual bool Update() override
		{
			if (!bEdited)
			{
				if (!State->TypeHandle.IsValid())
				{
					Test->AddError(TEXT("Profile Details did not provide a Type handle"));
					return true;
				}
				PreviousBuilds = State->LayoutBuilds;
				StartTime = FPlatformTime::Seconds();
				// Retain the current handle while the edit schedules its deferred rebuild.
				const TSharedPtr<IPropertyHandle> Handle = State->TypeHandle;
				if (Handle->SetValue(static_cast<uint8>(Type)) != FPropertyAccess::Success)
				{
					Test->AddError(TEXT("Profile Type edit failed"));
					return true;
				}
				bEdited = true;
				return false;
			}
			if (State->LayoutBuilds <= PreviousBuilds)
			{
				if (FPlatformTime::Seconds() - StartTime < 5.0) { return false; }
				Test->AddError(TEXT("Changing Type did not rebuild the existing Details view"));
				return true;
			}

			const TArray<FPropertyPath> Paths = State->View->GetPropertiesInOrderDisplayed();
			auto Count = [&Paths](const UStruct* Owner, FName Name)
			{
				int32 Found = 0;
				for (const FPropertyPath& Path : Paths)
				{
					const FProperty* Property = Path.GetLeafMostProperty().Property.Get();
					if (Property && Property->GetOwnerStruct() == Owner && Property->GetFName() == Name) { ++Found; }
				}
				return Found;
			};
			Test->TestEqual(TEXT("Type selection persisted"), State->Profile->Type, Type);
			Test->TestEqual(TEXT("Refreshed Camera picker"), Count(FComposableCameraParameterTableRow::StaticStruct(), TEXT("CameraType")), Type == EComposableCameraMeshProfileType::CameraType ? 1 : 0);
			Test->TestEqual(TEXT("Refreshed Modifier picker"), Count(UComposableCameraMeshProfile::StaticClass(), TEXT("ModifierAssets")), Type == EComposableCameraMeshProfileType::Modifier ? 1 : 0);
			Test->TestEqual(TEXT("Refreshed Action picker"), Count(FComposableCameraMeshActionConfig::StaticStruct(), TEXT("ActionAsset")), Type == EComposableCameraMeshProfileType::Action ? 1 : 0);
			Test->TestEqual(TEXT("Refreshed Patch picker"), Count(FComposableCameraMeshPatchConfig::StaticStruct(), TEXT("PatchAsset")), Type == EComposableCameraMeshProfileType::Patch ? 1 : 0);
			Test->TestEqual(TEXT("Refreshed Context stays hidden"), Count(FComposableCameraParameterTableRow::StaticStruct(), TEXT("ContextName")), 0);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		TSharedRef<FMeshProfileDetailsRefreshState> State;
		EComposableCameraMeshProfileType Type;
		int32 PreviousBuilds = 0;
		double StartTime = 0.0;
		bool bEdited = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshProfileDetailsLayoutTest,
	"ComposableCameraSystem.Editor.MeshCamera.ProfileDetailsLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileDetailsLayoutTest::RunTest(
	const FString& /*Parameters*/)
{
	UComposableCameraMeshProfile* Profile = NewObject<UComposableCameraMeshProfile>();
	for (EComposableCameraMeshProfileType Type : {EComposableCameraMeshProfileType::CameraType,
		EComposableCameraMeshProfileType::Modifier, EComposableCameraMeshProfileType::Action, EComposableCameraMeshProfileType::Patch})
	{
		Profile->Type = Type;
		const TSharedRef<IPropertyRowGenerator> Generator = MakeMeshProfileRowGenerator();
		Generator->SetObjects({Profile});
		const auto Nodes = GatherMeshProfileRows(Generator);
		auto Count = [&Nodes](const UStruct* Owner, FName Name)
		{
			int32 Found = 0;
			for (const auto& Node : Nodes)
			{
				const TSharedPtr<IPropertyHandle> Handle = Node->CreatePropertyHandle();
				const FProperty* Property = Handle.IsValid() ? Handle->GetProperty() : nullptr;
				if (Property && Property->GetOwnerStruct() == Owner && Property->GetFName() == Name) { ++Found; }
			}
			return Found;
		};
		TestEqual(TEXT("Exactly one Type selector"), Count(UComposableCameraMeshProfile::StaticClass(), TEXT("Type")), 1);
		TestEqual(TEXT("No redundant Camera parent"), Count(UComposableCameraMeshProfile::StaticClass(), TEXT("Camera")), 0);
		TestEqual(TEXT("No redundant Action parent"), Count(UComposableCameraMeshProfile::StaticClass(), TEXT("Action")), 0);
		TestEqual(TEXT("No redundant Patch parent"), Count(UComposableCameraMeshProfile::StaticClass(), TEXT("Patch")), 0);
		TestEqual(TEXT("Camera picker shown exclusively"), Count(FComposableCameraParameterTableRow::StaticStruct(), TEXT("CameraType")), Type == EComposableCameraMeshProfileType::CameraType ? 1 : 0);
		TestEqual(TEXT("Modifier array shown exclusively"), Count(UComposableCameraMeshProfile::StaticClass(), TEXT("ModifierAssets")), Type == EComposableCameraMeshProfileType::Modifier ? 1 : 0);
		TestEqual(TEXT("Action picker shown exclusively"), Count(FComposableCameraMeshActionConfig::StaticStruct(), TEXT("ActionAsset")), Type == EComposableCameraMeshProfileType::Action ? 1 : 0);
		TestEqual(TEXT("Patch picker shown exclusively"), Count(FComposableCameraMeshPatchConfig::StaticStruct(), TEXT("PatchAsset")), Type == EComposableCameraMeshProfileType::Patch ? 1 : 0);
		TestEqual(TEXT("Automatic Context name hidden"), Count(FComposableCameraParameterTableRow::StaticStruct(), TEXT("ContextName")), 0);
		TestEqual(TEXT("Layer-owned transient flag hidden"), Count(FComposableCameraActivateParams::StaticStruct(), TEXT("bIsTransient")), 0);
		TestEqual(TEXT("Layer-owned lifetime hidden"), Count(FComposableCameraActivateParams::StaticStruct(), TEXT("LifeTime")), 0);
		TestEqual(TEXT("Raw Action bindings hidden"), Count(FComposableCameraMeshActionConfig::StaticStruct(), TEXT("Bindings")), 0);
		TestEqual(TEXT("Default Camera Activation Params section hidden"), Count(FComposableCameraParameterTableRow::StaticStruct(), TEXT("ActivationParams")), 0);
		int32 ActivationGroups = 0;
		for (const auto& Node : Nodes)
		{
			if (Node->GetNodeName() == FName(TEXT("Activation"))) { ++ActivationGroups; }
		}
		TestEqual(TEXT("Exactly one Camera Activation group, none for other Types"), ActivationGroups, Type == EComposableCameraMeshProfileType::CameraType ? 1 : 0);
		if (Type == EComposableCameraMeshProfileType::CameraType)
		{
			TestEqual(TEXT("Camera Transition shown once"), Count(FComposableCameraParameterTableRow::StaticStruct(), TEXT("TransitionOverride")), 1);
			TestEqual(TEXT("Camera parameters shown once"), Count(FComposableCameraParameterTableRow::StaticStruct(), TEXT("Parameters")), 1);
			TestEqual(TEXT("Initial transform remains configurable"), Count(FComposableCameraActivateParams::StaticStruct(), TEXT("InitialTransform")), 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshProfileTypeRefreshTest,
	"ComposableCameraSystem.Editor.MeshCamera.ProfileTypeRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileTypeRefreshTest::RunTest(const FString&)
{
	const TSharedRef<FMeshProfileDetailsRefreshState> State = MakeShared<FMeshProfileDetailsRefreshState>();
	FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	State->View = Module.CreateDetailView(FDetailsViewArgs());
	const TWeakPtr<FMeshProfileDetailsRefreshState> WeakState = State;
	State->View->RegisterInstancedCustomPropertyLayout(UComposableCameraMeshProfile::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([WeakState]() -> TSharedRef<IDetailCustomization>
		{
			return MakeShared<FMeshProfileRefreshTestCustomization>(WeakState);
		}));
	State->View->RegisterInstancedCustomPropertyTypeLayout(FComposableCameraExposedParameterValues::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FComposableCameraParameterTableRowCustomization::MakeInstance));
	State->View->SetObject(State->Profile.Get());
	// A row generator rebuilds on RequestRefresh too; only a real Details view exposes this bug.
	// Let editor ticks process the refresh. Never call ForceRefresh manually in this regression.
	for (EComposableCameraMeshProfileType Type : {EComposableCameraMeshProfileType::Modifier,
		EComposableCameraMeshProfileType::Action, EComposableCameraMeshProfileType::Patch, EComposableCameraMeshProfileType::CameraType})
	{
		ADD_LATENT_AUTOMATION_COMMAND(FMeshProfileSwitchTypeCommand(this, State, Type));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshProfileGeneratedParametersTest,
	"ComposableCameraSystem.Editor.MeshCamera.GeneratedProfileParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileGeneratedParametersTest::RunTest(const FString&)
{
	UComposableCameraMeshProfile* Profile = NewObject<UComposableCameraMeshProfile>();
	UComposableCameraTypeAsset* Camera = NewObject<UComposableCameraTypeAsset>();
	UComposableCameraPatchTypeAsset* Patch = NewObject<UComposableCameraPatchTypeAsset>();
	UComposableCameraActionTypeAsset* Action = NewObject<UComposableCameraActionTypeAsset>();
	Action->ActionTemplate = NewObject<UComposableCameraMoveToAction>(Action);
	FComposableCameraExposedParameter Exposed;
	Exposed.ParameterName = TEXT("MeshDistance");
	Exposed.DisplayName = FText::FromString(TEXT("Mesh Distance"));
	Camera->ExposedParameters.Add(Exposed);
	Exposed.ParameterName = TEXT("MeshStrength");
	Exposed.DisplayName = FText::FromString(TEXT("Mesh Strength"));
	Patch->ExposedParameters.Add(Exposed);
	Profile->Camera.CameraType = Camera;
	Profile->Action.ActionAsset = Action;
	Profile->Patch.PatchAsset = Patch;
	for (EComposableCameraMeshProfileType Type : {EComposableCameraMeshProfileType::CameraType,
		EComposableCameraMeshProfileType::Action, EComposableCameraMeshProfileType::Patch})
	{
		Profile->Type = Type;
		const TSharedRef<IPropertyRowGenerator> Generator = MakeMeshProfileRowGenerator();
		Generator->SetObjects({Profile});
		TSet<FString> Labels;
		for (const auto& Node : GatherMeshProfileRows(Generator))
		{
			TArray<FString> FilterStrings;
			Node->GetFilterStrings(FilterStrings);
			for (const FString& Label : FilterStrings) { Labels.Add(Label); }
		}
		TestEqual(TEXT("Camera exposed row follows selected family"), Labels.Contains(TEXT("Mesh Distance")), Type == EComposableCameraMeshProfileType::CameraType);
		TestEqual(TEXT("Patch exposed row follows selected family"), Labels.Contains(TEXT("Mesh Strength")), Type == EComposableCameraMeshProfileType::Patch);
		TestEqual(TEXT("Action property row follows K2 exposure"), Labels.Contains(TEXT("Target Position")), Type == EComposableCameraMeshProfileType::Action);
		TestFalse(TEXT("Action base lifecycle stays outside exposed parameters"), Labels.Contains(TEXT("Duration")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshActionObjectPickerTest,
	"ComposableCameraSystem.Editor.MeshCamera.ActionObjectParameterPicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshActionObjectPickerTest::RunTest(const FString&)
{
	// Resolve the runtime reflection fixture without depending on that module's private headers.
	UClass* ActionClass = FindObject<UClass>(nullptr, TEXT("/Script/ComposableCameraSystem.ComposableCameraActionAssetTestAction"));
	if (!TestNotNull(TEXT("Object-input Action fixture exists"), ActionClass)) { return false; }
	UComposableCameraMeshProfile* Profile = NewObject<UComposableCameraMeshProfile>();
	Profile->Type = EComposableCameraMeshProfileType::Action;
	UComposableCameraActionTypeAsset* Asset = NewObject<UComposableCameraActionTypeAsset>();
	Asset->ActionTemplate = NewObject<UComposableCameraActionBase>(Asset, ActionClass);
	Profile->Action.ActionAsset = Asset;
	const TSharedRef<IPropertyRowGenerator> Generator = MakeMeshProfileRowGenerator();
	Generator->SetObjects({Profile});
	bool bFoundPicker = false;
	for (const auto& Node : GatherMeshProfileRows(Generator))
	{
		TArray<FString> Labels;
		Node->GetFilterStrings(Labels);
		if (Labels.Contains(TEXT("Asset Object")))
		{
			const FNodeWidgets Widgets = Node->CreateNodeWidgets();
			bFoundPicker = Widgets.ValueWidget.IsValid() && ContainsMeshObjectPicker(Widgets.ValueWidget.ToSharedRef());
			break;
		}
	}
	TestTrue(TEXT("Exposed Action Object input renders an asset picker"), bFoundPicker);
	TestTrue(TEXT("Building the picker preserves an unset override"), Profile->Action.Parameters.Values.IsEmpty());
	return true;
}

#endif
