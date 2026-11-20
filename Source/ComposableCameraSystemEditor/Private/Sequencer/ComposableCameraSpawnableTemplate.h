// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

namespace ComposableCameraSystem::SpawnableTemplate
{
/** Call only on the MovieScene-owned template, never on its temporary source/live actor.
 * UE5.6's legacy actor spawner duplicates RF_AllFlags, including RF_Transient.
 * Components need the same treatment as in MovieSceneHelpers::MakeSpawnableTemplateFromInstance. */
inline void MakePersistent(AActor& Template)
{
	bool bChanged = false;
	const auto Persist = [&bChanged](UObject& Object)
	{
		if (Object.HasAnyFlags(RF_Transient))
		{
			Object.Modify();
			Object.ClearFlags(RF_Transient);
			bChanged = true;
		}
	};
	Persist(Template);
	for (UActorComponent* Component : Template.GetComponents())
		if (Component) Persist(*Component);
	if (bChanged) Template.MarkPackageDirty();
}
}
