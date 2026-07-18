// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraModifierDetails.h"

#include "ClassViewerFilter.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "IPropertyUtilities.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComposableCameraModifierDetails"

namespace
{
	class FComposableCameraCustomModifierClassFilter final : public IClassViewerFilter
	{
	public:
		virtual bool IsClassAllowed(
			const FClassViewerInitializationOptions& InInitOptions,
			const UClass* InClass,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InClass
				&& InClass != UComposableCameraModifierBase::StaticClass()
				&& InClass->IsChildOf(UComposableCameraModifierBase::StaticClass())
				&& !InClass->HasAnyClassFlags(
					CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
		}

		virtual bool IsUnloadedClassAllowed(
			const FClassViewerInitializationOptions& InInitOptions,
			const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(UComposableCameraModifierBase::StaticClass())
				&& !InUnloadedClassData->HasAnyClassFlags(
					CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
		}
	};
}

TSharedRef<IDetailCustomization> FComposableCameraModifierDetails::MakeInstance()
{
	return MakeShared<FComposableCameraModifierDetails>();
}

void FComposableCameraModifierDetails::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomClassLayout(
		UComposableCameraNodeModifierDataAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FComposableCameraModifierDetails::MakeInstance));
}

void FComposableCameraModifierDetails::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomClassLayout(
			UComposableCameraNodeModifierDataAsset::StaticClass()->GetFName());
	}
}

void FComposableCameraModifierDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	WeakPropertyUtilities = DetailBuilder.GetPropertyUtilities().ToWeakPtr();
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
	ModifierAsset = CustomizedObjects.Num() == 1
		? Cast<UComposableCameraNodeModifierDataAsset>(CustomizedObjects[0].Get())
		: nullptr;

	const TSharedPtr<IPropertyHandle> ModifiersHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UComposableCameraNodeModifierDataAsset, Modifiers));
	if (!ModifiersHandle.IsValid() || !ModifiersHandle->IsValidHandle())
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Node Override"),
		LOCTEXT("NodeOverrideCategory", "Node Override"),
		ECategoryPriority::Important);

	TSharedRef<FDetailArrayBuilder> ModifiersBuilder = MakeShared<FDetailArrayBuilder>(
		ModifiersHandle.ToSharedRef(),
		/* bGenerateHeader = */ true,
		/* bDisplayResetToDefault = */ true,
		/* bDisplayElementNum = */ true);
	ModifiersBuilder->SetDisplayName(LOCTEXT("ModifiersArray", "Modifiers"));
	ModifiersBuilder->OnGenerateArrayElementWidget(
		FOnGenerateArrayElementWidget::CreateSP(
			this, &FComposableCameraModifierDetails::GenerateModifierElement));
	Category.AddCustomBuilder(ModifiersBuilder);
}

void FComposableCameraModifierDetails::GenerateModifierElement(
	TSharedRef<IPropertyHandle> ElementHandle,
	int32 ArrayIndex,
	IDetailChildrenBuilder& ChildrenBuilder)
{
	const TWeakPtr<IPropertyUtilities> LocalWeakUtilities = WeakPropertyUtilities;
	UComposableCameraModifierBase* Modifier = EnsureWrapperForElement(ElementHandle, ArrayIndex);
	IDetailPropertyRow& ElementRow = ChildrenBuilder.AddProperty(ElementHandle);
	if (!Modifier)
	{
		return;
	}
	ElementHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(
		[LocalWeakUtilities]()
		{
			RefreshDetails(LocalWeakUtilities);
		}));

	// Repair entries authored through the old default inline layout while this
	// customization was registered on the nested UObject class. That UI wrote
	// NodeClass but never created NodeTemplate, so runtime application and the
	// parameter rows had no data source.
	if (!Modifier->bUseCustomModifierClass && !Modifier->NodeTemplate && Modifier->NodeClass)
	{
		UClass* SelectedClass = Modifier->NodeClass.Get();
		if (SelectedClass
			&& SelectedClass->IsChildOf(UComposableCameraCameraNodeBase::StaticClass())
			&& !SelectedClass->HasAnyClassFlags(CLASS_Abstract))
		{
			Modifier->NodeTemplate = NewObject<UComposableCameraCameraNodeBase>(
				Modifier, SelectedClass, NAME_None, RF_Transactional);
			Modifier->MarkPackageDirty();
		}
	}

	// Keep the array element row for index/insert/delete/reorder controls, but
	// replace its old polymorphic class picker with a compact mode summary.
	ElementRow.CustomWidget(/* bShowChildren = */ false)
	.NameContent()
	[
		ElementHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(300.f)
	[
		SNew(STextBlock)
		.Text_Lambda([WeakModifier = TWeakObjectPtr<UComposableCameraModifierBase>(Modifier)]()
		{
			return GetModeSummary(WeakModifier);
		})
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	const TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier = Modifier;
	ChildrenBuilder.AddCustomRow(LOCTEXT("UseCustomModifierClassFilter", "Use Custom Modifier Class"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("UseCustomModifierClass", "Use Custom Modifier Class"))
		.ToolTipText(LOCTEXT("UseCustomModifierClassTooltip",
			"Unchecked: select a node type and override checked fields. Checked: select a custom Modifier subclass."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([WeakModifier]()
		{
			return GetModeCheckState(WeakModifier);
		})
		.OnCheckStateChanged_Lambda([WeakModifier, LocalWeakUtilities](ECheckBoxState NewState)
		{
			OnModeCheckStateChanged(WeakModifier, LocalWeakUtilities, NewState);
		})
	];

	if (Modifier->bUseCustomModifierClass)
	{
		AddCustomModifierRows(Modifier, ArrayIndex, ChildrenBuilder);
	}
	else
	{
		AddGenericModifierRows(Modifier, ArrayIndex, ChildrenBuilder);
	}
}

UComposableCameraModifierBase* FComposableCameraModifierDetails::EnsureWrapperForElement(
	const TSharedRef<IPropertyHandle>& ElementHandle,
	int32 ArrayIndex) const
{
	UComposableCameraNodeModifierDataAsset* Asset = ModifierAsset.Get();
	if (!Asset || !Asset->Modifiers.IsValidIndex(ArrayIndex))
	{
		// Multi-object Details cannot safely mutate one shared array slot. Exact
		// base entries can still use the custom layout; other values retain UE's
		// default row until edited as a single asset.
		UObject* ElementObject = nullptr;
		if (ElementHandle->GetValue(ElementObject) == FPropertyAccess::Success)
		{
			UComposableCameraModifierBase* Existing =
				Cast<UComposableCameraModifierBase>(ElementObject);
			return Existing
				&& Existing->GetClass() == UComposableCameraModifierBase::StaticClass()
				? Existing
				: nullptr;
		}
		return nullptr;
	}

	UComposableCameraModifierBase* Existing = Asset->Modifiers[ArrayIndex].Get();
	if (Existing && Existing->GetClass() == UComposableCameraModifierBase::StaticClass())
	{
		return Existing;
	}

	// IPropertyHandle::SetValue deliberately fails for EditInlineNew object
	// nodes in UE5.6. Normalize the authoritative asset slot directly instead.
	Asset->Modify();
	UComposableCameraModifierBase* Wrapper = NewObject<UComposableCameraModifierBase>(
		Asset, UComposableCameraModifierBase::StaticClass(), NAME_None, RF_Transactional);
	if (Existing)
	{
		UComposableCameraModifierBase* CustomModifier = DuplicateObject<UComposableCameraModifierBase>(
			Existing, Wrapper);
		if (!CustomModifier)
		{
			return nullptr;
		}
		CustomModifier->SetFlags(RF_Transactional);
		Wrapper->bUseCustomModifierClass = true;
		Wrapper->CustomModifier = CustomModifier;
	}

	Asset->Modifiers[ArrayIndex] = Wrapper;
	Asset->MarkPackageDirty();
	return Wrapper;
}

FText FComposableCameraModifierDetails::GetModeSummary(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier)
{
	const UComposableCameraModifierBase* Modifier = WeakModifier.Get();
	if (!Modifier)
	{
		return FText::GetEmpty();
	}
	if (Modifier->bUseCustomModifierClass)
	{
		return Modifier->CustomModifier
			? Modifier->CustomModifier->GetClass()->GetDisplayNameText()
			: LOCTEXT("CustomModifierModeSummary", "Custom Modifier Class");
	}
	return Modifier->NodeTemplate
		? Modifier->NodeTemplate->GetClass()->GetDisplayNameText()
		: LOCTEXT("NodeTypeModeSummary", "Node Type");
}

ECheckBoxState FComposableCameraModifierDetails::GetModeCheckState(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier)
{
	if (const UComposableCameraModifierBase* Modifier = WeakModifier.Get())
	{
		return Modifier->bUseCustomModifierClass
			? ECheckBoxState::Checked
			: ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Undetermined;
}

void FComposableCameraModifierDetails::OnModeCheckStateChanged(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities,
	ECheckBoxState NewState)
{
	UComposableCameraModifierBase* Modifier = WeakModifier.Get();
	if (!Modifier || NewState == ECheckBoxState::Undetermined)
	{
		return;
	}

	const bool bUseCustomModifierClass = NewState == ECheckBoxState::Checked;
	if (Modifier->bUseCustomModifierClass == bUseCustomModifierClass)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ChangeModifierMode", "Change Modifier Mode"));
	Modifier->Modify();
	Modifier->bUseCustomModifierClass = bUseCustomModifierClass;
	Modifier->MarkPackageDirty();
	RefreshDetails(WeakPropertyUtilities);
}

void FComposableCameraModifierDetails::AddGenericModifierRows(
	UComposableCameraModifierBase* Modifier,
	int32 ArrayIndex,
	IDetailChildrenBuilder& ChildrenBuilder) const
{
	const TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier = Modifier;
	const TWeakPtr<IPropertyUtilities> LocalWeakUtilities = WeakPropertyUtilities;

	ChildrenBuilder.AddCustomRow(LOCTEXT("NodeTypeFilter", "Node Type"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("NodeType", "Node Type"))
		.ToolTipText(LOCTEXT("NodeTypeTooltip",
			"Camera node type to override. Includes built-in nodes and Blueprint subclasses."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(300.f)
	[
		SNew(SClassPropertyEntryBox)
		.MetaClass(UComposableCameraCameraNodeBase::StaticClass())
		.SelectedClass_Lambda([WeakModifier]() -> const UClass*
		{
			const UComposableCameraModifierBase* CurrentModifier = WeakModifier.Get();
			return CurrentModifier && CurrentModifier->NodeTemplate
				? CurrentModifier->NodeTemplate->GetClass()
				: nullptr;
		})
		.OnSetClass_Lambda([WeakModifier, LocalWeakUtilities](const UClass* SelectedClass)
		{
			OnNodeClassSelected(WeakModifier, LocalWeakUtilities, SelectedClass);
		})
		.AllowNone(false)
	];

	UComposableCameraCameraNodeBase* NodeTemplate = Modifier->NodeTemplate;
	if (!NodeTemplate)
	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("SelectNodeTypeFilter", "Select Node Type"))
	.WholeRowContent()
	[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectNodeType", "Select a node type to expose its parameters."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		];
		return;
	}

	const TArray<UObject*> ExternalObjects { NodeTemplate };
	for (TFieldIterator<FProperty> PropertyIt(NodeTemplate->GetClass()); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!UComposableCameraModifierBase::IsNodePropertyOverridable(Property))
		{
			continue;
		}

		const FName PropertyName = Property->GetFName();
		FAddPropertyParams AddParams;
		AddParams.UniqueId(FName(*FString::Printf(
			TEXT("Modifier_%d_%s"), ArrayIndex, *PropertyName.ToString())));
		IDetailPropertyRow* Row = ChildrenBuilder.AddExternalObjectProperty(
			ExternalObjects, PropertyName, AddParams);
		if (!Row)
		{
			continue;
		}

		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> ValueWidget;
		Row->GetDefaultWidgets(NameWidget, ValueWidget);
		if (!ValueWidget.IsValid())
		{
			continue;
		}

		const TSharedPtr<IPropertyHandle> PropertyHandle = Row->GetPropertyHandle();
		Row->CustomWidget(/* bShowChildren = */ true)
		.NameContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([WeakModifier, PropertyName]()
				{
					return GetOverrideCheckState(WeakModifier, PropertyName);
				})
				.OnCheckStateChanged_Lambda([WeakModifier, PropertyName](ECheckBoxState NewState)
				{
					OnOverrideCheckStateChanged(WeakModifier, NewState, PropertyName);
				})
				.ToolTipText(LOCTEXT("OverrideTooltip",
					"When checked, this value replaces the matching runtime camera-node property "
					"and wins over its graph wire or exposed parameter."))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(Property->GetDisplayNameText())
				.ToolTipText(Property->GetToolTipText())
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.IsEnabled_Lambda([WeakModifier, PropertyHandle, PropertyName]()
			{
				return PropertyHandle.IsValid() && PropertyHandle->IsEditable()
					&& GetOverrideCheckState(WeakModifier, PropertyName) == ECheckBoxState::Checked;
			})
			[ValueWidget.ToSharedRef()]
		];
	}
}

void FComposableCameraModifierDetails::AddCustomModifierRows(
	UComposableCameraModifierBase* Modifier,
	int32 ArrayIndex,
	IDetailChildrenBuilder& ChildrenBuilder) const
{
	const TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier = Modifier;
	const TWeakPtr<IPropertyUtilities> LocalWeakUtilities = WeakPropertyUtilities;
	const TSharedRef<FComposableCameraCustomModifierClassFilter> ClassFilter =
		MakeShared<FComposableCameraCustomModifierClassFilter>();

	ChildrenBuilder.AddCustomRow(LOCTEXT("CustomModifierClassFilter", "Custom Modifier Class"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("CustomModifierClass", "Custom Modifier Class"))
		.ToolTipText(LOCTEXT("CustomModifierClassTooltip",
			"User-authored Blueprint or C++ subclass of ComposableCameraModifierBase."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(300.f)
	[
		SNew(SClassPropertyEntryBox)
		.MetaClass(UComposableCameraModifierBase::StaticClass())
		.SelectedClass_Lambda([WeakModifier]() -> const UClass*
		{
			const UComposableCameraModifierBase* CurrentModifier = WeakModifier.Get();
			return CurrentModifier && CurrentModifier->CustomModifier
				? CurrentModifier->CustomModifier->GetClass()
				: nullptr;
		})
		.OnSetClass_Lambda([WeakModifier, LocalWeakUtilities](const UClass* SelectedClass)
		{
			OnCustomModifierClassSelected(WeakModifier, LocalWeakUtilities, SelectedClass);
		})
		.ClassViewerFilters({ ClassFilter })
		.AllowAbstract(false)
		.AllowNone(false)
	];

	UComposableCameraModifierBase* CustomModifier = Modifier->CustomModifier.Get();
	if (!CustomModifier)
	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("SelectCustomModifierClassFilter", "Select Custom Modifier Class"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectCustomModifierClass",
				"Select a custom Modifier class to expose its fields."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		];
		return;
	}

	const TWeakObjectPtr<UComposableCameraModifierBase> WeakCustomModifier = CustomModifier;
	ChildrenBuilder.AddCustomRow(LOCTEXT("CustomTargetNodeClassFilter", "Node Class"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("CustomTargetNodeClass", "Node Class"))
		.ToolTipText(LOCTEXT("CustomTargetNodeClassTooltip",
			"Exact camera-node class passed to this custom modifier."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(300.f)
	[
		SNew(SClassPropertyEntryBox)
		.MetaClass(UComposableCameraCameraNodeBase::StaticClass())
		.SelectedClass_Lambda([WeakCustomModifier]() -> const UClass*
		{
			const UComposableCameraModifierBase* CurrentModifier = WeakCustomModifier.Get();
			return CurrentModifier ? CurrentModifier->NodeClass.Get() : nullptr;
		})
		.OnSetClass_Lambda([WeakCustomModifier](const UClass* SelectedClass)
		{
			OnCustomTargetNodeClassSelected(WeakCustomModifier, SelectedClass);
		})
		.AllowAbstract(false)
		.AllowNone(false)
	];

	const FName ModePropertyName = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierBase, bUseCustomModifierClass);
	const FName CustomObjectPropertyName = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierBase, CustomModifier);
	const FName NodeTemplatePropertyName = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierBase, NodeTemplate);
	const FName OverridePropertiesName = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierBase, OverrideProperties);
	const FName NodeClassPropertyName = GET_MEMBER_NAME_CHECKED(
		UComposableCameraModifierBase, NodeClass);
	const TArray<UObject*> ExternalObjects { CustomModifier };

	for (TFieldIterator<FProperty> PropertyIt(CustomModifier->GetClass()); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		const FName PropertyName = Property->GetFName();
		if (!Property->HasAnyPropertyFlags(CPF_Edit)
			|| Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated)
			|| PropertyName == ModePropertyName
			|| PropertyName == CustomObjectPropertyName
			|| PropertyName == NodeTemplatePropertyName
			|| PropertyName == OverridePropertiesName
			|| PropertyName == NodeClassPropertyName)
		{
			continue;
		}

		FAddPropertyParams AddParams;
		AddParams.UniqueId(FName(*FString::Printf(
			TEXT("CustomModifier_%d_%s"), ArrayIndex, *PropertyName.ToString())));
		if (IDetailPropertyRow* Row = ChildrenBuilder.AddExternalObjectProperty(
			ExternalObjects, PropertyName, AddParams))
		{
			// Custom modifier objects are authored as asset templates. Keep their
			// EditDefaultsOnly fields writable even though they are nested objects.
			Row->IsEnabled(true);
		}
	}
}

ECheckBoxState FComposableCameraModifierDetails::GetOverrideCheckState(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
	FName PropertyName)
{
	if (const UComposableCameraModifierBase* Modifier = WeakModifier.Get())
	{
		return Modifier->OverrideProperties.Contains(PropertyName)
			? ECheckBoxState::Checked
			: ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Undetermined;
}

void FComposableCameraModifierDetails::OnOverrideCheckStateChanged(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
	ECheckBoxState NewState,
	FName PropertyName)
{
	UComposableCameraModifierBase* Modifier = WeakModifier.Get();
	if (!Modifier || NewState == ECheckBoxState::Undetermined)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ToggleModifierPropertyOverride", "Toggle Node Property Override"));
	Modifier->Modify();
	if (NewState == ECheckBoxState::Checked)
	{
		Modifier->OverrideProperties.Add(PropertyName);
	}
	else
	{
		Modifier->OverrideProperties.Remove(PropertyName);
	}
	Modifier->MarkPackageDirty();
}

void FComposableCameraModifierDetails::OnNodeClassSelected(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities,
	const UClass* SelectedClass)
{
	UComposableCameraModifierBase* Modifier = WeakModifier.Get();
	if (!Modifier || !SelectedClass
		|| !SelectedClass->IsChildOf(UComposableCameraCameraNodeBase::StaticClass())
		|| SelectedClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return;
	}
	if (Modifier->NodeTemplate && Modifier->NodeTemplate->GetClass() == SelectedClass)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SelectModifierNodeType", "Select Modifier Node Type"));
	Modifier->Modify();
	UComposableCameraCameraNodeBase* NewTemplate = NewObject<UComposableCameraCameraNodeBase>(
		Modifier, const_cast<UClass*>(SelectedClass), NAME_None, RF_Transactional);
	if (!NewTemplate)
	{
		return;
	}
	Modifier->NodeTemplate = NewTemplate;
	Modifier->NodeClass = const_cast<UClass*>(SelectedClass);
	Modifier->OverrideProperties.Reset();
	Modifier->MarkPackageDirty();
	RefreshDetails(WeakPropertyUtilities);
}

void FComposableCameraModifierDetails::OnCustomModifierClassSelected(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakModifier,
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities,
	const UClass* SelectedClass)
{
	UComposableCameraModifierBase* Modifier = WeakModifier.Get();
	if (!Modifier || !SelectedClass
		|| SelectedClass == UComposableCameraModifierBase::StaticClass()
		|| !SelectedClass->IsChildOf(UComposableCameraModifierBase::StaticClass())
		|| SelectedClass->HasAnyClassFlags(
			CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return;
	}
	if (Modifier->CustomModifier
		&& Modifier->CustomModifier->GetClass() == SelectedClass)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT(
		"SelectCustomModifierClass", "Select Custom Modifier Class"));
	Modifier->Modify();
	UComposableCameraModifierBase* NewCustomModifier = NewObject<UComposableCameraModifierBase>(
		Modifier, const_cast<UClass*>(SelectedClass), NAME_None, RF_Transactional);
	if (!NewCustomModifier)
	{
		return;
	}
	Modifier->CustomModifier = NewCustomModifier;
	Modifier->MarkPackageDirty();
	RefreshDetails(WeakPropertyUtilities);
}

void FComposableCameraModifierDetails::OnCustomTargetNodeClassSelected(
	TWeakObjectPtr<UComposableCameraModifierBase> WeakCustomModifier,
	const UClass* SelectedClass)
{
	UComposableCameraModifierBase* CustomModifier = WeakCustomModifier.Get();
	if (!CustomModifier || !SelectedClass
		|| !SelectedClass->IsChildOf(UComposableCameraCameraNodeBase::StaticClass())
		|| SelectedClass->HasAnyClassFlags(CLASS_Abstract)
		|| CustomModifier->NodeClass.Get() == SelectedClass)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT(
		"SelectCustomModifierTargetNodeClass", "Select Custom Modifier Node Class"));
	CustomModifier->Modify();
	CustomModifier->NodeClass = const_cast<UClass*>(SelectedClass);
	CustomModifier->MarkPackageDirty();
}

void FComposableCameraModifierDetails::RefreshDetails(
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities)
{
	if (const TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtilities.Pin())
	{
		PropertyUtilities->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
