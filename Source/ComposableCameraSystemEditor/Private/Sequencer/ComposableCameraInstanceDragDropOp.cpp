// Copyright 2026 Sulley. All Rights Reserved.
#include "Sequencer/ComposableCameraInstanceDragDropOp.h"
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ISequencer.h"
#include "ITimeSlider.h"
#include "AnimatedRange.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "Styling/AppStyle.h"

namespace { TWeakPtr<FComposableCameraInstanceDragDropOp> GCameraInstanceDrag; }

TSharedPtr<FComposableCameraInstanceDragDropOp> FComposableCameraInstanceDragDropOp::New(
	TSharedRef<FComposableCameraShotAuthoringSession> Session, double Duration, FString& Error)
{
	Error.Reset();
	const auto* Values = Session->GetInstanceValues();
	if (!Values || !Session->GetSequence() || !Session->CanEdit() || Session->IsEditing())
	{ Error = TEXT("Create a Shot draft for an open Level Sequence, or open an editable Camera Instance."); return nullptr; }
	if (!FMath::IsFinite(Duration) || Duration <= 0. || Duration > 3600.)
	{ Error = TEXT("Choose a duration between zero and one hour."); return nullptr; }
	const auto Op = MakeShared<FComposableCameraInstanceDragDropOp>();
	Op->Snapshot = NewObject<UComposableCameraInstanceData>(GetTransientPackage(), NAME_None, RF_Transient);
	Op->Snapshot->CopyForAuthoring(*Values);
	Op->SourceSequence = Session->GetSequence();
	Op->SourceHost = Session->GetHost();
	Op->SourceSession = Session;
	Op->DurationSeconds = Duration;
	Op->Label = Session->IsSequenceDraft() ? Session->GetDraftName()
		: Values->SourcePreset ? FText::FromString(Values->SourcePreset->GetName()) : NSLOCTEXT("CameraInstance", "DraftLabel", "Camera Shot");
	// FAssetData can describe a loaded transient UObject. This neither registers nor saves an asset.
	Op->Init({FAssetData(Op->Snapshot.Get())}, {}, TScriptInterface<IAssetFactoryInterface>());
	Op->DefaultHoverText = NSLOCTEXT("CameraInstance", "DragHint", "Create Camera Instance in this Level Sequence");
	Op->CurrentHoverText = Op->DefaultHoverText;
	Op->Construct();
	GCameraInstanceDrag = Op;
	return Op;
}

TSharedPtr<FComposableCameraInstanceDragDropOp> FComposableCameraInstanceDragDropOp::FindForSnapshot(const UObject* Object)
{
	// Slate clears its active drag pointer before dispatching OnDrop. The event
	// still owns the operation while native HandleAssetAdded receives its payload.
	const auto Op = GCameraInstanceDrag.Pin();
	return Op && Op->GetSnapshot() == Object ? Op : nullptr;
}

bool FComposableCameraInstanceDragDropOp::CanDrop(const ISequencer& Sequencer, FString& Error) const
{
	Error.Reset();
	if (bDropped || !Snapshot || !SourceSequence || Sequencer.GetFocusedMovieSceneSequence() != SourceSequence)
	{ Error = TEXT("Drop into the draft's original focused Level Sequence. Subject bindings belong to that sequence."); return false; }
	const auto* Scene = SourceSequence->GetMovieScene();
	if (!Scene || Scene->IsReadOnly() || Sequencer.IsReadOnly() || !SourceSequence->AllowsSpawnableObjects() || !Sequencer.GetPlaybackContext())
	{ Error = TEXT("The destination sequence is closed, read-only or cannot create a camera."); return false; }
	return true;
}

void FComposableCameraInstanceDragDropOp::UpdateFeedback(const ISequencer& Sequencer,
	FFrameNumber Frame, TRange<FFrameNumber>& PreviewRange)
{
	// A handled drag must always publish finite bounds for STrackAreaView::OnPaint.
	PreviewRange = TRange<FFrameNumber>(Frame, Frame);
	FString Error;
	const bool bAllowed = CanDrop(Sequencer, Error) && ComposableCameraSystem::CameraInstanceAuthoring::MakeCreationRange(
		Sequencer.GetFocusedTickResolution(), Frame, DurationSeconds, PreviewRange, Error);
	SetToolTip(bAllowed ? DefaultHoverText : FText::FromString(Error),
		FAppStyle::GetBrush(bAllowed ? "Graph.ConnectorFeedback.OK" : "Graph.ConnectorFeedback.Error"));
}

FFrameNumber FComposableCameraInstanceDragDropOp::DropFrame(const ISequencer& Sequencer) const
{
	const auto Slider = Sequencer.GetTopTimeSliderWidget();
	if (!Slider || FSlateApplication::Get().GetModifierKeys().IsShiftDown()) return Sequencer.GetLocalTime().Time.FloorToFrame();
	const FGeometry& Geometry = Slider->GetCachedGeometry();
	const double X = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()).X;
	const double Width = Geometry.GetLocalSize().X;
	if (Width <= 0. || X < 0. || X > Width) return Sequencer.GetLocalTime().Time.FloorToFrame();
	const auto Range = Sequencer.GetViewRange();
	const double Seconds = FMath::Lerp(Range.GetLowerBoundValue(), Range.GetUpperBoundValue(), X / Width);
	return FFrameRate::Snap(Sequencer.GetFocusedTickResolution().AsFrameTime(Seconds),
		Sequencer.GetFocusedTickResolution(), Sequencer.GetFocusedDisplayRate()).RoundToFrame();
}

UMovieSceneComposableCameraInstanceTrack* FComposableCameraInstanceDragDropOp::Drop(TSharedRef<ISequencer> Sequencer, FFrameNumber Frame)
{
	FString Error;
	UMovieSceneComposableCameraInstanceTrack* Track = nullptr;
	if (CanDrop(*Sequencer, Error)) Track = ComposableCameraSystem::CameraInstanceAuthoring::Create(
		Sequencer, Snapshot, Frame, Error, DurationSeconds, Label);
	if (Track)
	{
		bDropped = true;
		Sequencer->SetLocalTimeDirectly(Frame);
		Sequencer->ForceEvaluate();
		if (const auto Session = SourceSession.Pin(); Session && Session->GetHost() == SourceHost.Get())
			Session->Bind(Track->Instance->bComposition ? &Track->Instance->Shot : nullptr, Track->Instance);
	}
	if (!Error.IsEmpty())
	{
		FNotificationInfo Info(FText::FromString(Error)); Info.ExpireDuration = 6.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	return Track;
}

TSharedPtr<SWidget> FComposableCameraInstanceDragDropOp::GetDefaultDecorator() const
{
	return FDecoratedDragDropOp::GetDefaultDecorator();
}

void FComposableCameraInstanceDragDropOp::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Snapshot);
	Collector.AddReferencedObject(SourceSequence);
}
