// Copyright 2026 Sulley. All Rights Reserved.
#include "MeshCamera/ComposableCameraMeshLayerEditWork.h"

namespace UE::ComposableCamera::MeshEditor
{
	namespace { TUniquePtr<FQueuedThreadPool> EditPool; }
	FQueuedThreadPool& GetMeshLayerEditPool()
	{
		if (!EditPool)
		{
			check(IsInGameThread());
			EditPool.Reset(FQueuedThreadPool::Allocate());
			const bool bCreated = EditPool->Create(2, 0, TPri_Normal, TEXT("CCSMeshEdit"));
			checkf(bCreated, TEXT("Cannot create Mesh Layer editor work pool"));
		}
		return *EditPool;
	}
	void ShutdownMeshLayerEditWork()
	{
		check(IsInGameThread());
		if (EditPool) { EditPool->Destroy(); EditPool.Reset(); }
	}
}
