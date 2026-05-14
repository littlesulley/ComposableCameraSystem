// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editors/ComposableCameraGraphNodeBase.h"
#include "ComposableCameraOutputGraphNode.generated.h"

/**
 * Special graph node representing the camera pipeline's terminal sentinel.
 * Has a single exec input pin - the end of the camera execution chain.
 * Always present at the far right of the graph. Cannot be deleted or duplicated.
 *
 * The PN_ExecIn name constant is inherited from UComposableCameraGraphNodeBase.
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraOutputGraphNode: public UComposableCameraGraphNodeBase
{
	GENERATED_BODY()

public:
	// UEdGraphNode Interface 

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }
};
