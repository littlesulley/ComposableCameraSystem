// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "Widgets/Input/SButton.h"

class FComposableCameraShotAuthoringSession;
class FComposableCameraInstanceDragDropOp;

class SShotEditorSequenceDragButton : public SButton
{
public:
	SLATE_BEGIN_ARGS(SShotEditorSequenceDragButton) : _Duration(5.) {}
		SLATE_ARGUMENT(TSharedPtr<FComposableCameraShotAuthoringSession>, Session)
		SLATE_ATTRIBUTE(double, Duration)
		SLATE_EVENT(FOnTextChanged, OnError)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event) override;
	TSharedPtr<FComposableCameraInstanceDragDropOp> MakeOperation();
private:
	TWeakPtr<FComposableCameraShotAuthoringSession> WeakSession;
	TAttribute<double> Duration;
	FOnTextChanged OnError;
	FReply AddAtPlayhead();
};
