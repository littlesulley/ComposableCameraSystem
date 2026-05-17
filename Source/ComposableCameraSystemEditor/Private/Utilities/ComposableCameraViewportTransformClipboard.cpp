// Copyright 2026 Sulley. All Rights Reserved.

#include "Utilities/ComposableCameraViewportTransformClipboard.h"

#include "ComposableCameraSystemEditorModule.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IKeyArea.h"
#include "ISequencer.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTrack.h"
#include "MovieSceneToolHelpers.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/Extensions/ITrackExtension.h"
#include "MVVM/Selection/Selection.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "ScopedTransaction.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneTransformTrack.h"
#include "UObject/NoExportTypes.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ComposableCameraViewportTransformClipboard"

namespace
{
	const FName ViewportTransformToolMenuOwner(TEXT("ComposableCameraViewportTransformClipboard"));
	FDelegateHandle StartupCallbackHandle;

	FLevelEditorViewportClient* ResolveActiveLevelViewportClient()
	{
		if (GCurrentLevelEditingViewportClient)
		{
			return GCurrentLevelEditingViewportClient;
		}
		return GLastKeyLevelEditingViewportClient;
	}

	FString ExportTransformForClipboard(const FTransform& Transform)
	{
		FString Text;
		TBaseStructure<FTransform>::Get()->ExportText(Text,
			&Transform,
			/*Defaults=*/ nullptr,
			/*OwnerObject=*/ nullptr,
			PPF_None,
			/*ExportRootScope=*/ nullptr);
		return Text;
	}

	void NotifyViewportTransformResult(const FText& Text, SNotificationItem::ECompletionState State)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		FNotificationInfo Info(Text);
		Info.ExpireDuration = 2.0f;
		Info.bUseThrobber = false;
		Info.bUseSuccessFailIcons = true;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(State);
		}
	}

	class FComposableCameraViewportTransformCommands: public TCommands<FComposableCameraViewportTransformCommands>
	{
	public:
		FComposableCameraViewportTransformCommands()
			: TCommands<FComposableCameraViewportTransformCommands>(TEXT("ComposableCameraViewportTransform"),
				LOCTEXT("ViewportTransformCommands", "Composable Camera Viewport Transform"),
				NAME_None,
				FAppStyle::GetAppStyleSetName())
		{
		}

		virtual void RegisterCommands() override
		{
			UI_COMMAND(CopyActiveViewportCameraTransform,
				"Copy Viewport Camera Transform",
				"Copy the active Level Editor viewport camera transform as pasteable FTransform text.",
				EUserInterfaceActionType::Button,
				FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::C));

			UI_COMMAND(KeyActiveViewportCameraTransform,
				"Key Viewport Camera Transform",
				"Key the active Level Editor viewport camera transform onto selected CCS Level Sequence Transform tracks.",
				EUserInterfaceActionType::Button,
				FInputChord(EModifierKey::Control | EModifierKey::Alt, EKeys::K));
		}

		TSharedPtr<FUICommandInfo> CopyActiveViewportCameraTransform;
		TSharedPtr<FUICommandInfo> KeyActiveViewportCameraTransform;
	};

	void ExecuteCopyActiveViewportCameraTransform()
	{
		(void)FComposableCameraViewportTransformClipboard::CopyActiveLevelViewportCameraTransform();
	}

	void ExecuteKeyActiveViewportCameraTransform()
	{
		(void)FComposableCameraViewportTransformClipboard::KeyActiveLevelViewportCameraTransformToSequencer();
	}

	FAutoConsoleCommand GCopyActiveViewportCameraTransformCommand(TEXT("CCS.Editor.CopyActiveViewportCameraTransform"),
		TEXT("Copy the active Level Editor viewport camera transform to the system clipboard as pasteable FTransform text."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteCopyActiveViewportCameraTransform));

	FAutoConsoleCommand GKeyActiveViewportCameraTransformCommand(TEXT("CCS.Editor.KeyActiveViewportCameraTransformToSequencer"),
		TEXT("Key the active Level Editor viewport camera transform onto selected CCS Level Sequence Transform tracks."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteKeyActiveViewportCameraTransform));

	TArray<TSharedPtr<ISequencer>> ResolveLiveSequencers()
	{
		if (!FModuleManager::Get().IsModuleLoaded("ComposableCameraSystemEditor"))
		{
			return {};
		}

		FComposableCameraSystemEditorModule& EditorModule =
			FModuleManager::GetModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
		return EditorModule.GetLiveSequencers();
	}

	bool IsComposableCameraLevelSequenceBindingOrDescendant(UMovieScene& MovieScene, const FGuid& BindingGuid)
	{
		if (!BindingGuid.IsValid())
		{
			return false;
		}

		if (FMovieSceneSpawnable* Spawnable = MovieScene.FindSpawnable(BindingGuid))
		{
			const UObject* ObjectTemplate = Spawnable->GetObjectTemplate();
			return ObjectTemplate
				&& (ObjectTemplate->IsA<AComposableCameraLevelSequenceActor>()
					|| ObjectTemplate->IsA<UComposableCameraLevelSequenceComponent>());
		}

		if (FMovieScenePossessable* Possessable = MovieScene.FindPossessable(BindingGuid))
		{
#if WITH_EDITORONLY_DATA
			const UClass* PossessedClass = Possessable->GetLoadedPossessedObjectClass();
			if (!PossessedClass)
			{
				PossessedClass = Possessable->GetPossessedObjectClass();
			}
			if (PossessedClass
				&& (PossessedClass->IsChildOf(AComposableCameraLevelSequenceActor::StaticClass())
					|| PossessedClass->IsChildOf(UComposableCameraLevelSequenceComponent::StaticClass())))
			{
				return true;
			}
#endif
			return IsComposableCameraLevelSequenceBindingOrDescendant(MovieScene, Possessable->GetParent());
		}

		return false;
	}

	bool IsTransformTrackType(const UMovieSceneTrack* Track)
	{
		return Cast<UMovieScene3DTransformTrack>(Track) || Cast<UMovieSceneTransformTrack>(Track);
	}

	bool IsComposableCameraLevelSequenceTransformTrack(UMovieScene& MovieScene, const UMovieSceneTrack* Track)
	{
		FGuid BindingGuid;
		return IsTransformTrackType(Track)
			&& MovieScene.FindTrackBinding(*Track, BindingGuid)
			&& IsComposableCameraLevelSequenceBindingOrDescendant(MovieScene, BindingGuid);
	}

	void AddTransformTrackIfValid(UMovieScene& MovieScene,
		UMovieSceneTrack* Track,
		TArray<UMovieSceneTrack*>& OutTracks)
	{
		if (Track && IsComposableCameraLevelSequenceTransformTrack(MovieScene, Track))
		{
			OutTracks.AddUnique(Track);
		}
	}

	void AppendOutlinerSelectedTransformTracks(ISequencer& Sequencer,
		UMovieScene& MovieScene,
		TArray<UMovieSceneTrack*>& OutTracks)
	{
		using namespace UE::Sequencer;

		const TSharedPtr<FSequencerEditorViewModel> ViewModel = Sequencer.GetViewModel();
		const TSharedPtr<FSequencerSelection> Selection = ViewModel.IsValid() ? ViewModel->GetSelection() : nullptr;
		if (!Selection.IsValid())
		{
			return;
		}

		for (TWeakViewModelPtr<IOutlinerExtension> WeakSelectedOutliner: Selection->Outliner.GetSelected())
		{
			TViewModelPtr<IOutlinerExtension> SelectedOutliner = WeakSelectedOutliner.Pin();
			if (!SelectedOutliner)
			{
				continue;
			}

			FViewModelPtr SelectedModel = SelectedOutliner.AsModel();
			TViewModelPtr<ITrackExtension> TrackExtension = SelectedModel
				? SelectedModel->FindAncestorOfType<ITrackExtension>(/*bIncludeThis=*/ true)
				: nullptr;
			if (TrackExtension)
			{
				AddTransformTrackIfValid(MovieScene, TrackExtension->GetTrack(), OutTracks);
			}
		}
	}

	void AppendSelectedTransformTracks(ISequencer& Sequencer,
		UMovieScene& MovieScene,
		TArray<UMovieSceneTrack*>& OutTracks)
	{
		TArray<UMovieSceneTrack*> SelectedTracks;
		Sequencer.GetSelectedTracks(SelectedTracks);
		for (UMovieSceneTrack* SelectedTrack: SelectedTracks)
		{
			AddTransformTrackIfValid(MovieScene, SelectedTrack, OutTracks);
		}

		TArray<TPair<UMovieSceneTrack*, int32>> SelectedTrackRows;
		Sequencer.GetSelectedTrackRows(SelectedTrackRows);
		for (const TPair<UMovieSceneTrack*, int32>& SelectedTrackRow: SelectedTrackRows)
		{
			AddTransformTrackIfValid(MovieScene, SelectedTrackRow.Key, OutTracks);
		}

		TArray<UMovieSceneSection*> SelectedSections;
		Sequencer.GetSelectedSections(SelectedSections);
		for (UMovieSceneSection* SelectedSection: SelectedSections)
		{
			AddTransformTrackIfValid(MovieScene,
				SelectedSection ? SelectedSection->GetTypedOuter<UMovieSceneTrack>() : nullptr,
				OutTracks);
		}

		TArray<const IKeyArea*> SelectedKeyAreas;
		Sequencer.GetSelectedKeyAreas(SelectedKeyAreas);
		for (const IKeyArea* SelectedKeyArea: SelectedKeyAreas)
		{
			UMovieSceneSection* OwningSection = SelectedKeyArea ? SelectedKeyArea->GetOwningSection() : nullptr;
			AddTransformTrackIfValid(MovieScene,
				OwningSection ? OwningSection->GetTypedOuter<UMovieSceneTrack>() : nullptr,
				OutTracks);
		}
	}

	void AppendTransformTracksForSelectedBindings(ISequencer& Sequencer,
		UMovieScene& MovieScene,
		TArray<UMovieSceneTrack*>& OutTracks)
	{
		TArray<FGuid> Bindings;
		Sequencer.GetSelectedObjects(Bindings);
		for (const FGuid& Binding: Bindings)
		{
			if (!Binding.IsValid())
			{
				continue;
			}

			for (UMovieSceneTrack* Track: MovieScene.FindTracks(UMovieScene3DTransformTrack::StaticClass(), Binding))
			{
				AddTransformTrackIfValid(MovieScene, Track, OutTracks);
			}
			for (UMovieSceneTrack* Track: MovieScene.FindTracks(UMovieSceneTransformTrack::StaticClass(), Binding))
			{
				AddTransformTrackIfValid(MovieScene, Track, OutTracks);
			}
		}
	}

	TArray<UMovieSceneTrack*> ResolveTransformTracksToKey(ISequencer& Sequencer, UMovieScene& MovieScene)
	{
		TArray<UMovieSceneTrack*> Tracks;
		AppendOutlinerSelectedTransformTracks(Sequencer, MovieScene, Tracks);
		AppendSelectedTransformTracks(Sequencer, MovieScene, Tracks);
		if (Tracks.IsEmpty())
		{
			AppendTransformTracksForSelectedBindings(Sequencer, MovieScene, Tracks);
		}

		return Tracks;
	}

	struct FResolvedTransformTrackSelection
	{
		TSharedPtr<ISequencer> Sequencer;
		TWeakObjectPtr<UMovieScene> MovieScene;
		TArray<UMovieSceneTrack*> Tracks;
	};

	FResolvedTransformTrackSelection ResolveSequencerAndTransformTracksToKey()
	{
		for (const TSharedPtr<ISequencer>& Sequencer: ResolveLiveSequencers())
		{
			UMovieSceneSequence* Sequence = Sequencer.IsValid() ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
			UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
			if (!MovieScene)
			{
				continue;
			}

			TArray<UMovieSceneTrack*> Tracks = ResolveTransformTracksToKey(*Sequencer, *MovieScene);
			if (!Tracks.IsEmpty())
			{
				return { Sequencer, MovieScene, MoveTemp(Tracks) };
			}
		}

		return {};
	}

	UMovieScene3DTransformSection* FindOrCreateTransformSectionForTrack(UMovieSceneTrack& TransformTrack,
		FFrameNumber KeyTime)
	{
		UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(&TransformTrack);
		if (!PropertyTrack)
		{
			return nullptr;
		}

		PropertyTrack->Modify();

		const bool bHadSections = PropertyTrack->GetAllSections().Num() > 0;
		bool bSectionAdded = false;
		UMovieScene3DTransformSection* TransformSection =
			Cast<UMovieScene3DTransformSection>(PropertyTrack->FindOrAddSection(KeyTime, bSectionAdded));
		if (!TransformSection)
		{
			return nullptr;
		}

		TransformSection->Modify();
		PropertyTrack->SetSectionToKey(TransformSection);

		if (bSectionAdded && !bHadSections)
		{
			TransformSection->SetRange(TRange<FFrameNumber>::All());
		}

		const EMovieSceneTransformChannel ChannelsToKey = EMovieSceneTransformChannel::AllTransform;
		const EMovieSceneTransformChannel ExistingChannels = TransformSection->GetMask().GetChannels();
		if (!EnumHasAllFlags(ExistingChannels, ChannelsToKey))
		{
			TransformSection->SetMask(ExistingChannels | ChannelsToKey);
		}

		return TransformSection;
	}

	bool KeyViewportCameraTransformTracks(const FEditorViewportClient& ViewportClient,
		ISequencer& Sequencer,
		UMovieScene& MovieScene,
		const TArray<UMovieSceneTrack*>& TransformTracks,
		const FText& SourceLabel)
	{
		if (!Sequencer.IsAllowedToChange())
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("Key viewport camera transform failed: Sequencer does not allow edits."));
			NotifyViewportTransformResult(LOCTEXT("KeySequencerReadOnly", "Sequencer does not allow edits."),
				SNotificationItem::CS_Fail);
			return false;
		}

		if (TransformTracks.IsEmpty())
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("Key viewport camera transform failed: no selected CCS Level Sequence Transform track."));
			NotifyViewportTransformResult(LOCTEXT("KeyNoTransformParameterTrack", "Select a CCS Level Sequence Transform track first."),
				SNotificationItem::CS_Fail);
			return false;
		}

		const FTransform ViewportWorldTransform(ViewportClient.GetViewRotation(),
			ViewportClient.GetViewLocation(),
			FVector::OneVector);
		const FFrameNumber KeyTime = Sequencer.GetLocalTime().Time.RoundToFrame();

		FScopedTransaction Transaction(LOCTEXT("KeyViewportCameraTransformTransaction", "Key Viewport Camera Transform"));

		int32 KeyedCount = 0;
		int32 SkippedCount = 0;
		for (UMovieSceneTrack* TransformTrack: TransformTracks)
		{
			if (!TransformTrack || !IsComposableCameraLevelSequenceTransformTrack(MovieScene, TransformTrack))
			{
				++SkippedCount;
				continue;
			}

			UMovieScene3DTransformSection* TransformSection =
				FindOrCreateTransformSectionForTrack(*TransformTrack, KeyTime);
			if (!TransformSection)
			{
				++SkippedCount;
				continue;
			}

			TArray<FFrameNumber> Frames;
			Frames.Add(KeyTime);
			TArray<FTransform> Transforms;
			Transforms.Add(ViewportWorldTransform);
			const bool bKeyed = MovieSceneToolHelpers::AddTransformKeys(TransformSection,
				Frames,
				Transforms,
				EMovieSceneTransformChannel::AllTransform);

			if (bKeyed)
			{
				++KeyedCount;
			}
			else
			{
				++SkippedCount;
			}
		}

		if (KeyedCount <= 0)
		{
			Transaction.Cancel();
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("Key viewport camera transform failed: no CCS Level Sequence Transform tracks were keyed."));
			NotifyViewportTransformResult(LOCTEXT("KeyNoTransformParameterTracks", "No CCS Level Sequence Transform tracks were keyed."),
				SNotificationItem::CS_Fail);
			return false;
		}

		Sequencer.NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChangedRefreshImmediately);
		Sequencer.ForceEvaluate();

		UE_LOG(LogComposableCameraSystemEditor, Log,
			TEXT("Keyed %d CCS Level Sequence Transform track(s) from %s at frame %d. Skipped: %d."),
			KeyedCount,
			*SourceLabel.ToString(),
			KeyTime.Value,
			SkippedCount);
		NotifyViewportTransformResult(FText::Format(LOCTEXT("KeySucceeded", "Keyed {0} CCS Transform track(s)."),
				FText::AsNumber(KeyedCount)),
			SNotificationItem::CS_Success);
		return true;
	}
}

void FComposableCameraViewportTransformClipboard::Register()
{
	FComposableCameraViewportTransformCommands::Register();

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetGlobalLevelEditorActions()->MapAction(FComposableCameraViewportTransformCommands::Get().CopyActiveViewportCameraTransform,
		FExecuteAction::CreateStatic(&ExecuteCopyActiveViewportCameraTransform),
		FCanExecuteAction::CreateStatic(&FComposableCameraViewportTransformClipboard::CanCopyActiveLevelViewportCameraTransform));
	LevelEditorModule.GetGlobalLevelEditorActions()->MapAction(FComposableCameraViewportTransformCommands::Get().KeyActiveViewportCameraTransform,
		FExecuteAction::CreateStatic(&ExecuteKeyActiveViewportCameraTransform),
		FCanExecuteAction::CreateStatic(&FComposableCameraViewportTransformClipboard::CanKeyActiveLevelViewportCameraTransformToSequencer));

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		StartupCallbackHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&FComposableCameraViewportTransformClipboard::RegisterMenus));
	}
}

void FComposableCameraViewportTransformClipboard::Unregister()
{
	if (StartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(StartupCallbackHandle);
		StartupCallbackHandle.Reset();
	}
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->UnregisterOwnerByName(ViewportTransformToolMenuOwner);
	}

	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetGlobalLevelEditorActions()->UnmapAction(FComposableCameraViewportTransformCommands::Get().CopyActiveViewportCameraTransform);
		LevelEditorModule.GetGlobalLevelEditorActions()->UnmapAction(FComposableCameraViewportTransformCommands::Get().KeyActiveViewportCameraTransform);
	}

	FComposableCameraViewportTransformCommands::Unregister();
}

bool FComposableCameraViewportTransformClipboard::CanCopyActiveLevelViewportCameraTransform()
{
	return ResolveActiveLevelViewportClient() != nullptr;
}

bool FComposableCameraViewportTransformClipboard::CopyActiveLevelViewportCameraTransform()
{
	FLevelEditorViewportClient* ViewportClient = ResolveActiveLevelViewportClient();
	if (!ViewportClient)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Copy viewport camera transform failed: no active Level Editor viewport."));
		NotifyViewportTransformResult(LOCTEXT("NoActiveViewport", "No active Level Editor viewport."),
			SNotificationItem::CS_Fail);
		return false;
	}

	return CopyViewportCameraTransform(*ViewportClient, LOCTEXT("LevelEditorViewport", "Level Editor viewport"));
}

bool FComposableCameraViewportTransformClipboard::CopyViewportCameraTransform(const FEditorViewportClient& ViewportClient,
	const FText& SourceLabel)
{
	const FTransform CameraTransform(ViewportClient.GetViewRotation(),
		ViewportClient.GetViewLocation(),
		FVector::OneVector);
	const FString ClipboardText = ExportTransformForClipboard(CameraTransform);

	if (ClipboardText.IsEmpty())
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Copy viewport camera transform failed: export produced an empty string."));
		NotifyViewportTransformResult(LOCTEXT("CopyExportFailed", "Could not export viewport camera transform."),
			SNotificationItem::CS_Fail);
		return false;
	}

	FPlatformApplicationMisc::ClipboardCopy(*ClipboardText);

	UE_LOG(LogComposableCameraSystemEditor, Log,
		TEXT("Copied %s camera transform to clipboard: %s"),
		*SourceLabel.ToString(), *ClipboardText);
	NotifyViewportTransformResult(FText::Format(LOCTEXT("CopySucceeded", "Copied {0} camera transform."), SourceLabel),
		SNotificationItem::CS_Success);
	return true;
}

bool FComposableCameraViewportTransformClipboard::CanKeyActiveLevelViewportCameraTransformToSequencer()
{
	return ResolveActiveLevelViewportClient() != nullptr;
}

bool FComposableCameraViewportTransformClipboard::KeyActiveLevelViewportCameraTransformToSequencer()
{
	FLevelEditorViewportClient* ViewportClient = ResolveActiveLevelViewportClient();
	if (!ViewportClient)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Key viewport camera transform failed: no active Level Editor viewport."));
		NotifyViewportTransformResult(LOCTEXT("KeyNoActiveViewport", "No active Level Editor viewport."),
			SNotificationItem::CS_Fail);
		return false;
	}

	return KeyViewportCameraTransformToSequencer(
		*ViewportClient,
		LOCTEXT("LevelEditorViewport", "Level Editor viewport"));
}

bool FComposableCameraViewportTransformClipboard::KeyViewportCameraTransformToSequencer(const FEditorViewportClient& ViewportClient,
	const FText& SourceLabel)
{
	FResolvedTransformTrackSelection Selection = ResolveSequencerAndTransformTracksToKey();
	TSharedPtr<ISequencer> Sequencer = Selection.Sequencer;
	UMovieScene* MovieScene = Selection.MovieScene.Get();
	if (!Sequencer.IsValid())
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Key viewport camera transform failed: no open Sequencer with selected CCS Level Sequence Transform tracks."));
		NotifyViewportTransformResult(LOCTEXT("KeyNoSequencer", "Select a CCS Level Sequence Transform track first."),
			SNotificationItem::CS_Fail);
		return false;
	}

	if (!MovieScene)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Key viewport camera transform failed: focused MovieScene is no longer valid."));
		NotifyViewportTransformResult(LOCTEXT("KeyNoMovieScene", "Focused MovieScene is no longer valid."),
			SNotificationItem::CS_Fail);
		return false;
	}

	const TArray<UMovieSceneTrack*> TransformTracks = MoveTemp(Selection.Tracks);
	return KeyViewportCameraTransformTracks(ViewportClient, *Sequencer, *MovieScene, TransformTracks, SourceLabel);
}

void FComposableCameraViewportTransformClipboard::RegisterMenus()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FToolMenuOwnerScoped OwnerScoped(ViewportTransformToolMenuOwner);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection("ComposableCameraSystem");
	Section.Label = LOCTEXT("ComposableCameraSystemSection", "Composable Camera System");
	Section.AddEntry(FToolMenuEntry::InitMenuEntryWithCommandList(FComposableCameraViewportTransformCommands::Get().CopyActiveViewportCameraTransform,
		LevelEditorModule.GetGlobalLevelEditorActions(),
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Copy")));
	Section.AddEntry(FToolMenuEntry::InitMenuEntryWithCommandList(FComposableCameraViewportTransformCommands::Get().KeyActiveViewportCameraTransform,
		LevelEditorModule.GetGlobalLevelEditorActions(),
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon()));
}

#undef LOCTEXT_NAMESPACE
