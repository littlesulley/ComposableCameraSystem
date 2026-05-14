#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
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
	using T_CameraModifier = TMap<FGameplayTag, T_NodeModifierArray>;
}
