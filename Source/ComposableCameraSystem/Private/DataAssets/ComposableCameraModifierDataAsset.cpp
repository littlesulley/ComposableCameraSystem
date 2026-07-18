// Copyright 2026 Sulley. All Rights Reserved.

#include "DataAssets/ComposableCameraModifierDataAsset.h"

#include "Modifiers/ComposableCameraModifierBase.h"
#include "UObject/UObjectGlobals.h"

bool UComposableCameraNodeModifierDataAsset::MatchesCameraTags(
	const FGameplayTagContainer& InCameraTags) const
{
	return CameraTagQuery.IsEmpty() || CameraTagQuery.Matches(InCameraTags);
}

void UComposableCameraNodeModifierDataAsset::PostLoad()
{
	Super::PostLoad();

	// Before entries became fixed wrappers, the array directly owned derived
	// Modifier instances. Preserve those objects by duplicating each one below a
	// new base wrapper and selecting Custom Modifier Class mode.
	for (TObjectPtr<UComposableCameraModifierBase>& Entry : Modifiers)
	{
		UComposableCameraModifierBase* LegacyModifier = Entry.Get();
		if (!LegacyModifier
			|| LegacyModifier->GetClass() == UComposableCameraModifierBase::StaticClass())
		{
			continue;
		}

		UComposableCameraModifierBase* Wrapper = NewObject<UComposableCameraModifierBase>(this);
		UComposableCameraModifierBase* CustomModifier = DuplicateObject<UComposableCameraModifierBase>(
			LegacyModifier, Wrapper);
		if (!Wrapper || !CustomModifier)
		{
			continue;
		}

		Wrapper->SetFlags(RF_Transactional);
		CustomModifier->SetFlags(RF_Transactional);
		Wrapper->bUseCustomModifierClass = true;
		Wrapper->CustomModifier = CustomModifier;
		Entry = Wrapper;
	}

	// Previous assets stored an exact-match list. Preserve its OR semantics by
	// migrating it to an ANY query. Empty legacy lists naturally remain the new
	// empty-query wildcard.
	if (!CameraTags.IsEmpty())
	{
		if (CameraTagQuery.IsEmpty())
		{
			CameraTagQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(CameraTags);
		}
		CameraTags.Reset();
	}
}
