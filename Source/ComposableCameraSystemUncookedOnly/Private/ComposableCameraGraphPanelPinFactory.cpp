// Copyright Sulley. All rights reserved.

#include "ComposableCameraGraphPanelPinFactory.h"

#include "Engine/DataTable.h"
#include "EdGraphSchema_K2.h"

#include "K2Node_ActivateComposableCamera.h"
#include "K2Node_ActivateComposableCameraFromDataTable.h"
#include "K2Node_AddCameraPatch.h"
#include "K2Node_PlayCutsceneSequence.h"
#include "SGraphPinComposableCameraContextName.h"
#include "SGraphPinComposableCameraDataTable.h"
#include "SGraphPinComposableCameraRowName.h"

TSharedPtr<class SGraphPin> FComposableCameraGraphPanelPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (!InPin)
	{
		return nullptr;
	}

	UObject* Outer = InPin->GetOuter();

	// UK2Node_ActivateComposableCameraFromDataTable pins 
	// Two custom widgets on this node: a filtered DataTable asset picker,
	// and a live-refreshing row-name combo driven by the node's
	// OnDataTablePinChanged delegate.
	if (UK2Node_ActivateComposableCameraFromDataTable* DataTableActivateNode =
			Cast<UK2Node_ActivateComposableCameraFromDataTable>(Outer))
	{
		if (InPin->Direction == EGPD_Input)
		{
			// DataTable asset pin - filter to our required row struct.
			if (InPin->PinName == UK2Node_ActivateComposableCameraFromDataTable::DataTablePinName
				&& InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
			{
				const UClass* PinClass = Cast<UClass>(InPin->PinType.PinSubCategoryObject.Get());
				if (PinClass && PinClass->IsChildOf(UDataTable::StaticClass()))
				{
					return SNew(SGraphPinComposableCameraDataTable, InPin);
				}
			}

			// RowName pin - searchable combo populated from the literal DT.
			if (InPin->PinName == UK2Node_ActivateComposableCameraFromDataTable::RowNamePinName
				&& InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
			{
				return SNew(SGraphPinComposableCameraRowName, InPin);
			}
		}
	}

	// UK2Node_ActivateComposableCamera - ContextName pin 
	// Static combo populated from UComposableCameraProjectSettings.
	if (UK2Node_ActivateComposableCamera* ActivateNode =
			Cast<UK2Node_ActivateComposableCamera>(Outer))
	{
		if (InPin->Direction == EGPD_Input
			&& InPin->PinName == UK2Node_ActivateComposableCamera::PN_ContextName
			&& InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return SNew(SGraphPinComposableCameraContextName, InPin);
		}
	}

	// UK2Node_PlayCutsceneSequence - ContextName pin 
	if (UK2Node_PlayCutsceneSequence* CutsceneNode =
			Cast<UK2Node_PlayCutsceneSequence>(Outer))
	{
		if (InPin->Direction == EGPD_Input
			&& InPin->PinName == UK2Node_PlayCutsceneSequence::PN_ContextName
			&& InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return SNew(SGraphPinComposableCameraContextName, InPin);
		}
	}

	// UK2Node_AddCameraPatch - ContextName pin 
	if (UK2Node_AddCameraPatch* AddPatchNode =
			Cast<UK2Node_AddCameraPatch>(Outer))
	{
		if (InPin->Direction == EGPD_Input
			&& InPin->PinName == UK2Node_AddCameraPatch::PN_ContextName
			&& InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return SNew(SGraphPinComposableCameraContextName, InPin);
		}
	}

	return nullptr;
}
