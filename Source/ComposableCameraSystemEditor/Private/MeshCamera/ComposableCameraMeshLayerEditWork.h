// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "Async/Async.h"
#include "Misc/QueuedThreadPool.h"

namespace UE::ComposableCamera::MeshEditor
{
	FQueuedThreadPool& GetMeshLayerEditPool();
	void ShutdownMeshLayerEditWork();
	/** Native computation and retirement share a module-owned pool. Close cancels; unload joins. */
	template<typename Callable>
	auto AsyncMeshLayerEdit(Callable&& Work)
	{
		return AsyncPool(GetMeshLayerEditPool(), Forward<Callable>(Work));
	}
}
