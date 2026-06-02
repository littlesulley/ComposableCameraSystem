# Runtime Previewer Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Camera Type Asset editor Runtime Previewer dock tab that shows the live PIE controlled pawn fixed at preview origin, with the game camera drawn at pawn-local position and an editor-controlled observer view.

**Architecture:** Reuse the existing Camera Type Asset debug binding and ticker. The toolkit owns the tab spawner and pushes the bound camera plus `FComposableCameraDebugSnapshot` into a new `SEditorViewport` widget. The viewport client owns an `FAdvancedPreviewScene`, spawns editor-only preview proxies, copies the live pawn pose into preview space, and draws camera/frustum/movement overlays.

**Tech Stack:** UE 5.6 editor module, Slate, `SEditorViewport`, `FEditorViewportClient`, `FAdvancedPreviewScene`, existing CCS debug snapshot, editor automation tests.

---

## Source Path

Use this exact root:

```text
C:/Users/Sulley/Documents/Unreal Projects/UE5_6/Plugins/ComposableCameraSystem
```

Do not edit `Binaries`, `Intermediate`, `Saved`, `Cooked`, `Temp`, engine install directories, or host-project copies.

## File Structure

Create:

- `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h`
  - Slate viewport widget. Owns `FAdvancedPreviewScene` and the runtime preview viewport client.
- `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp`
  - Widget construction, teardown, forwarding API, and `MakeEditorViewportClient`.
- `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
  - Editor viewport client declaration, pure helper functions, status enum, live-data struct, and proxy state.
- `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
  - Preview scene sync, pawn proxy lifecycle, camera/frustum drawing, HUD overlay, and observer camera setup.
- `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`
  - Automation coverage for helper math, status text, and widget construction.

Modify:

- `Source/ComposableCameraSystemEditor/Public/Toolkits/ComposableCameraTypeAssetEditorToolkit.h`
  - Add runtime previewer tab ID, tab spawner, widget weak state, and update helper methods.
- `Source/ComposableCameraSystemEditor/Private/Toolkits/ComposableCameraTypeAssetEditorToolkit.cpp`
  - Register/unregister/spawn Runtime Previewer tab and push debug snapshots into it.
- `Docs/superpowers/specs/2026-06-02-runtime-previewer-window-design.md`
  - Record the implementation decision to reuse the Shot Editor's CST proxy pose-copy pattern instead of `UPoseableMeshComponent`.
- `Docs/EditorDesignDoc.md`
  - Document the new tab, Window menu entry, data flow, lifecycle, and observer camera behavior.
- `Docs/TechDoc.md`
  - Document the editor preview viewport technique and lifetime gotchas.

Do not modify `ComposableCameraSystemEditor.Build.cs` unless the IDE compile reports a missing module. Current dependencies already include `AdvancedPreviewScene`, `Engine`, `Slate`, `SlateCore`, `UnrealEd`, `RenderCore`, and `RHI`.

---

### Task 1: Pure Helpers and Automation Tests

**Files:**

- Create: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
- Create: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
- Create: `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`

- [ ] **Step 1: Write failing helper tests**

Create `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraRuntimePreviewerViewportClient.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraRuntimePreviewerTests"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerRelativeTransformTest,
	"ComposableCameraSystem.RuntimePreviewer.RelativeTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerRelativeTransformTest::RunTest(const FString& /*Parameters*/)
{
	const FTransform PawnWorld(
		FRotator(0.0, 90.0, 0.0),
		FVector(1000.0, 2000.0, 50.0),
		FVector::OneVector);

	const FTransform CameraWorld(
		FRotator(0.0, 90.0, 0.0),
		FVector(1000.0, 1680.0, 190.0),
		FVector::OneVector);

	const FTransform Local =
		ComposableCameraSystem::RuntimePreviewer::MakePawnRelativeTransform(CameraWorld, PawnWorld);

	TestTrue(TEXT("Camera local location is finite"), Local.GetLocation().ContainsNaN() == false);
	TestEqual(TEXT("Camera local X follows pawn-forward offset"),
		FMath::RoundToInt(Local.GetLocation().X), -320);
	TestEqual(TEXT("Camera local Y removes pawn world translation"),
		FMath::RoundToInt(Local.GetLocation().Y), 0);
	TestEqual(TEXT("Camera local Z keeps height above pawn"),
		FMath::RoundToInt(Local.GetLocation().Z), 140);
	TestEqual(TEXT("Camera local yaw removes pawn yaw"),
		FMath::RoundToInt(Local.Rotator().Yaw), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerStatusTextTest,
	"ComposableCameraSystem.RuntimePreviewer.StatusText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerStatusTextTest::RunTest(const FString& /*Parameters*/)
{
	const ERuntimePreviewerStatus Statuses[] = {
		ERuntimePreviewerStatus::NotInPIE,
		ERuntimePreviewerStatus::NoCameraBound,
		ERuntimePreviewerStatus::InvalidSnapshot,
		ERuntimePreviewerStatus::NoControlledPawn,
		ERuntimePreviewerStatus::FallbackPawn,
		ERuntimePreviewerStatus::Live,
	};

	TArray<FString> Seen;
	Seen.Reserve(UE_ARRAY_COUNT(Statuses));

	for (ERuntimePreviewerStatus Status : Statuses)
	{
		const FText Text = RuntimePreviewerStatusToText(Status);
		TestFalse(FString::Printf(TEXT("Status %d has visible text"), static_cast<int32>(Status)),
			Text.IsEmpty());

		const FString AsString = Text.ToString();
		TestFalse(FString::Printf(TEXT("Status %d text is unique"), static_cast<int32>(Status)),
			Seen.Contains(AsString));
		Seen.Add(AsString);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#undef LOCTEXT_NAMESPACE
```

- [ ] **Step 2: Verify tests fail before implementation**

Run inside Rider, Visual Studio Test Explorer, or Unreal Editor Automation:

```text
ComposableCameraSystem.RuntimePreviewer.*
```

Expected result before implementation:

```text
Compile fails because Editors/ComposableCameraRuntimePreviewerViewportClient.h does not exist.
```

Do not run Unreal automation or build commands from shell.

- [ ] **Step 3: Add helper declarations**

Create `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraDebugSnapshot.h"
#include "EditorViewportClient.h"
#include "UObject/WeakObjectPtr.h"

class APlayerController;
class AActor;
class APawn;
class FCanvas;
class FPrimitiveDrawInterface;
class FPreviewScene;
class FSceneView;
class UStaticMesh;
class USkeletalMesh;
class USkeletalMeshComponent;

enum class ERuntimePreviewerStatus : uint8
{
	NotInPIE,
	NoCameraBound,
	InvalidSnapshot,
	NoControlledPawn,
	FallbackPawn,
	Live,
};

COMPOSABLECAMERASYSTEMEDITOR_API FText RuntimePreviewerStatusToText(ERuntimePreviewerStatus Status);

namespace ComposableCameraSystem::RuntimePreviewer
{
	inline FTransform MakePawnRelativeTransform(const FTransform& WorldTransform, const FTransform& PawnWorldTransform)
	{
		return WorldTransform.GetRelativeTransform(PawnWorldTransform);
	}

	inline FVector MakePawnRelativeVector(const FVector& WorldVector, const FTransform& PawnWorldTransform)
	{
		return PawnWorldTransform.InverseTransformVectorNoScale(WorldVector);
	}
}

struct FComposableCameraRuntimePreviewData
{
	TWeakObjectPtr<AComposableCameraCameraBase> Camera;
	TWeakObjectPtr<APawn> ControlledPawn;
	FComposableCameraDebugSnapshot Snapshot;
	FVector PawnVelocity = FVector::ZeroVector;
	bool bIsPIEActive = false;
};

class FComposableCameraRuntimePreviewerViewportClient : public FEditorViewportClient
{
public:
	FComposableCameraRuntimePreviewerViewportClient(
		FPreviewScene* InPreviewScene,
		const TSharedRef<class SEditorViewport>& InEditorViewportWidget);

	virtual ~FComposableCameraRuntimePreviewerViewportClient();

	void SetRuntimePreviewData(const FComposableCameraRuntimePreviewData& InData);
	void ClearRuntimeState();
	void ReleaseSceneResources();
	void FramePreviewSubject();

	ERuntimePreviewerStatus GetStatus() const { return Status; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

private:
	void UpdatePreviewScene();
	void DestroyPreviewActors();
	void RebuildPawnProxyIfNeeded(USkeletalMeshComponent* SourceMesh);
	void SyncPawnProxy(const FTransform& PawnWorldTransform, USkeletalMeshComponent* SourceMesh);
	void DrawRuntimeCameraGizmo(FPrimitiveDrawInterface* PDI) const;
	void DrawMovementGizmo(FPrimitiveDrawInterface* PDI) const;
	APawn* ResolveControlledPawn() const;

private:
	FPreviewScene* PreviewScene = nullptr;
	FComposableCameraRuntimePreviewData Data;
	ERuntimePreviewerStatus Status = ERuntimePreviewerStatus::NotInPIE;

	TWeakObjectPtr<AActor> PawnProxyActor;
	TWeakObjectPtr<AActor> FallbackPawnProxyActor;
	TWeakObjectPtr<USkeletalMesh> LastSkeletalMesh;

	FTransform LastCameraLocalTransform = FTransform::Identity;
	FVector LastVelocityLocal = FVector::ZeroVector;
	bool bHasCameraLocalTransform = false;
};
```

- [ ] **Step 4: Add helper implementation**

Create `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp` with only the status helper for this task:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraRuntimePreviewerViewportClient.h"

#define LOCTEXT_NAMESPACE "ComposableCameraRuntimePreviewer"

FText RuntimePreviewerStatusToText(ERuntimePreviewerStatus Status)
{
	switch (Status)
	{
	case ERuntimePreviewerStatus::NotInPIE:
		return LOCTEXT("RuntimePreviewer_NotInPIE", "Start PIE to preview runtime camera relation.");
	case ERuntimePreviewerStatus::NoCameraBound:
		return LOCTEXT("RuntimePreviewer_NoCameraBound", "Select a running camera instance from the Debug picker.");
	case ERuntimePreviewerStatus::InvalidSnapshot:
		return LOCTEXT("RuntimePreviewer_InvalidSnapshot", "Bound camera has no valid debug snapshot yet.");
	case ERuntimePreviewerStatus::NoControlledPawn:
		return LOCTEXT("RuntimePreviewer_NoControlledPawn", "No controlled pawn found for the bound camera.");
	case ERuntimePreviewerStatus::FallbackPawn:
		return LOCTEXT("RuntimePreviewer_FallbackPawn", "Live camera relation shown with fallback pawn marker.");
	case ERuntimePreviewerStatus::Live:
		return LOCTEXT("RuntimePreviewer_Live", "Live runtime preview.");
	}

	return LOCTEXT("RuntimePreviewer_Unknown", "Runtime preview state unknown.");
}

#undef LOCTEXT_NAMESPACE
```

- [ ] **Step 5: Run helper tests**

Run inside Rider, Visual Studio Test Explorer, or Unreal Editor Automation:

```text
ComposableCameraSystem.RuntimePreviewer.RelativeTransform
ComposableCameraSystem.RuntimePreviewer.StatusText
```

Expected result after helper implementation:

```text
RuntimePreviewer.RelativeTransform passes.
RuntimePreviewer.StatusText passes.
```

- [ ] **Step 6: Commit helper test scaffold**

```bash
git add Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp
git commit -m "test: add runtime previewer helper coverage"
```

---

### Task 2: Viewport Widget Shell

**Files:**

- Create: `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h`
- Create: `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp`
- Modify: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
- Modify: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
- Modify: `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`

- [ ] **Step 1: Create Slate widget header**

Create `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"

class AComposableCameraCameraBase;
class FComposableCameraRuntimePreviewerViewportClient;
struct FComposableCameraDebugSnapshot;
struct FComposableCameraRuntimePreviewData;

class COMPOSABLECAMERASYSTEMEDITOR_API SComposableCameraRuntimePreviewer : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraRuntimePreviewer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SComposableCameraRuntimePreviewer() override;

	void SetRuntimePreviewData(const FComposableCameraRuntimePreviewData& InData);
	void ClearRuntimeState();
	void FramePreviewSubject();

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TUniquePtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FComposableCameraRuntimePreviewerViewportClient> ViewportClient;
};
```

- [ ] **Step 2: Create Slate widget implementation**

Create `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp`:

```cpp
// Copyright 2026 Sulley. All Rights Reserved.

#include "Widgets/SComposableCameraRuntimePreviewer.h"

#include "AdvancedPreviewScene.h"
#include "Editors/ComposableCameraRuntimePreviewerViewportClient.h"

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

void SComposableCameraRuntimePreviewer::SetRuntimePreviewData(const FComposableCameraRuntimePreviewData& InData)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetRuntimePreviewData(InData);
	}
}

void SComposableCameraRuntimePreviewer::ClearRuntimeState()
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->ClearRuntimeState();
	}
}

void SComposableCameraRuntimePreviewer::FramePreviewSubject()
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->FramePreviewSubject();
	}
}

TSharedRef<FEditorViewportClient> SComposableCameraRuntimePreviewer::MakeEditorViewportClient()
{
	check(PreviewScene.IsValid());
	ViewportClient = MakeShared<FComposableCameraRuntimePreviewerViewportClient>(PreviewScene.Get(), SharedThis(this));
	return ViewportClient.ToSharedRef();
}
```

- [ ] **Step 3: Add widget construction test**

Append this test to `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp` before `#endif // WITH_DEV_AUTOMATION_TESTS`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerWidgetConstructsTest,
	"ComposableCameraSystem.RuntimePreviewer.WidgetConstructs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerWidgetConstructsTest::RunTest(const FString& /*Parameters*/)
{
	TSharedRef<SComposableCameraRuntimePreviewer> Widget =
		SNew(SComposableCameraRuntimePreviewer);

	TestTrue(TEXT("Runtime Previewer widget default visibility is visible"),
		Widget->GetVisibility().IsVisible());
	Widget->ClearRuntimeState();
	return true;
}
```

Also add this include at the top:

```cpp
#include "Widgets/SComposableCameraRuntimePreviewer.h"
```

- [ ] **Step 4: Implement minimal viewport client**

Add these includes near the top of `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`, below the existing include:

```cpp
#include "CanvasTypes.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PreviewScene.h"
#include "SceneManagement.h"
```

Append these implementations after `RuntimePreviewerStatusToText`:

```cpp
FComposableCameraRuntimePreviewerViewportClient::FComposableCameraRuntimePreviewerViewportClient(
	FPreviewScene* InPreviewScene,
	const TSharedRef<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(nullptr, InPreviewScene, InEditorViewportWidget)
	, PreviewScene(InPreviewScene)
{
	SetViewLocation(FVector(-350.f, -450.f, 250.f));
	SetViewRotation(FRotator(-20.f, 38.f, 0.f));
	SetViewMode(VMI_Lit);
	EngineShowFlags.SetSelectionOutline(false);
	EngineShowFlags.SetGrid(true);
	ViewFOV = 60.f;
	bSetListenerPosition = false;
}

FComposableCameraRuntimePreviewerViewportClient::~FComposableCameraRuntimePreviewerViewportClient()
{
	Data = FComposableCameraRuntimePreviewData();
	PawnProxyActor.Reset();
	FallbackPawnProxyActor.Reset();
	LastSkeletalMesh.Reset();
}

void FComposableCameraRuntimePreviewerViewportClient::SetRuntimePreviewData(const FComposableCameraRuntimePreviewData& InData)
{
	Data = InData;
}

void FComposableCameraRuntimePreviewerViewportClient::ClearRuntimeState()
{
	Data = FComposableCameraRuntimePreviewData();
	Status = ERuntimePreviewerStatus::NotInPIE;
	bHasCameraLocalTransform = false;
	LastCameraLocalTransform = FTransform::Identity;
	LastVelocityLocal = FVector::ZeroVector;
	DestroyPreviewActors();
}

void FComposableCameraRuntimePreviewerViewportClient::ReleaseSceneResources()
{
	ClearRuntimeState();
	PreviewScene = nullptr;
}

void FComposableCameraRuntimePreviewerViewportClient::FramePreviewSubject()
{
	SetViewLocation(FVector(-350.f, -450.f, 250.f));
	SetViewRotation(FRotator(-20.f, 38.f, 0.f));
	Invalidate();
}

void FComposableCameraRuntimePreviewerViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	if (Viewport == nullptr)
	{
		return;
	}
	UpdatePreviewScene();
}

void FComposableCameraRuntimePreviewerViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
	DrawRuntimeCameraGizmo(PDI);
	DrawMovementGizmo(PDI);
}

void FComposableCameraRuntimePreviewerViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	const FText StatusText = RuntimePreviewerStatusToText(Status);
	FCanvasTextItem Item(FVector2D(12.f, 12.f), StatusText, GEngine->GetSmallFont(), FLinearColor::White);
	Item.EnableShadow(FLinearColor::Black);
	Canvas.DrawItem(Item);
}

APawn* FComposableCameraRuntimePreviewerViewportClient::ResolveControlledPawn() const
{
	return Data.ControlledPawn.Get();
}

void FComposableCameraRuntimePreviewerViewportClient::UpdatePreviewScene()
{
	if (!Data.bIsPIEActive)
	{
		Status = ERuntimePreviewerStatus::NotInPIE;
		DestroyPreviewActors();
		return;
	}
	if (!Data.Camera.IsValid())
	{
		Status = ERuntimePreviewerStatus::NoCameraBound;
		DestroyPreviewActors();
		return;
	}
	if (!Data.Snapshot.bIsValid)
	{
		Status = ERuntimePreviewerStatus::InvalidSnapshot;
		return;
	}
	if (!Data.ControlledPawn.IsValid())
	{
		Status = ERuntimePreviewerStatus::NoControlledPawn;
		DestroyPreviewActors();
		return;
	}
}

void FComposableCameraRuntimePreviewerViewportClient::DestroyPreviewActors()
{
	if (AActor* Actor = PawnProxyActor.Get())
	{
		Actor->Destroy();
	}
	if (AActor* Actor = FallbackPawnProxyActor.Get())
	{
		Actor->Destroy();
	}
	PawnProxyActor.Reset();
	FallbackPawnProxyActor.Reset();
	LastSkeletalMesh.Reset();
}

void FComposableCameraRuntimePreviewerViewportClient::RebuildPawnProxyIfNeeded(USkeletalMeshComponent* /*SourceMesh*/)
{
}

void FComposableCameraRuntimePreviewerViewportClient::SyncPawnProxy(
	const FTransform& /*PawnWorldTransform*/,
	USkeletalMeshComponent* /*SourceMesh*/)
{
}

void FComposableCameraRuntimePreviewerViewportClient::DrawRuntimeCameraGizmo(FPrimitiveDrawInterface* /*PDI*/) const
{
}

void FComposableCameraRuntimePreviewerViewportClient::DrawMovementGizmo(FPrimitiveDrawInterface* /*PDI*/) const
{
}
```

- [ ] **Step 5: Run helper tests**

Run inside IDE or Unreal Editor Automation:

```text
ComposableCameraSystem.RuntimePreviewer.*
```

Expected result after this task:

```text
RuntimePreviewer.RelativeTransform passes.
RuntimePreviewer.StatusText passes.
RuntimePreviewer.WidgetConstructs passes.
```

If compile reports missing `SEditorViewport`, `AdvancedPreviewScene`, or `FEditorViewportClient`, confirm the includes match `SShotEditorViewport` and do not add new module dependencies until the include path is correct.

- [ ] **Step 6: Commit viewport shell**

```bash
git add Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp
git commit -m "feat: add runtime previewer viewport shell"
```

---

### Task 3: Runtime Data Bridge From Toolkit

**Files:**

- Modify: `Source/ComposableCameraSystemEditor/Public/Toolkits/ComposableCameraTypeAssetEditorToolkit.h`
- Modify: `Source/ComposableCameraSystemEditor/Private/Toolkits/ComposableCameraTypeAssetEditorToolkit.cpp`

- [ ] **Step 1: Add toolkit members and tab API**

In `Source/ComposableCameraSystemEditor/Public/Toolkits/ComposableCameraTypeAssetEditorToolkit.h`, add forward declarations near existing class declarations:

```cpp
class APawn;
class SComposableCameraRuntimePreviewer;
struct FComposableCameraDebugSnapshot;
```

Add private methods beside existing tab spawners:

```cpp
TSharedRef<SDockTab> SpawnTab_RuntimePreviewer(const FSpawnTabArgs& Args);
```

Add runtime preview helper methods under Runtime Debug Monitoring:

```cpp
void PushRuntimePreviewData(const FComposableCameraDebugSnapshot& Snapshot);
void ClearRuntimePreviewer();
APawn* ResolveDebuggedControlledPawn() const;
```

Add widget member beside `GraphEditorWidget`:

```cpp
TSharedPtr<SComposableCameraRuntimePreviewer> RuntimePreviewerWidget;
```

Add tab ID:

```cpp
static const FName RuntimePreviewerTabId;
```

- [ ] **Step 2: Register runtime previewer tab**

In `Source/ComposableCameraSystemEditor/Private/Toolkits/ComposableCameraTypeAssetEditorToolkit.cpp`, add includes:

```cpp
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Editors/ComposableCameraRuntimePreviewerViewportClient.h"
#include "Widgets/SComposableCameraRuntimePreviewer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
```

Add tab ID beside the other tab IDs:

```cpp
const FName FComposableCameraTypeAssetEditorToolkit::RuntimePreviewerTabId(TEXT("ComposableCameraTypeAsset_RuntimePreviewer"));
```

Register the tab in `RegisterTabSpawners` after Build Messages:

```cpp
InTabManager->RegisterTabSpawner(RuntimePreviewerTabId,
	FOnSpawnTab::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::SpawnTab_RuntimePreviewer))
	.SetDisplayName(LOCTEXT("RuntimePreviewerTab", "Runtime Previewer"))
	.SetGroup(LocalWorkspaceMenuCategory)
	.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent"));
```

Unregister it in `UnregisterTabSpawners`:

```cpp
InTabManager->UnregisterTabSpawner(RuntimePreviewerTabId);
```

Do not add `RuntimePreviewerTabId` to `StandaloneDefaultLayout`. The Window menu entry comes from the registered spawner.

- [ ] **Step 3: Add tab content**

Add below `SpawnTab_BuildMessages`:

```cpp
TSharedRef<SDockTab> FComposableCameraTypeAssetEditorToolkit::SpawnTab_RuntimePreviewer(const FSpawnTabArgs& Args)
{
	RuntimePreviewerWidget = SNew(SComposableCameraRuntimePreviewer);

	return SNew(SDockTab)
		.Label(LOCTEXT("RuntimePreviewerTabLabel", "Runtime Previewer"))
		.OnTabClosed_Lambda([this](TSharedRef<SDockTab>)
		{
			RuntimePreviewerWidget.Reset();
		})
		[RuntimePreviewerWidget.ToSharedRef()];
}
```

- [ ] **Step 4: Push debug data into previewer**

Add helper implementations near existing runtime debug methods:

```cpp
APawn* FComposableCameraTypeAssetEditorToolkit::ResolveDebuggedControlledPawn() const
{
	if (!DebuggedCamera.IsValid())
	{
		return nullptr;
	}

	if (AComposableCameraPlayerCameraManager* PCM = DebuggedCamera->GetOwningPlayerCameraManager())
	{
		if (APlayerController* PC = PCM->GetOwningPlayerController())
		{
			return PC->GetPawn();
		}
	}

	return nullptr;
}

void FComposableCameraTypeAssetEditorToolkit::PushRuntimePreviewData(const FComposableCameraDebugSnapshot& Snapshot)
{
	if (!RuntimePreviewerWidget.IsValid())
	{
		return;
	}

	APawn* Pawn = ResolveDebuggedControlledPawn();

	FComposableCameraRuntimePreviewData Data;
	Data.Camera = DebuggedCamera;
	Data.ControlledPawn = Pawn;
	Data.Snapshot = Snapshot;
	Data.PawnVelocity = Pawn ? Pawn->GetVelocity() : FVector::ZeroVector;
	Data.bIsPIEActive = GEditor && GEditor->PlayWorld.Get() != nullptr;

	RuntimePreviewerWidget->SetRuntimePreviewData(Data);
}

void FComposableCameraTypeAssetEditorToolkit::ClearRuntimePreviewer()
{
	if (RuntimePreviewerWidget.IsValid())
	{
		RuntimePreviewerWidget->ClearRuntimeState();
	}
}
```

In `DebugTick`, after snapshot validity passes and after graph debug state is updated, call:

```cpp
PushRuntimePreviewData(Snapshot);
```

In every path that clears graph state because PIE is gone, camera is invalid, or snapshot is invalid, also call:

```cpp
ClearRuntimePreviewer();
```

In `OnPIEEnded`, after `ClearGraphNodeDebugState();`, call:

```cpp
ClearRuntimePreviewer();
```

In `BuildDebugInstancePickerWidget` "None" action, after `ClearGraphNodeDebugState();`, call:

```cpp
ClearRuntimePreviewer();
```

- [ ] **Step 5: Run construction tests**

Run inside IDE or Unreal Editor Automation:

```text
ComposableCameraSystem.RuntimePreviewer.*
```

Expected result:

```text
All RuntimePreviewer tests pass.
Opening a Camera Type Asset shows Window -> Runtime Previewer.
The tab opens and shows an empty-state HUD when PIE is not running.
```

- [ ] **Step 6: Commit toolkit bridge**

```bash
git add Source/ComposableCameraSystemEditor/Public/Toolkits/ComposableCameraTypeAssetEditorToolkit.h Source/ComposableCameraSystemEditor/Private/Toolkits/ComposableCameraTypeAssetEditorToolkit.cpp Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp
git commit -m "feat: wire runtime previewer tab into camera type editor"
```

---

### Task 4: Pawn Proxy and Pose Copy

**Files:**

- Modify: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
- Modify: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`

- [ ] **Step 1: Add mesh and actor includes**

In `ComposableCameraRuntimePreviewerViewportClient.cpp`, add:

```cpp
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Pawn.h"
```

Add constants near the top:

```cpp
namespace
{
	const FVector RuntimePreviewerFallbackPawnScale(0.7f, 0.7f, 1.8f);
	const TCHAR* const RuntimePreviewerFallbackMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
}
```

- [ ] **Step 2: Implement proxy rebuild**

Replace the empty `RebuildPawnProxyIfNeeded`:

```cpp
void FComposableCameraRuntimePreviewerViewportClient::RebuildPawnProxyIfNeeded(USkeletalMeshComponent* SourceMesh)
{
	if (!PreviewScene)
	{
		return;
	}

	USkeletalMesh* SourceAsset = SourceMesh ? SourceMesh->GetSkeletalMeshAsset() : nullptr;
	if (SourceAsset == LastSkeletalMesh.Get() && (PawnProxyActor.IsValid() || FallbackPawnProxyActor.IsValid()))
	{
		return;
	}

	DestroyPreviewActors();
	LastSkeletalMesh = SourceAsset;

	UWorld* PreviewWorld = PreviewScene->GetWorld();
	if (!PreviewWorld)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bNoFail = true;

	if (SourceAsset)
	{
		ASkeletalMeshActor* Proxy = PreviewWorld->SpawnActor<ASkeletalMeshActor>(Params);
		if (Proxy && Proxy->GetSkeletalMeshComponent())
		{
			USkeletalMeshComponent* ProxyMesh = Proxy->GetSkeletalMeshComponent();
			ProxyMesh->SetSkeletalMeshAsset(SourceAsset);
			ProxyMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			ProxyMesh->SetComponentTickEnabled(false);
			Proxy->SetActorTickEnabled(false);
			ProxyMesh->UpdateBounds();
			ProxyMesh->MarkRenderTransformDirty();
			ProxyMesh->MarkRenderStateDirty();
			PawnProxyActor = Proxy;
			Status = ERuntimePreviewerStatus::Live;
			return;
		}
	}

	UStaticMesh* FallbackMesh = LoadObject<UStaticMesh>(nullptr, RuntimePreviewerFallbackMeshPath);
	AStaticMeshActor* Fallback = PreviewWorld->SpawnActor<AStaticMeshActor>(Params);
	if (Fallback && Fallback->GetStaticMeshComponent())
	{
		if (FallbackMesh)
		{
			Fallback->GetStaticMeshComponent()->SetStaticMesh(FallbackMesh);
		}
		Fallback->SetActorScale3D(RuntimePreviewerFallbackPawnScale);
		FallbackPawnProxyActor = Fallback;
		Status = ERuntimePreviewerStatus::FallbackPawn;
	}
}
```

- [ ] **Step 3: Implement pawn-local sync**

Replace `SyncPawnProxy`:

```cpp
void FComposableCameraRuntimePreviewerViewportClient::SyncPawnProxy(
	const FTransform& PawnWorldTransform,
	USkeletalMeshComponent* SourceMesh)
{
	if (SourceMesh && PawnProxyActor.IsValid())
	{
		AActor* Proxy = PawnProxyActor.Get();
		USkeletalMeshComponent* ProxyMesh = Cast<USkeletalMeshComponent>(Proxy->GetRootComponent());
		if (!ProxyMesh)
		{
			return;
		}

		const FTransform SourceMeshLocal =
			ComposableCameraSystem::RuntimePreviewer::MakePawnRelativeTransform(
				SourceMesh->GetComponentTransform(),
				PawnWorldTransform);

		Proxy->SetActorTransform(SourceMeshLocal);

		const TArray<FTransform>& SourceCST = SourceMesh->GetComponentSpaceTransforms();
		TArray<FTransform>& ProxyCST = ProxyMesh->GetEditableComponentSpaceTransforms();
		if (SourceCST.Num() > 0 && SourceCST.Num() == ProxyCST.Num())
		{
			ProxyCST = SourceCST;
			ProxyMesh->ApplyEditedComponentSpaceTransforms();
			ProxyMesh->UpdateBounds();
			ProxyMesh->MarkRenderTransformDirty();
			ProxyMesh->MarkRenderStateDirty();
		}

		Status = ERuntimePreviewerStatus::Live;
		return;
	}

	if (FallbackPawnProxyActor.IsValid())
	{
		FallbackPawnProxyActor->SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		Status = ERuntimePreviewerStatus::FallbackPawn;
	}
}
```

- [ ] **Step 4: Update preview scene with pawn and camera local data**

Extend `UpdatePreviewScene` after the controlled-pawn validity checks:

```cpp
	APawn* Pawn = Data.ControlledPawn.Get();
	const FTransform PawnWorldTransform = Pawn->GetActorTransform();

	USkeletalMeshComponent* SourceMesh = Pawn->FindComponentByClass<USkeletalMeshComponent>();
	RebuildPawnProxyIfNeeded(SourceMesh);
	SyncPawnProxy(PawnWorldTransform, SourceMesh);

	const FTransform CameraWorldTransform(
		Data.Snapshot.FinalPose.Rotation,
		Data.Snapshot.FinalPose.Position,
		FVector::OneVector);

	LastCameraLocalTransform =
		ComposableCameraSystem::RuntimePreviewer::MakePawnRelativeTransform(CameraWorldTransform, PawnWorldTransform);
	LastVelocityLocal =
		ComposableCameraSystem::RuntimePreviewer::MakePawnRelativeVector(Data.PawnVelocity, PawnWorldTransform);
	bHasCameraLocalTransform = true;
```

- [ ] **Step 5: Run runtime preview helper tests and manual tab check**

Run inside IDE or Unreal Editor Automation:

```text
ComposableCameraSystem.RuntimePreviewer.*
```

Manual check inside Unreal:

```text
Open Camera Type Asset.
Open Window -> Runtime Previewer.
Start PIE.
Bind a matching camera with Debug picker.
Move pawn.
```

Expected:

```text
The previewer creates either a skeletal proxy or fallback cylinder.
The proxy remains centered around preview origin.
No actor in the PIE world is moved.
```

- [ ] **Step 6: Commit pose proxy**

```bash
git add Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp
git commit -m "feat: mirror controlled pawn pose in runtime previewer"
```

---

### Task 5: Camera, Frustum, Movement, and HUD Drawing

**Files:**

- Modify: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`

- [ ] **Step 1: Add frustum and movement drawing helpers**

Replace `DrawRuntimeCameraGizmo`:

```cpp
void FComposableCameraRuntimePreviewerViewportClient::DrawRuntimeCameraGizmo(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bHasCameraLocalTransform)
	{
		return;
	}

	const FVector CameraLocation = LastCameraLocalTransform.GetLocation();
	const FRotator CameraRotation = LastCameraLocalTransform.Rotator();
	const FMatrix CameraToWorld = FRotationTranslationMatrix(CameraRotation, CameraLocation);

	const FLinearColor CameraColor(0.25f, 0.65f, 1.0f, 1.0f);
	constexpr float AxisLength = 45.f;
	constexpr float FrustumLength = 140.f;
	constexpr float FrustumHalfWidth = 65.f;
	constexpr float FrustumHalfHeight = 38.f;
	constexpr float Thickness = 2.0f;

	const FVector Forward = CameraToWorld.TransformVector(FVector::ForwardVector);
	const FVector Right = CameraToWorld.TransformVector(FVector::RightVector);
	const FVector Up = CameraToWorld.TransformVector(FVector::UpVector);
	const FVector Center = CameraLocation + Forward * FrustumLength;

	const FVector Corners[4] = {
		Center + Right * FrustumHalfWidth + Up * FrustumHalfHeight,
		Center - Right * FrustumHalfWidth + Up * FrustumHalfHeight,
		Center - Right * FrustumHalfWidth - Up * FrustumHalfHeight,
		Center + Right * FrustumHalfWidth - Up * FrustumHalfHeight,
	};

	PDI->DrawPoint(CameraLocation, CameraColor, 14.f, SDPG_World);
	PDI->DrawLine(CameraLocation, CameraLocation + Forward * AxisLength, CameraColor, SDPG_World, Thickness);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		PDI->DrawLine(CameraLocation, Corners[Index], CameraColor, SDPG_World, Thickness);
		PDI->DrawLine(Corners[Index], Corners[(Index + 1) % 4], CameraColor, SDPG_World, Thickness);
	}
}
```

Replace `DrawMovementGizmo`:

```cpp
void FComposableCameraRuntimePreviewerViewportClient::DrawMovementGizmo(FPrimitiveDrawInterface* PDI) const
{
	if (!PDI)
	{
		return;
	}

	FVector Direction = LastVelocityLocal;
	Direction.Z = 0.f;
	if (!Direction.Normalize())
	{
		return;
	}

	const FLinearColor MoveColor(1.0f, 0.82f, 0.25f, 1.0f);
	const FVector Start(0.f, 0.f, 8.f);
	const FVector End = Start + Direction * 160.f;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();

	PDI->DrawLine(Start, End, MoveColor, SDPG_World, 2.0f);
	PDI->DrawLine(End, End - Direction * 28.f + Right * 18.f, MoveColor, SDPG_World, 2.0f);
	PDI->DrawLine(End, End - Direction * 28.f - Right * 18.f, MoveColor, SDPG_World, 2.0f);
}
```

- [ ] **Step 2: Add HUD details**

Replace the body of `DrawCanvas` after `FEditorViewportClient::DrawCanvas(...)`:

```cpp
	const FLinearColor White = FLinearColor::White;
	const FLinearColor Muted(0.72f, 0.78f, 0.86f, 1.f);

	TArray<FString, TInlineAllocator<6>> Lines;
	Lines.Add(RuntimePreviewerStatusToText(Status).ToString());

	if (Data.Camera.IsValid())
	{
		Lines.Add(FString::Printf(TEXT("Camera: %s"), *Data.Camera->GetName()));
	}
	if (Data.ControlledPawn.IsValid())
	{
		Lines.Add(FString::Printf(TEXT("Pawn: %s"), *Data.ControlledPawn->GetName()));
	}
	if (bHasCameraLocalTransform)
	{
		const FVector Local = LastCameraLocalTransform.GetLocation();
		Lines.Add(FString::Printf(TEXT("Camera Local: X %.1f  Y %.1f  Z %.1f"), Local.X, Local.Y, Local.Z));
		Lines.Add(FString::Printf(TEXT("FOV: %.1f deg"), Data.Snapshot.FinalPose.GetEffectiveFieldOfView()));
	}

	FVector2D Pos(12.f, 12.f);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FLinearColor Color = (Index == 0) ? White : Muted;
		FCanvasTextItem Item(Pos, FText::FromString(Lines[Index]), GEngine->GetSmallFont(), Color);
		Item.EnableShadow(FLinearColor::Black);
		Canvas.DrawItem(Item);
		Pos.Y += 16.f;
	}
```

- [ ] **Step 3: Run visual manual check**

Manual check inside Unreal:

```text
Open Runtime Previewer during PIE.
Bind matching camera.
Move pawn forward, backward, and sideways.
Rotate the observer view by dragging in the Runtime Previewer viewport.
```

Expected:

```text
Pawn proxy remains in preview space.
Camera point and frustum update relative to pawn.
Movement arrow points in pawn-local movement direction.
Dragging changes only the observer view.
The game camera and game pawn do not move due to previewer input.
```

- [ ] **Step 4: Commit drawing**

```bash
git add Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp
git commit -m "feat: draw runtime camera relation in previewer"
```

---

### Task 6: Frame Command and Empty-State Polish

**Files:**

- Modify: `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h`
- Modify: `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp`
- Modify: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`

- [ ] **Step 1: Add viewport toolbar**

In `SComposableCameraRuntimePreviewer.h`, add:

```cpp
virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
```

In `SComposableCameraRuntimePreviewer.cpp`, add includes:

```cpp
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SHorizontalBox.h"
#include "Widgets/Text/STextBlock.h"
```

Add implementation:

```cpp
TSharedPtr<SWidget> SComposableCameraRuntimePreviewer::MakeViewportToolbar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(NSLOCTEXT("SComposableCameraRuntimePreviewer", "FramePawnTooltip", "Frame the preview pawn and runtime camera relation."))
			.OnClicked_Lambda([this]()
			{
				FramePreviewSubject();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("SComposableCameraRuntimePreviewer", "FramePawn", "Frame"))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			]
		];
}
```

- [ ] **Step 2: Improve FramePreviewSubject**

Replace `FramePreviewSubject`:

```cpp
void FComposableCameraRuntimePreviewerViewportClient::FramePreviewSubject()
{
	FVector Center = FVector::ZeroVector;
	float Distance = 520.f;

	if (bHasCameraLocalTransform)
	{
		const FVector CameraLocal = LastCameraLocalTransform.GetLocation();
		Center = (CameraLocal + FVector(0.f, 0.f, 90.f)) * 0.5f;
		Distance = FMath::Clamp(CameraLocal.Size() + 240.f, 360.f, 1800.f);
	}

	SetViewLocation(Center + FVector(-Distance, -Distance, Distance * 0.55f));
	SetViewRotation((Center - GetViewLocation()).Rotation());
	Invalidate();
}
```

- [ ] **Step 3: Manual check empty states**

Manual check:

```text
Open Runtime Previewer before PIE.
Start PIE with no matching camera.
Start PIE with matching camera but Debug picker set to None.
Bind camera, then stop PIE.
```

Expected:

```text
HUD changes between NotInPIE, NoCameraBound, Live, and NotInPIE.
No stale pawn proxy remains after PIE ends.
Frame button remains usable and does not crash in empty states.
```

- [ ] **Step 4: Commit polish**

```bash
git add Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp
git commit -m "feat: polish runtime previewer viewport controls"
```

---

### Task 7: Documentation Updates

**Files:**

- Modify: `Docs/superpowers/specs/2026-06-02-runtime-previewer-window-design.md`
- Modify: `Docs/EditorDesignDoc.md`
- Modify: `Docs/TechDoc.md`

- [ ] **Step 1: Update design spec pose-copy decision**

In `Docs/superpowers/specs/2026-06-02-runtime-previewer-window-design.md`, replace the `## Pose Copy` preferred implementation bullets with:

```markdown
Preferred first implementation:

- Reuse the Shot Editor preview proxy pattern.
- Spawn an editor-only `ASkeletalMeshActor` in the `FAdvancedPreviewScene`.
- Mirror the live pawn's `USkeletalMeshComponent::GetSkeletalMeshAsset()`.
- Disable proxy animation and component tick.
- Each preview tick, copy source component-space transforms into the proxy's
  editable component-space transform array and call
  `ApplyEditedComponentSpaceTransforms()`.

This supersedes the initial `UPoseableMeshComponent` idea because the existing
Shot Editor already proves this pattern for cross-world preview pose mirroring.
It avoids leader-pose propagation issues and prevents the proxy anim graph from
overwriting copied transforms.
```

- [ ] **Step 2: Update EditorDesignDoc**

In `Docs/EditorDesignDoc.md`, update `Updated:` to:

```markdown
Updated: 2026-06-02
```

In `## 3. Asset Editor`, add `runtime previewer tab` to Current surfaces:

```markdown
- runtime previewer tab for live PIE pawn/camera relation inspection.
```

In `## 16. Runtime Debug From Editor`, add this subsection after the existing debug picker / graph overlay description:

```markdown
### Runtime Previewer tab

The Camera Type Asset editor registers a `Runtime Previewer` dock tab in its
Window menu. The tab is not part of the default layout. It uses the same
debugged camera selected by the toolbar Debug picker.

During PIE, the toolkit pushes the bound
`AComposableCameraCameraBase` and its `FComposableCameraDebugSnapshot` into the
previewer each editor tick. The previewer resolves the owning PCM, player
controller, and controlled pawn, then treats the pawn world transform as the
preview origin. The live pawn's world translation is removed, while skeletal
pose and facing remain visible.

The previewer owns an `FAdvancedPreviewScene` and an editor-only viewport
client. Mouse input controls only the observer camera inside that preview
scene. It never drives the PIE pawn or the runtime camera. The runtime camera is
drawn as a pawn-local marker plus frustum, with a pawn-local movement arrow and
HUD text for status, bound camera, pawn, relative camera position, and FOV.

Skeletal preview uses the same proxy strategy as the Shot Editor: spawn a
transient skeletal proxy in the preview scene, disable its animation tick, copy
component-space transforms from the live pawn mesh each preview tick, then call
`ApplyEditedComponentSpaceTransforms()`. If no skeletal mesh is available, the
previewer draws a capsule-like fallback marker and still shows camera relation
data when possible.
```

- [ ] **Step 3: Update TechDoc**

In `Docs/TechDoc.md`, update `Updated:` to:

```markdown
Updated: 2026-06-02
```

In the editor module map near Shot Editor, change:

```markdown
- asset definitions, factories, graph editor, toolkit, schema.
- details customizations, widgets, Sequencer track editors, Shot Editor.
```

to:

```markdown
- asset definitions, factories, graph editor, toolkit, schema.
- details customizations, widgets, Sequencer track editors, Shot Editor,
  Runtime Previewer.
```

In `## 21. Gotchas`, add:

```markdown
- `SEditorViewport` clients can outlive the widget member that owns
  `FAdvancedPreviewScene`. Widget destructors must clear `Viewport` and release
  scene-bound proxy actors before the preview scene is destroyed.
- Preview-world skeletal proxies must have animation ticking disabled before
  per-frame component-space transform copies, or the proxy anim graph can
  overwrite the copied live pose.
```

- [ ] **Step 4: Review docs against source**

Read the edited sections and confirm:

```text
EditorDesignDoc mentions Window menu entry, shared Debug picker source, pawn-origin transform, observer camera isolation, and proxy pose copy.
TechDoc mentions Runtime Previewer and the two preview-scene gotchas.
Spec no longer claims UPoseableMeshComponent is the selected first implementation.
```

- [ ] **Step 5: Commit docs**

Docs are ignored by `.gitignore`, so force-add these paths:

```bash
git add -f Docs/superpowers/specs/2026-06-02-runtime-previewer-window-design.md Docs/EditorDesignDoc.md Docs/TechDoc.md
git commit -m "docs: document runtime previewer window"
```

---

### Task 8: Final Review and Handoff

**Files:**

- Read: `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h`
- Read: `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp`
- Read: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
- Read: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
- Read: `Source/ComposableCameraSystemEditor/Public/Toolkits/ComposableCameraTypeAssetEditorToolkit.h`
- Read: `Source/ComposableCameraSystemEditor/Private/Toolkits/ComposableCameraTypeAssetEditorToolkit.cpp`
- Read: `Docs/EditorDesignDoc.md`
- Read: `Docs/TechDoc.md`

- [ ] **Step 1: Run project review skill**

Read `.agents/skills/composable-camera-review/SKILL.md` and run its checklist because this change is non-trivial: new tab, new viewport client, multiple files, per-frame editor tick, and docs.

- [ ] **Step 2: Verify no forbidden shell build was run**

Confirm implementation notes state:

```text
No UBT, Build.bat, RunUBT.bat, dotnet, msbuild, Unreal Editor, or automation tests were invoked from shell.
```

- [ ] **Step 3: IDE compile handoff**

Ask the user to compile in Rider or Visual Studio. Since this adds headers and new source files, request a full editor restart instead of Live Coding.

Expected handoff text:

```text
Changed editor module headers and new source files. Full IDE build + editor restart needed. Live Coding is not enough.
```

- [ ] **Step 4: Manual verification checklist**

Ask the user to run this in Unreal Editor:

```text
1. Open a Camera Type Asset.
2. Start PIE with a matching CCS runtime camera.
3. Select the camera in the Debug picker.
4. Open Window -> Runtime Previewer.
5. Move the controlled pawn.
6. Confirm pawn pose updates while preview pawn stays origin-locked.
7. Confirm camera marker/frustum updates relative to pawn.
8. Drag inside Runtime Previewer and confirm only observer view changes.
9. Stop PIE and confirm the tab clears without crash.
```

- [ ] **Step 5: Commit final fixes after IDE feedback**

If the IDE compile or manual check reveals fixes, commit them with a focused message:

```bash
git add Source/ComposableCameraSystemEditor Docs
git commit -m "fix: stabilize runtime previewer window"
```

If no fixes are needed, do not create an empty commit.
