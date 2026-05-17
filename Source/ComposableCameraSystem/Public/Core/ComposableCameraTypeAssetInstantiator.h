// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ComposableCameraParameterBlock.h"

class AComposableCameraCameraBase;
class UComposableCameraTypeAsset;

namespace UE::ComposableCameras
{
	/**
	 * Populate a freshly-spawned AComposableCameraCameraBase from a type asset and
	 * a caller-provided parameter block.
	 *
	 * Does all the work that used to live exclusively inside
	 * AComposableCameraPlayerCameraManager::OnTypeAssetCameraConstructed:
	 *
	 *   - Duplicates node templates into Camera->CameraNodes / Camera->ComputeNodes
	 *     (nulling out orphans: nodes not referenced by any execution chain
	 *     preserve index correspondence with TypeAsset->NodeTemplates while
	 *     saving memory and init cost).
	 *   - Allocates Camera->OwnedRuntimeDataBlock via TypeAsset->BuildRuntimeDataLayout.
	 *   - Applies ParameterBlock via TypeAsset->ApplyParameterBlock (POD bytes)
	 *     and TypeAsset->ApplyDelegateBindings (delegate UPROPERTYs. Can't live
	 *     in the POD data block).
	 *   - Wires the data block to each node via SetRuntimeDataBlock(..., NodeIndex).
	 *     Compute nodes use the offset index space
	 *     NodeIndex = TypeAsset->NodeTemplates.Num() + ComputeIdx to match the
	 *     layout that BuildRuntimeDataLayout allocated.
	 *   - Copies FullExecChain / ComputeFullExecChain from the asset onto the camera.
	 *   - Performs the legacy ComputeNodes reorder only when ComputeFullExecChain
	 *     is empty (pre-existing compatibility for assets saved before the full
	 *     exec chain existed).
	 *   - Calls Camera->InitializeNodes() so every populated node's OnInitialize
	 *     fires exactly once, with OwningCamera / OwningPlayerCameraManager /
	 *     RuntimeDataBlock all wired.
	 *   - Duplicates the type asset's EnterTransition onto the camera.
	 *
	 * PCM-independent by construction: does not read or write any PCM state. The
	 * existing PCM activation path calls this from its OnTypeAssetCameraConstructed
	 * dynamic-delegate callback (a thin wrapper); the Level Sequence component
	 * path will call this directly after spawning its internal camera with a null
	 * PCM. Nodes on the camera see whatever CameraManager value was passed into
	 * Camera->Initialize earlier (nullptr is valid. See
	 * AComposableCameraCameraBase::Initialize and individual node
	 * GetLevelSequenceCompatibility overrides).
	 *
	 * Early-returns if Camera or TypeAsset is null; does not log.
	 *
	 * @param Camera          Target camera actor. Expected freshly spawned with
	 *                        CameraNodes / ComputeNodes empty; any pre-existing
	 *                        entries are cleared inside.
	 * @param TypeAsset       Source type asset. Its NodeTemplates / ComputeNodeTemplates
	 *                        are duplicated into Camera; the asset itself is not modified.
	 * @param ParameterBlock  Caller-provided parameter values. Stored on Camera as
	 *                        SourceParameterBlock for reactivation on modifier changes.
	 */
	COMPOSABLECAMERASYSTEM_API void ConstructCameraFromTypeAsset(
		AComposableCameraCameraBase* Camera,
		UComposableCameraTypeAsset* TypeAsset,
		const FComposableCameraParameterBlock& ParameterBlock);
}
