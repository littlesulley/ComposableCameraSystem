// Copyright Sulley. All rights reserved.

#include "DataAssets/ComposableCameraPatchTypeAsset.h"

#include "Nodes/ComposableCameraCameraNodeBase.h"

#if WITH_EDITOR
void UComposableCameraPatchTypeAsset::ValidateAdditional(
	TArray<FComposableCameraBuildMessage>& OutMessages) const
{
	for (int32 i = 0; i < NodeTemplates.Num(); ++i)
	{
		const UComposableCameraCameraNodeBase* Node = NodeTemplates[i];
		if (!Node)
		{
			continue; // Null-node error is already surfaced by the base Build() walk.
		}

		const EComposableCameraNodePatchCompatibility Compat = Node->GetPatchCompatibility();
		if (Compat == EComposableCameraNodePatchCompatibility::Compatible)
		{
			continue;
		}

		FComposableCameraBuildMessage Msg;
		Msg.NodeIndex = i;

		if (Compat == EComposableCameraNodePatchCompatibility::Incompatible)
		{
			Msg.Severity = 2; // Error
			Msg.Message = FText::Format(
				FText::FromString(TEXT("Node '{0}' is not compatible with a Camera Patch graph — it synthesizes pose from scratch or delegates to external sources (see EComposableCameraNodePatchCompatibility). The Patch will still run but produce unexpected output; remove the node or move this logic back to a regular CameraTypeAsset.")),
				FText::FromString(Node->GetClass()->GetName()));
		}
		else // CompatibleWithCaveat
		{
			Msg.Severity = 1; // Warning
			Msg.Message = FText::Format(
				FText::FromString(TEXT("Node '{0}' has caveats in a Camera Patch graph — it overrides a pose field in a way that may discard meaningful upstream data. Confirm this is intentional for this Patch's authoring intent.")),
				FText::FromString(Node->GetClass()->GetName()));
		}
		OutMessages.Add(Msg);
	}
}
#endif
