// Copyright Sulley. All rights reserved.

#include "ComposableCameraGraphPanelPinFactory.h"

#include "Variables/ComposableCameraVariableCollection.h"
#include "K2Node_SetComposableCameraVariableCollection.h"
#include "K2Node_GetComposableCameraVariableCollection.h"
#include "SComposableCameraGraphPinCollectionVariable.h"

TSharedPtr<class SGraphPin> FComposableCameraGraphPanelPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		if (UObject* Outer = InPin->GetOuter())
		{
			if (Outer->IsA(UK2Node_SetComposableCameraVariableCollection::StaticClass())
				|| Outer->IsA(UK2Node_GetComposableCameraVariableCollection::StaticClass()))
			{
				
				
				if (UK2Node_SetComposableCameraVariableCollection* Node = Cast<UK2Node_SetComposableCameraVariableCollection>(Outer))
				{
					if (Node->GetVariableCollection())
					{
						TArray<FString> VariableNames = Node->GetVariableNames();
						return SNew(SComposableCameraGraphPinCollectionVariable, InPin, VariableNames);
					}
				}
				if (UK2Node_GetComposableCameraVariableCollection* Node = Cast<UK2Node_GetComposableCameraVariableCollection>(Outer))
				{
					if (Node->GetVariableCollection())
					{
						TArray<FString> VariableNames = Node->GetVariableNames();
						return SNew(SComposableCameraGraphPinCollectionVariable, InPin, VariableNames);
					}
				}
			}
		}
		
	}

	return nullptr;
}
