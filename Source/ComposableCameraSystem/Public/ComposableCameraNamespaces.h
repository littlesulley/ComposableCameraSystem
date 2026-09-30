// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "CoreTypes.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectPtr.h"

class UComposableCameraNodeModifierDataAsset;
class UComposableCameraModifierBase;
class UComposableCameraCameraNodeBase;

namespace ComposableCameraModifier
{
	using T_NodeClass = TSubclassOf<UComposableCameraCameraNodeBase>;
	
	struct FModifierEntry
	{
		// TObjectPtr (not raw UObject*) so the new FReferenceCollector::AddReferencedObject
		// overload accepts these fields directly. The raw-pointer overload is now
		// deprecated under incremental GC and emits C4996.
		TObjectPtr<UComposableCameraModifierBase> Modifier;
		TObjectPtr<UComposableCameraNodeModifierDataAsset> Asset;
		uint64 RegistrationOrder = 0;

		bool operator==(const FModifierEntry& Other) const
		{
			return Modifier == Other.Modifier && Asset == Other.Asset;
		}

		bool operator!=(const FModifierEntry& Other) const
		{
			return !(*this == Other);
		}
	};

	using T_NodeModifier = TMap<T_NodeClass, FModifierEntry>;
	using T_NodeModifierArray = TMap<T_NodeClass, TArray<FModifierEntry>>;

	/**
	 * Effective generic modifiers are resolved per property. NAME_None is
	 * reserved for the legacy whole-node Custom Modifier winner.
	 */
	using T_PropertyModifier = TMap<FName, FModifierEntry>;
	using T_EffectiveModifier = TMap<T_NodeClass, T_PropertyModifier>;
}
