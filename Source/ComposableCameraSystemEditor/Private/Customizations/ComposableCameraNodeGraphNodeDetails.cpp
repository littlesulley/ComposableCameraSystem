// Copyright Sulley. All rights reserved.

#include "Customizations/ComposableCameraNodeGraphNodeDetails.h"

#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraNodePinTypes.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraph.h"
#include "IDetailPropertyRow.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComposableCameraNodeGraphNodeDetails"

TSharedRef<IDetailCustomization> FComposableCameraNodeGraphNodeDetails::MakeInstance()
{
	return MakeShared<FComposableCameraNodeGraphNodeDetails>();
}

void FComposableCameraNodeGraphNodeDetails::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomClassLayout(
		UComposableCameraNodeGraphNode::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FComposableCameraNodeGraphNodeDetails::MakeInstance));
}

void FComposableCameraNodeGraphNodeDetails::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomClassLayout(
			UComposableCameraNodeGraphNode::StaticClass()->GetFName());
	}
}

void FComposableCameraNodeGraphNodeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Resolve the single graph node. Multi-select bails because per-pin
	// state isn't meaningful across a heterogeneous selection.
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}

	UComposableCameraNodeGraphNode* GraphNode = Cast<UComposableCameraNodeGraphNode>(Objects[0].Get());
	if (!GraphNode || !GraphNode->NodeTemplate)
	{
		return;
	}

	WeakGraphNode = GraphNode;

	// Hide graph-node-level plumbing from the auto-generated grid.
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraNodeGraphNode, NodeTemplate));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraNodeGraphNode, NodeIndex));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UComposableCameraNodeGraphNode, RuntimePinOverrides));

	// ── Build declared-input-pin lookup ─────────────────────────────────
	TArray<FComposableCameraNodePinDeclaration> Declarations;
	GraphNode->NodeTemplate->GatherAllPinDeclarations(Declarations);

	// Map: PinName → declaration index (input pins only).
	TMap<FName, int32> InputPinNameToIndex;
	for (int32 i = 0; i < Declarations.Num(); ++i)
	{
		if (Declarations[i].Direction == EComposableCameraPinDirection::Input)
		{
			InputPinNameToIndex.Add(Declarations[i].PinName, i);
		}
	}

	// ── Match UPROPERTYs to declared input pins ─────────────────────────
	//
	// For each EditAnywhere UPROPERTY on the node template, check if its
	// name matches a declared input pin name exactly. If so, the Details
	// row gets the "As Pin / [Exposed]" control strip and the native
	// value widget is used as the default-value editor.
	//
	// PropNameToPinName maps each matched UPROPERTY FName to the pin FName
	// it represents. MatchedPinNames tracks which pins got matched so we
	// can emit string-fallback rows for the rest.
	TMap<FName, FName> PropNameToPinName;
	TSet<FName> MatchedPinNames;

	for (TFieldIterator<FProperty> PropIt(GraphNode->NodeTemplate->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		const FName PropName = Property->GetFName();

		if (InputPinNameToIndex.Contains(PropName))
		{
			PropNameToPinName.Add(PropName, PropName);
			MatchedPinNames.Add(PropName);
		}
	}

	// ── Unified "Properties" category ───────────────────────────────────
	IDetailCategoryBuilder& PropertiesCategory = DetailBuilder.EditCategory(
		TEXT("Properties"),
		LOCTEXT("PropertiesCategory", "Properties"),
		ECategoryPriority::Important);

	const TArray<UObject*> ExternalObjects{ GraphNode->NodeTemplate.Get() };

	// ── Helper: build the "As Pin" + "[Exposed]" widget strip ───────────
	//
	// Reused by both the inline (CustomWidget) path and the sub-row fallback.
	auto BuildPinControlStrip = [this](FName PinName) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, PinName]()
				{
					return GetAsPinCheckState(PinName);
				})
				.OnCheckStateChanged_Lambda([this, PinName](ECheckBoxState NewState)
				{
					OnAsPinCheckChanged(NewState, PinName);
				})
				.ToolTipText(LOCTEXT("AsPinCheckboxTooltip",
					"When checked, this input appears as a wireable / exposable pin on "
					"the graph node. When unchecked, the pin is hidden from the graph and "
					"the property value becomes a constant — turning this off on a wired "
					"or exposed pin auto-breaks the wire and auto-unexposes."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AsPinLabel", "As Pin"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExposedChip", "[Exposed]"))
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this, PinName]()
				{
					return GetExposedChipVisibility(PinName);
				})
				.ToolTipText(LOCTEXT("ExposedChipTooltip",
					"This pin is exposed as a camera parameter. The value shown is the "
					"per-instance fallback; the runtime uses the exposed parameter value "
					"when one is provided."))
			];
	};

	// ── Detect subobject pin prefixes ──────────────────────────────────
	//
	// Compound pin names like "PushInterpolator.Speed" indicate subobject
	// property pins. Build a set of prefixes (e.g. "PushInterpolator.")
	// so we can identify Instanced UPROPERTYs that have exposable children.
	TSet<FString> SubobjectPinPrefixes;
	for (const auto& Pair : InputPinNameToIndex)
	{
		const FString PinNameStr = Pair.Key.ToString();
		int32 DotIndex;
		if (PinNameStr.FindChar(TEXT('.'), DotIndex))
		{
			SubobjectPinPrefixes.Add(PinNameStr.Left(DotIndex + 1));
		}
	}

	// ── Surface NodeTemplate UPROPERTYs ─────────────────────────────────
	//
	// Each UPROPERTY is added via AddExternalObjectProperty.
	//
	// Three property categories are handled differently:
	//
	//   A. Top-level pin-matched properties → CustomWidget(true) inlines
	//      "As Pin" + "[Exposed]" controls on the same row.
	//
	//   B. Instanced UObject properties with subobject pins → the parent
	//      row is added normally (class picker visible), then each of the
	//      subobject's child properties is added as a separate external
	//      property row, with pin controls on matched children.
	//
	//   C. Non-pin UPROPERTYs → plain native row, no controls.
	//
	// Every external-object property row gets a ForceRefreshDetails
	// callback so that EditCondition and subobject class changes trigger
	// a detail-panel rebuild.
	for (TFieldIterator<FProperty> PropIt(GraphNode->NodeTemplate->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		const FName PropName = Property->GetFName();

		// ── Check if this is an Instanced subobject with compound pins ──
		const FString PropPrefix = PropName.ToString() + TEXT(".");
		const bool bIsSubobjectWithPins =
			Property->HasAllPropertyFlags(CPF_InstancedReference) &&
			SubobjectPinPrefixes.Contains(PropPrefix);

		if (bIsSubobjectWithPins)
		{
			// ── Category B: Instanced subobject with exposable children.
			//
			// The parent row shows the class picker; its native child expansion
			// is suppressed via CustomWidget(false) so that we can add the
			// children ourselves with inline pin controls.

			IDetailPropertyRow* ParentRow = PropertiesCategory.AddExternalObjectProperty(
				ExternalObjects, PropName);
			if (ParentRow)
			{
				// Extract the class-picker value widget before switching to
				// custom mode (GetDefaultWidgets must be called first).
				TSharedPtr<SWidget> ParentNameWidget;
				TSharedPtr<SWidget> ParentValueWidget;
				ParentRow->GetDefaultWidgets(ParentNameWidget, ParentValueWidget);

				// Show only the class picker — no native child expansion.
				ParentRow->CustomWidget(/*bShowChildren=*/ false)
				.NameContent()
				[
					SNew(STextBlock)
					.Text(Property->GetDisplayNameText())
					.ToolTipText(Property->GetToolTipText())
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				]
				.ValueContent()
				[
					ParentValueWidget.ToSharedRef()
				];

				if (TSharedPtr<IPropertyHandle> Handle = ParentRow->GetPropertyHandle())
				{
					// When the class picker changes, the subobject's properties
					// (and therefore the compound pins) change entirely. Reconstruct
					// the graph node's pins first so the new declarations are in
					// place, then refresh the Details panel to show the new children.
					Handle->SetOnPropertyValueChanged(
						FSimpleDelegate::CreateLambda([this, &DetailBuilder]()
						{
							if (UComposableCameraNodeGraphNode* GN = GetGraphNode())
							{
								GN->ReconstructPins();
								if (UEdGraph* OwningGraph = GN->GetGraph())
								{
									OwningGraph->NotifyGraphChanged();
								}
							}
							DetailBuilder.ForceRefreshDetails();
						}));
				}
			}

			// Resolve the subobject pointer to add its child properties.
			const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property);
			UObject* Subobject = ObjProp
				? ObjProp->GetObjectPropertyValue(
					ObjProp->ContainerPtrToValuePtr<void>(GraphNode->NodeTemplate.Get()))
				: nullptr;

			if (!Subobject)
			{
				continue;
			}

			// Add each child property as an indented external-object row.
			// All children get left padding so they visually nest under the
			// parent class picker. Pin-matched children additionally receive
			// the inline "As Pin" + "[Exposed]" control strip.
			const TArray<UObject*> SubExternalObjects{ Subobject };
			for (TFieldIterator<FProperty> SubPropIt(Subobject->GetClass()); SubPropIt; ++SubPropIt)
			{
				FProperty* SubProperty = *SubPropIt;
				if (!SubProperty || !SubProperty->HasAnyPropertyFlags(CPF_Edit))
				{
					continue;
				}

				const FName SubPropName = SubProperty->GetFName();
				IDetailPropertyRow* SubRow = PropertiesCategory.AddExternalObjectProperty(
					SubExternalObjects, SubPropName);
				if (!SubRow)
				{
					continue;
				}

				// Attach refresh callback for subobject property changes.
				if (TSharedPtr<IPropertyHandle> SubHandle = SubRow->GetPropertyHandle())
				{
					SubHandle->SetOnPropertyValueChanged(
						FSimpleDelegate::CreateLambda([&DetailBuilder]()
						{
							DetailBuilder.ForceRefreshDetails();
						}));
				}

				// Extract the native value widget before CustomWidget.
				TSharedPtr<SWidget> SubNameWidget;
				TSharedPtr<SWidget> SubValueWidget;
				SubRow->GetDefaultWidgets(SubNameWidget, SubValueWidget);

				// Indented name widget — shared by both pin-matched and plain children.
				TSharedRef<SWidget> IndentedName =
					SNew(SBox)
					.Padding(FMargin(12.f, 0.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Text(SubProperty->GetDisplayNameText())
						.ToolTipText(SubProperty->GetToolTipText())
						.Font(IDetailLayoutBuilder::GetDetailFont())
					];

				// Check if this child property has a compound pin name.
				const FName CompoundPinName(*(PropPrefix + SubProperty->GetName()));
				const bool bHasPin = InputPinNameToIndex.Contains(CompoundPinName);

				if (bHasPin)
				{
					MatchedPinNames.Add(CompoundPinName);

					// Same decoupling pattern as Category A: REPLACE the row's
					// underlying EditCondition with an always-true attribute so
					// the pin control strip stays interactive regardless of
					// meta=EditCondition on the subobject child property.
					// IDetailPropertyRow::IsEnabled only narrows, it cannot
					// widen — EditCondition override is the only way to break
					// the auto-disable cascade. The native value widget below
					// is still greyed via IPropertyHandle::IsEditable() when
					// the real EditCondition evaluates to false.
					SubRow->EditCondition(TAttribute<bool>(true), FOnBooleanValueChanged());
					TSharedPtr<IPropertyHandle> SubRowHandle = SubRow->GetPropertyHandle();

					// Pin-matched child: indented name + value + pin controls.
					SubRow->CustomWidget(/*bShowChildren=*/ true)
					.NameContent()
					[
						IndentedName
					]
					.ValueContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.IsEnabled_Lambda([SubRowHandle]()
							{
								return SubRowHandle.IsValid() && SubRowHandle->IsEditable();
							})
							[
								SubValueWidget.ToSharedRef()
							]
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(8.f, 0.f, 0.f, 0.f)
						[
							BuildPinControlStrip(CompoundPinName)
						]
					];
				}
				else
				{
					// Non-pin child: indented name + native value, no pin controls.
					SubRow->CustomWidget(/*bShowChildren=*/ true)
					.NameContent()
					[
						IndentedName
					]
					.ValueContent()
					[
						SubValueWidget.ToSharedRef()
					];
				}
			}

			continue; // Parent + children handled, skip the top-level pin logic.
		}

		// ── Category A & C: normal top-level properties ─────────────────

		IDetailPropertyRow* Row = PropertiesCategory.AddExternalObjectProperty(
			ExternalObjects, PropName);
		if (!Row)
		{
			continue;
		}

		// Force a full detail rebuild when any NodeTemplate property
		// changes, so EditCondition on sibling external properties is
		// re-evaluated (e.g. Method change reveals RelativeActor).
		//
		// Instanced properties (interpolators, etc.) need extra handling:
		// when the subobject is initially None, no compound pins are
		// declared, so the property lands here instead of in Category B.
		// If the user then assigns a class, the new subobject's child
		// properties should become available as compound pins. We must
		// call ReconstructPins to pick those up, not just refresh the
		// Details panel.
		const bool bIsInstancedRef = Property->HasAnyPropertyFlags(CPF_InstancedReference);
		if (TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle())
		{
			if (bIsInstancedRef)
			{
				Handle->SetOnPropertyValueChanged(
					FSimpleDelegate::CreateLambda([this, &DetailBuilder]()
					{
						if (UComposableCameraNodeGraphNode* GN = GetGraphNode())
						{
							GN->ReconstructPins();
							if (UEdGraph* OwningGraph = GN->GetGraph())
							{
								OwningGraph->NotifyGraphChanged();
							}
						}
						DetailBuilder.ForceRefreshDetails();
					}));
			}
			else
			{
				Handle->SetOnPropertyValueChanged(
					FSimpleDelegate::CreateLambda([&DetailBuilder]()
					{
						DetailBuilder.ForceRefreshDetails();
					}));
			}
		}

		const FName* FoundPinName = PropNameToPinName.Find(PropName);
		if (!FoundPinName)
		{
			// Category C: non-pin UPROPERTY — plain native row, no controls.
			continue;
		}

		// Category A: top-level pin-matched property.
		const FName PinName = *FoundPinName;

		// Decouple the row-wide enabled state from the property's
		// meta=EditCondition so the inline "As Pin" checkbox never greys out
		// even when a sibling EditCondition evaluates to false (e.g.
		// FilmbackNode's AspectRatioAxisConstraint gated by
		// bOverrideAspectRatioAxisConstraint). IDetailPropertyRow::IsEnabled
		// AND-combines with the auto-computed enabled state, so it can only
		// narrow, not widen — instead we REPLACE the underlying EditCondition
		// with an always-true attribute and no setter. This breaks the row's
		// auto-disable cascade entirely. The native ValueWidget below is
		// wrapped in an IsEnabled-driven SBox keyed off IPropertyHandle::
		// IsEditable(), which still reflects the real metadata-driven state
		// independent of this override — so only the pin control strip stays
		// interactive when the property is non-editable.
		Row->EditCondition(TAttribute<bool>(true), FOnBooleanValueChanged());
		TSharedPtr<IPropertyHandle> RowHandle = Row->GetPropertyHandle();

		// Extract the default value widget from the native row BEFORE
		// switching to CustomWidget mode. The name widget is built
		// manually — GetDefaultWidgets yields an empty/broken name for
		// some struct types.
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> ValueWidget;
		Row->GetDefaultWidgets(NameWidget, ValueWidget);

		Row->CustomWidget(/*bShowChildren=*/ true)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(Property->GetDisplayNameText())
			.ToolTipText(Property->GetToolTipText())
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				// EditCondition / CPF_EditConst greying applied ONLY here, not
				// to the pin control strip to the right.
				SNew(SBox)
				.IsEnabled_Lambda([RowHandle]()
				{
					return RowHandle.IsValid() && RowHandle->IsEditable();
				})
				[
					ValueWidget.ToSharedRef()
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				BuildPinControlStrip(PinName)
			]
		];
	}

	// ── Fallback rows for unmatched pins ────────────────────────────────
	//
	// Declared input pins that had no matching UPROPERTY (neither exact
	// nor Context-prefixed). Scalar types get a string-based text editor;
	// Actor/Object types get a read-only "None" label since a runtime object
	// reference cannot be meaningfully authored as a string.
	for (const FComposableCameraNodePinDeclaration& Decl : Declarations)
	{
		if (Decl.Direction != EComposableCameraPinDirection::Input)
		{
			continue;
		}
		if (MatchedPinNames.Contains(Decl.PinName))
		{
			continue;
		}

		const FName PinName = Decl.PinName;
		const FText DisplayName = Decl.DisplayName.IsEmpty()
			? FText::FromName(PinName)
			: Decl.DisplayName;
		const FString ClassDefault = Decl.DefaultValueString;

		// Build the value widget: text editor for scalars, read-only label
		// for Actor/Object types that can only be wired or exposed.
		const bool bIsObjectType =
			Decl.PinType == EComposableCameraPinType::Actor ||
			Decl.PinType == EComposableCameraPinType::Object;

		TSharedRef<SWidget> ValueWidget = bIsObjectType
			? StaticCastSharedRef<SWidget>(
				SNew(STextBlock)
				.Text(LOCTEXT("ObjectPinNone", "None"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.ToolTipText(LOCTEXT("ObjectPinFallbackTooltip",
					"Actor/Object pins cannot have a default value. "
					"Wire this pin or expose it as a camera parameter.")))
			: StaticCastSharedRef<SWidget>(
				SNew(SEditableTextBox)
				.Text_Lambda([this, PinName, ClassDefault]()
				{
					return GetPinDefaultValueText(PinName, ClassDefault);
				})
				.OnTextCommitted_Lambda([this, PinName]
					(const FText& NewText, ETextCommit::Type CommitType)
				{
					OnPinDefaultValueCommitted(NewText, CommitType, PinName);
				})
				.SelectAllTextWhenFocused(true)
				.ToolTipText(LOCTEXT("StringFallbackTooltip",
					"Default value for this pin (no matching UPROPERTY found). "
					"Enter the value as a string in the same format the runtime parses.")));

		PropertiesCategory.AddCustomRow(DisplayName)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(DisplayName)
			.ToolTipText(Decl.Tooltip)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		[
			SNew(SHorizontalBox)

			// Value widget (text editor or read-only "None").
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				ValueWidget
			]

			// "As Pin" checkbox.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, PinName]()
				{
					return GetAsPinCheckState(PinName);
				})
				.OnCheckStateChanged_Lambda([this, PinName](ECheckBoxState NewState)
				{
					OnAsPinCheckChanged(NewState, PinName);
				})
				.ToolTipText(LOCTEXT("AsPinCheckboxTooltip",
					"When checked, this input appears as a wireable / exposable pin on "
					"the graph node. When unchecked, the pin is hidden from the graph and "
					"the property value above becomes the constant — turning this off on "
					"a wired or exposed pin auto-breaks the wire and auto-unexposes."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AsPinLabel", "As Pin"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]

			// "[Exposed]" chip.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExposedChip", "[Exposed]"))
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this, PinName]()
				{
					return GetExposedChipVisibility(PinName);
				})
			]
		];
	}
}

// ─── Private helpers ────────────────────────────────────────────────────────

UComposableCameraNodeGraphNode* FComposableCameraNodeGraphNodeDetails::GetGraphNode() const
{
	return WeakGraphNode.Get();
}

ECheckBoxState FComposableCameraNodeGraphNodeDetails::GetAsPinCheckState(FName PinName) const
{
	if (UComposableCameraNodeGraphNode* GraphNode = GetGraphNode())
	{
		return GraphNode->GetEffectivePinAsPin(PinName)
			? ECheckBoxState::Checked
			: ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Undetermined;
}

void FComposableCameraNodeGraphNodeDetails::OnAsPinCheckChanged(ECheckBoxState NewState, FName PinName)
{
	UComposableCameraNodeGraphNode* GraphNode = GetGraphNode();
	if (!GraphNode || NewState == ECheckBoxState::Undetermined)
	{
		return;
	}

	const bool bNewAsPin = (NewState == ECheckBoxState::Checked);

	{
		FScopedTransaction Transaction(LOCTEXT("ToggleAsPin", "Toggle Pin Visibility"));
		GraphNode->SetPinAsPin(PinName, bNewAsPin);
		GraphNode->ReconstructPins();
	}

	if (UEdGraph* OwningGraph = GraphNode->GetGraph())
	{
		OwningGraph->NotifyGraphChanged();
	}
}

EVisibility FComposableCameraNodeGraphNodeDetails::GetExposedChipVisibility(FName PinName) const
{
	if (UComposableCameraNodeGraphNode* GraphNode = GetGraphNode())
	{
		if (GraphNode->IsInputPinExposed(PinName))
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

FText FComposableCameraNodeGraphNodeDetails::GetPinDefaultValueText(FName PinName, FString ClassDefault) const
{
	if (UComposableCameraNodeGraphNode* GraphNode = GetGraphNode())
	{
		if (const FComposableCameraPinOverride* Override = GraphNode->FindPinOverride(PinName))
		{
			if (Override->bHasDefaultOverride)
			{
				return FText::FromString(Override->DefaultValueOverride);
			}
		}
	}
	return FText::FromString(ClassDefault);
}

void FComposableCameraNodeGraphNodeDetails::OnPinDefaultValueCommitted(
	const FText& NewText, ETextCommit::Type CommitType, FName PinName)
{
	if (CommitType != ETextCommit::OnEnter && CommitType != ETextCommit::OnUserMovedFocus)
	{
		return;
	}

	UComposableCameraNodeGraphNode* GraphNode = GetGraphNode();
	if (!GraphNode)
	{
		return;
	}

	{
		FScopedTransaction Transaction(LOCTEXT("EditPinDefault", "Edit Pin Default Value"));
		GraphNode->SetPinDefaultOverride(PinName, NewText.ToString());
	}

	if (UEdGraph* OwningGraph = GraphNode->GetGraph())
	{
		OwningGraph->NotifyGraphChanged();
	}
}

#undef LOCTEXT_NAMESPACE
