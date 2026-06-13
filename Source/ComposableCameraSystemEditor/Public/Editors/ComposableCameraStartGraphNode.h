// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editors/ComposableCameraGraphNodeBase.h"
#include "ComposableCameraStartGraphNode.generated.h"

/**
 * Special graph node marking the start of the camera pipeline.
 * Has a single execution output pin indicating where the execution chain begins.
 * Always present at the far left of the graph. Cannot be deleted or duplicated.
 *
 * The PN_ExecOut name constant and the CreateExecOutPin helper are inherited
 * from UComposableCameraGraphNodeBase.
 */
UCLASS()
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraStartGraphNode: public UComposableCameraGraphNodeBase
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
