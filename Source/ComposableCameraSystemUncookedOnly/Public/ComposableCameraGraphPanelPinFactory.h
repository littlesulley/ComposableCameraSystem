// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "UObject/Object.h"

class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API FComposableCameraGraphPanelPinFactory
	: public FGraphPanelPinFactory
{
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* InGraphPinObj) const override;
};
