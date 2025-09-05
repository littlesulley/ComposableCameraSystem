// Copyright Sulley. All rights reserved.

#include "Customizations/ComposableCameraContextParameterDetailsCustomization.h"

#include "ComposableCameraEditorStyle.h"
#include "ComposableCameraSystemEditorModule.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "Toolkits/ToolkitManager.h"
#include "ComposableCameraMacros.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Variables/ComposableCameraParameter.h"
#include "Variables/ComposableCameraVariable.h"
#include "Variables/ComposableCameraVariableCollection.h"
#include "Widgets/ComposableCameraVariablePicker.h"

#define LOCTEXT_NAMESPACE "ComposableCameraContextParameterDetailsCustomization"

void FComposableCameraContextParameterDetailsCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
#define COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(ValueType, ValueName) \
	PropertyEditorModule.RegisterCustomPropertyTypeLayout( \
		F##ValueName##ComposableCameraContextParameter::StaticStruct()->GetFName(), \
		FOnGetPropertyTypeCustomizationInstance::CreateLambda ( \
		 [] { return MakeShared<F##ValueName##ComposableCameraContextParameterDetailsCustomization>(); }));
	
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_ALL_TYPES()
#undef COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE
}

void FComposableCameraContextParameterDetailsCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
#define COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(ValueType, ValueName) \
	PropertyEditorModule.UnregisterCustomPropertyTypeLayout \
		(F##ValueName##ComposableCameraContextParameter::StaticStruct()->GetFName());
		COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_ALL_TYPES()
#undef COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE
	}
}

void FComposableCameraContextParameterDetailsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Gather up the things we need
	PropertyUtilities = CustomizationUtils.GetPropertyUtilities();
	StructProperty = PropertyHandle;

	// All context parameters should have a "Value" and "Variable" property
	ValueProperty = PropertyHandle->GetChildHandle(TEXT("Value"));
	VariableProperty = PropertyHandle->GetChildHandle(TEXT("Variable"));
	ensure(ValueProperty && VariableProperty);

	// Get the type of context variable we need for this context parameter
	VariableClass = nullptr;
	if (FObjectProperty* VariableObjectProperty = CastField<FObjectProperty>(VariableProperty->GetProperty()))
	{
		VariableClass = VariableObjectProperty->PropertyClass;
	}
	ensure(VariableClass);

	// Update our variable info once now. We will then update it each tick
	GetVariableCollectionUsedByOwnedCamera(false);
	UpdateVariableInfo();

	// Only build widget when this node is instantiated inside a composable camera, but for the CDO node itself
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	for (UObject* OuterObject : OuterObjects)
	{
		if (OuterObject->HasAnyFlags(RF_ClassDefaultObject))
		{
			bShouldTick = false;
			return;
		}
	}

	// Create the parameter value editor (float editor, vector editor, etc.)
	TSharedRef<SWidget> ValueWidget = ValueProperty->CreatePropertyValueWidgetWithCustomization(nullptr);
	ValueWidget->SetEnabled(TAttribute<bool>::CreateSP(this, &FComposableCameraContextParameterDetailsCustomization::IsValueEditorEnabled));

	// Create the whole UI layout
	TSharedRef<FComposableCameraEditorStyle> Style = FComposableCameraEditorStyle::Get();
	
	HeaderRow
	.NameContent()
	[
		StructProperty->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(100.0f)
	.HAlign(HAlign_Fill)
	[
		SAssignNew(LayoutBox, SHorizontalBox)
		+SHorizontalBox::Slot()
		.Padding(0)
		.FillWidth(1.f)
		[
			ValueWidget
		]
		+SHorizontalBox::Slot()
		.Padding(0)
		.AutoWidth()
		.HAlign(HAlign_Right)
		[
			ValueProperty->CreateDefaultPropertyButtonWidgets()
		]
		+SHorizontalBox::Slot()
		.Padding(2.f)
		.AutoWidth()
		[
			SAssignNew(VariableBrowserButton, SComboButton)
			.HasDownArrow(true)
			.ContentPadding(1.f)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.IsEnabled(true)
			.ToolTipText(this, &FComposableCameraContextParameterDetailsCustomization::GetCameraVariableBrowserToolTip)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(Style->GetBrush("CameraContextParameter.VariableBrowser"))
				]
				+SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.Padding(2.f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					.Visibility(this, &FComposableCameraContextParameterDetailsCustomization::GetVariableInfoTextVisibility)
					.MaxDesiredWidth(this, &FComposableCameraContextParameterDetailsCustomization::GetVariableInfoTextMaxWidth)
					[
						SNew(STextBlock)
						.Text(this, &FComposableCameraContextParameterDetailsCustomization::GetVariableInfoText)
						.MinDesiredWidth(20.f)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]
				+SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.Padding(2.f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					.Visibility(this, &FComposableCameraContextParameterDetailsCustomization::GetVariableErrorTextVisibility)
					.MaxDesiredWidth(this, &FComposableCameraContextParameterDetailsCustomization::GetVariableErrorTextMaxWidth)
					[
						SNew(STextBlock)
						.Text(this, &FComposableCameraContextParameterDetailsCustomization::GetVariableErrorText)
						.MinDesiredWidth(20.f)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.ColorAndOpacity(FStyleColors::Error)
					]
				]
			]
			.OnGetMenuContent(this, &FComposableCameraContextParameterDetailsCustomization::BuildCameraVariableBrowser)
		]
	];

	HeaderRow.OverrideResetToDefault(
		FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateSP(this, &FComposableCameraContextParameterDetailsCustomization::IsResetToDefaultVisible),
			FResetToDefaultHandler::CreateSP(this, &FComposableCameraContextParameterDetailsCustomization::OnResetToDefault)));
}

void FComposableCameraContextParameterDetailsCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	FPropertyAccess::Result Result = ValueProperty->GetNumChildren(NumChildren);
	if (Result == FPropertyAccess::Success)
	{
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			if (TSharedPtr<IPropertyHandle> ChildProperty = ValueProperty->GetChildHandle(ChildIndex))
			{
				ChildBuilder.AddProperty(ChildProperty.ToSharedRef());
			}
		}
	}
}

void FComposableCameraContextParameterDetailsCustomization::Tick(float DeltaTime)
{
	if (bShouldTick)
	{
		// Update variable collection used by the owned camera
		GetVariableCollectionUsedByOwnedCamera(true);
		UpdateVariableInfo();
	}
}

void FComposableCameraContextParameterDetailsCustomization::UpdateVariableInfo()
{
	VariableInfo = FComposableCameraVariableInfo();
	
	UObject* VariableObject = nullptr;
	FPropertyAccess::Result Result = VariableProperty->GetValue(VariableObject);
	if (Result == FPropertyAccess::Success)
	{
		if (VariableObject)
		{
			if (UComposableCameraVariable* Variable = Cast<UComposableCameraVariable>(VariableObject))
			{
				VariableInfo.VariableValue = EComposableCameraVariableValue::Set;
				VariableInfo.CommonVariable = Variable;
				VariableInfo.InfoText = Variable->DisplayName.IsEmpty() ?
					FText::FromName(Variable->GetFName()) :
					FText::FromString(Variable->DisplayName);
			}
			else
			{
				VariableInfo.VariableValue = EComposableCameraVariableValue::Invalid;
				VariableInfo.ErrorText = LOCTEXT("InvalidVariableObject", "Invalid Variable");
			}
		}
		else
		{
			VariableInfo.VariableValue = EComposableCameraVariableValue::NotSet;
			VariableInfo.CommonVariable = nullptr;
			VariableInfo.InfoText = FText();
			VariableInfo.ErrorText = FText();
		}
	}
	else if (Result == FPropertyAccess::MultipleValues)
	{
		VariableInfo.VariableValue = EComposableCameraVariableValue::MultipleSet;
		VariableInfo.InfoText = LOCTEXT("MultipleVariableValues", "Multiple Variables");
	}
	else
	{
		VariableInfo.VariableValue = EComposableCameraVariableValue::Invalid;
		VariableInfo.ErrorText = LOCTEXT("ErrorReadingVariable", "Error Reading Variable");
	}
}

void FComposableCameraContextParameterDetailsCustomization::GetVariableCollectionUsedByOwnedCamera(bool bExecuteOnChange)
{
	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);

	UComposableCameraVariableCollection* CurrentCollectionUsedByCamera = nullptr;
	
	if (UObject* Node = OuterObjects[0])
	{
		if (AComposableCameraCameraBase* Camera = Cast<AComposableCameraCameraBase>(Node->GetOuter()))
		{
			if (Camera->ContextVariables.IsPending())
			{
				Camera->ContextVariables.LoadSynchronous();
			}
			if (Camera->ContextVariables.IsValid())
			{
				CurrentCollectionUsedByCamera = Camera->ContextVariables.Get();
			}
		}
	}

	if (VariableCollectionUsedByCamera != CurrentCollectionUsedByCamera)
	{
		if (bExecuteOnChange)
		{
			OnVariableCollectionUsedByCameraChanged();
		}
		VariableCollectionUsedByCamera = CurrentCollectionUsedByCamera;
	}
}

bool FComposableCameraContextParameterDetailsCustomization::OnShouldFilterAsset(const FAssetData& AssetData)
{
	return AssetData != VariableCollectionUsedByCamera;
}

TSharedRef<SWidget> FComposableCameraContextParameterDetailsCustomization::BuildCameraVariableBrowser()
{
	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	constexpr bool bCloseSelfOnly = true;
	constexpr bool bSearchable = false;
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, bCloseSelfOnly, &FCoreStyle::Get(), bSearchable);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("CameraVariableOperations", "Current Parameter"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenVariableCollection"/*"GoToVariable"*/, "Open Variable Collection"/*"Go to variable"*/),
		LOCTEXT("OpenVariableCollection_Tooltip"/*"GoToVariable_ToolTip"*/, "Open the referenced camera variable collection."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.BrowseContent"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FComposableCameraContextParameterDetailsCustomization::OnOpenVariableCollection/*OnGoToVariable*/),
			FCanExecuteAction::CreateSP(this, &FComposableCameraContextParameterDetailsCustomization::CanOpenVariableCollection))
		);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("ClearVariable", "Clear variable"),
		LOCTEXT("ClearVariable_ToolTip", "Clears the variable from the camera parameter."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Delete"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FComposableCameraContextParameterDetailsCustomization::OnClearVariable),
			FCanExecuteAction::CreateSP(this, &FComposableCameraContextParameterDetailsCustomization::CanClearVariable))
		);
	MenuBuilder.EndSection();

	FComposableCameraVariablePickerConfig PickerConfig;
	PickerConfig.ComposableCameraVariableClass = VariableClass;
	PickerConfig.InitialComposableCameraVariableSelection = VariableInfo.CommonVariable;
	PickerConfig.ComposableCameraVariableCollectionSaveSettingsName = TEXT("ComposableCameraParameterVariablePropertyPicker");
	PickerConfig.OnCameraVariableSelected = FOnCameraVariableSelected::CreateSP(
			this, &FComposableCameraContextParameterDetailsCustomization::OnSetVariable);
	PickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(
		this, &FComposableCameraContextParameterDetailsCustomization::OnShouldFilterAsset);
	FComposableCameraSystemEditorModule& EditorModule = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	TSharedRef<SWidget> PickerWidget = EditorModule.CreateCameraVariablePicker(PickerConfig);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("CameraVariableBrowser", "Browse"));
	{
		TSharedRef<SWidget> VariableBrowser = SNew(SBox)
			.MinDesiredWidth(300.f)
			.MinDesiredHeight(300.f)
			[
				PickerWidget
			];
		MenuBuilder.AddWidget(VariableBrowser, FText(), true, false);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

bool FComposableCameraContextParameterDetailsCustomization::IsValueEditorEnabled() const
{
	return VariableInfo.VariableValue == EComposableCameraVariableValue::NotSet;
}

FText FComposableCameraContextParameterDetailsCustomization::GetCameraVariableBrowserToolTip() const
{
	return LOCTEXT(
				"SetVariable_ToolTip", 
				"Selects a camera variable to drive this parameter.");
}

FText FComposableCameraContextParameterDetailsCustomization::GetVariableInfoText() const
{
	return VariableInfo.InfoText;
}

EVisibility FComposableCameraContextParameterDetailsCustomization::GetVariableInfoTextVisibility() const
{
	const bool bShowVariableInfoText = !VariableInfo.InfoText.IsEmpty();
	return bShowVariableInfoText ? EVisibility::Visible : EVisibility::Collapsed;
}

FOptionalSize FComposableCameraContextParameterDetailsCustomization::GetVariableInfoTextMaxWidth() const
{
	constexpr float FixedSpace = 1.f + (2.f+ 16.f + 2.f) + (2.f + 16.f + 2.f) + 1.f;

	const bool bShowVariableInfoText = !VariableInfo.InfoText.IsEmpty();
	const float LayoutBoxWidth = LayoutBox ? LayoutBox->GetPaintSpaceGeometry().GetLocalSize().X : 0.f;
	return bShowVariableInfoText ? FOptionalSize((LayoutBoxWidth - FixedSpace) / 3.f) : FOptionalSize(0);
}

FText FComposableCameraContextParameterDetailsCustomization::GetVariableErrorText() const
{
	return VariableInfo.ErrorText;
}

EVisibility FComposableCameraContextParameterDetailsCustomization::GetVariableErrorTextVisibility() const
{
	const bool bShowVariableErrorText = !VariableInfo.ErrorText.IsEmpty();
	return bShowVariableErrorText ? EVisibility::Visible : EVisibility::Collapsed;
}

FOptionalSize FComposableCameraContextParameterDetailsCustomization::GetVariableErrorTextMaxWidth() const
{
	// See comments in GetVariableInfoTextMaxWidth.
	constexpr float FixedSpace = 1.f + (2.f+ 16.f + 2.f) + (2.f + 16.f + 2.f) + 1.f;

	const bool bShowVariableErrorText = !VariableInfo.ErrorText.IsEmpty();
	const float LayoutBoxWidth = LayoutBox ? LayoutBox->GetPaintSpaceGeometry().GetLocalSize().X : 0.f;
	return bShowVariableErrorText ? FOptionalSize((LayoutBoxWidth - FixedSpace) / 3.f) : FOptionalSize(0);
}

bool FComposableCameraContextParameterDetailsCustomization::CanOpenVariableCollection() const
{
	UObject* VariableObject = nullptr;
	FPropertyAccess::Result PropertyAccessResult = VariableProperty->GetValue(VariableObject);
	return VariableObject && PropertyAccessResult == FPropertyAccess::Success;
}

void FComposableCameraContextParameterDetailsCustomization::OnOpenVariableCollection() const 
{
	UObject* VariableObject = nullptr;
	FPropertyAccess::Result PropertyAccessResult = VariableProperty->GetValue(VariableObject);
	if (VariableObject && PropertyAccessResult == FPropertyAccess::Success)
	{
		UComposableCameraVariableCollection* VariableCollection = VariableObject->GetTypedOuter<UComposableCameraVariableCollection>();
		if (VariableCollection)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(VariableCollection);
		}
	}
}

void FComposableCameraContextParameterDetailsCustomization::OnSetVariable(UComposableCameraVariable* InVariable)
{
	TArray<void*> RawData;
	StructProperty->AccessRawData(RawData);

	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);

	check(!OuterObjects.Num() || (OuterObjects.Num() == RawData.Num() && RawData.Num() == 1));

	{
		FScopedTransaction Transaction(FText::Format(LOCTEXT("SetPropertyValue", "Set {0}"), StructProperty->GetPropertyDisplayName()));
		StructProperty->NotifyPreChange();

		for (int32 ValueIndex = 0; ValueIndex < RawData.Num(); ++ValueIndex)
		{
			SetParameterVariable(RawData[ValueIndex], InVariable);
		}

		StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	}

	FPropertyChangedEvent ChangeEvent(StructProperty->GetProperty(), EPropertyChangeType::ValueSet, OuterObjects);
	PropertyUtilities->NotifyFinishedChangingProperties(ChangeEvent);
	PropertyUtilities->RequestForceRefresh();
	VariableBrowserButton->SetIsOpen(false);
}

void FComposableCameraContextParameterDetailsCustomization::OnVariableCollectionUsedByCameraChanged()
{
	OnSetVariable(nullptr);
}

bool FComposableCameraContextParameterDetailsCustomization::CanClearVariable() const
{
	return VariableInfo.CommonVariable != nullptr;
}

void FComposableCameraContextParameterDetailsCustomization::OnClearVariable()
{
	OnSetVariable(nullptr);
}

bool FComposableCameraContextParameterDetailsCustomization::IsResetToDefaultVisible(
	TSharedPtr<IPropertyHandle> InPropertyHandle) const
{
	return ValueProperty->CanResetToDefault() || VariableProperty->CanResetToDefault();
}

void FComposableCameraContextParameterDetailsCustomization::OnResetToDefault(
	TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	ValueProperty->ResetToDefault();
	VariableProperty->ResetToDefault();
	PropertyUtilities->RequestForceRefresh();
}

#define COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(ValueType, ValueName) \
bool F##ValueName##ComposableCameraContextParameterDetailsCustomization::HasNonUserOverride(void* InRawData) \
{ \
	F##ValueName##ComposableCameraContextParameter* Parameter = (F##ValueName##ComposableCameraContextParameter*)InRawData; \
	return Parameter->HasNonUserOverride(); \
} \
void F##ValueName##ComposableCameraContextParameterDetailsCustomization::SetParameterVariable(void* InRawData, UComposableCameraVariable* InVariable) \
{ \
	F##ValueName##ComposableCameraContextParameter* Parameter = (F##ValueName##ComposableCameraContextParameter*)InRawData; \
	Parameter->Variable = CastChecked<U##ValueName##ComposableCameraVariable>(InVariable, ECastCheckedType::NullAllowed); \
	Parameter->VariableID = InVariable ? InVariable->GetVariableID() : FComposableCameraVariableID(); \
}
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_ALL_TYPES()
#undef COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE

#undef LOCTEXT_NAMESPACE