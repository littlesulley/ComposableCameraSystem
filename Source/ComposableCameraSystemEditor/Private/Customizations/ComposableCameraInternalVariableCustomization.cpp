// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraInternalVariableCustomization.h"

#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DetailLayoutBuilder.h"
#include "IStructureDetailsView.h"
#include "UObject/StructOnScope.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "EdGraphSchema_K2.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SEnumCombo.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComposableCameraInternalVariableCustomization"

// Static Registration 

TSharedRef<IPropertyTypeCustomization> FComposableCameraInternalVariableCustomization::MakeInstance()
{
	return MakeShared<FComposableCameraInternalVariableCustomization>();
}

void FComposableCameraInternalVariableCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FComposableCameraInternalVariable::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FComposableCameraInternalVariableCustomization::MakeInstance));
}

void FComposableCameraInternalVariableCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FComposableCameraInternalVariable::StaticStruct()->GetFName());
	}
}

// Header 

void FComposableCameraInternalVariableCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& Utils)
{
	StructHandle = PropertyHandle;
	PropertyUtilities = Utils.GetPropertyUtilities();

	// Display the VariableName as the header so the array element reads as
	// a named entry rather than "[0]", "[1]", etc.
	TSharedPtr<IPropertyHandle> NameHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableName));

	HeaderRow.NameContent()
	[PropertyHandle->CreatePropertyNameWidget()]
	.ValueContent()
	.MinDesiredWidth(120.f)
	[NameHandle.IsValid()
			? NameHandle->CreatePropertyValueWidget()
			: SNullWidget::NullWidget];
}

// Children 

void FComposableCameraInternalVariableCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& Utils)
{
	// Resolve the type handles we need to read the current VariableType.
	TSharedPtr<IPropertyHandle> TypeHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableType));
	TSharedPtr<IPropertyHandle> StructTypeHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, StructType));
	TSharedPtr<IPropertyHandle> EnumTypeHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, EnumType));
	TSharedPtr<IPropertyHandle> InitialValueHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, InitialValueString));

	// When VariableType changes, rebuild the customization so the
	// InitialValueString widget adapts to the new type.
	if (TypeHandle.IsValid())
	{
		TypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utils = PropertyUtilities]()
			{
				if (Utils.IsValid())
				{
					Utils->ForceRefresh();
				}
			}));
	}

	// Also rebuild when StructType changes (for Struct pin type).
	if (StructTypeHandle.IsValid())
	{
		StructTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utils = PropertyUtilities]()
			{
				if (Utils.IsValid())
				{
					Utils->ForceRefresh();
				}
			}));
	}

	// Also rebuild when EnumType changes (for Enum pin type) so the
	// SEnumComboBox rebuilds against the new UEnum.
	if (EnumTypeHandle.IsValid())
	{
		EnumTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utils = PropertyUtilities]()
			{
				if (Utils.IsValid())
				{
					Utils->ForceRefresh();
				}
			}));
	}

	// Add all child properties in declaration order, replacing only
	// InitialValueString with a typed widget.
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid())
		{
			continue;
		}

		// Skip VariableGuid - it's an internal editor identity field, not user-facing.
		if (ChildHandle->GetProperty()->GetFName() ==
			GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableGuid))
		{
			continue;
		}

		// Skip VariableName - already shown in the header.
		if (ChildHandle->GetProperty()->GetFName() ==
			GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableName))
		{
			continue;
		}

		// Custom picker for EnumType -- walks all UEnum reflection objects
		// (native + BP-defined) instead of the default asset picker, which
		// only enumerates UUserDefinedEnum assets and leaves the dropdown
		// empty for projects with only native UENUM(BlueprintType) enums.
		if (ChildHandle->GetProperty()->GetFName() ==
			GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, EnumType))
		{
			ChildBuilder.AddCustomRow(LOCTEXT("EnumTypeSearchText", "Enum Type"))
			.NameContent()
			[ChildHandle->CreatePropertyNameWidget()]
			.ValueContent()
			.MinDesiredWidth(200.f)
			.MaxDesiredWidth(600.f)
			[BuildEnumTypePicker(EnumTypeHandle)];
			continue;
		}

		if (ChildHandle->GetProperty()->GetFName() ==
			GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, InitialValueString))
		{
			// Read current VariableType.
			EComposableCameraPinType PinType = EComposableCameraPinType::Float;
			if (TypeHandle.IsValid())
			{
				uint8 TypeByte = 0;
				TypeHandle->GetValue(TypeByte);
				PinType = static_cast<EComposableCameraPinType>(TypeByte);
			}

			// Read current StructType (for Struct pin type).
			UScriptStruct* CurrentStructType = nullptr;
			if (StructTypeHandle.IsValid())
			{
				UObject* ObjValue = nullptr;
				StructTypeHandle->GetValue(ObjValue);
				CurrentStructType = Cast<UScriptStruct>(ObjValue);
			}

			// Read current EnumType (for Enum pin type).
			UEnum* CurrentEnumType = nullptr;
			if (EnumTypeHandle.IsValid())
			{
				UObject* ObjValue = nullptr;
				EnumTypeHandle->GetValue(ObjValue);
				CurrentEnumType = Cast<UEnum>(ObjValue);
			}

			// Struct with a known UScriptStruct gets a full inline struct
			// editor. All other types use a single typed widget row.
			if (PinType == EComposableCameraPinType::Struct && CurrentStructType)
			{
				BuildStructDefaultValueRows(ChildBuilder, InitialValueHandle, CurrentStructType);
			}
			else
			{
				ChildBuilder.AddCustomRow(LOCTEXT("InitialValueSearchText", "Initial Value"))
				.NameContent()
				[SNew(STextBlock)
					.Text(LOCTEXT("InitialValueLabel", "Initial Value"))
					.Font(IDetailLayoutBuilder::GetDetailFont())]
				.ValueContent()
				.MinDesiredWidth(200.f)
				.MaxDesiredWidth(600.f)
				[BuildTypedDefaultValueWidget(InitialValueHandle, PinType, CurrentStructType, CurrentEnumType)];
			}

			continue;
		}

		// All other properties: add with default rendering.
		ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}

// Typed Widget Builder 

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildTypedDefaultValueWidget(TSharedPtr<IPropertyHandle> InitialValueHandle,
	EComposableCameraPinType PinType,
	UScriptStruct* StructType,
	UEnum* EnumType)
{
	if (!InitialValueHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	switch (PinType)
	{
	// Bool 
	case EComposableCameraPinType::Bool:
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([InitialValueHandle]() -> ECheckBoxState
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)
					? ECheckBoxState::Checked: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([InitialValueHandle](ECheckBoxState NewState)
			{
				InitialValueHandle->SetValue(NewState == ECheckBoxState::Checked
						? FString(TEXT("true"))
						: FString(TEXT("false")));
			});
	}

	// Int32
	case EComposableCameraPinType::Int32:
	{
		// Per-widget drag cache. SSpinBox displays its text by reading
		// ValueAttribute.Get() every paint; ValueAttribute is bound to the
		// Value_Lambda below, which normally reads the persisted FString
		// through the IPropertyHandle. Per-tick SetValue writes during drag
		// fire PostEditChangeProperty broadcasts that other subscribers
		// (engine internals + our K2 nodes' OnObjectPropertyChanged listeners)
		// react to, and the resulting refresh cascade can invalidate the
		// drag mid-gesture - users see one tick of motion then a freeze.
		// Routing live drag through a TSharedRef<TOptional<T>> cache keeps
		// the per-tick updates entirely local: Value_Lambda returns the
		// cache when set, OnValueChanged updates it, and the final
		// OnValueCommitted call writes once to the handle and clears the
		// cache so subsequent reads go back through the handle. See
		// TechDoc 7.2 for the FString-handle round-trip gotcha.
		TSharedRef<TOptional<int32>> DragCache = MakeShared<TOptional<int32>>();
		return SNew(SNumericEntryBox<int32>)
			.AllowSpin(true)
			.Value_Lambda([InitialValueHandle, DragCache]() -> TOptional<int32>
			{
				if (DragCache->IsSet()) return DragCache->GetValue();
				FString Value;
				InitialValueHandle->GetValue(Value);
				if (Value.IsEmpty()) return 0;
				return FCString::Atoi(*Value);
			})
			.OnValueChanged_Lambda([DragCache](int32 NewValue)
			{
				*DragCache = NewValue;
			})
			.OnValueCommitted_Lambda([InitialValueHandle, DragCache](int32 NewValue, ETextCommit::Type)
			{
				DragCache->Reset();
				InitialValueHandle->SetValue(FString::FromInt(NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// Float
	case EComposableCameraPinType::Float:
	{
		TSharedRef<TOptional<float>> DragCache = MakeShared<TOptional<float>>();
		return SNew(SNumericEntryBox<float>)
			.AllowSpin(true)
			.Value_Lambda([InitialValueHandle, DragCache]() -> TOptional<float>
			{
				if (DragCache->IsSet()) return DragCache->GetValue();
				FString Value;
				InitialValueHandle->GetValue(Value);
				if (Value.IsEmpty()) return 0.f;
				return FCString::Atof(*Value);
			})
			.OnValueChanged_Lambda([DragCache](float NewValue)
			{
				*DragCache = NewValue;
			})
			.OnValueCommitted_Lambda([InitialValueHandle, DragCache](float NewValue, ETextCommit::Type)
			{
				DragCache->Reset();
				InitialValueHandle->SetValue(FString::SanitizeFloat(NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// Double
	case EComposableCameraPinType::Double:
	{
		TSharedRef<TOptional<double>> DragCache = MakeShared<TOptional<double>>();
		return SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.Value_Lambda([InitialValueHandle, DragCache]() -> TOptional<double>
			{
				if (DragCache->IsSet()) return DragCache->GetValue();
				FString Value;
				InitialValueHandle->GetValue(Value);
				if (Value.IsEmpty()) return 0.0;
				return FCString::Atod(*Value);
			})
			.OnValueChanged_Lambda([DragCache](double NewValue)
			{
				*DragCache = NewValue;
			})
			.OnValueCommitted_Lambda([InitialValueHandle, DragCache](double NewValue, ETextCommit::Type)
			{
				DragCache->Reset();
				InitialValueHandle->SetValue(FString::Printf(TEXT("%.6f"), NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// Vector2D 
	case EComposableCameraPinType::Vector2D:
	{
		static const TCHAR* Labels[] = { TEXT("X"), TEXT("Y") };
		static const TCHAR* Prefix = TEXT("Vector2D");

		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 2; ++C)
		{
			if (C > 0)
			{
				Box->AddSlot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
				[SNullWidget::NullWidget];
			}
			Box->AddSlot().FillWidth(1.f)
			[BuildNumericComponentWidget(InitialValueHandle, C, 2, Labels, Prefix)];
		}
		return Box;
	}

	// Vector3D 
	case EComposableCameraPinType::Vector3D:
	{
		static const TCHAR* Labels[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
		static const TCHAR* Prefix = TEXT("Vector3D");

		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 3; ++C)
		{
			if (C > 0)
			{
				Box->AddSlot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
				[SNullWidget::NullWidget];
			}
			Box->AddSlot().FillWidth(1.f)
			[BuildNumericComponentWidget(InitialValueHandle, C, 3, Labels, Prefix)];
		}
		return Box;
	}

	// Vector4 
	case EComposableCameraPinType::Vector4:
	{
		static const TCHAR* Labels[] = { TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT("W") };
		static const TCHAR* Prefix = TEXT("Vector4");

		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 4; ++C)
		{
			if (C > 0)
			{
				Box->AddSlot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
				[SNullWidget::NullWidget];
			}
			Box->AddSlot().FillWidth(1.f)
			[BuildNumericComponentWidget(InitialValueHandle, C, 4, Labels, Prefix)];
		}
		return Box;
	}

	// Rotator 
	case EComposableCameraPinType::Rotator:
	{
		static const TCHAR* Labels[] = { TEXT("P"), TEXT("Y"), TEXT("R") };
		static const TCHAR* Prefix = TEXT("Rotator");

		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (int32 C = 0; C < 3; ++C)
		{
			if (C > 0)
			{
				Box->AddSlot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
				[SNullWidget::NullWidget];
			}
			Box->AddSlot().FillWidth(1.f)
			[BuildNumericComponentWidget(InitialValueHandle, C, 3, Labels, Prefix)];
		}
		return Box;
	}

	// Name 
	case EComposableCameraPinType::Name:
	{
		return SNew(SEditableTextBox)
			.Text_Lambda([InitialValueHandle]() -> FText
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				return FText::FromString(Value);
			})
			.OnTextCommitted_Lambda([InitialValueHandle](const FText& NewText, ETextCommit::Type)
			{
				InitialValueHandle->SetValue(NewText.ToString());
			})
			.HintText(LOCTEXT("NameHint", "Enter FName"))
			.SelectAllTextWhenFocused(true)
			.ClearKeyboardFocusOnCommit(false);
	}

	// Enum 
	// Persists the entry's authored name (e.g. "EMyEnum::Alpha") rather
	// than its int value. Names are SCM-friendly and survive enum
	// renumbering. The runtime side parses via UEnum::GetValueByNameString.
	case EComposableCameraPinType::Enum:
	{
		if (!EnumType)
		{
			return SNew(STextBlock)
				.Text(LOCTEXT("EnumTypeMissing", "Set an Enum Type to enable selection."))
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground());
		}

		return SNew(SEnumComboBox, EnumType)
			.CurrentValue_Lambda([InitialValueHandle, EnumType]() -> int32
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				int64 Parsed = EnumType->GetValueByNameString(Value);
				if (Parsed == INDEX_NONE)
				{
					Parsed = Value.IsNumeric() ? FCString::Atoi64(*Value) : 0;
				}
				return static_cast<int32>(Parsed);
			})
			.OnEnumSelectionChanged_Lambda(
				[InitialValueHandle, EnumType](int32 NewIndex, ESelectInfo::Type)
			{
				const FName EntryName = EnumType->GetNameByValue(static_cast<int64>(NewIndex));
				InitialValueHandle->SetValue(EntryName.ToString());
			});
	}

	// Actor / Object - not supported 
	case EComposableCameraPinType::Actor:
	case EComposableCameraPinType::Object:
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("ActorObjectNotSupported",
				"Actor/Object cannot be set as a default value."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	// Transform - Location / Rotation / Scale rows 
	case EComposableCameraPinType::Transform:
	{
		return BuildTransformWidget(InitialValueHandle);
	}

	// Delegate - not applicable as a variable default 
	case EComposableCameraPinType::Delegate:
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("DelegateNoDefault",
				"Delegate parameters are bound at activation time."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	// Struct - text fallback 
	case EComposableCameraPinType::Struct:
	default:
	{
		FText Hint;
		if (StructType)
		{
			Hint = FText::Format(LOCTEXT("StructHint", "ImportText format for {0}"),
				FText::FromString(StructType->GetName()));
		}
		else
		{
			Hint = LOCTEXT("DefaultHint", "Enter value as text");
		}

		return SNew(SEditableTextBox)
			.Text_Lambda([InitialValueHandle]() -> FText
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				return FText::FromString(Value);
			})
			.OnTextCommitted_Lambda([InitialValueHandle](const FText& NewText, ETextCommit::Type)
			{
				InitialValueHandle->SetValue(NewText.ToString());
			})
			.HintText(Hint)
			.SelectAllTextWhenFocused(true)
			.ClearKeyboardFocusOnCommit(false);
	}
	}
}

// Transform Widget 

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildTransformWidget(TSharedPtr<IPropertyHandle> InitialValueHandle)
{
	if (!InitialValueHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Parse FTransform from the stored string. FTransform::ToString() produces:
	// (Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=0,Y=0,Z=0),Scale3D=(X=1,Y=1,Z=1))
	// and InitFromString round-trips it.
	auto ParseTransform = [](const FString& Str) -> FTransform
	{
		FTransform T = FTransform::Identity;
		if (!Str.IsEmpty())
		{
			T.InitFromString(Str);
		}
		return T;
	};

	// Helper: build one row of 3 labelled spinners for a sub-component
	// (Translation, Rotation, or Scale).
	struct FRowDesc
	{
		FText Label;
		// Getter: extract the 3 doubles from a FTransform.
		TFunction<FVector(const FTransform&)> Get;
		// Setter: write 3 doubles back into a FTransform.
		TFunction<void(FTransform&, const FVector&)> Set;
	};

	auto BuildRow = [InitialValueHandle, ParseTransform](const FText& RowLabel,
		const TFunction<FVector(const FTransform&)>& Getter,
		const TFunction<void(FTransform&, const FVector&)>& Setter) -> TSharedRef<SWidget>
	{
		static const TCHAR* XYZ[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

		// Row label (e.g. "Location")
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[SNew(STextBlock)
			.Text(RowLabel)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.MinDesiredWidth(52.f)];

		for (int32 C = 0; C < 3; ++C)
		{
			if (C > 0)
			{
				Row->AddSlot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
				[SNullWidget::NullWidget];
			}

			Row->AddSlot()
			.FillWidth(1.f)
			[SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 2.f, 0.f)
				[SNew(STextBlock)
					.Text(FText::FromString(XYZ[C]))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())]

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					[&]() -> TSharedRef<SWidget>
					{
						// Per-component drag cache. See TechDoc 7.2 - live drag
						// must not write the handle each tick or the resulting
						// PostEditChangeProperty cascade aborts the gesture.
						TSharedRef<TOptional<double>> DragCache = MakeShared<TOptional<double>>();
						return SNew(SNumericEntryBox<double>)
							.AllowSpin(true)
							.Value_Lambda([InitialValueHandle, ParseTransform, Getter, C, DragCache]() -> TOptional<double>
							{
								if (DragCache->IsSet()) return DragCache->GetValue();
								FString Str;
								InitialValueHandle->GetValue(Str);
								FVector V = Getter(ParseTransform(Str));
								return V[C];
							})
							.OnValueChanged_Lambda([DragCache](double NewValue)
							{
								*DragCache = NewValue;
							})
							.OnValueCommitted_Lambda(
								[InitialValueHandle, ParseTransform, Getter, Setter, C, DragCache]
								(double NewValue, ETextCommit::Type)
							{
								DragCache->Reset();
								FString Str;
								InitialValueHandle->GetValue(Str);
								FTransform T = ParseTransform(Str);
								FVector V = Getter(T);
								V[C] = NewValue;
								Setter(T, V);
								InitialValueHandle->SetValue(T.ToString());
							})
							.MinDesiredValueWidth(40.f);
					}()
				]];
		}

		return Row;
	};

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[BuildRow(LOCTEXT("TransformLocation", "Location"),
				[](const FTransform& T) -> FVector { return T.GetTranslation(); },
				[](FTransform& T, const FVector& V) { T.SetTranslation(V); })]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[BuildRow(LOCTEXT("TransformRotation", "Rotation"),
				[](const FTransform& T) -> FVector
				{
					FRotator R = T.Rotator();
					return FVector(R.Pitch, R.Yaw, R.Roll);
				},
				[](FTransform& T, const FVector& V)
				{
					T.SetRotation(FRotator(V.X, V.Y, V.Z).Quaternion());
				})]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[BuildRow(LOCTEXT("TransformScale", "Scale"),
				[](const FTransform& T) -> FVector { return T.GetScale3D(); },
				[](FTransform& T, const FVector& V) { T.SetScale3D(V); })];
}

// Struct Default Value (inline IStructureDetailsView) 

void FComposableCameraInternalVariableCustomization::BuildStructDefaultValueRows(IDetailChildrenBuilder& ChildBuilder,
	TSharedPtr<IPropertyHandle> InitialValueHandle,
	UScriptStruct* InStructType)
{
	if (!InitialValueHandle.IsValid() || !InStructType)
	{
		return;
	}

	// Allocate struct memory and initialize to the struct's default values.
	StructDefaultValueScope = MakeShared<FStructOnScope>(InStructType);

	// Parse the current InitialValueString into the struct memory.
	FString CurrentValue;
	InitialValueHandle->GetValue(CurrentValue);
	if (!CurrentValue.IsEmpty())
	{
		InStructType->ImportText(
			*CurrentValue,
			StructDefaultValueScope->GetStructMemory(),
			/*OwnerObject=*/ nullptr,
			PPF_None,
			GLog,
			InStructType->GetName(),
			/*bAllowNativeOverride=*/ true);
	}

	// Create a minimal IStructureDetailsView - no search, no options, no
	// scrollbar - so it embeds cleanly inside the parent details panel.
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

	StructDefaultValueView = PropertyModule.CreateStructureDetailView(ViewArgs, StructViewArgs, StructDefaultValueScope);

	// Serialize the struct memory back to InitialValueString whenever the
	// user finishes editing a property in the struct view.
	//
	// Capture only weak refs to objects we don't own - the embedded struct
	// view widget can outlive `this` (the customization is rebuilt whenever
	// the variable type changes; the struct view widget can survive into the
	// next tick on Slate's deferred-deletion path), and an event firing on
	// the dead customization's `this` would crash. `WeakScope` covers the
	// struct memory, and `InitialValueHandle` is intentionally captured by
	// TSharedPtr (NOT TWeakPtr) per the IPropertyHandle lifetime gotcha
	// (TechDoc Section 7.2): GetChildHandle() returns a fresh TSharedPtr each
	// call and the property tree does not retain a parallel strong ref, so
	// a weak capture would Pin() to null on every fire.
	TWeakPtr<FStructOnScope> WeakScope = StructDefaultValueScope;
	StructDefaultValueView->GetOnFinishedChangingPropertiesDelegate().AddLambda(
		[WeakScope, InitialValueHandle, InStructType](const FPropertyChangedEvent&)
		{
			TSharedPtr<FStructOnScope> ScopePinned = WeakScope.Pin();
			if (!ScopePinned.IsValid()
				|| !InitialValueHandle.IsValid()
				|| !InitialValueHandle->IsValidHandle())
			{
				return;
			}
			FString NewValue;
			InStructType->ExportText(NewValue,
				ScopePinned->GetStructMemory(),
				/*Defaults=*/ nullptr,
				/*OwnerObject=*/ nullptr,
				PPF_None,
				/*ExportRootScope=*/ nullptr);
			InitialValueHandle->SetValue(NewValue);
		});

	// Embed the struct view widget as a whole-row custom row so it takes
	// the full width rather than being crammed into the value column.
	TSharedPtr<SWidget> ViewWidget = StructDefaultValueView->GetWidget();

	ChildBuilder.AddCustomRow(LOCTEXT("StructInitialValueSearch", "Initial Value"))
	.WholeRowContent()
	.MinDesiredWidth(300.f)
	[SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 2.f, 0.f, 4.f)
		[SNew(STextBlock)
			.Text(LOCTEXT("StructInitialValueLabel", "Initial Value"))
			.Font(IDetailLayoutBuilder::GetDetailFont())]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[ViewWidget.IsValid() ? ViewWidget.ToSharedRef() : SNullWidget::NullWidget]];
}

// Multi-Component Numeric Widget 

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildNumericComponentWidget(TSharedPtr<IPropertyHandle> InitialValueHandle,
	int32 ComponentIndex,
	int32 NumComponents,
	const TCHAR* const* ComponentLabels,
	const TCHAR* Prefix)
{
	// Parse the string into components. The format varies by type but all
	// use the UE standard "Name=Value" format. We parse via the specific
	// struct's InitFromString to stay in sync with the runtime parser.
	//
	// Read: parse full string -> extract component
	// Write: parse full string -> replace component -> write back

	auto ParseComponents = [NumComponents, Prefix](const FString& Str, TArray<double>& Out)
	{
		Out.SetNumZeroed(NumComponents);

		if (Str.IsEmpty())
		{
			return;
		}

		// Try parsing as the native struct type first.
		if (NumComponents == 2)
		{
			FVector2D V;
			if (V.InitFromString(Str))
			{
				Out[0] = V.X;
				Out[1] = V.Y;
				return;
			}
		}
		else if (NumComponents == 3)
		{
			// Try FVector first, then FRotator.
			FVector V;
			if (V.InitFromString(Str))
			{
				Out[0] = V.X;
				Out[1] = V.Y;
				Out[2] = V.Z;
				return;
			}
			FRotator R;
			if (R.InitFromString(Str))
			{
				Out[0] = R.Pitch;
				Out[1] = R.Yaw;
				Out[2] = R.Roll;
				return;
			}
		}
		else if (NumComponents == 4)
		{
			FVector4 V;
			if (V.InitFromString(Str))
			{
				Out[0] = V.X;
				Out[1] = V.Y;
				Out[2] = V.Z;
				Out[3] = V.W;
				return;
			}
		}
	};

	// Per-widget drag cache. See TechDoc 7.2 - live drag must not write the
	// IPropertyHandle each tick or PostEditChangeProperty broadcasts will
	// trigger a refresh cascade that abort the gesture after the first move.
	// Cache holds the in-flight component value; Value_Lambda returns it
	// during drag; OnValueCommitted writes the full reassembled struct
	// string once at gesture end.
	TSharedRef<TOptional<double>> DragCache = MakeShared<TOptional<double>>();

	auto CommitFromCache = [InitialValueHandle, ComponentIndex, NumComponents, Prefix, ParseComponents]
		(double NewValue)
	{
		FString Value;
		InitialValueHandle->GetValue(Value);
		TArray<double> Components;
		ParseComponents(Value, Components);
		if (Components.IsValidIndex(ComponentIndex))
		{
			Components[ComponentIndex] = NewValue;
		}

		// Rebuild the string in the canonical format for this type.
		FString Result;
		if (NumComponents == 2)
		{
			Result = FVector2D(Components[0], Components[1]).ToString();
		}
		else if (NumComponents == 3)
		{
			// Detect Rotator vs Vector by prefix hint.
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

		InitialValueHandle->SetValue(Result);
	};

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 2.f, 0.f)
		[SNew(STextBlock)
			.Text(FText::FromString(ComponentLabels[ComponentIndex]))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.Value_Lambda([InitialValueHandle, ComponentIndex, NumComponents, ParseComponents, DragCache]()
				-> TOptional<double>
			{
				if (DragCache->IsSet()) return DragCache->GetValue();
				FString Value;
				InitialValueHandle->GetValue(Value);
				TArray<double> Components;
				ParseComponents(Value, Components);
				return Components.IsValidIndex(ComponentIndex)
					? Components[ComponentIndex]
					: 0.0;
			})
			.OnValueChanged_Lambda([DragCache](double NewValue)
			{
				*DragCache = NewValue;
			})
			.OnValueCommitted_Lambda([DragCache, CommitFromCache](double NewValue, ETextCommit::Type)
			{
				DragCache->Reset();
				CommitFromCache(NewValue);
			})
			.MinDesiredValueWidth(40.f)];
}

// Enum Type Picker 

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildEnumTypePicker(TSharedPtr<IPropertyHandle> EnumTypeHandle)
{
	if (!EnumTypeHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	return SNew(SComboButton)
		.ContentPadding(FMargin(4.f, 1.f))
		.OnGetMenuContent_Lambda([this, EnumTypeHandle]()
		{
			return BuildEnumTypeMenu(EnumTypeHandle);
		})
		.ButtonContent()
		[SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([this, EnumTypeHandle]()
			{
				return GetEnumTypeButtonText(EnumTypeHandle);
			})];
}

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildEnumTypeMenu(TSharedPtr<IPropertyHandle> EnumTypeHandle)
{
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	// "None" entry first -- clears the selection.
	MenuBuilder.AddMenuEntry(LOCTEXT("EnumTypeNone", "None"),
		LOCTEXT("EnumTypeNoneTooltip", "Clear the selected enum type."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([EnumTypeHandle]()
		{
			if (EnumTypeHandle.IsValid())
			{
				EnumTypeHandle->SetValue(static_cast<UObject*>(nullptr));
			}
		})));

	MenuBuilder.AddMenuSeparator();

	// Walk every loaded UEnum and use the canonical BP-variable-type filter
	// (`UEdGraphSchema_K2::IsAllowableBlueprintVariableType`) -- the same
	// gate the Blueprint editor's variable-type picker uses. It accepts
	// UUserDefinedEnum (BP-defined) unconditionally and any native UEnum
	// flagged `UENUM(BlueprintType)` (`GetBoolMetaData("BlueprintType")`),
	// while rejecting hidden / deprecated / non-exposable enums. Mirroring
	// this filter keeps the menu consistent with what BP authors are used
	// to seeing elsewhere.
	TArray<UEnum*> SortedEnums;
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		UEnum* EnumCandidate = *It;
		if (!EnumCandidate)
		{
			continue;
		}
		if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(EnumCandidate))
		{
			continue;
		}
		SortedEnums.Add(EnumCandidate);
	}

	SortedEnums.Sort([](const UEnum& A, const UEnum& B)
	{
		return A.GetName().Compare(B.GetName(), ESearchCase::IgnoreCase) < 0;
	});

	for (UEnum* EnumValue: SortedEnums)
	{
		// Capture by value -- ownership stays with the engine; weak-ref
		// not needed because UEnum lifetime tracks the engine, not the
		// menu widget.
		MenuBuilder.AddMenuEntry(FText::FromString(EnumValue->GetName()),
			FText::FromString(EnumValue->GetPathName()),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([EnumTypeHandle, EnumValue]()
			{
				if (EnumTypeHandle.IsValid())
				{
					EnumTypeHandle->SetValue(static_cast<UObject*>(EnumValue));
				}
			})));
	}

	return MenuBuilder.MakeWidget();
}

FText FComposableCameraInternalVariableCustomization::GetEnumTypeButtonText(TSharedPtr<IPropertyHandle> EnumTypeHandle) const
{
	if (!EnumTypeHandle.IsValid())
	{
		return LOCTEXT("EnumTypeNone", "None");
	}
	UObject* CurrentValue = nullptr;
	EnumTypeHandle->GetValue(CurrentValue);
	if (const UEnum* CurrentEnum = Cast<UEnum>(CurrentValue))
	{
		return FText::FromString(CurrentEnum->GetName());
	}
	return LOCTEXT("EnumTypeNone", "None");
}

#undef LOCTEXT_NAMESPACE
