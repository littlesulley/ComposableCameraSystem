// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraGraphNodeFactory.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraVariableGraphNode.h"
#include "Editors/SComposableCameraGraphNode.h"
#include "Editors/SComposableCameraVariableGraphNode.h"

TSharedPtr<SGraphNode> FComposableCameraGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UComposableCameraNodeGraphNode* CameraNode = Cast<UComposableCameraNodeGraphNode>(InNode))
	{
		return SNew(SComposableCameraGraphNode, CameraNode);
	}

	if (UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(InNode))
	{
		return SNew(SComposableCameraVariableGraphNode, VarNode);
	}

	// Not our node type - fall through to the next factory in the chain.
	return nullptr;
}
