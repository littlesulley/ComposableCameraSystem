// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraSystemEditWindow.h"

#include "ComposableCameraEditorStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SComposableCameraSystemEditWindow.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "ComposableCameraSystemEditWindow"

namespace
{
	TWeakPtr<SComposableCameraSystemEditWindow> LiveWindow;
}

const FName FComposableCameraSystemEditWindow::TabId(TEXT("ComposableCameraSystemEditWindow"));

void FComposableCameraSystemEditWindow::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
		FOnSpawnTab::CreateStatic(&FComposableCameraSystemEditWindow::SpawnTab))
		.SetDisplayName(LOCTEXT("Title", "Composable Camera System Edit Window"))
		.SetTooltipText(LOCTEXT("Tooltip", "Welcome, learning resources and live CCS debugging tools."))
		.SetIcon(FSlateIcon(FComposableCameraEditorStyle::Get()->GetStyleSetName(),
			"ComposableCamera.EditWindow", "ComposableCamera.EditWindow.Small"))
		.SetMenuType(ETabSpawnerMenuType::Hidden); // Explicit entry in the shared CCS Tools submenu.
}

void FComposableCameraSystemEditWindow::UnregisterTabSpawner()
{
	LiveWindow.Reset();
	if (FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(TabId)))
		{
			// Drop value attributes and callbacks while their module still exists.
			Tab->SetContent(SNullWidget::NullWidget);
			Tab->RequestCloseTab();
		}
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
	}
}

TSharedRef<SDockTab> FComposableCameraSystemEditWindow::SpawnTab(const FSpawnTabArgs& /*Args*/)
{
	TSharedRef<SComposableCameraSystemEditWindow> Content = SNew(SComposableCameraSystemEditWindow);
	LiveWindow = Content;
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("Title", "Composable Camera System Edit Window"))
		[Content];
}

void FComposableCameraSystemEditWindow::OpenLiveEditing(AComposableCameraCameraBase* Camera)
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TabId));
	if (const auto Window = LiveWindow.Pin()) Window->OpenLiveEditing(Camera);
}

#undef LOCTEXT_NAMESPACE
