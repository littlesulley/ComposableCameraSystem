// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"

#if WITH_EDITOR

/**
 * Per-node debug entry captured by AComposableCameraCameraBase::SnapshotDebugState().
 *
 * Correlates with editor graph nodes via NodeIndex (matching
 * UComposableCameraNodeGraphNode::NodeIndex) so the editor can map
 * runtime state back to the visual node without name-based lookup.
 */
struct FComposableCameraNodeDebugEntry
{
	/** Display name of the node class (resolved via GetCameraNodeDisplayName or GetName). */
	FString NodeDisplayName;

	/** Index in the type asset's NodeTemplates array. Maps 1:1 to
	 *  UComposableCameraNodeGraphNode::NodeIndex in the editor graph. */
	int32 NodeIndex = INDEX_NONE;

	/** True if this node was ticked this frame (always true for nodes in the
	 *  exec chain; false for nodes that were skipped or inactive). */
	bool bWasTicked = false;

	/** Camera pose snapshot captured immediately after this node's TickNode
	 *  completed. Lets the editor show the cumulative pose at each stage. */
	FComposableCameraPose PoseAfterNode;

	/** Output pin values as human-readable strings for overlay display.
	 *  Key = pin name, Value = formatted value string.
	 *  Only populated for pins that have entries in the RuntimeDataBlock. */
	TArray<TPair<FName, FString>> OutputPinValues;
};

/**
 * Complete debug snapshot of a running camera instance, captured on demand
 * by the editor's debug ticker during PIE.
 *
 * Designed for pull-based polling: the editor calls
 * AComposableCameraCameraBase::SnapshotDebugState() once per editor tick
 * (not once per game tick) to avoid affecting runtime perf.
 *
 * All arrays use TArray for simplicity. The snapshot is short-lived (replaced
 * every editor tick), so allocation cost is acceptable.
 */
struct FComposableCameraDebugSnapshot
{
	/** False until the first successful snapshot. */
	bool bIsValid = false;

	/** The camera's final pose this frame (after all nodes + post-tick). */
	FComposableCameraPose FinalPose;

	/** Per-node debug entries, in execution order. */
	TArray<FComposableCameraNodeDebugEntry> NodeEntries;

	/** Current values of exposed parameters as human-readable strings. */
	TArray<TPair<FName, FString>> ExposedParameterValues;

	/** Current values of internal variables as human-readable strings. */
	TArray<TPair<FName, FString>> InternalVariableValues;

	/** Name of the context this camera is running in (from the context stack). */
	FName ContextName;

	/** Whether this camera is currently the active (topmost) camera being evaluated. */
	bool bIsActiveCamera = false;

	/** Reset the snapshot to an invalid empty state. */
	void Reset()
	{
		bIsValid = false;
		FinalPose = FComposableCameraPose();
		NodeEntries.Reset();
		ExposedParameterValues.Reset();
		InternalVariableValues.Reset();
		ContextName = NAME_None;
		bIsActiveCamera = false;
	}
};

#endif // WITH_EDITOR
