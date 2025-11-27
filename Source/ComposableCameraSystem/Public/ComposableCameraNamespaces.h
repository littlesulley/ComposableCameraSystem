#pragma once

struct FGameplayTag;
class UComposableCameraNodeModifierDataAsset;
class UComposableCameraModifierBase;
class UComposableCameraCameraNodeBase;

namespace ComposableCameraModifier
{
	using T_NodeClass = TSubclassOf<UComposableCameraCameraNodeBase>;
	
	struct FModifierEntry
	{
		UComposableCameraModifierBase* Modifier;
		UComposableCameraNodeModifierDataAsset* Asset;

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
