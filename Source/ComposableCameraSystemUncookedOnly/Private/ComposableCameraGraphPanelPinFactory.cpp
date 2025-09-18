// Copyright Sulley. All rights reserved.

#include "ComposableCameraGraphPanelPinFactory.h"
#include "Variables/ComposableCameraVariableCollection.h"
#include "K2Node_SetComposableCameraVariableCollection.h"
#include "SComposableCameraGraphPinCollectionVariable.h"

TSharedPtr<class SGraphPin> FComposableCameraGraphPanelPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		if (UObject* Outer = InPin->GetOuter())
		{
			if (Outer->IsA(UK2Node_SetComposableCameraVariableCollection::StaticClass()))
			{
				UK2Node_SetComposableCameraVariableCollection* Node = CastChecked<UK2Node_SetComposableCameraVariableCollection>(Outer);
				UComposableCameraVariableCollection* Collection = Node->GetVariableCollection();

				if (Collection)
				{
					TArray<FString> VariableNames = Node->GetVariableNames();
					return SNew(SComposableCameraGraphPinCollectionVariable, InPin, VariableNames);
				}
			}
		}
		
	}

	return nullptr;
}
