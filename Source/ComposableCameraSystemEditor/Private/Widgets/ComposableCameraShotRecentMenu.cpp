// Copyright 2026 Sulley. All Rights Reserved.
#include "Widgets/ComposableCameraShotRecentMenu.h"

#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraGraphNodeBase.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "Styling/SlateIconFinder.h"
#include "UObject/Package.h"
#include "Widgets/ComposableCameraShotEditorStyle.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotRecentMenu"

namespace ComposableCameraSystem::ShotRecentMenu
{
namespace
{
FText SequenceContext(const UObject& Host)
{
	const auto* Sequence = Host.GetTypedOuter<UMovieSceneSequence>();
	return Sequence ? FText::Format(LOCTEXT("Sequence", "Sequence: {0}"), FText::FromString(Sequence->GetName()))
		: LOCTEXT("UnknownSequence", "Sequence unavailable");
}

FText SectionTiming(const UMovieSceneComposableCameraShotSection& Section)
{
	const auto* Scene = Section.GetTypedOuter<UMovieScene>();
	const TRange<FFrameNumber> Range = Section.GetRange();
	FNumberFormattingOptions SecondsFormat;
	SecondsFormat.SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2).SetUseGrouping(false);
	const auto Bound = [&](bool bPresent, FFrameNumber Frame)
	{
		return bPresent && Scene
			? FText::Format(LOCTEXT("Seconds", "{0} s"), FText::AsNumber(Scene->GetTickResolution().AsSeconds(FFrameTime(Frame)), &SecondsFormat))
			: LOCTEXT("OpenBound", "Open");
	};
	return FText::Format(LOCTEXT("Timing", "{0} - {1}  |  Row {2}"),
		Bound(Range.HasLowerBound(), Range.HasLowerBound() ? Range.GetLowerBoundValue() : FFrameNumber(0)),
		Bound(Range.HasUpperBound(), Range.HasUpperBound() ? Range.GetUpperBoundValue() : FFrameNumber(0)),
		FText::AsNumber(Section.GetRowIndex()));
}
}

FPresentation Describe(const UObject* Host, const FText& FallbackLabel)
{
	FPresentation Result;
	Result.Title = FallbackLabel;
	Result.Kind = LOCTEXT("ShotKind", "Shot");
	if (!IsValid(Host))
	{
		Result.ToolTip = FallbackLabel;
		return Result;
	}
	Result.Icon = FSlateIconFinder::FindIconForClass(Host->GetClass());
	if (const auto* Instance = Cast<UComposableCameraInstanceData>(Host))
	{
		Result.Kind = LOCTEXT("InstanceKind", "Instance");
		Result.Icon = FSlateIconFinder::FindIconForClass(ULevelSequence::StaticClass());
		Result.Context = SequenceContext(*Instance);
		if (const auto* Track = Instance->GetTypedOuter<UMovieSceneComposableCameraInstanceTrack>())
		{
			// UE5.6's binding-label getter is non-const, even though this only reads metadata.
			UMovieScene* Scene = Track->GetTypedOuter<UMovieScene>();
			const FGuid Binding = Track->FindObjectBindingGuid();
			Result.Title = Scene && Binding.IsValid() ? Scene->GetObjectDisplayName(Binding) : Track->GetDisplayName();
		}
	}
	else if (const auto* Section = Cast<UMovieSceneComposableCameraShotSection>(Host))
	{
		Result.Kind = LOCTEXT("SectionKind", "Section");
		Result.Icon = FSlateIconFinder::FindIconForClass(ULevelSequence::StaticClass());
		const bool bInline = Section->Source == EComposableCameraShotSource::Inline;
		const FString AssetName = Section->ShotAssetRef.GetAssetName(); // Soft reference only; never load for a label.
		Result.Title = !Section->ShotLabel.IsEmpty() ? FText::FromString(Section->ShotLabel)
			: bInline ? FText::Format(LOCTEXT("InlineTitle", "Inline ({0})"), FText::AsNumber(Section->InlineShot.Targets.Num()))
			: AssetName.IsEmpty() ? LOCTEXT("NoAsset", "No Shot asset") : FText::FromString(AssetName);
		Result.Context = SequenceContext(*Section);
		if (const auto* Track = Section->GetTypedOuter<UMovieSceneTrack>())
		{
			UMovieScene* Scene = Track->GetTypedOuter<UMovieScene>();
			const FGuid Binding = Track->FindObjectBindingGuid();
			const FText TrackContext = Scene && Binding.IsValid()
				? FText::Format(LOCTEXT("BoundTrack", "{0} / {1}"), Scene->GetObjectDisplayName(Binding), Track->GetDisplayName())
				: Track->GetDisplayName();
			Result.Context = FText::Format(LOCTEXT("SectionContext", "{0} / {1}"), Result.Context, TrackContext);
		}
		Result.Detail = FText::Format(LOCTEXT("SectionDetail", "{0}  |  {1}"), SectionTiming(*Section),
			bInline ? LOCTEXT("InlineSource", "Inline")
				: FText::Format(LOCTEXT("AssetSource", "Asset reference: {0}"),
					AssetName.IsEmpty() ? LOCTEXT("NoAsset", "No Shot asset") : FText::FromString(AssetName)));
	}
	else if (const auto* Asset = Cast<UComposableCameraShotAsset>(Host))
	{
		Result.Kind = LOCTEXT("AssetKind", "Asset");
		Result.Title = FText::FromString(Asset->GetName());
		Result.Context = FText::Format(LOCTEXT("AssetFolder", "Folder: {0}"),
			FText::FromString(FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName())));
	}
	else if (const auto* Node = Cast<UComposableCameraCompositionFramingNode>(Host))
	{
		Result.Kind = LOCTEXT("NodeKind", "Node");
		Result.Title = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(Node->GetClass());
		const auto* Type = Node->GetTypedOuter<UComposableCameraTypeAsset>();
		const auto* Actor = Node->GetTypedOuter<AActor>();
		const FText Owner = Type ? FText::FromString(Type->GetName())
			: Actor ? FText::FromString(Actor->GetActorLabel()) : FText::FromString(Node->GetOuter()->GetName());
		Result.Context = FText::Format(LOCTEXT("NodeContext", "{0} / {1}"), Owner, FText::FromString(Node->GetName()));
	}

	// Full text remains available when the fixed-width row elides a long name/path.
	Result.ToolTip = FText::Format(LOCTEXT("EntryTip", "{0}\nType: {1}\n{2}{3}\n{4}\n\nClick to reopen."),
		Result.Title, Result.Kind, Result.Context,
		Result.Detail.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("DetailLine", "\n{0}"), Result.Detail),
		FText::FromString(Host->GetPathName()));
	return Result;
}

TSharedRef<SWidget> MakeEntry(const FPresentation& Presentation, bool bCurrent)
{
	const FSlateColor Muted = FSlateColor::UseSubduedForeground();
	return SNew(SBox).WidthOverride(460.f).Padding(FMargin(2.f, 5.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, 10.f, 0.f)
		[SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
			[SNew(SImage).Image(Presentation.Icon.GetIcon())]]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[SNew(STextBlock).Text(Presentation.Title).Font(FAppStyle::GetFontStyle("BoldFont"))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)
				[SNew(STextBlock).Text(LOCTEXT("Current", "Current")).ColorAndOpacity(ShotEditorStyle::Accent())
					.Visibility(bCurrent ? EVisibility::Visible : EVisibility::Collapsed)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[SNew(SBorder).BorderImage(FAppStyle::GetBrush("Brushes.Recessed")).Padding(FMargin(6.f, 1.f))
					[SNew(STextBlock).Text(Presentation.Kind).Font(FAppStyle::GetFontStyle("SmallFont")).ColorAndOpacity(Muted)]]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[SNew(STextBlock).Text(Presentation.Context).Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(Muted).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[SNew(STextBlock).Text(Presentation.Detail).Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(Muted).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Visibility(Presentation.Detail.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)]
		]
	];
}
}

#undef LOCTEXT_NAMESPACE
