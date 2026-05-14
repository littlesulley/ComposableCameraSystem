// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraGraphNodeBase.h"
#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "ComposableCameraGraphNodeBase"

const FName UComposableCameraGraphNodeBase::PN_ExecIn(TEXT("ExecIn"));
const FName UComposableCameraGraphNodeBase::PN_ExecOut(TEXT("ExecOut"));

UEdGraphPin* UComposableCameraGraphNodeBase::CreateExecInPin()
{
	FEdGraphPinType ExecType;
	ExecType.PinCategory = UEdGraphSchema_K2::PC_Exec;

	UEdGraphPin* ExecInPin = CreatePin(EGPD_Input, ExecType, PN_ExecIn);
	if (ExecInPin)
	{
		// Empty friendly label - the exec pin shows just the arrow glyph,
		// matching Blueprint exec pin presentation.
		ExecInPin->PinFriendlyName = LOCTEXT("ExecInName", "");
	}
	return ExecInPin;
}

UEdGraphPin* UComposableCameraGraphNodeBase::CreateExecOutPin()
{
	FEdGraphPinType ExecType;
	ExecType.PinCategory = UEdGraphSchema_K2::PC_Exec;

	UEdGraphPin* ExecOutPin = CreatePin(EGPD_Output, ExecType, PN_ExecOut);
	if (ExecOutPin)
	{
		ExecOutPin->PinFriendlyName = LOCTEXT("ExecOutName", "");
	}
	return ExecOutPin;
}

FText UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(const UClass* NodeClass)
{
	if (!NodeClass)
	{
		return FText::GetEmpty();
	}

	// Author-provided override wins. We check HasMetaData explicitly rather
	// than calling GetDisplayNameText unconditionally because UClass's default
	// GetDisplayNameText synthesizes a name from the class identifier when no
	// metadata is set, which would short-circuit our prefix/suffix munging
	// below and produce a different label than the legacy code did.
	static const FName NAME_DisplayName(TEXT("DisplayName"));
	if (NodeClass->HasMetaData(NAME_DisplayName))
	{
		return NodeClass->GetDisplayNameText();
	}

	// Blueprint-generated classes: use the Blueprint asset name (the user's
	// chosen name) rather than the generated "_C" class identifier.
	if (NodeClass->ClassGeneratedBy)
	{
		return FText::FromString(FName::NameToDisplayString(NodeClass->ClassGeneratedBy->GetName(), /*bIsBool=*/ false));
	}

	// Legacy munging fallback: strip the "ComposableCamera" prefix and the
	// trailing "Node" suffix, then insert a space before each upper-case
	// letter that follows a lower-case one (camel-case -> "Camel Case").
	FString ClassName = NodeClass->GetName();
	ClassName.RemoveFromStart(TEXT("ComposableCamera"));
	ClassName.RemoveFromEnd(TEXT("Node"));

	FString DisplayName;
	DisplayName.Reserve(ClassName.Len() + 4);
	for (int32 i = 0; i < ClassName.Len(); ++i)
	{
		if (i > 0 && FChar::IsUpper(ClassName[i]) && !FChar::IsUpper(ClassName[i - 1]))
		{
			DisplayName.AppendChar(TEXT(' '));
		}
		DisplayName.AppendChar(ClassName[i]);
	}

	return FText::FromString(DisplayName);
}

#undef LOCTEXT_NAMESPACE
