// Copyright Sulley. All rights reserved.

#include "SGraphPinComposableCameraDataTable.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Engine/DataTable.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "Widgets/Layout/SBox.h"

#include "K2Node_ActivateComposableCameraFromDataTable.h"

namespace ComposableCameraDataTablePin
{
	// Asset-registry tag names used across UE versions to record a
	// DataTable's row struct. We check all of them to stay robust across
	// engine changes: "RowStructurePath" holds a full soft-object path
	// (newer), and "RowStructure" has historically held either the full
	// path name or just the struct's short name depending on version.
	static const FName RowStructurePathTag(TEXT("RowStructurePath"));
	static const FName RowStructureTag(TEXT("RowStructure"));

	/** Return true if the tag value refers to the required struct. */
	static bool DoesTagValueMatchStruct(const FString& TagValue, const UScriptStruct* RequiredStruct)
	{
		if (TagValue.IsEmpty() || RequiredStruct == nullptr)
		{
			return false;
		}

		// Full path name comparison - the most common modern form.
		const FString RequiredPath = RequiredStruct->GetStructPathName().ToString();
		if (TagValue == RequiredPath)
		{
			return true;
		}

		// Short-name comparison - the legacy form.
		const FString RequiredShortName = RequiredStruct->GetName();
		if (TagValue == RequiredShortName)
		{
			return true;
		}

		// Path-like tag whose object name portion matches. Handles any
		// stray variation like "/Script/Foo.Bar" where Bar is the short
		// name but the tag happens to contain the full path.
		int32 DotIdx = INDEX_NONE;
		if (TagValue.FindLastChar(TEXT('.'), DotIdx))
		{
			const FString TagObjectName = TagValue.Mid(DotIdx + 1);
			if (TagObjectName == RequiredShortName)
			{
				return true;
			}
		}

		return false;
	}
}

void SGraphPinComposableCameraDataTable::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPinObject::Construct(SGraphPinObject::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget> SGraphPinComposableCameraDataTable::GenerateAssetPicker()
{
	// Build our own FAssetPickerConfig so we can install a custom
	// OnShouldFilterAsset delegate. The base class's OnShouldFilterAsset
	// is non-virtual, so overriding it from a subclass doesn't change
	// the filter that its own GenerateAssetPicker installs - we have to
	// replace the whole thing.
	FAssetPickerConfig AssetPickerConfig;

	// Only list DataTables (and subclasses) from the content browser.
	AssetPickerConfig.Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;

	AssetPickerConfig.bAllowNullSelection = true;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.bAllowDragging = false;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;
	AssetPickerConfig.SaveSettingsName = TEXT("SGraphPinComposableCameraDataTable");

	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &SGraphPinComposableCameraDataTable::ShouldFilterDataTable);
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SGraphPinComposableCameraDataTable::HandleAssetSelected);
	AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateSP(this, &SGraphPinComposableCameraDataTable::HandleAssetEnterPressed);

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Dimensions match what SGraphPinObject uses for its default picker so
	// the combo layout doesn't shift when the custom picker appears.
	return SNew(SBox)
		.HeightOverride(300.f)
		.WidthOverride(300.f)
		[ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)];
}

void SGraphPinComposableCameraDataTable::HandleAssetSelected(const FAssetData& AssetData)
{
	// Forward to the base's virtual so the canonical pin write + combo
	// close path runs unchanged. Called through the derived pointer so
	// if the base is virtual we still land on the base implementation
	// (we don't override it); if a future engine version makes it
	// non-virtual we still reach it via the explicit scope qualifier.
	SGraphPinObject::OnAssetSelectedFromPicker(AssetData);
}

void SGraphPinComposableCameraDataTable::HandleAssetEnterPressed(const TArray<FAssetData>& InSelectedAssets)
{
	SGraphPinObject::OnAssetEnterPressedInPicker(InSelectedAssets);
}

bool SGraphPinComposableCameraDataTable::ShouldFilterDataTable(const FAssetData& AssetData) const
{
	// Fast-reject anything that's not even a DataTable. The picker is
	// already configured for UDataTable so this is mostly defensive.
	if (!AssetData.IsInstanceOf(UDataTable::StaticClass()))
	{
		return true;
	}

	const UScriptStruct* RequiredStruct =
		UK2Node_ActivateComposableCameraFromDataTable::GetRequiredRowStruct();
	if (!RequiredStruct)
	{
		// No requirement - accept all DataTables. This branch exists only
		// to keep the widget robust if the K2 node's static is ever
		// changed to return nullptr in some configuration.
		return false;
	}

	// Fast path: asset registry tag comparison 
	// Both known tag names are checked, and the comparator accepts the
	// full path name, the short name, and the path-object-name portion.
	// This avoids loading the asset when the metadata is sufficient.
	FString TagValue;
	if (AssetData.GetTagValue(ComposableCameraDataTablePin::RowStructurePathTag, TagValue))
	{
		if (ComposableCameraDataTablePin::DoesTagValueMatchStruct(TagValue, RequiredStruct))
		{
			return false;
		}
	}
	if (AssetData.GetTagValue(ComposableCameraDataTablePin::RowStructureTag, TagValue))
	{
		if (ComposableCameraDataTablePin::DoesTagValueMatchStruct(TagValue, RequiredStruct))
		{
			return false;
		}
	}

	// Slow path: sync-load and inspect the actual row struct 
	// Fallback for the case where the asset registry tag format changes
	// or is missing entirely. DataTable assets are small, and the picker
	// only runs this on the tiny set of DataTable assets in the project,
	// so the one-shot sync load here is tolerable. The correct answer
	// always wins over the cheap one.
	if (const UDataTable* DataTable = Cast<UDataTable>(AssetData.GetAsset()))
	{
		const UScriptStruct* RowStruct = DataTable->GetRowStruct();
		if (RowStruct && RowStruct->IsChildOf(RequiredStruct))
		{
			return false;
		}
	}

	return true;
}
