// Copyright Sulley. All Rights Reserved.

#include "Editors/ComposableCameraShotEditor.h"

#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editor.h"
#include "EditorHooks/EditorHooks.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SShotEditorRoot.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotEditor"

const FName FComposableCameraShotEditor::TabId(TEXT("ComposableCameraShotEditor"));

namespace
{
	/**
	 * Module-lifetime weak ref to the single live root widget. Set by
	 * `SpawnTab`, consumed by `OpenForShot` to swap context on subsequent
	 * invocations. Goes stale automatically when the tab is closed (Slate
	 * teardown drops the SharedRef chain).
	 */
	TWeakPtr<SShotEditorRoot> GLiveRootWidget;
}

TSharedRef<SDockTab> FComposableCameraShotEditor::SpawnTab(const FSpawnTabArgs& /*Args*/)
{
	TSharedRef<SShotEditorRoot> Root = SNew(SShotEditorRoot);
	GLiveRootWidget = Root;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("ShotEditorTabLabel", "Shot Editor"))
		[
			Root
		];
}

void FComposableCameraShotEditor::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateStatic(&FComposableCameraShotEditor::SpawnTab))
		.SetDisplayName(LOCTEXT("ShotEditorMenuName", "Shot Editor"))
		.SetTooltipText(LOCTEXT("ShotEditorMenuTooltip",
			"Authoring tool for ComposableCameraSystem Shots — drag actors to compose framing, "
			"set anchor + distance + lens. Open via the 'Open Shot Editor' button on a "
			"CompositionFramingNode in the Camera Type Asset Editor."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);   // Hidden from "Window" menu —
		// the editor opens contextually from a node button. Phase D.x may flip
		// this to ETabSpawnerMenuType::Enabled if a global menu entry is wanted.

	// Bind the runtime → editor delegate hook so node CallInEditor buttons
	// route into OpenForShot.
	FOpenShotEditor::OpenShotEditorDelegate.BindStatic(&FComposableCameraShotEditor::OpenForShot);
}

void FComposableCameraShotEditor::UnregisterTabSpawner()
{
	FOpenShotEditor::OpenShotEditorDelegate.Unbind();

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
	}
	GLiveRootWidget.Reset();
}

UObject* FComposableCameraShotEditor::GetCurrentLiveHost()
{
	if (const TSharedPtr<SShotEditorRoot> Live = GLiveRootWidget.Pin())
	{
		return Live->GetActiveHost();
	}
	return nullptr;
}

void FComposableCameraShotEditor::OpenForShot(FComposableCameraShot* Shot, UObject* HostObject)
{
	// Spawn or focus the tab. TryInvokeTab respects the existing instance
	// when present, so single-instance semantics are preserved.
	const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TabId));
	if (!Tab.IsValid())
	{
		// Nomad tab spawn can fail if the tab manager isn't in a state to
		// accept docking (extremely rare in practice — startup race).
		// Silently no-op; the next click will retry.
		return;
	}

	// SpawnTab populates GLiveRootWidget; consume it to swap context. If
	// the tab was already open before this call, the widget is the same
	// one (no respawn) so SetActiveShot just rewires its data.
	if (const TSharedPtr<SShotEditorRoot> Live = GLiveRootWidget.Pin())
	{
		Live->SetActiveShot(Shot, HostObject);
	}
}

void FComposableCameraShotEditor::OpenForShotSection(UMovieSceneComposableCameraShotSection* Section)
{
	if (!Section)
	{
		// Defensive — null section opens the placeholder "no shot loaded"
		// rather than no-op'ing silently. Designer sees the editor "respond"
		// to their click even if the underlying state is empty.
		OpenForShot(nullptr, nullptr);
		return;
	}

	// Swap the editor's active Shot FIRST, then open LS.
	//
	// The swap is fast (synchronous SetActiveShot → OnActiveShotChanged →
	// RefreshShotListItems all in one stack frame). The LS auto-open is
	// the slow path — `OpenEditorForAsset` runs Sequencer init, which can
	// pump nested Slate ticks during layout / spawnable resolution. If
	// any of those nested ticks land on our Shot Editor's tick boundary
	// (every 0.5s, RefreshShotListItems runs) BEFORE the swap completes,
	// the refresh sees `ActiveHost` still pointing at the OLD section
	// while the user-click selection is on the NEW section — triggering
	// the conditional "selection ≠ current → snap to current" path,
	// which visibly flickers the selection back to the old row before
	// the swap finally commits and a final refresh restores it. Doing
	// the swap first means the LS-open's nested ticks see the already-
	// committed current section and the refresh is a no-op.
	FComposableCameraShot* Shot = Section->ResolveShotEditorShot();
	UObject* Host = Section->ResolveShotEditorHost();
	OpenForShot(Shot, Host);

	// Now auto-open LS. Without a live `FSequencer` instance, the editor
	// module's `OnSequencerCreated` cache is empty, which means
	// `FShotTargetActorOverride` bindings can't resolve to actors and the
	// preview Viewport renders empty (no proxy actors spawn — the
	// effective shot's `Targets[i].Actor` stays None). `bFocusIfOpen=false`
	// avoids stealing focus when the LS is already open in some hidden
	// tab. When it ISN'T open, `OpenEditorForAsset` creates + focuses it.
	if (UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>())
	{
		if (ULevelSequence* LS = Cast<ULevelSequence>(MovieScene->GetOuter()))
		{
			if (UAssetEditorSubsystem* AssetEditor = GEditor
					? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
					: nullptr)
			{
				if (!AssetEditor->FindEditorForAsset(LS, /*bFocusIfOpen=*/false))
				{
					AssetEditor->OpenEditorForAsset(LS);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
