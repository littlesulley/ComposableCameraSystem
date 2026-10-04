// Copyright 2026 Sulley. All Rights Reserved.

#include "Utilities/ComposableCameraEditorToolsMenu.h"

#include "ComposableCameraEditorStyle.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "Editors/ComposableCameraSystemEditWindow.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "ComposableCameraEditorToolsMenu"

const FName FComposableCameraEditorToolsMenu::MenuName(TEXT("LevelEditor.MainMenu.Tools.ComposableCameraSystem"));

namespace
{
	const FName ToolsMenuOwner(TEXT("ComposableCameraEditorToolsMenu"));
	FDelegateHandle ToolsMenuStartupCallbackHandle;

	void OpenEditorTab(FName TabId)
	{
		// Focusing Shot Editor must not replace its current authoring context.
		FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TabId));
	}
}

void FComposableCameraEditorToolsMenu::Register()
{
	if (UToolMenus::IsToolMenuUIEnabled() && !ToolsMenuStartupCallbackHandle.IsValid())
	{
		ToolsMenuStartupCallbackHandle = UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateStatic(&FComposableCameraEditorToolsMenu::RegisterMenus));
	}
}

void FComposableCameraEditorToolsMenu::Unregister()
{
	if (ToolsMenuStartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(ToolsMenuStartupCallbackHandle);
		ToolsMenuStartupCallbackHandle.Reset();
	}
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->UnregisterOwnerByName(ToolsMenuOwner);
	}
}

void FComposableCameraEditorToolsMenu::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(ToolsMenuOwner);
	UToolMenu* Tools = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!Tools) return;
	FToolMenuSection& RootSection = Tools->FindOrAddSection(TEXT("ComposableCameraSystem"));
	RootSection.Label = FText::GetEmpty();
	RootSection.AddSubMenu(TEXT("ComposableCameraSystem"),
		LOCTEXT("Submenu", "Composable Camera System"),
		LOCTEXT("SubmenuTooltip", "Camera editors, mesh layers, viewport tools and Sequencer utilities."),
		FNewToolMenuChoice(), false,
		FSlateIcon(FComposableCameraEditorStyle::Get()->GetStyleSetName(),
			"ComposableCamera.Tools", "ComposableCamera.Tools.Small"));

	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName);
	FToolMenuSection& Windows = Menu->FindOrAddSection(TEXT("Windows"), LOCTEXT("Windows", "Editors"));
	Windows.AddMenuEntry(TEXT("ComposableCameraSystemEditWindow"),
		LOCTEXT("EditWindow", "Composable Camera System Edit Window"),
		LOCTEXT("EditWindowTooltip", "Open the camera welcome page, learning resources and debugging tools."),
		FSlateIcon(FComposableCameraEditorStyle::Get()->GetStyleSetName(),
			"ComposableCamera.EditWindow", "ComposableCamera.EditWindow.Small"),
		FUIAction(FExecuteAction::CreateStatic(&OpenEditorTab, FComposableCameraSystemEditWindow::TabId)));
	Windows.AddMenuEntry(TEXT("ComposableCameraShotEditor"),
		LOCTEXT("ShotEditor", "Shot Editor"),
		LOCTEXT("ShotEditorTooltip", "Open or focus Shot Editor, preserving its current Shot."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent"),
		FUIAction(FExecuteAction::CreateStatic(&OpenEditorTab, FComposableCameraShotEditor::TabId)));

	// Establish the display order before tool-specific startup callbacks contribute entries.
	Menu->FindOrAddSection(TEXT("MeshLayers"), LOCTEXT("MeshLayers", "Mesh Camera Layers"));
	Menu->FindOrAddSection(TEXT("Viewport"), LOCTEXT("Viewport", "Viewport Camera"));
	Menu->FindOrAddSection(TEXT("Sequencer"), LOCTEXT("Sequencer", "Sequencer"));
}

#undef LOCTEXT_NAMESPACE
