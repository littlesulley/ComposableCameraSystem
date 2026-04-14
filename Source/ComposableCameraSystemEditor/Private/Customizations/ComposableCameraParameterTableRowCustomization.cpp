// Copyright Sulley. All rights reserved.

#include "Customizations/ComposableCameraParameterTableRowCustomization.h"

#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/StructOnScope.h"
#include "ScopedTransaction.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComposableCameraParameterTableRowCustomization"

FComposableCameraParameterTableRowCustomization::~FComposableCameraParameterTableRowCustomization()
{
	UnbindAssetChangeDelegate();
}

void FComposableCameraParameterTableRowCustomization::UnbindAssetChangeDelegate()
{
	if (AssetChangedDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(AssetChangedDelegateHandle);
		AssetChangedDelegateHandle.Reset();
	}
	WatchedTypeAsset.Reset();
}

TSharedRef<IPropertyTypeCustomization> FComposableCameraParameterTableRowCustomization::MakeInstance()
{
	return MakeShared<FComposableCameraParameterTableRowCustomization>();
}

void FComposableCameraParameterTableRowCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	// Register against the WRAPPER sub-struct, not the row. FStructureDetailsView
	// (used by the DataTable row editor) does not invoke IPropertyTypeCustomization
	// at the root struct level — only on child struct properties. The wrapper
	// exists precisely to give us a child-level hook.
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FComposableCameraExposedParameterValues::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FComposableCameraParameterTableRowCustomization::MakeInstance));
}

void FComposableCameraParameterTableRowCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(
			FComposableCameraExposedParameterValues::StaticStruct()->GetFName());
	}
}

void FComposableCameraParameterTableRowCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Render the wrapper's header with the default display name — the label
	// "Exposed Parameters" comes from the row's meta=(DisplayName=...) on the
	// Parameters field.
	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FComposableCameraParameterTableRowCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	WrapperPropertyHandle = PropertyHandle;
	PropertyUtilities = CustomizationUtils.GetPropertyUtilities();

	// Clear per-rebuild state from previous CustomizeChildren invocation.
	UnbindAssetChangeDelegate();
	StructScopes.Reset();
	StructDetailViews.Reset();

	// Walk up one level to the parent row. In the DataTable row editor, the
	// parent is the FComposableCameraParameterTableRow root struct — which
	// IS where the CameraType field lives.
	RowPropertyHandle = PropertyHandle->GetParentHandle();
	if (!RowPropertyHandle.IsValid())
	{
		return;
	}

	CameraTypeHandle = RowPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FComposableCameraParameterTableRow, CameraType));
	ValuesHandle = PropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FComposableCameraExposedParameterValues, Values));

	if (!CameraTypeHandle.IsValid() || !ValuesHandle.IsValid())
	{
		return;
	}

	// When the CameraType pick changes, rebuild the per-parameter widgets by
	// forcing the details panel to re-run CustomizeChildren. Without this the
	// widgets stay tied to whichever type was selected at construction time.
	CameraTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(
		this, &FComposableCameraParameterTableRowCustomization::RequestRefresh));

	// Resolve the selected camera type by reading the sibling field. Sync-load
	// is fine here — this is an editor panel, not a hot path, and DataTable
	// edits are rare.
	UComposableCameraTypeAsset* TypeAsset = nullptr;
	{
		TArray<void*> RawData;
		CameraTypeHandle->AccessRawData(RawData);
		if (RawData.Num() == 1 && RawData[0] != nullptr)
		{
			TSoftObjectPtr<UComposableCameraTypeAsset>* SoftPtr =
				static_cast<TSoftObjectPtr<UComposableCameraTypeAsset>*>(RawData[0]);
			if (SoftPtr)
			{
				TypeAsset = SoftPtr->LoadSynchronous();
			}
		}
	}

	// Watch the resolved type asset for content changes (e.g. exposed
	// parameter list edited in the graph editor). The global broadcast
	// fires for every UObject — filter by our specific instance.
	// Capture a weak PropertyUtilities for the refresh callback instead of
	// capturing `this`, so the lambda stays valid even if the customization
	// is rebuilt.
	if (TypeAsset)
	{
		WatchedTypeAsset = TypeAsset;
		TWeakPtr<IPropertyUtilities> WeakUtils = PropertyUtilities;

		AssetChangedDelegateHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
			[WeakAsset = WatchedTypeAsset, WeakUtils](UObject* Object, FPropertyChangedEvent&)
			{
				if (Object && Object == WeakAsset.Get())
				{
					if (TSharedPtr<IPropertyUtilities> Utils = WeakUtils.Pin())
					{
						Utils->ForceRefresh();
					}
				}
			});
	}

	IDetailGroup& ParamsGroup = ChildBuilder.AddGroup(
		TEXT("ExposedParameters"),
		LOCTEXT("ExposedParametersGroup", "Exposed Parameters"));

	if (!TypeAsset)
	{
		ParamsGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoCameraTypeSelected",
				"Select a Camera Type to see its exposed parameters."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	const TArray<FComposableCameraExposedParameter>& Exposed = TypeAsset->GetExposedParameters();
	const TArray<FComposableCameraInternalVariable>& ExposedVars = TypeAsset->ExposedVariables;

	if (Exposed.Num() == 0)
	{
		ParamsGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoExposedParameters",
				"This camera type has no exposed parameters."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
	else
	{
		for (const FComposableCameraExposedParameter& Param : Exposed)
		{
			const FName ParamName = Param.ParameterName;
			const EComposableCameraPinType PinType = Param.PinType;
			UScriptStruct* StructType = Param.StructType;
			const FString ParamDefault = TypeAsset->GetExposedParameterDefaultValue(Param);

			const FText DisplayName = Param.DisplayName.IsEmpty()
				? FText::FromName(ParamName)
				: Param.DisplayName;

			const FText Tooltip = Param.Tooltip.IsEmpty()
				? GetFormatHint(PinType, StructType)
				: FText::Format(
					LOCTEXT("ParameterTooltipWithFormat", "{0}\n\nExpected format: {1}"),
					Param.Tooltip,
					GetFormatHint(PinType, StructType));

			ParamsGroup.AddWidgetRow()
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, ParamName]()
					{
						return IsParameterOverridden(ParamName)
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged(FOnCheckStateChanged::CreateSP(
						this,
						&FComposableCameraParameterTableRowCustomization::OnOverrideToggled,
						ParamName, ParamDefault))
					.ToolTipText(LOCTEXT("OverrideCheckboxTooltip",
						"When checked, the value in this row overrides the default from the camera type asset."))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(DisplayName)
					.ToolTipText(Tooltip)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
			.ValueContent()
			.MinDesiredWidth(200.f)
			.MaxDesiredWidth(600.f)
			[
				SNew(SBox)
				.IsEnabled_Lambda([this, ParamName]()
				{
					return IsParameterOverridden(ParamName);
				})
				[
					BuildTypedValueWidget(ParamName, PinType, StructType, ParamDefault)
				]
			];
		}
	}

	// ── Exposed Variables group ───────────────────────────────────────────
	//
	// Exposed variables share the same row-level string keyspace as exposed
	// parameters (the runtime's ApplyParameterBlock walks them in its own
	// phase after the parameters). From the row editor's point of view they
	// behave identically: we render the same checkbox + typed-widget pair
	// using the variable's FName as the map key. We render them in a
	// separate IDetailGroup so the authoring distinction between "one-shot
	// override" (parameter) and "initial value for a persistent slot"
	// (variable) stays legible. Only emit the group if the type has at
	// least one exposed variable — an empty group would just be noise.
	if (ExposedVars.Num() > 0)
	{
		IDetailGroup& VarsGroup = ChildBuilder.AddGroup(
			TEXT("ExposedVariables"),
			LOCTEXT("ExposedVariablesGroup", "Exposed Variables"));

		for (const FComposableCameraInternalVariable& Var : ExposedVars)
		{
			if (Var.VariableName.IsNone())
			{
				continue;
			}

			const FName VarName = Var.VariableName;
			const EComposableCameraPinType PinType = Var.VariableType;
			UScriptStruct* StructType = Var.StructType;
			const FString VarDefault = Var.InitialValueString;

			// FComposableCameraInternalVariable has no DisplayName — the
			// FName is the display label. Matches the graph editor's
			// treatment of variable names.
			const FText DisplayName = FText::FromName(VarName);

			const FText Tooltip = Var.Tooltip.IsEmpty()
				? GetFormatHint(PinType, StructType)
				: FText::Format(
					LOCTEXT("VariableTooltipWithFormat", "{0}\n\nExpected format: {1}"),
					Var.Tooltip,
					GetFormatHint(PinType, StructType));

			VarsGroup.AddWidgetRow()
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, VarName]()
					{
						return IsParameterOverridden(VarName)
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged(FOnCheckStateChanged::CreateSP(
						this,
						&FComposableCameraParameterTableRowCustomization::OnOverrideToggled,
						VarName, VarDefault))
					.ToolTipText(LOCTEXT("OverrideVariableCheckboxTooltip",
						"When checked, the initial value in this row overrides the variable's default from the camera type asset."))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(DisplayName)
					.ToolTipText(Tooltip)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
			.ValueContent()
			.MinDesiredWidth(200.f)
			.MaxDesiredWidth(600.f)
			[
				SNew(SBox)
				.IsEnabled_Lambda([this, VarName]()
				{
					return IsParameterOverridden(VarName);
				})
				[
					BuildTypedValueWidget(VarName, PinType, StructType, VarDefault)
				]
			];
		}
	}

	// Auto-remove orphaned entries: keys in the Values map that no longer
	// correspond to any exposed parameter or variable on the current
	// CameraType. This keeps the row clean after type swaps or asset edits
	// without requiring manual user intervention.
	TSet<FName> KnownNames;
	KnownNames.Reserve(Exposed.Num() + ExposedVars.Num());
	for (const FComposableCameraExposedParameter& Param : Exposed)
	{
		KnownNames.Add(Param.ParameterName);
	}
	for (const FComposableCameraInternalVariable& Var : ExposedVars)
	{
		if (!Var.VariableName.IsNone())
		{
			KnownNames.Add(Var.VariableName);
		}
	}

	if (FComposableCameraExposedParameterValues* Wrapper = GetWrapperPtr())
	{
		TArray<FName> OrphanNames;
		for (const TPair<FName, FString>& Entry : Wrapper->Values)
		{
			if (!KnownNames.Contains(Entry.Key))
			{
				OrphanNames.Add(Entry.Key);
			}
		}

		if (OrphanNames.Num() > 0 && ValuesHandle.IsValid())
		{
			ValuesHandle->NotifyPreChange();
			for (FName Name : OrphanNames)
			{
				Wrapper->Values.Remove(Name);
			}
			ValuesHandle->NotifyPostChange(EPropertyChangeType::ArrayRemove);
		}
	}
}

FComposableCameraExposedParameterValues* FComposableCameraParameterTableRowCustomization::GetWrapperPtr() const
{
	if (!WrapperPropertyHandle.IsValid())
	{
		return nullptr;
	}

	TArray<void*> RawData;
	WrapperPropertyHandle->AccessRawData(RawData);
	if (RawData.Num() != 1 || RawData[0] == nullptr)
	{
		return nullptr;
	}
	return static_cast<FComposableCameraExposedParameterValues*>(RawData[0]);
}

FComposableCameraParameterTableRow* FComposableCameraParameterTableRowCustomization::GetRowPtr() const
{
	if (!RowPropertyHandle.IsValid())
	{
		return nullptr;
	}

	TArray<void*> RawData;
	RowPropertyHandle->AccessRawData(RawData);
	if (RawData.Num() != 1 || RawData[0] == nullptr)
	{
		return nullptr;
	}
	return static_cast<FComposableCameraParameterTableRow*>(RawData[0]);
}

void FComposableCameraParameterTableRowCustomization::RequestRefresh()
{
	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}

bool FComposableCameraParameterTableRowCustomization::IsParameterOverridden(FName ParameterName) const
{
	if (const FComposableCameraExposedParameterValues* Wrapper = GetWrapperPtr())
	{
		return Wrapper->Values.Contains(ParameterName);
	}
	return false;
}

void FComposableCameraParameterTableRowCustomization::OnOverrideToggled(
	ECheckBoxState NewState,
	FName ParameterName,
	FString DefaultValue)
{
	FComposableCameraExposedParameterValues* Wrapper = GetWrapperPtr();
	if (!Wrapper || !ValuesHandle.IsValid())
	{
		return;
	}

	if (NewState == ECheckBoxState::Checked)
	{
		// Enable override — seed the map with the asset's default value so the
		// text box starts with something meaningful rather than blank.
		FScopedTransaction Transaction(LOCTEXT("EnableParameterOverride", "Enable Parameter Override"));
		ValuesHandle->NotifyPreChange();

		Wrapper->Values.FindOrAdd(ParameterName) = DefaultValue;

		ValuesHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		ValuesHandle->NotifyFinishedChangingProperties();
	}
	else
	{
		// Disable override — remove from the map so the runtime uses the asset default.
		FScopedTransaction Transaction(LOCTEXT("DisableParameterOverride", "Disable Parameter Override"));
		ValuesHandle->NotifyPreChange();

		Wrapper->Values.Remove(ParameterName);

		ValuesHandle->NotifyPostChange(EPropertyChangeType::ArrayRemove);
		ValuesHandle->NotifyFinishedChangingProperties();
	}
}

// OnRemoveOrphan and OnRemoveAllOrphans removed — orphaned entries are now
// auto-cleaned in CustomizeChildren before the widget tree is built.

// ─── Typed Value Widget Helpers ──────────────────────────────────────────

FString FComposableCameraParameterTableRowCustomization::GetParameterString(
	FName ParameterName, const FString& DefaultValue) const
{
	if (const FComposableCameraExposedParameterValues* Wrapper = GetWrapperPtr())
	{
		if (const FString* Found = Wrapper->Values.Find(ParameterName))
		{
			return *Found;
		}
	}
	return DefaultValue;
}

void FComposableCameraParameterTableRowCustomization::SetParameterString(
	FName ParameterName, const FString& NewValue)
{
	FComposableCameraExposedParameterValues* Wrapper = GetWrapperPtr();
	if (!Wrapper || !ValuesHandle.IsValid())
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("EditParameterValue_Typed", "Edit Parameter Value"));
	ValuesHandle->NotifyPreChange();

	Wrapper->Values.FindOrAdd(ParameterName) = NewValue;

	ValuesHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	ValuesHandle->NotifyFinishedChangingProperties();
}

TSharedRef<SWidget> FComposableCameraParameterTableRowCustomization::BuildTypedValueWidget(
	FName ParameterName,
	EComposableCameraPinType PinType,
	UScriptStruct* StructType,
	const FString& DefaultValue)
{
	switch (PinType)
	{
	// ── Bool ──────────────────────────────────────────────────────────────
	case EComposableCameraPinType::Bool:
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, ParameterName, DefaultValue]() -> ECheckBoxState
			{
				const FString Val = GetParameterString(ParameterName, DefaultValue);
				return Val.Equals(TEXT("true"), ESearchCase::IgnoreCase)
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, ParameterName](ECheckBoxState NewState)
			{
				SetParameterString(ParameterName,
					NewState == ECheckBoxState::Checked
						? FString(TEXT("true"))
						: FString(TEXT("false")));
			});
	}

	// ── Int32 ─────────────────────────────────────────────────────────────
	case EComposableCameraPinType::Int32:
	{
		return SNew(SNumericEntryBox<int32>)
			.AllowSpin(true)
			.Value_Lambda([this, ParameterName, DefaultValue]() -> TOptional<int32>
			{
				const FString Val = GetParameterString(ParameterName, DefaultValue);
				if (Val.IsEmpty()) return 0;
				return FCString::Atoi(*Val);
			})
			.OnValueCommitted_Lambda([this, ParameterName](int32 NewValue, ETextCommit::Type)
			{
				SetParameterString(ParameterName, FString::FromInt(NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// ── Float ─────────────────────────────────────────────────────────────
	case EComposableCameraPinType::Float:
	{
		return SNew(SNumericEntryBox<float>)
			.AllowSpin(true)
			.Value_Lambda([this, ParameterName, DefaultValue]() -> TOptional<float>
			{
				const FString Val = GetParameterString(ParameterName, DefaultValue);
				if (Val.IsEmpty()) return 0.f;
				return FCString::Atof(*Val);
			})
			.OnValueCommitted_Lambda([this, ParameterName](float NewValue, ETextCommit::Type)
			{
				SetParameterString(ParameterName, FString::SanitizeFloat(NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// ── Double ────────────────────────────────────────────────────────────
	case EComposableCameraPinType::Double:
	{
		return SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.Value_Lambda([this, ParameterName, DefaultValue]() -> TOptional<double>
			{
				const FString Val = GetParameterString(ParameterName, DefaultValue);
				if (Val.IsEmpty()) return 0.0;
				return FCString::Atod(*Val);
			})
			.OnValueCommitted_Lambda([this, ParameterName](double NewValue, ETextCommit::Type)
			{
				SetParameterString(ParameterName, FString::Printf(TEXT("%.6f"), NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// ── Vector2D ──────────────────────────────────────────────────────────
	case EComposableCameraPinType::Vector2D:
	{
		static const TCHAR* Labels[] = { TEXT("X"), TEXT("Y") };
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 2; ++C)
		{
			Box->AddSlot().FillWidth(1.f).Padding(C > 0 ? 4.f : 0.f, 0.f, 0.f, 0.f)
			[
				BuildNumericComponentWidget(ParameterName, C, 2, Labels, TEXT("Vector2D"), DefaultValue)
			];
		}
		return Box;
	}

	// ── Vector3D ──────────────────────────────────────────────────────────
	case EComposableCameraPinType::Vector3D:
	{
		static const TCHAR* Labels[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 3; ++C)
		{
			Box->AddSlot().FillWidth(1.f).Padding(C > 0 ? 4.f : 0.f, 0.f, 0.f, 0.f)
			[
				BuildNumericComponentWidget(ParameterName, C, 3, Labels, TEXT("Vector3D"), DefaultValue)
			];
		}
		return Box;
	}

	// ── Vector4 ───────────────────────────────────────────────────────────
	case EComposableCameraPinType::Vector4:
	{
		static const TCHAR* Labels[] = { TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT("W") };
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 4; ++C)
		{
			Box->AddSlot().FillWidth(1.f).Padding(C > 0 ? 4.f : 0.f, 0.f, 0.f, 0.f)
			[
				BuildNumericComponentWidget(ParameterName, C, 4, Labels, TEXT("Vector4"), DefaultValue)
			];
		}
		return Box;
	}

	// ── Rotator ───────────────────────────────────────────────────────────
	case EComposableCameraPinType::Rotator:
	{
		static const TCHAR* Labels[] = { TEXT("P"), TEXT("Y"), TEXT("R") };
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 3; ++C)
		{
			Box->AddSlot().FillWidth(1.f).Padding(C > 0 ? 4.f : 0.f, 0.f, 0.f, 0.f)
			[
				BuildNumericComponentWidget(ParameterName, C, 3, Labels, TEXT("Rotator"), DefaultValue)
			];
		}
		return Box;
	}

	// ── Transform ─────────────────────────────────────────────────────────
	case EComposableCameraPinType::Transform:
	{
		return BuildTransformWidget(ParameterName, DefaultValue);
	}

	// ── Actor / Object — not expressible precisely ────────────────────────
	case EComposableCameraPinType::Actor:
	case EComposableCameraPinType::Object:
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("ActorObjectNotSupported",
				"Actor/Object types cannot be set from a DataTable row."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	// ── Struct — inline details view if type is known ─────────────────────
	case EComposableCameraPinType::Struct:
	{
		if (StructType)
		{
			return BuildStructValueWidget(ParameterName, StructType, DefaultValue);
		}

		// Fall through to text box if StructType is unset.
		return SNew(STextBlock)
			.Text(LOCTEXT("StructTypeUnset", "Struct type not set — invalid parameter."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	// ── Unknown type — raw text fallback ──────────────────────────────────
	default:
	{
		return SNew(SEditableTextBox)
			.Text_Lambda([this, ParameterName, DefaultValue]() -> FText
			{
				return FText::FromString(GetParameterString(ParameterName, DefaultValue));
			})
			.OnTextCommitted_Lambda([this, ParameterName](const FText& NewText, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
				{
					SetParameterString(ParameterName, NewText.ToString());
				}
			})
			.HintText(LOCTEXT("DefaultHint", "Enter value as text"))
			.SelectAllTextWhenFocused(true)
			.ClearKeyboardFocusOnCommit(false);
	}
	}
}

// ─── Multi-Component Numeric Widget ─────────────────────────────────────

TSharedRef<SWidget> FComposableCameraParameterTableRowCustomization::BuildNumericComponentWidget(
	FName ParameterName,
	int32 ComponentIndex,
	int32 NumComponents,
	const TCHAR* const* ComponentLabels,
	const TCHAR* Prefix,
	const FString& DefaultValue)
{
	auto ParseComponents = [NumComponents](const FString& Str, TArray<double>& Out)
	{
		Out.SetNumZeroed(NumComponents);
		if (Str.IsEmpty())
		{
			return;
		}

		if (NumComponents == 2)
		{
			FVector2D V;
			if (V.InitFromString(Str)) { Out[0] = V.X; Out[1] = V.Y; return; }
		}
		else if (NumComponents == 3)
		{
			FVector V;
			if (V.InitFromString(Str)) { Out[0] = V.X; Out[1] = V.Y; Out[2] = V.Z; return; }
			FRotator R;
			if (R.InitFromString(Str)) { Out[0] = R.Pitch; Out[1] = R.Yaw; Out[2] = R.Roll; return; }
		}
		else if (NumComponents == 4)
		{
			FVector4 V;
			if (V.InitFromString(Str)) { Out[0] = V.X; Out[1] = V.Y; Out[2] = V.Z; Out[3] = V.W; return; }
		}
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 2.f, 0.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(ComponentLabels[ComponentIndex]))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.Value_Lambda([this, ParameterName, DefaultValue, ComponentIndex, NumComponents, ParseComponents]()
				-> TOptional<double>
			{
				const FString Val = GetParameterString(ParameterName, DefaultValue);
				TArray<double> Components;
				ParseComponents(Val, Components);
				return Components.IsValidIndex(ComponentIndex) ? Components[ComponentIndex] : 0.0;
			})
			.OnValueCommitted_Lambda(
				[this, ParameterName, DefaultValue, ComponentIndex, NumComponents, Prefix, ParseComponents]
				(double NewValue, ETextCommit::Type)
			{
				const FString Val = GetParameterString(ParameterName, DefaultValue);
				TArray<double> Components;
				ParseComponents(Val, Components);
				if (Components.IsValidIndex(ComponentIndex))
				{
					Components[ComponentIndex] = NewValue;
				}

				FString Result;
				if (NumComponents == 2)
				{
					Result = FVector2D(Components[0], Components[1]).ToString();
				}
				else if (NumComponents == 3)
				{
					if (FCString::Strcmp(Prefix, TEXT("Rotator")) == 0)
					{
						Result = FRotator(Components[0], Components[1], Components[2]).ToString();
					}
					else
					{
						Result = FVector(Components[0], Components[1], Components[2]).ToString();
					}
				}
				else if (NumComponents == 4)
				{
					Result = FVector4(Components[0], Components[1], Components[2], Components[3]).ToString();
				}

				SetParameterString(ParameterName, Result);
			})
			.MinDesiredValueWidth(40.f)
		];
}

// ─── Transform Widget ───────────────────────────────────────────────────

TSharedRef<SWidget> FComposableCameraParameterTableRowCustomization::BuildTransformWidget(
	FName ParameterName,
	const FString& DefaultValue)
{
	auto ParseTransform = [](const FString& Str) -> FTransform
	{
		FTransform T = FTransform::Identity;
		if (!Str.IsEmpty())
		{
			T.InitFromString(Str);
		}
		return T;
	};

	auto BuildRow = [this, ParameterName, DefaultValue, ParseTransform](
		const FText& RowLabel,
		const TFunction<FVector(const FTransform&)>& Getter,
		const TFunction<void(FTransform&, const FVector&)>& Setter) -> TSharedRef<SWidget>
	{
		static const TCHAR* XYZ[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock)
			.Text(RowLabel)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.MinDesiredWidth(52.f)
		];

		for (int32 C = 0; C < 3; ++C)
		{
			Row->AddSlot()
			.FillWidth(1.f)
			.Padding(C > 0 ? 4.f : 0.f, 0.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 2.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(XYZ[C]))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SNumericEntryBox<double>)
					.AllowSpin(true)
					.Value_Lambda([this, ParameterName, DefaultValue, ParseTransform, Getter, C]()
						-> TOptional<double>
					{
						const FString Val = GetParameterString(ParameterName, DefaultValue);
						FVector V = Getter(ParseTransform(Val));
						return V[C];
					})
					.OnValueCommitted_Lambda(
						[this, ParameterName, DefaultValue, ParseTransform, Getter, Setter, C]
						(double NewValue, ETextCommit::Type)
					{
						const FString Val = GetParameterString(ParameterName, DefaultValue);
						FTransform T = ParseTransform(Val);
						FVector V = Getter(T);
						V[C] = NewValue;
						Setter(T, V);
						SetParameterString(ParameterName, T.ToString());
					})
					.MinDesiredValueWidth(40.f)
				]
			];
		}

		return Row;
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
		[
			BuildRow(
				LOCTEXT("TransformLocation", "Location"),
				[](const FTransform& T) -> FVector { return T.GetTranslation(); },
				[](FTransform& T, const FVector& V) { T.SetTranslation(V); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
		[
			BuildRow(
				LOCTEXT("TransformRotation", "Rotation"),
				[](const FTransform& T) -> FVector
				{
					FRotator R = T.Rotator();
					return FVector(R.Pitch, R.Yaw, R.Roll);
				},
				[](FTransform& T, const FVector& V)
				{
					T.SetRotation(FRotator(V.X, V.Y, V.Z).Quaternion());
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)
		[
			BuildRow(
				LOCTEXT("TransformScale", "Scale"),
				[](const FTransform& T) -> FVector { return T.GetScale3D(); },
				[](FTransform& T, const FVector& V) { T.SetScale3D(V); })
		];
}

// ─── Inline Struct Details View ──────────────────────────────────────────

TSharedRef<SWidget> FComposableCameraParameterTableRowCustomization::BuildStructValueWidget(
	FName ParameterName,
	UScriptStruct* InStructType,
	const FString& DefaultValue)
{
	// Allocate struct memory, initialize to defaults, then parse the current
	// stored string (or the asset default) into it.
	TSharedPtr<FStructOnScope> StructScope = MakeShared<FStructOnScope>(InStructType);

	const FString CurrentValue = GetParameterString(ParameterName, DefaultValue);
	if (!CurrentValue.IsEmpty())
	{
		InStructType->ImportText(
			*CurrentValue,
			StructScope->GetStructMemory(),
			/*OwnerObject=*/ nullptr,
			PPF_None,
			GLog,
			InStructType->GetName(),
			/*bAllowNativeOverride=*/ true);
	}

	// Create a minimal IStructureDetailsView.
	FPropertyEditorModule& PropertyModule =
		FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs ViewArgs;
	ViewArgs.bHideSelectionTip = true;
	ViewArgs.bAllowSearch = false;
	ViewArgs.bShowOptions = false;
	ViewArgs.bShowPropertyMatrixButton = false;
	ViewArgs.bShowKeyablePropertiesOption = false;
	ViewArgs.bShowAnimatedPropertiesOption = false;
	ViewArgs.bShowScrollBar = false;

	FStructureDetailsViewArgs StructViewArgs;

	TSharedPtr<IStructureDetailsView> StructView =
		PropertyModule.CreateStructureDetailView(ViewArgs, StructViewArgs, StructScope);

	// Serialize back to the Values map whenever the user edits a property.
	StructView->GetOnFinishedChangingPropertiesDelegate().AddLambda(
		[this, ParameterName, StructScope, InStructType](const FPropertyChangedEvent&)
		{
			if (StructScope.IsValid())
			{
				FString NewValue;
				InStructType->ExportText(
					NewValue,
					StructScope->GetStructMemory(),
					/*Defaults=*/ nullptr,
					/*OwnerObject=*/ nullptr,
					PPF_None,
					/*ExportRootScope=*/ nullptr);
				SetParameterString(ParameterName, NewValue);
			}
		});

	// Keep the scope and view alive for the lifetime of this customization rebuild.
	StructScopes.Add(StructScope);
	StructDetailViews.Add(StructView);

	TSharedPtr<SWidget> ViewWidget = StructView->GetWidget();
	return ViewWidget.IsValid() ? ViewWidget.ToSharedRef() : SNullWidget::NullWidget;
}

FText FComposableCameraParameterTableRowCustomization::GetFormatHint(
	EComposableCameraPinType PinType, UScriptStruct* StructType) const
{
	switch (PinType)
	{
	case EComposableCameraPinType::Bool:
		return LOCTEXT("HintBool", "true / false");
	case EComposableCameraPinType::Int32:
		return LOCTEXT("HintInt32", "integer (e.g. 42)");
	case EComposableCameraPinType::Float:
		return LOCTEXT("HintFloat", "float (e.g. 3.14)");
	case EComposableCameraPinType::Double:
		return LOCTEXT("HintDouble", "double (e.g. 3.14159)");
	case EComposableCameraPinType::Vector2D:
		return LOCTEXT("HintVector2D", "(X=1.0,Y=2.0)");
	case EComposableCameraPinType::Vector3D:
		return LOCTEXT("HintVector3D", "(X=1.0,Y=2.0,Z=3.0)");
	case EComposableCameraPinType::Vector4:
		return LOCTEXT("HintVector4", "(X=1.0,Y=2.0,Z=3.0,W=4.0)");
	case EComposableCameraPinType::Rotator:
		return LOCTEXT("HintRotator", "(Pitch=0.0,Yaw=0.0,Roll=0.0)");
	case EComposableCameraPinType::Transform:
		return LOCTEXT("HintTransform",
			"(Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=0,Y=0,Z=0),Scale3D=(X=1,Y=1,Z=1))");
	case EComposableCameraPinType::Actor:
		return LOCTEXT("HintActor",
			"Actor parameters cannot be set from a DataTable row — use Object with a class/archetype soft path instead.");
	case EComposableCameraPinType::Object:
		return LOCTEXT("HintObject",
			"Soft object path (e.g. /Game/Path/ToAsset.ToAsset)");
	case EComposableCameraPinType::Struct:
		return StructType
			? FText::Format(
				LOCTEXT("HintStruct", "Struct literal for {0} (use Unreal's ImportText format)"),
				FText::FromString(StructType->GetName()))
			: LOCTEXT("HintStructNoType", "Struct literal (StructType unset — invalid pin)");
	default:
		return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
