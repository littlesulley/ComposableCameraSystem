// Copyright Sulley. All rights reserved.

#include "DataAssets/ComposableCameraTransitionTableDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Misc/PackageName.h"
#include "Transitions/ComposableCameraTransitionBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ComposableCameraTransitionTableDataAsset"

// ------------------------------------------------------------
// FComposableCameraTransitionTableEntry
// ------------------------------------------------------------

void FComposableCameraTransitionTableEntry::UpdateDisplayTitle()
{
	const bool bMissingSource = SourceTypeAsset.IsNull();
	const bool bMissingTarget = TargetTypeAsset.IsNull();

	const FString SourceName = bMissingSource
		? TEXT("(None)")
		: FPackageName::GetShortName(SourceTypeAsset.GetAssetName());

	const FString TargetName = bMissingTarget
		? TEXT("(None)")
		: FPackageName::GetShortName(TargetTypeAsset.GetAssetName());

	if (bMissingSource && bMissingTarget)
	{
		DisplayTitle = FString::Printf(TEXT("%s -> %s  [Source and Target are both required]"), *SourceName, *TargetName);
	}
	else if (bMissingSource)
	{
		DisplayTitle = FString::Printf(TEXT("%s -> %s  [Missing Source. Entry will be ignored]"), *SourceName, *TargetName);
	}
	else if (bMissingTarget)
	{
		DisplayTitle = FString::Printf(TEXT("%s -> %s  [Missing Target. Entry will be ignored]"), *SourceName, *TargetName);
	}
	else
	{
		DisplayTitle = FString::Printf(TEXT("%s -> %s"), *SourceName, *TargetName);
	}
}

// ------------------------------------------------------------
// UComposableCameraTransitionTableDataAsset
// ------------------------------------------------------------

void UComposableCameraTransitionTableDataAsset::PostLoad()
{
	Super::PostLoad();

	for (FComposableCameraTransitionTableEntry& Entry : Entries)
	{
		Entry.UpdateDisplayTitle();
	}
}

UComposableCameraTransitionBase* UComposableCameraTransitionTableDataAsset::FindTransition(
	const UComposableCameraTypeAsset* Source,
	const UComposableCameraTypeAsset* Target) const
{
	// Exact-match only. Both Source and Target must be non-null and match
	// the entry's assets. First matching entry in declaration order wins.

	if (!Source || !Target)
	{
		return nullptr;
	}

	for (const FComposableCameraTransitionTableEntry& Entry : Entries)
	{
		if (!Entry.Transition)
		{
			continue;
		}

		if (Entry.SourceTypeAsset.Get() == Source && Entry.TargetTypeAsset.Get() == Target)
		{
			return Entry.Transition;
		}
	}

	return nullptr;
}

#if WITH_EDITOR
void UComposableCameraTransitionTableDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	for (FComposableCameraTransitionTableEntry& Entry : Entries)
	{
		Entry.UpdateDisplayTitle();
	}
}

EDataValidationResult UComposableCameraTransitionTableDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FComposableCameraTransitionTableEntry& Entry = Entries[Index];

		if (Entry.SourceTypeAsset.IsNull())
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingSource",
					"Entry [{0}] has no Source Type Asset. Both Source and Target are required."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.TargetTypeAsset.IsNull())
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingTarget",
					"Entry [{0}] has no Target Type Asset. Both Source and Target are required."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (!Entry.Transition)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("MissingTransition",
					"Entry [{0}] has no Transition. This entry will be skipped at runtime."),
				FText::AsNumber(Index)));
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
