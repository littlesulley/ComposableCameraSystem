// Copyright Sulley. All rights reserved.

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
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComposableCameraInternalVariableCustomization"

// ─── Static Registration ────────────────────────────────────────────────

TSharedRef<IPropertyTypeCustomization> FComposableCameraInternalVariableCustomization::MakeInstance()
{
	return MakeShared<FComposableCameraInternalVariableCustomization>();
}

void FComposableCameraInternalVariableCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FComposableCameraInternalVariable::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FComposableCameraInternalVariableCustomization::MakeInstance));
}

void FComposableCameraInternalVariableCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(
			FComposableCameraInternalVariable::StaticStruct()->GetFName());
	}
}

// ─── Header ──────────────────────────────────────────────────────────────

void FComposableCameraInternalVariableCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& Utils)
{
	StructHandle = PropertyHandle;
	PropertyUtilities = Utils.GetPropertyUtilities();

	// Display the VariableName as the header so the array element reads as
	// a named entry rather than "[0]", "[1]", etc.
	TSharedPtr<IPropertyHandle> NameHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableName));

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(120.f)
	[
		NameHandle.IsValid()
			? NameHandle->CreatePropertyValueWidget()
			: SNullWidget::NullWidget
	];
}

// ─── Children ────────────────────────────────────────────────────────────

void FComposableCameraInternalVariableCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& Utils)
{
	// Resolve the type handles we need to read the current VariableType.
	TSharedPtr<IPropertyHandle> TypeHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableType));
	TSharedPtr<IPropertyHandle> StructTypeHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, StructType));
	TSharedPtr<IPropertyHandle> InitialValueHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, InitialValueString));

	// When VariableType changes, rebuild the customization so the
	// InitialValueString widget adapts to the new type.
	if (TypeHandle.IsValid())
	{
		TypeHandle->SetOnPropertyValueChanged(
			FSimpleDelegate::CreateLambda([Utils = PropertyUtilities]()
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
		StructTypeHandle->SetOnPropertyValueChanged(
			FSimpleDelegate::CreateLambda([Utils = PropertyUtilities]()
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

		// Skip VariableGuid — it's an internal editor identity field, not user-facing.
		if (ChildHandle->GetProperty()->GetFName() ==
			GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableGuid))
		{
			continue;
		}

		// Skip VariableName — already shown in the header.
		if (ChildHandle->GetProperty()->GetFName() ==
			GET_MEMBER_NAME_CHECKED(FComposableCameraInternalVariable, VariableName))
		{
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
				[
					SNew(STextBlock)
					.Text(LOCTEXT("InitialValueLabel", "Initial Value"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(200.f)
				.MaxDesiredWidth(600.f)
				[
					BuildTypedDefaultValueWidget(InitialValueHandle, PinType, CurrentStructType)
				];
			}

			continue;
		}

		// All other properties: add with default rendering.
		ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
	}
}

// ─── Typed Widget Builder ────────────────────────────────────────────────

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildTypedDefaultValueWidget(
	TSharedPtr<IPropertyHandle> InitialValueHandle,
	EComposableCameraPinType PinType,
	UScriptStruct* StructType)
{
	if (!InitialValueHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	switch (PinType)
	{
	// ── Bool ─────────────────────────────────────────────────────────
	case EComposableCameraPinType::Bool:
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([InitialValueHandle]() -> ECheckBoxState
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([InitialValueHandle](ECheckBoxState NewState)
			{
				InitialValueHandle->SetValue(
					NewState == ECheckBoxState::Checked
						? FString(TEXT("true"))
						: FString(TEXT("false")));
			});
	}

	// ── Int32 ────────────────────────────────────────────────────────
	case EComposableCameraPinType::Int32:
	{
		return SNew(SNumericEntryBox<int32>)
			.AllowSpin(true)
			.Value_Lambda([InitialValueHandle]() -> TOptional<int32>
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				if (Value.IsEmpty()) return 0;
				return FCString::Atoi(*Value);
			})
			.OnValueCommitted_Lambda([InitialValueHandle](int32 NewValue, ETextCommit::Type)
			{
				InitialValueHandle->SetValue(FString::FromInt(NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// ── Float ────────────────────────────────────────────────────────
	case EComposableCameraPinType::Float:
	{
		return SNew(SNumericEntryBox<float>)
			.AllowSpin(true)
			.Value_Lambda([InitialValueHandle]() -> TOptional<float>
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				if (Value.IsEmpty()) return 0.f;
				return FCString::Atof(*Value);
			})
			.OnValueCommitted_Lambda([InitialValueHandle](float NewValue, ETextCommit::Type)
			{
				InitialValueHandle->SetValue(FString::SanitizeFloat(NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// ── Double ───────────────────────────────────────────────────────
	case EComposableCameraPinType::Double:
	{
		return SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.Value_Lambda([InitialValueHandle]() -> TOptional<double>
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				if (Value.IsEmpty()) return 0.0;
				return FCString::Atod(*Value);
			})
			.OnValueCommitted_Lambda([InitialValueHandle](double NewValue, ETextCommit::Type)
			{
				InitialValueHandle->SetValue(FString::Printf(TEXT("%.6f"), NewValue));
			})
			.MinDesiredValueWidth(60.f);
	}

	// ── Vector2D ─────────────────────────────────────────────────────
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
				[
					SNullWidget::NullWidget
				];
			}
			Box->AddSlot().FillWidth(1.f)
			[
				BuildNumericComponentWidget(InitialValueHandle, C, 2, Labels, Prefix)
			];
		}
		return Box;
	}

	// ── Vector3D ─────────────────────────────────────────────────────
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
				[
					SNullWidget::NullWidget
				];
			}
			Box->AddSlot().FillWidth(1.f)
			[
				BuildNumericComponentWidget(InitialValueHandle, C, 3, Labels, Prefix)
			];
		}
		return Box;
	}

	// ── Vector4 ──────────────────────────────────────────────────────
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
				[
					SNullWidget::NullWidget
				];
			}
			Box->AddSlot().FillWidth(1.f)
			[
				BuildNumericComponentWidget(InitialValueHandle, C, 4, Labels, Prefix)
			];
		}
		return Box;
	}

	// ── Rotator ──────────────────────────────────────────────────────
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
				[
					SNullWidget::NullWidget
				];
			}
			Box->AddSlot().FillWidth(1.f)
			[
				BuildNumericComponentWidget(InitialValueHandle, C, 3, Labels, Prefix)
			];
		}
		return Box;
	}

	// ── Actor / Object — not supported ──────────────────────────────
	case EComposableCameraPinType::Actor:
	case EComposableCameraPinType::Object:
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("ActorObjectNotSupported",
				"Actor/Object cannot be set as a default value."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	// ── Transform — Location / Rotation / Scale rows ────────────────
	case EComposableCameraPinType::Transform:
	{
		return BuildTransformWidget(InitialValueHandle);
	}

	// ── Struct — text fallback ──────────────────────────────────────
	case EComposableCameraPinType::Struct:
	default:
	{
		FText Hint;
		if (StructType)
		{
			Hint = FText::Format(
				LOCTEXT("StructHint", "ImportText format for {0}"),
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

// ─── Transform Widget ───────────────────────────────────────────────────

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildTransformWidget(
	TSharedPtr<IPropertyHandle> InitialValueHandle)
{
	if (!InitialValueHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Parse FTransform from the stored string. FTransform::ToString() produces:
	//   (Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=0,Y=0,Z=0),Scale3D=(X=1,Y=1,Z=1))
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

	auto BuildRow = [InitialValueHandle, ParseTransform](
		const FText& RowLabel,
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
		[
			SNew(STextBlock)
			.Text(RowLabel)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.MinDesiredWidth(52.f)
		];

		for (int32 C = 0; C < 3; ++C)
		{
			if (C > 0)
			{
				Row->AddSlot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNullWidget::NullWidget
				];
			}

			Row->AddSlot()
			.FillWidth(1.f)
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
					.Value_Lambda([InitialValueHandle, ParseTransform, Getter, C]() -> TOptional<double>
					{
						FString Str;
						InitialValueHandle->GetValue(Str);
						FVector V = Getter(ParseTransform(Str));
						return V[C];
					})
					.OnValueCommitted_Lambda(
						[InitialValueHandle, ParseTransform, Getter, Setter, C]
						(double NewValue, ETextCommit::Type)
					{
						FString Str;
						InitialValueHandle->GetValue(Str);
						FTransform T = ParseTransform(Str);
						FVector V = Getter(T);
						V[C] = NewValue;
						Setter(T, V);
						InitialValueHandle->SetValue(T.ToString());
					})
					.MinDesiredValueWidth(40.f)
				]
			];
		}

		return Row;
	};

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[
			BuildRow(
				LOCTEXT("TransformLocation", "Location"),
				[](const FTransform& T) -> FVector { return T.GetTranslation(); },
				[](FTransform& T, const FVector& V) { T.SetTranslation(V); })
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
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

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 1.f)
		[
			BuildRow(
				LOCTEXT("TransformScale", "Scale"),
				[](const FTransform& T) -> FVector { return T.GetScale3D(); },
				[](FTransform& T, const FVector& V) { T.SetScale3D(V); })
		];
}

// ─── Struct Default Value (inline IStructureDetailsView) ─────────────────

void FComposableCameraInternalVariableCustomization::BuildStructDefaultValueRows(
	IDetailChildrenBuilder& ChildBuilder,
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

	// Create a minimal IStructureDetailsView — no search, no options, no
	// scrollbar — so it embeds cleanly inside the parent details panel.
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

	StructDefaultValueView = PropertyModule.CreateStructureDetailView(
		ViewArgs, StructViewArgs, StructDefaultValueScope);

	// Serialize the struct memory back to InitialValueString whenever the
	// user finishes editing a property in the struct view.
	StructDefaultValueView->GetOnFinishedChangingPropertiesDelegate().AddLambda(
		[this, InitialValueHandle, InStructType](const FPropertyChangedEvent&)
		{
			if (StructDefaultValueScope.IsValid())
			{
				FString NewValue;
				InStructType->ExportText(
					NewValue,
					StructDefaultValueScope->GetStructMemory(),
					/*Defaults=*/ nullptr,
					/*OwnerObject=*/ nullptr,
					PPF_None,
					/*ExportRootScope=*/ nullptr);
				InitialValueHandle->SetValue(NewValue);
			}
		});

	// Embed the struct view widget as a whole-row custom row so it takes
	// the full width rather than being crammed into the value column.
	TSharedPtr<SWidget> ViewWidget = StructDefaultValueView->GetWidget();

	ChildBuilder.AddCustomRow(LOCTEXT("StructInitialValueSearch", "Initial Value"))
	.WholeRowContent()
	.MinDesiredWidth(300.f)
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 2.f, 0.f, 4.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StructInitialValueLabel", "Initial Value"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ViewWidget.IsValid() ? ViewWidget.ToSharedRef() : SNullWidget::NullWidget
		]
	];
}

// ─── Multi-Component Numeric Widget ──────────────────────────────────────

TSharedRef<SWidget> FComposableCameraInternalVariableCustomization::BuildNumericComponentWidget(
	TSharedPtr<IPropertyHandle> InitialValueHandle,
	int32 ComponentIndex,
	int32 NumComponents,
	const TCHAR* const* ComponentLabels,
	const TCHAR* Prefix)
{
	// Parse the string into components. The format varies by type but all
	// use the UE standard "Name=Value" format. We parse via the specific
	// struct's InitFromString to stay in sync with the runtime parser.
	//
	// Read: parse full string → extract component
	// Write: parse full string → replace component → write back

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
			.Value_Lambda([InitialValueHandle, ComponentIndex, NumComponents, ParseComponents]()
				-> TOptional<double>
			{
				FString Value;
				InitialValueHandle->GetValue(Value);
				TArray<double> Components;
				ParseComponents(Value, Components);
				return Components.IsValidIndex(ComponentIndex)
					? Components[ComponentIndex]
					: 0.0;
			})
			.OnValueCommitted_Lambda(
				[InitialValueHandle, ComponentIndex, NumComponents, Prefix, ParseComponents, ComponentLabels]
				(double NewValue, ETextCommit::Type)
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
			})
			.MinDesiredValueWidth(40.f)
		];
}

#undef LOCTEXT_NAMESPACE
