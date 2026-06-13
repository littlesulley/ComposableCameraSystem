// Copyright 2026 Sulley. All Rights Reserved.

#include "Widgets/SComposableCameraRuntimePreviewer.h"

#include "Editors/ComposableCameraRuntimePreviewerViewportClient.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SComposableCameraRuntimePreviewer"

void SComposableCameraRuntimePreviewer::Construct(const FArguments& /*InArgs*/)
{
	PreviewScene = MakeUnique<FAdvancedPreviewScene>(
		FPreviewScene::ConstructionValues()
			.SetCreatePhysicsScene(false)
			.SetTransactional(false)
			.AllowAudioPlayback(false));

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SComposableCameraRuntimePreviewer::~SComposableCameraRuntimePreviewer()
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->Viewport = nullptr;
		ViewportClient->ReleaseSceneResources();
	}
}

void SComposableCameraRuntimePreviewer::SetPreviewData(const FComposableCameraRuntimePreviewData& InData)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetPreviewData(InData);
		Invalidate();
	}
}

void SComposableCameraRuntimePreviewer::ClearPreviewData(ERuntimePreviewerStatus Status)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->ClearPreviewData(Status);
		Invalidate();
	}
}

void SComposableCameraRuntimePreviewer::FramePreviewSubject()
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->FramePreviewSubject();
		Invalidate();
	}
}

TSharedRef<FEditorViewportClient> SComposableCameraRuntimePreviewer::MakeEditorViewportClient()
{
	check(PreviewScene.IsValid());
	ViewportClient = MakeShared<FComposableCameraRuntimePreviewerViewportClient>(PreviewScene.Get(), SharedThis(this));
	return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SComposableCameraRuntimePreviewer::MakeViewportToolbar()
{
	TWeakPtr<FComposableCameraRuntimePreviewerViewportClient> WeakClient = ViewportClient;

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(FMargin(6.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.ToolTipText(LOCTEXT("FramePreviewSubjectTooltip", "Frame preview subject."))
				.OnClicked_Lambda([WeakClient]()
				{
					if (TSharedPtr<FComposableCameraRuntimePreviewerViewportClient> Client = WeakClient.Pin())
					{
						Client->FramePreviewSubject();
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Search"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
