// Copyright Sulley. All rights reserved.

#include "SGraphPinComposableCameraContextName.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "SSearchableComboBox.h"
#include "ScopedTransaction.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#include "Utils/ComposableCameraProjectSettings.h"

#define LOCTEXT_NAMESPACE "SGraphPinComposableCameraContextName"

void SGraphPinComposableCameraContextName::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget> SGraphPinComposableCameraContextName::GetDefaultValueWidget()
{
	RefreshContextOptions();

	return SNew(SBox)
		.MinDesiredWidth(120.f)
		.MaxDesiredWidth(260.f)
		[SAssignNew(ContextCombo, SSearchableComboBox)
				.OptionsSource(&ContextOptions)
				.OnGenerateWidget(this, &SGraphPinComposableCameraContextName::OnGenerateOptionWidget)
				.OnSelectionChanged(this, &SGraphPinComposableCameraContextName::OnOptionSelected)
				.OnComboBoxOpening_Lambda([this]() { RefreshContextOptions(); if (ContextCombo.IsValid()) { ContextCombo->RefreshOptions(); } })
				.ContentPadding(FMargin(2.f, 0.f))
				.ToolTipText(LOCTEXT("ContextNameTooltip",
					"Pick a context name from Project Settings > Composable Camera System. "
					"Select 'None' to use the default context."))
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
				.Content()
				[SNew(STextBlock)
					.Text_Lambda([this]() { return GetCurrentContextNameText(); })]];
}

// Data Resolution 

void SGraphPinComposableCameraContextName::RefreshContextOptions()
{
	ContextOptions.Reset();

	// "None" sentinel so the user can clear the selection.
	ContextOptions.Add(MakeShared<FString>(FName(NAME_None).ToString()));

	TArray<FName> Names = UComposableCameraProjectSettings::GetContextNames();
	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	ContextOptions.Reserve(ContextOptions.Num() + Names.Num());
	for (const FName& Name: Names)
	{
		ContextOptions.Add(MakeShared<FString>(Name.ToString()));
	}
}

// Combo Callbacks 

TSharedRef<SWidget> SGraphPinComposableCameraContextName::OnGenerateOptionWidget(TSharedPtr<FString> InOption)
{
	return SNew(STextBlock)
		.Text(FText::FromString(InOption.IsValid() ? *InOption: FString()));
}

void SGraphPinComposableCameraContextName::OnOptionSelected(TSharedPtr<FString> InOption, ESelectInfo::Type InSelectInfo)
{
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

	const FScopedTransaction Transaction(LOCTEXT("SetContextName", "Set Context Name"));
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

FText SGraphPinComposableCameraContextName::GetCurrentContextNameText() const
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

#undef LOCTEXT_NAMESPACE
