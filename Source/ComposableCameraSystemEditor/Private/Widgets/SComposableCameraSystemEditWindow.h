// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/ComposableCameraConsoleControls.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UWorld;
class AComposableCameraCameraBase;
class SComposableCameraLiveEditPanel;

/** Welcome/resources and opt-in live debugging; console values are never mirrored. */
class SComposableCameraSystemEditWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraSystemEditWindow) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	void OpenLiveEditing(AComposableCameraCameraBase* Camera);

private:
	TSharedRef<SWidget> BuildWelcomePage();
	TSharedRef<SWidget> BuildDebuggingPage();
	TSharedRef<SWidget> BuildPageButton(const FText& Label, int32 PageIndex);
	TSharedRef<SWidget> BuildResourceLink(const FText& Label, const FText& Description, const FString& Url);
	void SwitchPage(int32 PageIndex);
	void RefreshControls();
	void RebuildGroups();
	TSharedRef<SWidget> BuildControl(const FComposableCameraConsoleControl& Control);
	TSharedRef<SWidget> BuildWorldMenu();
	UWorld* ResolveWorld() const;
	FText GetWorldLabel() const;
	bool ShouldShowWorldSelector() const;
	void SelectWorld(TWeakObjectPtr<UWorld> World, bool bAutomatic);
	void SetValue(const FString& Name, const FString& Value);
	FReply ExecuteCommand(const FComposableCameraConsoleControl& Control);

	TArray<FComposableCameraConsoleControl> Controls;
	TMap<FString, FString> CommandArguments;
	TSet<EComposableCameraConsoleControlGroup> CollapsedGroups;
	TSharedPtr<SVerticalBox> GroupsBox;
	TSharedPtr<SComposableCameraLiveEditPanel> LiveEditingPanel;
	TWeakObjectPtr<UWorld> SelectedWorld;
	bool bAutoWorld = true;
	int32 ActivePageIndex = 0;
	FString SearchText;
	FText Status;
};
