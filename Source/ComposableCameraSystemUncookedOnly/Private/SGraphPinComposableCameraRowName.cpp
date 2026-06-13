// Copyright 2026 Sulley. All Rights Reserved.

#include "SGraphPinComposableCameraRowName.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/DataTable.h"
#include "SSearchableComboBox.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#include "K2Node_ActivateComposableCameraFromDataTable.h"

#define LOCTEXT_NAMESPACE "SGraphPinComposableCameraRowName"

void SGraphPinComposableCameraRowName::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
	BindToOwningNodeDelegate();
}

SGraphPinComposableCameraRowName::~SGraphPinComposableCameraRowName()
{
	UnbindFromOwningNodeDelegate();
}

TSharedRef<SWidget> SGraphPinComposableCameraRowName::GetDefaultValueWidget()
{
	RefreshRowOptions();

	// The outer SBox is the widget SGraphPin caches as "the default value
	// widget" for this pin. Its content is what we actually swap on live
	// DataTable-pin changes. Keeping the outer stable means SGraphPin's
	// internal layout doesn't have to know anything about the swap.
	SAssignNew(DefaultValueContainer, SBox)
		.MinDesiredWidth(120.f)
		.MaxDesiredWidth(260.f)
		[BuildInnerDefaultValueWidget()];

	return DefaultValueContainer.ToSharedRef();
}

TSharedRef<SWidget> SGraphPinComposableCameraRowName::BuildInnerDefaultValueWidget()
{
	// Drop any stale combo reference before we potentially build a new one.
	// If we end up in the text-fallback branch we want RowCombo cleared so
	// HandleDataTablePinChanged's "RefreshOptions on the existing combo"
	// fast path isn't accidentally taken against a dead widget.
	RowCombo.Reset();

	// When we cannot resolve a literal DataTable we fall back to a plain
	// text box so the author can still type a row name by hand. This
	// covers the "DataTable pin is linked to a variable" and "DataTable
	// pin is unset" cases.
	if (!HasResolvedDataTable())
	{
		return SNew(SEditableTextBox)
			.Text_Lambda([this]() { return GetCurrentRowNameText(); })
			.ToolTipText_Lambda([this]() { return GetWidgetTooltip(); })
			.IsReadOnly_Lambda([this]() { return !GetDefaultValueIsEditable(); })
			.OnTextCommitted(this, &SGraphPinComposableCameraRowName::OnFallbackTextCommitted)
			.SelectAllTextWhenFocused(true)
			.RevertTextOnEscape(true);
	}

	return SAssignNew(RowCombo, SSearchableComboBox)
		.OptionsSource(&RowOptions)
		.OnGenerateWidget(this, &SGraphPinComposableCameraRowName::OnGenerateRowOptionWidget)
		.OnSelectionChanged(this, &SGraphPinComposableCameraRowName::OnRowOptionSelected)
		.ContentPadding(FMargin(2.f, 0.f))
		.ToolTipText_Lambda([this]() { return GetWidgetTooltip(); })
		.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
		.Content()
		[SNew(STextBlock)
			.Text_Lambda([this]() { return GetCurrentRowNameText(); })];
}

// Owning K2 Node Access 

UK2Node_ActivateComposableCameraFromDataTable* SGraphPinComposableCameraRowName::GetOwningActivateNode() const
{
	const UEdGraphPin* ThisPin = GetPinObj();
	if (!ThisPin)
	{
		return nullptr;
	}
	return Cast<UK2Node_ActivateComposableCameraFromDataTable>(ThisPin->GetOwningNodeUnchecked());
}

// Data Resolution 

UDataTable* SGraphPinComposableCameraRowName::ResolveLiteralDataTable() const
{
	if (const UK2Node_ActivateComposableCameraFromDataTable* Node = GetOwningActivateNode())
	{
		return Node->ResolveLiteralDataTable();
	}
	return nullptr;
}

void SGraphPinComposableCameraRowName::RefreshRowOptions()
{
	RowOptions.Reset();

	// "None" sentinel so the user can clear the selection.
	RowOptions.Add(MakeShared<FString>(FName(NAME_None).ToString()));

	if (const UDataTable* DataTable = ResolveLiteralDataTable())
	{
		TArray<FName> RowNames = DataTable->GetRowNames();
		RowNames.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		RowOptions.Reserve(RowOptions.Num() + RowNames.Num());
		for (const FName& RowName: RowNames)
		{
			RowOptions.Add(MakeShared<FString>(RowName.ToString()));
		}
	}
}

bool SGraphPinComposableCameraRowName::HasResolvedDataTable() const
{
	// RowOptions always has the "None" sentinel. A valid DataTable was
	// resolved when there's more than one entry.
	return RowOptions.Num() > 1;
}

// Combo Callbacks 

TSharedRef<SWidget> SGraphPinComposableCameraRowName::OnGenerateRowOptionWidget(TSharedPtr<FString> InOption)
{
	return SNew(STextBlock)
		.Text(FText::FromString(InOption.IsValid() ? *InOption: FString()));
}

void SGraphPinComposableCameraRowName::OnRowOptionSelected(TSharedPtr<FString> InOption, ESelectInfo::Type InSelectInfo)
{
	// ESelectInfo::Direct fires when we programmatically set the selection
	// during a refresh - we don't want that to register as a user edit.
	if (InSelectInfo == ESelectInfo::Direct || !InOption.IsValid())
	{
		return;
	}

	UEdGraphPin* ThisPin = GetPinObj();
	if (!ThisPin)
	{
		return;
	}

	const FString& NewValueString = *InOption;
	if (ThisPin->GetDefaultAsString() == NewValueString)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetRowName", "Set Row Name"));
	if (const UEdGraphSchema* Schema = ThisPin->GetSchema())
	{
		Schema->TrySetDefaultValue(*ThisPin, NewValueString);
	}
	else if (UEdGraphNode* OwningNode = ThisPin->GetOwningNodeUnchecked())
	{
		OwningNode->Modify();
		ThisPin->DefaultValue = NewValueString;
	}
}

// Text Fallback 

void SGraphPinComposableCameraRowName::OnFallbackTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter && CommitType != ETextCommit::OnUserMovedFocus)
	{
		return;
	}

	UEdGraphPin* ThisPin = GetPinObj();
	if (!ThisPin)
	{
		return;
	}

	const FString NewValueString = NewText.ToString();
	if (ThisPin->GetDefaultAsString() == NewValueString)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetRowNameFallback", "Set Row Name"));
	if (const UEdGraphSchema* Schema = ThisPin->GetSchema())
	{
		Schema->TrySetDefaultValue(*ThisPin, NewValueString);
	}
	else if (UEdGraphNode* OwningNode = ThisPin->GetOwningNodeUnchecked())
	{
		OwningNode->Modify();
		ThisPin->DefaultValue = NewValueString;
	}
}

// Display Helpers 

FText SGraphPinComposableCameraRowName::GetCurrentRowNameText() const
{
	if (const UEdGraphPin* ThisPin = GetPinObj())
	{
		const FString Value = ThisPin->GetDefaultAsString();
		if (!Value.IsEmpty())
		{
			return FText::FromString(Value);
		}
	}
	return FText::FromName(NAME_None);
}

FText SGraphPinComposableCameraRowName::GetWidgetTooltip() const
{
	if (HasResolvedDataTable())
	{
		return LOCTEXT("RowNameComboTooltip",
			"Pick a row from the DataTable assigned on this node. "
			"Select 'None' to clear the value.");
	}

	if (const UK2Node_ActivateComposableCameraFromDataTable* Node = GetOwningActivateNode())
	{
		if (const UEdGraphPin* DataTablePin = Node->FindPin(UK2Node_ActivateComposableCameraFromDataTable::DataTablePinName, EGPD_Input))
		{
			if (DataTablePin->LinkedTo.Num() > 0)
			{
				return LOCTEXT("RowNameFallbackLinkedTooltip",
					"The DataTable pin is driven by another node, so the row "
					"list cannot be resolved statically. Type the row name by "
					"hand - it will be looked up at runtime.");
			}
		}
	}

	return LOCTEXT("RowNameFallbackEmptyTooltip",
		"Assign a DataTable on this node to get a searchable list of row "
		"names. Until then you can type the row name by hand - it will be "
		"looked up at runtime.");
}

// Live Refresh 

void SGraphPinComposableCameraRowName::HandleDataTablePinChanged()
{
	// Re-resolve the DataTable and rebuild the options array.
	const bool bWasCombo = HasResolvedDataTable();
	RefreshRowOptions();
	const bool bIsCombo = HasResolvedDataTable();

	if (bWasCombo != bIsCombo)
	{
		// The "has resolved" state actually flipped - we need to swap
		// between the combo box and the text fallback. SGraphPin caches
		// the return value of GetDefaultValueWidget() permanently and
		// there is no public API to ask it to rebuild, so we own the
		// outer container ourselves and swap its inner content here.
		if (DefaultValueContainer.IsValid())
		{
			DefaultValueContainer->SetContent(BuildInnerDefaultValueWidget());
		}
		return;
	}

	// State unchanged. If we're in combo mode, push the refreshed option
	// list into the existing SSearchableComboBox without rebuilding the
	// widget. If we're in fallback mode the text box just re-reads its
	// lambdas and tooltip - no action needed.
	if (bIsCombo && RowCombo.IsValid())
	{
		RowCombo->RefreshOptions();
	}
}

void SGraphPinComposableCameraRowName::BindToOwningNodeDelegate()
{
	UK2Node_ActivateComposableCameraFromDataTable* Node = GetOwningActivateNode();
	if (!Node)
	{
		return;
	}

	BoundNode = Node;
	DelegateHandle = Node->GetOnDataTablePinChanged().AddSP(this, &SGraphPinComposableCameraRowName::HandleDataTablePinChanged);
}

void SGraphPinComposableCameraRowName::UnbindFromOwningNodeDelegate()
{
	if (UK2Node_ActivateComposableCameraFromDataTable* Node = BoundNode.Get())
	{
		if (DelegateHandle.IsValid())
		{
			Node->GetOnDataTablePinChanged().Remove(DelegateHandle);
		}
	}
	DelegateHandle.Reset();
	BoundNode.Reset();
}

#undef LOCTEXT_NAMESPACE
