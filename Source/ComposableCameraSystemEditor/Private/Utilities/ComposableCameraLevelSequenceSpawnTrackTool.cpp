// Copyright Sulley. All Rights Reserved.

#include "Utilities/ComposableCameraLevelSequenceSpawnTrackTool.h"

#include "Algo/AnyOf.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Bindings/MovieSceneSpawnableBinding.h"
#include "Components/ActorComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "ISequencer.h"
#include "LevelEditor.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "MovieScene.h"
#include "MovieSceneBindingReferences.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTimeHelpers.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ComposableCameraLevelSequenceSpawnTrackTool"

namespace
{
	const FName SpawnTrackToolMenuOwner(TEXT("ComposableCameraLevelSequenceSpawnTrackTool"));
	FDelegateHandle StartupCallbackHandle;

	struct FSpawnInterval
	{
		FFrameNumber Start;
		FFrameNumber End;
	};

	struct FCameraCutGatherStats
	{
		int32 CameraCutCount = 0;
		int32 SkippedNoBinding = 0;
		int32 SkippedNonComposableCamera = 0;
		int32 SkippedNonSpawnable = 0;
		int32 SkippedOutsideFocusedSequence = 0;
		int32 SkippedInvalidRange = 0;
	};

	struct FResolvedSequencer
	{
		TSharedPtr<ISequencer> Sequencer;
		TWeakObjectPtr<UMovieSceneSequence> Sequence;
		TWeakObjectPtr<UMovieScene> MovieScene;
	};

	void NotifySpawnTrackResult(const FText& Text, SNotificationItem::ECompletionState State)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		FNotificationInfo Info(Text);
		Info.ExpireDuration = 3.0f;
		Info.bUseThrobber = false;
		Info.bUseSuccessFailIcons = true;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(State);
		}
	}

	class FComposableCameraLevelSequenceSpawnTrackCommands
		: public TCommands<FComposableCameraLevelSequenceSpawnTrackCommands>
	{
	public:
		FComposableCameraLevelSequenceSpawnTrackCommands()
			: TCommands<FComposableCameraLevelSequenceSpawnTrackCommands>(
				TEXT("ComposableCameraLevelSequenceSpawnTrack"),
				LOCTEXT("LevelSequenceSpawnTrackCommands", "Composable Camera Level Sequence Spawn Tracks"),
				NAME_None,
				FAppStyle::GetAppStyleSetName())
		{
		}

		virtual void RegisterCommands() override
		{
			UI_COMMAND(
				KeySpawnTracksFromCameraCuts,
				"Key Spawn Tracks From Camera Cuts",
				"Rebuild CCS Level Sequence Actor Spawn Tracks from the focused Level Sequence Camera Cut sections.",
				EUserInterfaceActionType::Button,
				FInputChord());
		}

		TSharedPtr<FUICommandInfo> KeySpawnTracksFromCameraCuts;
	};

	void ExecuteKeySpawnTracksFromCameraCuts()
	{
		(void)FComposableCameraLevelSequenceSpawnTrackTool::KeySpawnTracksFromCameraCuts();
	}

	FAutoConsoleCommand GKeySpawnTracksFromCameraCutsCommand(
		TEXT("CCS.Editor.KeySpawnTracksFromCameraCuts"),
		TEXT("Rebuild CCS Level Sequence Actor Spawn Tracks from the focused Level Sequence Camera Cut sections."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteKeySpawnTracksFromCameraCuts));

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

	FResolvedSequencer ResolveFocusedSequencerWithMovieScene()
	{
		FResolvedSequencer FirstResolved;
		for (const TSharedPtr<ISequencer>& Sequencer : ResolveLiveSequencers())
		{
			UMovieSceneSequence* Sequence = Sequencer.IsValid()
				? Sequencer->GetFocusedMovieSceneSequence()
				: nullptr;
			UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
			if (MovieScene)
			{
				FResolvedSequencer Resolved{ Sequencer, Sequence, MovieScene };
				if (!FirstResolved.Sequencer.IsValid())
				{
					FirstResolved = Resolved;
				}

				UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
				if (CameraCutTrack && !CameraCutTrack->GetAllSections().IsEmpty())
				{
					return Resolved;
				}
			}
		}

		return FirstResolved;
	}

	bool TryGetDiscreteRange(
		const TRange<FFrameNumber>& Range,
		FFrameNumber& OutStart,
		FFrameNumber& OutEnd)
	{
		if (!Range.HasLowerBound() || !Range.HasUpperBound())
		{
			return false;
		}

		OutStart = UE::MovieScene::DiscreteInclusiveLower(Range);
		OutEnd = UE::MovieScene::DiscreteExclusiveUpper(Range);
		return OutEnd > OutStart;
	}

	TOptional<FGuid> ResolveComposableCameraActorBindingRecursive(
		UMovieScene& MovieScene,
		const FGuid& BindingGuid,
		TSet<FGuid>& Visiting)
	{
		if (!BindingGuid.IsValid() || Visiting.Contains(BindingGuid))
		{
			return {};
		}
		Visiting.Add(BindingGuid);

		if (FMovieSceneSpawnable* Spawnable = MovieScene.FindSpawnable(BindingGuid))
		{
			const UObject* ObjectTemplate = Spawnable->GetObjectTemplate();
			if (ObjectTemplate && ObjectTemplate->IsA<AComposableCameraLevelSequenceActor>())
			{
				return BindingGuid;
			}
		}

		if (FMovieScenePossessable* Possessable = MovieScene.FindPossessable(BindingGuid))
		{
#if WITH_EDITORONLY_DATA
			const UClass* PossessedClass = Possessable->GetLoadedPossessedObjectClass();
			if (!PossessedClass)
			{
				PossessedClass = Possessable->GetPossessedObjectClass();
			}
			if (PossessedClass && PossessedClass->IsChildOf(AComposableCameraLevelSequenceActor::StaticClass()))
			{
				return BindingGuid;
			}
#endif

			const FGuid& ParentGuid = Possessable->GetParent();
			if (ParentGuid.IsValid())
			{
				return ResolveComposableCameraActorBindingRecursive(MovieScene, ParentGuid, Visiting);
			}
		}

		return {};
	}

	TOptional<FGuid> ResolveComposableCameraActorBinding(UMovieScene& MovieScene, const FGuid& BindingGuid)
	{
		TSet<FGuid> Visiting;
		return ResolveComposableCameraActorBindingRecursive(MovieScene, BindingGuid, Visiting);
	}

	bool IsSpawnableBinding(
		const UMovieSceneSequence& Sequence,
		UMovieScene& MovieScene,
		const FGuid& BindingGuid)
	{
		if (MovieScene.FindSpawnable(BindingGuid))
		{
			return true;
		}

		const FMovieSceneBindingReferences* BindingReferences = Sequence.GetBindingReferences();
		return BindingReferences
			&& Algo::AnyOf(
				BindingReferences->GetReferences(BindingGuid),
				[](const FMovieSceneBindingReference& BindingReference)
				{
					return BindingReference.CustomBinding
						&& BindingReference.CustomBinding->IsA<UMovieSceneSpawnableBindingBase>();
				});
	}

	TOptional<FGuid> ResolveComposableCameraActorBindingFromRuntimeObjects(
		ISequencer& Sequencer,
		const FMovieSceneObjectBindingID& CameraBindingID)
	{
		for (TWeakObjectPtr<> WeakObject : CameraBindingID.ResolveBoundObjects(Sequencer.GetFocusedTemplateID(), Sequencer))
		{
			UObject* Object = WeakObject.Get();
			AComposableCameraLevelSequenceActor* Actor = Cast<AComposableCameraLevelSequenceActor>(Object);
			if (!Actor)
			{
				if (const UActorComponent* Component = Cast<UActorComponent>(Object))
				{
					Actor = Cast<AComposableCameraLevelSequenceActor>(Component->GetOwner());
				}
			}

			if (!Actor)
			{
				continue;
			}

			const FGuid ActorBindingGuid = Sequencer.GetHandleToObject(Actor, /*bCreateHandleIfMissing=*/ false);
			if (ActorBindingGuid.IsValid())
			{
				return ActorBindingGuid;
			}
		}

		return {};
	}

	TOptional<FGuid> ResolveComposableCameraSpawnableBinding(
		ISequencer& Sequencer,
		const UMovieSceneSequence& Sequence,
		UMovieScene& MovieScene,
		const FMovieSceneObjectBindingID& CameraBindingID,
		FCameraCutGatherStats& InOutStats)
	{
		const FMovieSceneSequenceID FocusedSequenceID = Sequencer.GetFocusedTemplateID();
		UE::MovieScene::FFixedObjectBindingID FixedBindingID =
			CameraBindingID.ResolveToFixed(FocusedSequenceID, Sequencer);
		if (FixedBindingID.SequenceID != FocusedSequenceID)
		{
			++InOutStats.SkippedOutsideFocusedSequence;
			return {};
		}

		const TOptional<FGuid> StructuralActorBindingGuid =
			ResolveComposableCameraActorBinding(MovieScene, FixedBindingID.Guid);
		if (StructuralActorBindingGuid.IsSet())
		{
			if (IsSpawnableBinding(Sequence, MovieScene, StructuralActorBindingGuid.GetValue()))
			{
				return StructuralActorBindingGuid;
			}

			++InOutStats.SkippedNonSpawnable;
			return {};
		}

		const TOptional<FGuid> RuntimeActorBindingGuid =
			ResolveComposableCameraActorBindingFromRuntimeObjects(Sequencer, CameraBindingID);
		if (RuntimeActorBindingGuid.IsSet())
		{
			if (IsSpawnableBinding(Sequence, MovieScene, RuntimeActorBindingGuid.GetValue()))
			{
				return RuntimeActorBindingGuid;
			}

			++InOutStats.SkippedNonSpawnable;
			return {};
		}

		++InOutStats.SkippedNonComposableCamera;
		return {};
	}

	void AddInterval(
		TMap<FGuid, TArray<FSpawnInterval>>& IntervalsByBinding,
		const FGuid& BindingGuid,
		const FFrameNumber Start,
		const FFrameNumber End)
	{
		if (End > Start)
		{
			IntervalsByBinding.FindOrAdd(BindingGuid).Add({ Start, End });
		}
	}

	void MergeIntervals(TArray<FSpawnInterval>& Intervals)
	{
		Intervals.Sort([](const FSpawnInterval& A, const FSpawnInterval& B)
		{
			return A.Start == B.Start ? A.End < B.End : A.Start < B.Start;
		});

		TArray<FSpawnInterval> Merged;
		for (const FSpawnInterval& Interval : Intervals)
		{
			if (Merged.IsEmpty() || Interval.Start > Merged.Last().End)
			{
				Merged.Add(Interval);
				continue;
			}

			if (Interval.End > Merged.Last().End)
			{
				Merged.Last().End = Interval.End;
			}
		}

		Intervals = MoveTemp(Merged);
	}

	bool GatherCameraCutIntervals(
		ISequencer& Sequencer,
		const UMovieSceneSequence& Sequence,
		UMovieScene& MovieScene,
		TMap<FGuid, TArray<FSpawnInterval>>& OutIntervalsByBinding,
		FCameraCutGatherStats& OutStats,
		FFrameNumber& OutPlaybackStart,
		FFrameNumber& OutPlaybackEnd)
	{
		const TRange<FFrameNumber> PlaybackRange = MovieScene.GetPlaybackRange();
		if (!TryGetDiscreteRange(PlaybackRange, OutPlaybackStart, OutPlaybackEnd))
		{
			return false;
		}

		UMovieSceneTrack* CameraCutTrack = MovieScene.GetCameraCutTrack();
		if (!CameraCutTrack)
		{
			return true;
		}

		for (UMovieSceneSection* RawSection : CameraCutTrack->GetAllSections())
		{
			UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(RawSection);
			if (!CameraCutSection || !CameraCutSection->IsActive())
			{
				continue;
			}

			++OutStats.CameraCutCount;

			const FMovieSceneObjectBindingID& CameraBindingID = CameraCutSection->GetCameraBindingID();
			if (!CameraBindingID.IsValid())
			{
				++OutStats.SkippedNoBinding;
				continue;
			}

			const TOptional<FGuid> ActorBindingGuid =
				ResolveComposableCameraSpawnableBinding(
					Sequencer,
					Sequence,
					MovieScene,
					CameraBindingID,
					OutStats);
			if (!ActorBindingGuid.IsSet())
			{
				continue;
			}

			const TRange<FFrameNumber> ClampedRange =
				TRange<FFrameNumber>::Intersection(CameraCutSection->GetTrueRange(), PlaybackRange);
			FFrameNumber CutStart;
			FFrameNumber CutEnd;
			if (!TryGetDiscreteRange(ClampedRange, CutStart, CutEnd))
			{
				++OutStats.SkippedInvalidRange;
				continue;
			}

			AddInterval(OutIntervalsByBinding, ActorBindingGuid.GetValue(), CutStart, CutEnd);
		}

		for (TPair<FGuid, TArray<FSpawnInterval>>& Pair : OutIntervalsByBinding)
		{
			MergeIntervals(Pair.Value);
		}

		return true;
	}

	UMovieSceneSpawnTrack* FindOrCreateSpawnTrack(UMovieScene& MovieScene, const FGuid& BindingGuid)
	{
		if (UMovieSceneSpawnTrack* ExistingTrack = MovieScene.FindTrack<UMovieSceneSpawnTrack>(BindingGuid))
		{
			return ExistingTrack;
		}

		UMovieSceneSpawnTrack* NewTrack = MovieScene.AddTrack<UMovieSceneSpawnTrack>(BindingGuid);
		if (NewTrack)
		{
			NewTrack->SetObjectId(BindingGuid);
		}
		return NewTrack;
	}

	bool WriteSpawnTrackForIntervals(
		UMovieScene& MovieScene,
		const FGuid& BindingGuid,
		const TArray<FSpawnInterval>& Intervals,
		const FFrameNumber PlaybackStart,
		const FFrameNumber PlaybackEnd,
		int32& OutKeyCount)
	{
		if (Intervals.IsEmpty() || PlaybackEnd <= PlaybackStart)
		{
			return false;
		}

		UMovieSceneSpawnTrack* SpawnTrack = FindOrCreateSpawnTrack(MovieScene, BindingGuid);
		if (!SpawnTrack)
		{
			return false;
		}

		SpawnTrack->Modify();
		SpawnTrack->SetObjectId(BindingGuid);
		SpawnTrack->RemoveAllAnimationData();

		UMovieSceneSpawnSection* SpawnSection =
			Cast<UMovieSceneSpawnSection>(SpawnTrack->CreateNewSection());
		if (!SpawnSection)
		{
			return false;
		}

		SpawnSection->Modify();
		SpawnSection->SetRange(UE::MovieScene::MakeDiscreteRange(PlaybackStart, PlaybackEnd));

		FMovieSceneBoolChannel& Channel = SpawnSection->GetChannel();
		Channel.Reset();
		Channel.SetDefault(false);

		auto AddKey = [&Channel, &OutKeyCount](FFrameNumber Time, bool bValue)
		{
			Channel.GetData().UpdateOrAddKey(Time, bValue);
			++OutKeyCount;
		};

		if (PlaybackStart < Intervals[0].Start)
		{
			AddKey(PlaybackStart, false);
		}

		for (const FSpawnInterval& Interval : Intervals)
		{
			AddKey(Interval.Start, true);
			AddKey(Interval.End, false);
		}

		if (Intervals.Last().End < PlaybackEnd)
		{
			AddKey(PlaybackEnd, false);
		}

		SpawnTrack->AddSection(*SpawnSection);
		return true;
	}

	bool KeySpawnTracksFromCameraCutsInternal(
		ISequencer& Sequencer,
		UMovieSceneSequence& Sequence,
		UMovieScene& MovieScene)
	{
		if (!Sequencer.IsAllowedToChange())
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("Key Spawn Tracks From Camera Cuts failed: Sequencer does not allow edits."));
			NotifySpawnTrackResult(
				LOCTEXT("SpawnTrackReadOnly", "Sequencer does not allow edits."),
				SNotificationItem::CS_Fail);
			return false;
		}

		FFrameNumber PlaybackStart;
		FFrameNumber PlaybackEnd;
		FCameraCutGatherStats Stats;
		TMap<FGuid, TArray<FSpawnInterval>> IntervalsByBinding;
		if (!GatherCameraCutIntervals(Sequencer, Sequence, MovieScene, IntervalsByBinding, Stats, PlaybackStart, PlaybackEnd))
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("Key Spawn Tracks From Camera Cuts failed: playback range is invalid."));
			NotifySpawnTrackResult(
				LOCTEXT("SpawnTrackInvalidPlaybackRange", "Focused sequence playback range is invalid."),
				SNotificationItem::CS_Fail);
			return false;
		}

		if (IntervalsByBinding.IsEmpty())
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("Key Spawn Tracks From Camera Cuts found no spawnable CCS CameraCut bindings. Sequence='%s' Cuts=%d NoBinding=%d NonCCS=%d NonSpawnable=%d OutsideFocusedSequence=%d InvalidRange=%d."),
				*Sequence.GetName(),
				Stats.CameraCutCount,
				Stats.SkippedNoBinding,
				Stats.SkippedNonComposableCamera,
				Stats.SkippedNonSpawnable,
				Stats.SkippedOutsideFocusedSequence,
				Stats.SkippedInvalidRange);
			NotifySpawnTrackResult(
				LOCTEXT("SpawnTrackNoCuts", "No spawnable CCS CameraCut bindings found."),
				SNotificationItem::CS_Fail);
			return false;
		}

		FScopedTransaction Transaction(
			LOCTEXT("KeySpawnTracksFromCameraCutsTransaction", "Key Spawn Tracks From Camera Cuts"));
		Sequence.Modify();
		MovieScene.Modify();

		int32 KeyedTrackCount = 0;
		int32 KeyCount = 0;
		for (const TPair<FGuid, TArray<FSpawnInterval>>& Pair : IntervalsByBinding)
		{
			if (WriteSpawnTrackForIntervals(
				MovieScene,
				Pair.Key,
				Pair.Value,
				PlaybackStart,
				PlaybackEnd,
				KeyCount))
			{
				++KeyedTrackCount;
			}
		}

		if (KeyedTrackCount <= 0)
		{
			Transaction.Cancel();
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("Key Spawn Tracks From Camera Cuts failed: no Spawn Tracks were keyed."));
			NotifySpawnTrackResult(
				LOCTEXT("SpawnTrackNoTracksKeyed", "No Spawn Tracks were keyed."),
				SNotificationItem::CS_Fail);
			return false;
		}

		Sequencer.NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		Sequencer.ForceEvaluate();

		UE_LOG(LogComposableCameraSystemEditor, Log,
			TEXT("Keyed %d CCS Spawn Track(s) from %d CameraCut section(s), %d key(s). Skipped: NoBinding=%d NonCCS=%d NonSpawnable=%d OutsideFocusedSequence=%d InvalidRange=%d."),
			KeyedTrackCount,
			Stats.CameraCutCount,
			KeyCount,
			Stats.SkippedNoBinding,
			Stats.SkippedNonComposableCamera,
			Stats.SkippedNonSpawnable,
			Stats.SkippedOutsideFocusedSequence,
			Stats.SkippedInvalidRange);
		NotifySpawnTrackResult(
			FText::Format(
				LOCTEXT("SpawnTrackKeySucceeded", "Keyed {0} CCS Spawn Track(s)."),
				FText::AsNumber(KeyedTrackCount)),
			SNotificationItem::CS_Success);
		return true;
	}
}

void FComposableCameraLevelSequenceSpawnTrackTool::Register()
{
	FComposableCameraLevelSequenceSpawnTrackCommands::Register();

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetGlobalLevelEditorActions()->MapAction(
		FComposableCameraLevelSequenceSpawnTrackCommands::Get().KeySpawnTracksFromCameraCuts,
		FExecuteAction::CreateStatic(&ExecuteKeySpawnTracksFromCameraCuts),
		FCanExecuteAction::CreateStatic(&FComposableCameraLevelSequenceSpawnTrackTool::CanKeySpawnTracksFromCameraCuts));

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		StartupCallbackHandle = UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateStatic(&FComposableCameraLevelSequenceSpawnTrackTool::RegisterMenus));
	}
}

void FComposableCameraLevelSequenceSpawnTrackTool::Unregister()
{
	if (StartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(StartupCallbackHandle);
		StartupCallbackHandle.Reset();
	}
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->UnregisterOwnerByName(SpawnTrackToolMenuOwner);
	}

	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetGlobalLevelEditorActions()->UnmapAction(
			FComposableCameraLevelSequenceSpawnTrackCommands::Get().KeySpawnTracksFromCameraCuts);
	}

	FComposableCameraLevelSequenceSpawnTrackCommands::Unregister();
}

bool FComposableCameraLevelSequenceSpawnTrackTool::CanKeySpawnTracksFromCameraCuts()
{
	const FResolvedSequencer Resolved = ResolveFocusedSequencerWithMovieScene();
	return Resolved.Sequencer.IsValid() && Resolved.MovieScene.IsValid();
}

bool FComposableCameraLevelSequenceSpawnTrackTool::KeySpawnTracksFromCameraCuts()
{
	const FResolvedSequencer Resolved = ResolveFocusedSequencerWithMovieScene();
	TSharedPtr<ISequencer> Sequencer = Resolved.Sequencer;
	UMovieSceneSequence* Sequence = Resolved.Sequence.Get();
	UMovieScene* MovieScene = Resolved.MovieScene.Get();
	if (!Sequencer.IsValid() || !Sequence || !MovieScene)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Key Spawn Tracks From Camera Cuts failed: no open focused Sequencer."));
		NotifySpawnTrackResult(
			LOCTEXT("SpawnTrackNoSequencer", "Open a Level Sequence in Sequencer first."),
			SNotificationItem::CS_Fail);
		return false;
	}

	return KeySpawnTracksFromCameraCutsInternal(*Sequencer, *Sequence, *MovieScene);
}

void FComposableCameraLevelSequenceSpawnTrackTool::RegisterMenus()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FToolMenuOwnerScoped OwnerScoped(SpawnTrackToolMenuOwner);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection("ComposableCameraSystem");
	Section.Label = LOCTEXT("ComposableCameraSystemSection", "Composable Camera System");
	Section.AddEntry(FToolMenuEntry::InitMenuEntryWithCommandList(
		FComposableCameraLevelSequenceSpawnTrackCommands::Get().KeySpawnTracksFromCameraCuts,
		LevelEditorModule.GetGlobalLevelEditorActions(),
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon()));
}

#undef LOCTEXT_NAMESPACE
