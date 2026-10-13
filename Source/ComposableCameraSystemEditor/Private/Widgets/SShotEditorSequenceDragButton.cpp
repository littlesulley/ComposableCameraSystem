// Copyright 2026 Sulley. All Rights Reserved.
#include "Widgets/SShotEditorSequenceDragButton.h"
#include "Widgets/ComposableCameraShotEditorStyle.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Sequencer/ComposableCameraInstanceDragDropOp.h"
#include "ISequencer.h"
#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

void SShotEditorSequenceDragButton::Construct(const FArguments& Args)
{
	WeakSession = Args._Session; Duration = Args._Duration; OnError = Args._OnError;
	SButton::Construct(SButton::FArguments()
		.ToolTipText(NSLOCTEXT("ShotEditor", "DragIntoSequenceTip", "Drag this configuration into the timeline to create a new Camera Instance. Click to add at the playhead. Shift-drop also uses the playhead."))
		.ButtonStyle(ComposableCameraSystem::ShotEditorStyle::ActionStyle())
		.ContentPadding(FMargin(6.f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
		.IsEnabled_Lambda([this]() { const auto S = WeakSession.Pin(); return S && S->IsSequenceDraft() && S->CanEdit() && !S->IsEditing(); })
		.OnClicked(this, &SShotEditorSequenceDragButton::AddAtPlayhead)
		[SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
			[SNew(SImage).Image(FAppStyle::GetBrush("Icons.DragHandle"))
				.ColorAndOpacity(FSlateColor::UseForeground()).Visibility(EVisibility::HitTestInvisible)]]);
}

TSharedPtr<FComposableCameraInstanceDragDropOp> SShotEditorSequenceDragButton::MakeOperation()
{
	const auto Session = WeakSession.Pin();
	if (!Session || !Session->IsSequenceDraft()) return nullptr;
	FString Error;
	const auto Op = FComposableCameraInstanceDragDropOp::New(Session.ToSharedRef(), Duration.Get(), Error);
	OnError.ExecuteIfBound(FText::FromString(Error));
	return Op;
}

FReply SShotEditorSequenceDragButton::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	FReply Reply = SButton::OnMouseButtonDown(Geometry, Event);
	if (Reply.IsEventHandled() && Event.GetEffectingButton() == EKeys::LeftMouseButton)
		Reply.DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	return Reply;
}

FReply SShotEditorSequenceDragButton::OnDragDetected(const FGeometry&, const FPointerEvent&)
{
	if (const auto Op = MakeOperation()) return FReply::Handled().ReleaseMouseCapture().BeginDragDrop(Op.ToSharedRef());
	return FReply::Unhandled();
}

FReply SShotEditorSequenceDragButton::AddAtPlayhead()
{
	const auto Session = WeakSession.Pin();
	const auto Sequencer = Session ? Session->GetSequencer() : nullptr;
	if (Sequencer) if (const auto Op = MakeOperation()) Op->Drop(Sequencer.ToSharedRef(), Sequencer->GetLocalTime().Time.FloorToFrame());
	return FReply::Handled();
}
