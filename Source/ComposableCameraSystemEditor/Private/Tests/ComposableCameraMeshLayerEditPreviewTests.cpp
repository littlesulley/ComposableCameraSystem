// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerEditPreview.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerDocumentBuild.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/Actor.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "PrimitiveDrawInterface.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "UObject/Package.h"

namespace
{
	using namespace UE::ComposableCamera::MeshEditor;
	class FRestoredStrokeFixtureModeTools final : public FEditorModeTools
	{
	public:
		explicit FRestoredStrokeFixtureModeTools(UWorld& World) : FixtureWorld(&World) {}
		virtual UWorld* GetWorld() const override { return FixtureWorld.Get(); }
	private:
		TWeakObjectPtr<UWorld> FixtureWorld;
	};

	void AddEditPreviewTriangle(FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId, double X)
	{
		const int32 Base = Data.Vertices.Num();
		Data.Vertices.Append({FVector3f(X, 0.0, 0.0), FVector3f(X + 50.0, 0.0, 10.0), FVector3f(X, 50.0, 20.0)});
		Data.Indices.Append({Base, Base + 1, Base + 2}); Data.TriangleLayerIds.Add(LayerId);
	}

	TArray<UComposableCameraMeshLayerEditPreviewComponent*> EditPreviewComponents(FMeshLayerEditPreview& Preview)
	{
		TArray<UComposableCameraMeshLayerEditPreviewComponent*> Result;
		if (AActor* Actor = Preview.GetActor()) { Actor->GetComponents(Result); }
		return Result;
	}

	double EditPreviewArea(TConstArrayView<UComposableCameraMeshLayerEditPreviewComponent*> Components)
	{
		double Area = 0.0;
		for (const auto* Component : Components)
		{
			const auto Vertices = Component->GetVertices(); const auto Indices = Component->GetIndices();
			for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
			{
				const FVector3f A = Vertices[Indices[Index]].Position, B = Vertices[Indices[Index + 1]].Position, C = Vertices[Indices[Index + 2]].Position;
				Area += FMath::Abs(FVector3f::CrossProduct(B - A, C - A).Z) * 0.5;
			}
		}
		return Area;
	}

	bool SameEditPreviewTriangles(const UComposableCameraMeshLayerEditPreviewComponent& A,
		const UComposableCameraMeshLayerEditPreviewComponent& B)
	{
		const auto AV = A.GetVertices(), BV = B.GetVertices(); const auto AI = A.GetIndices(), BI = B.GetIndices();
		if (AV.Num() != BV.Num() || AI.Num() != BI.Num()) { return false; }
		// Progressive final regions can change the order of cells within a tile.
		// Compare the exact oriented triangle multiset, including every render attribute.
		TMap<FVector3f, TArray<int32>> Candidates;
		for (int32 Index = 0; Index + 2 < BI.Num(); Index += 3) { Candidates.FindOrAdd(BV[BI[Index]].Position).Add(Index); }
		for (int32 Index = 0; Index + 2 < AI.Num(); Index += 3)
		{
			auto* Other = Candidates.Find(AV[AI[Index]].Position); if (!Other) { return false; }
			int32 Match = INDEX_NONE;
			for (int32 Candidate = 0; Candidate < Other->Num(); ++Candidate)
			{
				bool bSame = true;
				for (int32 Corner = 0; bSame && Corner < 3; ++Corner)
				{
					const auto& VA = AV[AI[Index + Corner]]; const auto& VB = BV[BI[(*Other)[Candidate] + Corner]];
					bSame = VA.Position == VB.Position && VA.TangentX == VB.TangentX && VA.TangentZ == VB.TangentZ && VA.Color == VB.Color;
					for (int32 UV = 0; bSame && UV < static_cast<int32>(UE_ARRAY_COUNT(VA.TextureCoordinate)); ++UV) { bSame = VA.TextureCoordinate[UV] == VB.TextureCoordinate[UV]; }
				}
				if (bSame) { Match = Candidate; break; }
			}
			if (Match == INDEX_NONE) { return false; }
			Other->RemoveAtSwap(Match, 1, EAllowShrinking::No);
		}
		return true;
	}

	void CheckEditPreviewVisibility(FAutomationTestBase& Test, UWorld& World, UComposableCameraMeshLayerEditPreviewComponent& Component)
	{
		World.SendAllEndOfFrameUpdates(); FlushRenderingCommands();
		FPrimitiveSceneProxy* Proxy = Component.GetSceneProxy();
		if (!Test.TestNotNull(TEXT("Edit has a persistent render proxy"), Proxy)) { return; }
		FEngineShowFlags Flags(ESFIM_Game);
		FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(nullptr, World.Scene, Flags).SetTime(FGameTime()));
		FSceneViewInitOptions Options; Options.SetViewRectangle(FIntRect(0, 0, 64, 64)); Options.ViewFamily = &Family;
		FSceneView View(Options);
		bool bVisible = false, bCaptureVisible = false, bOccluded = true;
		ENQUEUE_RENDER_COMMAND(CCSMeshEditVisibilityTest)([Proxy, &View, &bVisible, &bCaptureVisible, &bOccluded](FRHICommandListImmediate&)
		{
			bVisible = Proxy->GetViewRelevance(&View).bDrawRelevance;
			View.bIsSceneCapture = true;
			bCaptureVisible = Proxy->GetViewRelevance(&View).bDrawRelevance;
			bOccluded = Proxy->CanBeOccluded();
		});
		FlushRenderingCommands();
		Test.TestTrue(TEXT("Game View retains Edit fill"), bVisible);
		Test.TestFalse(TEXT("Scene captures do not gain authoring overlays"), bCaptureVisible);
		Test.TestFalse(TEXT("No new temporal occlusion delays"), bOccluded);
	}

	class FEditPreviewCountingPDI final : public FPrimitiveDrawInterface
	{
	public:
		explicit FEditPreviewCountingPDI(const FSceneView* View) : FPrimitiveDrawInterface(View) {}
		int32 Meshes = 0, Resources = 0, Lines = 0;
		virtual bool IsHitTesting() override { return false; }
		virtual void SetHitProxy(HHitProxy*) override {}
		virtual void RegisterDynamicResource(FDynamicPrimitiveResource*) override { ++Resources; }
		virtual void AddReserveLines(uint8, int32, bool, bool) override {}
		virtual void DrawSprite(const FVector&, float, float, const FTexture*, const FLinearColor&, uint8,
			float, float, float, float, uint8, float) override {}
		virtual void DrawLine(const FVector&, const FVector&, const FLinearColor&, uint8, float, float, bool) override { ++Lines; }
		virtual void DrawTranslucentLine(const FVector&, const FVector&, const FLinearColor&, uint8, float, float, bool) override { ++Lines; }
		virtual void DrawPoint(const FVector&, const FLinearColor&, float, uint8) override {}
		virtual int32 DrawMesh(const FMeshBatch&) override { ++Meshes; return 1; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshEditPreviewBuffersTest,
	"ComposableCameraSystem.Editor.MeshCamera.EditPreviewPersistentBuffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshEditPreviewBuffersTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	const bool bResult = [this, World]()
	{
		if (!TestNotNull(TEXT("GeomMaterial exists"), GEngine ? GEngine->GeomMaterial.Get() : nullptr)) { return false; }
		TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted();
		Layers[0].LayerId = FGuid::NewGuid();
		Layers[0].DebugColor = FLinearColor(0.13791f, 0.54321f, 0.27123f, 0.32781f);
		FComposableCameraMeshSurfaceAuthoringData Data;
		AddEditPreviewTriangle(Data, Layers[0].LayerId, -1100.0); AddEditPreviewTriangle(Data, Layers[0].LayerId, 1000.0);
		FResolvedSurfaceVisualization Visualization; BuildAuthoringVisualization(Data, Layers, Visualization);
		FMeshLayerEditPreview Preview;
		World->GetPackage()->SetDirtyFlag(false);
		const FTransform Anchor(FRotator(0, 35, 0), FVector(125, 250, 500), FVector(2, 3, 4));
		if (!TestTrue(TEXT("Edit publishes existing resolved geometry"), Preview.Update(*World->PersistentLevel, Anchor, Visualization, Layers))) { return false; }
		const auto Components = EditPreviewComponents(Preview);
		if (!TestEqual(TEXT("Disjoint positive/negative tiles publish independently"), Components.Num(), 2)) { return false; }
		TestTrue(TEXT("Transient publication leaves the Level clean"), !World->GetPackage()->IsDirty());
		AActor* Actor = Preview.GetActor();
		TestTrue(TEXT("Actor belongs to edited Level and cannot enter PIE"), Actor->GetLevel() == World->PersistentLevel
			&& Actor->HasAnyFlags(RF_Transient | RF_DuplicateTransient) && !Actor->ShouldDuplicateInPIE());
		TestTrue(TEXT("Anchor retains translation/rotation/nonuniform scale"), Actor->GetActorTransform().Equals(Anchor));
		TestTrue(TEXT("No reverse geometry or coverage expansion"), FMath::IsNearlyEqual(EditPreviewArea(Components), 2500.0, 0.01));
		for (auto* Component : Components)
		{
			TestTrue(TEXT("Linear RGB and alpha avoid byte quantization"), Component->GetFillColor() == Layers[0].DebugColor);
			TestTrue(TEXT("Authoring material and selection/collision policy retained"), Component->GetMaterial(0) == GEngine->GeomMaterial.Get()
				&& !Component->bSelectable && Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			for (const auto& Vertex : Component->GetVertices())
			{
				const double LocalX = Vertex.Position.X < 0.0 ? Vertex.Position.X + 1100.0 : Vertex.Position.X - 1000.0;
				TestTrue(TEXT("Original plane plus exact document-Z offset"), FMath::IsNearlyEqual(double(Vertex.Position.Z),
					LocalX * 0.2 + Vertex.Position.Y * 0.4 + VisualizationSurfaceOffset, 1.e-4));
				TestTrue(TEXT("Original white vertex color and zero UV"), Vertex.Color == FColor::White && Vertex.TextureCoordinate[0] == FVector2f::ZeroVector);
			}
		}
		CheckEditPreviewVisibility(*this, *World, *Components[0]);
		FPrimitiveSceneProxy* Proxy = Components[0]->GetSceneProxy();
		for (int32 Frame = 0; Frame < 120; ++Frame) { TestTrue(TEXT("Stable readiness needs no publication"), Preview.IsReadyFor(World->PersistentLevel)); }
		TestTrue(TEXT("Stable frames retain actual render proxy"), Components[0]->GetSceneProxy() == Proxy && Components[0]->GetGeometryRevision() == 1);
		Layers.AddDefaulted(); Layers[1].LayerId = FGuid::NewGuid(); Layers[1].DebugColor = FLinearColor(0.65f, 0.3f, 0.2f, 0.8f);
		AddEditPreviewTriangle(Data, Layers[1].LayerId, -1100.0);
		Layers[0].bEnabled = false; BuildAuthoringVisualization(Data, Layers, Visualization);
		Preview.Update(*World->PersistentLevel, Anchor, Visualization, Layers);
		const auto Lower = EditPreviewComponents(Preview);
		if (!TestEqual(TEXT("Disabled top row reveals lower coverage"), Lower.Num(), 1)) { return false; }
		FLinearColor LowerColor = Layers[1].DebugColor; LowerColor.A = 0.5f;
		TestTrue(TEXT("Original alpha clamp and lower color retained"), Lower[0]->GetFillColor() == LowerColor);
		Layers[0].bEnabled = true; Layers.Swap(0, 1); BuildAuthoringVisualization(Data, Layers, Visualization);
		Preview.Update(*World->PersistentLevel, Anchor, Visualization, Layers);
		for (auto* Component : EditPreviewComponents(Preview))
		{
			TestTrue(TEXT("Reorder refreshes ownership and color together"), Component->GetFillColor()
				== (Component->GetVertices()[0].Position.X < 0.0 ? LowerColor : Layers[1].DebugColor));
		}
		const auto FinalComponents = EditPreviewComponents(Preview);
		Preview.Reset();
		for (const auto* Component : FinalComponents) { TestFalse(TEXT("Closing Edit unregisters all buffers"), Component->IsRegistered()); }
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshEditPreviewRegionalTest,
	"ComposableCameraSystem.Editor.MeshCamera.EditPreviewRegionalUpdates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshEditPreviewRegionalTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	const bool bResult = [this, World]()
	{
		TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted();
		Layers[0].LayerId = FGuid::NewGuid();
		FComposableCameraMeshSurfaceAuthoringData Data;
		AddEditPreviewTriangle(Data, Layers[0].LayerId, -1100.0); AddEditPreviewTriangle(Data, Layers[0].LayerId, 1000.0);
		FResolvedSurfaceVisualization Visualization; BuildAuthoringVisualization(Data, Layers, Visualization);
		FMeshLayerEditPreview Preview;
		if (!TestTrue(TEXT("Initial publication"), Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers))) { return false; }
		auto Components = EditPreviewComponents(Preview);
		if (Components.Num() != 2) { return false; }
		UComposableCameraMeshLayerEditPreviewComponent* Retained = Components[0]->GetVertices()[0].Position.X > 0.0 ? Components[0] : Components[1];
		UComposableCameraMeshLayerEditPreviewComponent* Erased = Retained == Components[0] ? Components[1] : Components[0];
		World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
		FPrimitiveSceneProxy* RetainedProxy = Retained->GetSceneProxy();
		Data.Reset(); AddEditPreviewTriangle(Data, Layers[0].LayerId, 1000.0);
		FBox2D Dirty(FVector2D(-1100, 0), FVector2D(-1050, 50));
		UpdateAuthoringVisualization(Data, Layers, Dirty, Visualization);
		TestTrue(TEXT("Erase publishes only affected tiles"), Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers, &Dirty));
		TestFalse(TEXT("Erased tile does not leave a ghost mesh"), Erased->IsRegistered());
		TestTrue(TEXT("Distant buffer and component retained"), Retained->GetSceneProxy() == RetainedProxy && Retained->GetGeometryRevision() == 1);
		const auto Remaining = EditPreviewComponents(Preview);
		TestTrue(TEXT("Hole removal preserves distant area"), FMath::IsNearlyEqual(EditPreviewArea(Remaining), 1250.0, 0.01));
		AddEditPreviewTriangle(Data, Layers[0].LayerId, -1100.0); UpdateAuthoringVisualization(Data, Layers, Dirty, Visualization);
		Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers, &Dirty);
		const auto Restored = EditPreviewComponents(Preview);
		TestTrue(TEXT("Undo-equivalent restoration retains full coverage"), FMath::IsNearlyEqual(EditPreviewArea(Restored), 2500.0, 0.01));
		Layers[0].bEnabled = false; BuildAuthoringVisualization(Data, Layers, Visualization);
		Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers);
		TestEqual(TEXT("Disabling the Layer removes every component"), EditPreviewComponents(Preview).Num(), 0);
		TestTrue(TEXT("Empty document is a completed cache, not repeated work"), Preview.IsReadyFor(World->PersistentLevel));
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshEditPreviewInvalidationTest,
	"ComposableCameraSystem.Editor.MeshCamera.EditPreviewInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshEditPreviewInvalidationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	const bool bResult = [this, World]()
	{
		FComposableCameraMeshLayerEdMode Mode;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(); Mode.Settings->Layers.AddDefaulted();
		Mode.Settings->NormalizeLayers();
		Mode.TargetLevel = World->PersistentLevel;
		AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, 0.0);
		BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Visualization);
		Mode.bVisualizationDirty = false;
		TestTrue(TEXT("Actual mode publishes the fill"), Mode.UpdateCachedPreview());
		const auto Components = EditPreviewComponents(*Mode.EditPreview);
		if (Components.IsEmpty()) { return false; }
		const uint64 Revision = Components[0]->GetGeometryRevision();
		FEngineShowFlags Flags(ESFIM_Editor);
		FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(nullptr, World->Scene, Flags).SetTime(FGameTime()));
		FSceneViewInitOptions Options; Options.SetViewRectangle(FIntRect(0, 0, 64, 64)); Options.ViewFamily = &Family;
		FSceneView View(Options);
		FEditPreviewCountingPDI PDI(&View);
		for (int32 Frame = 0; Frame < 30; ++Frame) { Mode.Render(&View, nullptr, &PDI); }
		TestEqual(TEXT("Stable mode rendering creates no PDI fill meshes"), PDI.Meshes, 0);
		TestEqual(TEXT("Stable mode rendering creates no PDI fill resources"), PDI.Resources, 0);
		TestEqual(TEXT("Stable mode rendering never uploads geometry"), Components[0]->GetGeometryRevision(), Revision);
		Mode.Settings->TouchDocument();
		Mode.RefreshDocumentState();
		TestTrue(TEXT("Unremembered source changes invalidate both caches"), Mode.bVisualizationDirty && Mode.bEditPreviewDirty);
		Mode.Settings->Layers[0].DebugColor = FLinearColor(0.1, 0.2, 0.3, 0.4);
		BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Visualization);
		Mode.bVisualizationDirty = false;
		Mode.UpdateCachedPreview();
		TestTrue(TEXT("Full refresh keeps geometry and publishes new color"), Components[0]->GetGeometryRevision() == Revision
			&& Components[0]->GetFillColor() == Mode.Settings->Layers[0].DebugColor);
		// Audit regression: mode work is shared across viewports, but every affected
		// static view must redraw after publication. Use real SceneViewports so
		// InvalidateDisplay follows the engine's RedrawRequested path.
		TArray<FLevelEditorViewportClient*> Views;
		if (GEditor)
		{
			for (auto* Client : GEditor->GetLevelViewportClients())
			{
				if (Client && Client->Viewport && Client->IsVisible() && Client->GetWorld()
					&& Client->GetWorld()->WorldType == EWorldType::Editor
					&& (Views.IsEmpty() || Client->GetWorld() == Views[0]->GetWorld())) { Views.Add(Client); }
			}
		}
		if (Views.Num() < 2)
		{
			AddWarning(TEXT("Open two visible Level Editor viewports to run the Edit multi-viewport redraw regression."));
		}
		else
		{
			UPackage* Package = Views[0]->GetWorld()->GetOutermost();
			struct FRestoreViewportFlags
			{
				FLevelEditorViewportClient* First; FLevelEditorViewportClient* Second;
				bool FirstRedraw; bool SecondRedraw; UPackage* Package; bool Dirty;
				~FRestoreViewportFlags()
				{
					First->bNeedsRedraw = FirstRedraw; Second->bNeedsRedraw = SecondRedraw;
					Package->SetDirtyFlag(Dirty);
				}
			} Restore{Views[0], Views[1], Views[0]->bNeedsRedraw, Views[1]->bNeedsRedraw, Package, Package->IsDirty()};
			FComposableCameraMeshLayerEdMode ViewportMode;
			ViewportMode.TargetLevel = Views[0]->GetWorld()->GetCurrentLevel();
			ViewportMode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>();
			ViewportMode.Settings->Layers.AddDefaulted(); ViewportMode.Settings->NormalizeLayers();
			AddEditPreviewTriangle(ViewportMode.Settings->WorkingData, ViewportMode.Settings->GetActiveLayerId(), 0.0);
			ViewportMode.Settings->TouchDocument();
			BuildAuthoringVisualization(ViewportMode.Settings->WorkingData, ViewportMode.Settings->Layers, ViewportMode.Visualization);
			if (!TestTrue(TEXT("Ready real-world Edit fill queues without camera input"),
				ViewportMode.EditPreview->QueuePreparedUpdate(*ViewportMode.TargetLevel.Get(), PrepareEditPreview(ViewportMode.Visualization, ViewportMode.Settings->Layers)))) { return false; }
			ViewportMode.bVisualizationDirty = ViewportMode.bEditPreviewDirty = false;
			Views[0]->bNeedsRedraw = Views[1]->bNeedsRedraw = false;
			ViewportMode.Tick(Views[0], 0.0f);
			ViewportMode.Tick(Views[1], 0.0f);
			TestTrue(TEXT("Publication invalidates both affected viewports in the same frame"),
				Views[0]->bNeedsRedraw && Views[1]->bNeedsRedraw);
		}
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshBudgetedEditPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.BudgetedEditPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshBudgetedEditPreviewTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	const bool bResult = [this, World]()
	{
		TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
		FComposableCameraMeshSurfaceAuthoringData Data; AddEditPreviewTriangle(Data, Layers[0].LayerId, -1100);
		FResolvedSurfaceVisualization Visualization; BuildAuthoringVisualization(Data, Layers, Visualization);
		FMeshLayerEditPreview Preview;
		if (!Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers)) { return false; }
		const auto Components = EditPreviewComponents(Preview);
		if (!TestEqual(TEXT("Fixture uses one negative-coordinate tile"), Components.Num(), 1)) { return false; }
		const uint64 Revision = Components[0]->GetGeometryRevision();
		const FBox2D Dirty(FVector2D(-1100, 0), FVector2D(-1050, 50));
		Preview.BeginUpdate(*World->PersistentLevel, Visualization, &Dirty);
		TestFalse(TEXT("Tile building yields instead of uploading a whole tile immediately"),
			Preview.AdvanceUpdate(*World->PersistentLevel, FTransform::Identity, Visualization, Layers, 1));
		TestEqual(TEXT("Incomplete tile retains its last complete geometry"), Components[0]->GetGeometryRevision(), Revision);
		int32 Steps = 0;
		while (!Preview.AdvanceUpdate(*World->PersistentLevel, FTransform::Identity, Visualization, Layers, 1) && ++Steps < 2000) {}
		TestTrue(TEXT("Tile completes under tiny work slices"), Steps < 2000);
		TestEqual(TEXT("Identical visible geometry does not recreate buffers"), Components[0]->GetGeometryRevision(), Revision);
		TestTrue(TEXT("Sloped area and visibility are retained"), FMath::IsNearlyEqual(EditPreviewArea(Components), 1250.0, 0.01)
			&& Preview.IsReadyFor(World->PersistentLevel));
		Layers[0].DebugColor = FLinearColor(0.11, 0.22, 0.33, 0.44);
		Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers, &Dirty);
		TestTrue(TEXT("Color-only changes publish without replacing geometry"), Components[0]->GetGeometryRevision() == Revision
			&& Components[0]->GetFillColor() == Layers[0].DebugColor);
		const auto OriginalShared = Components[0]->GetSharedGeometry();
		auto IdenticalShared = MakeShared<FEditPreviewGeometry, ESPMode::ThreadSafe>(*OriginalShared);
		Components[0]->SetSharedGeometry(MoveTemp(IdenticalShared), Layers[0].DebugColor);
		TestTrue(TEXT("Identical worker-owned buffers retain geometry identity and avoid upload"),
			Components[0]->GetSharedGeometry() == OriginalShared && Components[0]->GetGeometryRevision() == Revision);
		auto ChangedShared = MakeShared<FEditPreviewGeometry, ESPMode::ThreadSafe>(*OriginalShared);
		const float MidZ = static_cast<float>(OriginalShared->LocalBounds.Origin.Z);
		ChangedShared->Vertices[0].Position.Z = FMath::IsNearlyEqual(ChangedShared->Vertices[0].Position.Z, MidZ)
			? MidZ + static_cast<float>(OriginalShared->LocalBounds.BoxExtent.Z * 0.25) : MidZ;
		// Existing bounds remain conservative. Equal bounds cannot hide a position edit.
		Components[0]->SetSharedGeometry(MoveTemp(ChangedShared), Layers[0].DebugColor);
		TestTrue(TEXT("Equal topology and bounds with changed positions still updates the shared geometry"),
			Components[0]->GetSharedGeometry() != OriginalShared && Components[0]->GetGeometryRevision() == Revision + 1);
		Components[0]->SetSharedGeometry(OriginalShared, Layers[0].DebugColor);
		Preview.BeginUpdate(*World->PersistentLevel, Visualization, &Dirty); Preview.CancelUpdate();
		Visualization.Reset(); Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers);
		TestTrue(TEXT("Cancellation and empty coverage remove stale tiles"), EditPreviewComponents(Preview).IsEmpty()
			&& !Components[0]->IsRegistered() && Preview.IsReadyFor(World->PersistentLevel));
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshQueuedEditPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.QueuedEditPreviewSnapshots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshQueuedEditPreviewTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Snapshot fixture World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
		FComposableCameraMeshSurfaceAuthoringData Data; AddEditPreviewTriangle(Data, Layers[0].LayerId, -1100);
		FResolvedSurfaceVisualization Visualization; BuildAuthoringVisualization(Data, Layers, Visualization);
		FMeshLayerEditPreview Preview;
		if (!TestTrue(TEXT("Initial exact fill publishes"), Preview.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers))) { return false; }
		const FBox2D Dirty(FVector2D(-1100, 0), FVector2D(-1040, 50));
		for (int32 Stamp = 0; Stamp < 8; ++Stamp)
		{
			Data.Reset(); AddEditPreviewTriangle(Data, Layers[0].LayerId, -1100.0 + Stamp);
			BuildAuthoringVisualization(Data, Layers, Visualization);
			TestTrue(TEXT("Successive completed coverage snapshots enqueue"), Preview.QueueUpdate(*World->PersistentLevel, Visualization, &Dirty));
			TestEqual(TEXT("Repeated waiting tile updates coalesce instead of retaining a stamp FIFO"), Preview.GetQueuedTileCount(), 1);
		}
		const auto Expected = Visualization;
		Visualization.Reset(); Data.Reset();
		// Finite publication starts a worker and returns immediately even when its
		// result becomes ready quickly. It does not access the changed live coverage.
		TestFalse(TEXT("Ordinary publication schedules owned geometry without waiting"),
			Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 1.0));
		TestTrue(TEXT("A worker-owned tile remains pending"), Preview.HasQueuedUpdates());
		Visualization = Expected;
		for (auto& Cell : Visualization.Cells)
		{
			for (auto& Patch : Cell.Patches) { for (auto& Vertex : Patch.LocalVertices) { Vertex.Z += 7.0; } }
		}
		Preview.QueueUpdate(*World->PersistentLevel, Visualization, &Dirty);
		Preview.QueueUpdate(*World->PersistentLevel, Visualization, &Dirty);
		TestEqual(TEXT("An active tile has only one latest waiting successor"), Preview.GetQueuedTileCount(), 2);
		FMeshLayerEditPreview Reference;
		Reference.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers, &Dirty);
		Visualization.Reset(); // Pending output must own the newer polygons as well.
		TestTrue(TEXT("Explicit boundaries drain the active result and its latest successor"),
			Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0));
		const auto Actual = EditPreviewComponents(Preview), Complete = EditPreviewComponents(Reference);
		if (!TestEqual(TEXT("Coalescing retains component coverage"), Actual.Num(), Complete.Num()) || Actual.IsEmpty()) { return false; }
		TestTrue(TEXT("Latest snapshot retains the original component's linear color"), Actual[0]->GetFillColor() == Complete[0]->GetFillColor());
		TestTrue(TEXT("Latest snapshot keeps exact vertices, normals, winding and offset"), Actual[0]->GetIndices().Num() == Complete[0]->GetIndices().Num()
			&& Actual[0]->GetVertices().Num() == Complete[0]->GetVertices().Num());
		if (Actual[0]->GetVertices().Num() == Complete[0]->GetVertices().Num())
		{
			for (int32 Index = 0; Index < Actual[0]->GetVertices().Num(); ++Index)
			{
				const auto& A = Actual[0]->GetVertices()[Index]; const auto& B = Complete[0]->GetVertices()[Index];
				TestTrue(TEXT("Exact snapshot vertex attributes"), A.Position == B.Position && A.TangentX == B.TangentX && A.TangentZ == B.TangentZ && A.Color == B.Color);
			}
		}
		for (int32 Index = 0; Index < FMath::Min(Actual[0]->GetIndices().Num(), Complete[0]->GetIndices().Num()); ++Index)
		{
			TestEqual(TEXT("Exact snapshot index order"), Actual[0]->GetIndices()[Index], Complete[0]->GetIndices()[Index]);
		}
		const uint64 Revision = Actual[0]->GetGeometryRevision();
		Preview.QueueUpdate(*World->PersistentLevel, Expected, &Dirty);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 1.0);
		Preview.CancelUpdate();
		TestTrue(TEXT("Cancel discards active/waiting results without a callback"), !Preview.HasQueuedUpdates() && Preview.GetQueuedTileCount() == 0);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		TestEqual(TEXT("Cancelled output never replaces restored geometry"), Actual[0]->GetGeometryRevision(), Revision);
		Preview.QueueUpdate(*World->PersistentLevel, Visualization);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		TestTrue(TEXT("An empty latest snapshot removes the old fill"), EditPreviewComponents(Preview).IsEmpty() && Preview.IsReadyFor(World->PersistentLevel));
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshQueuedEditPreviewBatchTest,
	"ComposableCameraSystem.Editor.MeshCamera.QueuedEditPreviewBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshQueuedEditPreviewBatchTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Batch fixture World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
		FComposableCameraMeshSurfaceAuthoringData Data;
		for (int32 Tile = 1; Tile <= 4; ++Tile) { AddEditPreviewTriangle(Data, Layers[0].LayerId, Tile * 1000); }
		FResolvedSurfaceVisualization Visualization; BuildAuthoringVisualization(Data, Layers, Visualization);
		FMeshLayerEditPreview Preview, Reference;
		Reference.Update(*World->PersistentLevel, FTransform::Identity, Visualization, Layers);
		Preview.QueueUpdate(*World->PersistentLevel, Visualization);
		TestEqual(TEXT("Fixture spans four spatial tiles"), Preview.GetWaitingTileCount(), 4);
		TestFalse(TEXT("Finite call dispatches the batch without waiting"), Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 1.0));
		TestEqual(TEXT("All four nearest tiles start together instead of waiting extra editor frames"), Preview.GetWaitingTileCount(), 0);
		TestEqual(TEXT("Worker owns all four complete snapshots"), Preview.GetQueuedTileCount(), 4);
		Visualization.Reset(); Data.Reset();
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		const auto Actual = EditPreviewComponents(Preview), Expected = EditPreviewComponents(Reference);
		TestEqual(TEXT("Batch preserves every spatial tile"), Actual.Num(), Expected.Num());
		TestTrue(TEXT("Batch preserves total colored area"), FMath::IsNearlyEqual(EditPreviewArea(Actual), EditPreviewArea(Expected), 1.e-6));
		for (const auto* Component : Actual)
		{
			const auto* Match = Expected.FindByPredicate([Component](const auto* Other)
			{
				return !Other->GetVertices().IsEmpty() && !Component->GetVertices().IsEmpty()
					&& Other->GetVertices()[0].Position == Component->GetVertices()[0].Position;
			});
			if (!TestNotNull(TEXT("Corresponding exact tile exists"), Match)) { continue; }
			const auto A = Component->GetVertices(), B = (*Match)->GetVertices();
			TestTrue(TEXT("Batch retains exact topology and linear material color"), Component->GetIndices().Num() == (*Match)->GetIndices().Num()
				&& Component->GetFillColor() == (*Match)->GetFillColor());
			if (!TestEqual(TEXT("Batch retains vertex count"), A.Num(), B.Num())) { continue; }
			for (int32 Vertex = 0; Vertex < A.Num(); ++Vertex)
			{
				TestTrue(TEXT("Batch retains exact geometry and normal attributes"), A[Vertex].Position == B[Vertex].Position
					&& A[Vertex].TangentX == B[Vertex].TangentX && A[Vertex].TangentZ == B[Vertex].TangentZ && A[Vertex].Color == B[Vertex].Color);
			}
			for (int32 Index = 0; Index < FMath::Min(Component->GetIndices().Num(), (*Match)->GetIndices().Num()); ++Index)
			{
				TestEqual(TEXT("Batch retains index order"), Component->GetIndices()[Index], (*Match)->GetIndices()[Index]);
			}
		}
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshDocumentPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.RestoredDocumentPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshDocumentPreviewTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transactions exist"), GEditor)
		|| !TestFalse(TEXT("No unrelated transaction is active"), GEditor->IsTransactionActive())) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Restoration fixture World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		if (!TestNotNull(TEXT("Restoration fixture material exists"), GEngine ? GEngine->GeomMaterial.Get() : nullptr)) { return false; }
		FComposableCameraMeshLayerEdMode Mode;
		Mode.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Mode.Settings->Layers.AddDefaulted(); Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers();
		Mode.Settings->Layers[0].DebugColor = FLinearColor(0.13791f, 0.54321f, 0.27123f, 0.32781f);
		Mode.Settings->Layers[1].DebugColor = FLinearColor(0.3f, 0.2f, 0.7f, 0.9f);
		AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, -1100.0);
		AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[1].LayerId, -1090.0);
		AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, 1000.0);
		Mode.Settings->TouchDocument(); Mode.CaptureSavedDocument();
		const auto Saved = Mode.Settings->WorkingData;
		FResolvedSurfaceVisualization Expected; BuildAuthoringVisualization(Saved, Mode.Settings->Layers, Expected);
		FMeshLayerEditPreview Reference; Reference.Update(*World->PersistentLevel, FTransform::Identity, Expected, Mode.Settings->Layers);
		FEngineShowFlags Flags(ESFIM_Editor);
		FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(nullptr, World->Scene, Flags).SetTime(FGameTime()));
		FSceneViewInitOptions Options; Options.SetViewRectangle(FIntRect(0, 0, 64, 64)); Options.ViewFamily = &Family;
		FSceneView View(Options); FEditPreviewCountingPDI PDI(&View);
		Mode.Render(&View, nullptr, &PDI);
		TestTrue(TEXT("First Render never resolves, builds the index or uploads full fill"), Mode.bVisualizationDirty
			&& !Mode.AuthoringIndex->IsCurrent(Saved) && !Mode.EditPreview->GetActor() && PDI.Meshes == 0 && PDI.Resources == 0);
		auto Drain = [&]()
		{
			const double Deadline = FPlatformTime::Seconds() + 10.0;
			while (Mode.bVisualizationDirty && FPlatformTime::Seconds() < Deadline)
			{
				Mode.AdvanceDocumentPreview();
				Mode.AdvancePainting(MAX_int32, 0.001);
				if (Mode.bVisualizationDirty) { FPlatformProcess::Sleep(0.001f); }
			}
			if (Mode.bVisualizationDirty) { return false; }
			Mode.EditPreview->AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Mode.Settings->Layers, 0.0);
			return Mode.EditPreview->IsReadyFor(World->PersistentLevel) && !Mode.bEditPreviewDirty;
		};
		Mode.AdvanceDocumentPreview();
		TestTrue(TEXT("Opening schedules native work without an initial upload"), Mode.DocumentBuild->HasPending() && !Mode.EditPreview->GetActor());
		Mode.LostFocus(nullptr, nullptr);
		TestTrue(TEXT("Panel focus changes preserve the in-progress document load"), Mode.DocumentBuild->HasPending());
		Mode.Render(&View, nullptr, &PDI);
		TestEqual(TEXT("Waiting Render submits no fallback full mesh"), PDI.Meshes, 0);
		if (!TestTrue(TEXT("Stationary progression completes without hover or Render"), Drain())) { return false; }
		TestTrue(TEXT("Background index is installed for subsequent authoring"), Mode.AuthoringIndex->IsCurrent(Saved));
		const auto Actual = EditPreviewComponents(*Mode.EditPreview), Original = EditPreviewComponents(Reference);
		TestEqual(TEXT("Prepared publication retains tile/Layer component count"), Actual.Num(), Original.Num());
		for (const auto* Component : Actual)
		{
			const auto* Match = Original.FindByPredicate([Component](const auto* Candidate)
			{
				return Component->TranslucencySortPriority == Candidate->TranslucencySortPriority
					&& Component->CalcBounds(FTransform::Identity).Origin.Equals(Candidate->CalcBounds(FTransform::Identity).Origin, 1.e-5);
			});
			if (!TestNotNull(TEXT("Every prepared component matches the original full publisher"), Match)) { return false; }
			TestTrue(TEXT("Linear color, clamped alpha and index count are identical"), Component->GetFillColor() == (*Match)->GetFillColor()
				&& Component->GetIndices().Num() == (*Match)->GetIndices().Num());
			TestTrue(TEXT("Resident full worker keeps exact oriented fan triangles, heights, normals, color and UVs"), SameEditPreviewTriangles(*Component, **Match));
		}
		const double SavedArea = EditPreviewArea(Actual);
		Mode.Settings->WorkingData.Reset(); Mode.Settings->TouchDocument(); Mode.RefreshDocumentState();
		Mode.AdvanceDocumentPreview();
		TestTrue(TEXT("Restoration can cancel a full rebuild while pending"), Mode.DocumentBuild->HasPending());
		TestTrue(TEXT("Discard restores saved source immediately"), Mode.DiscardWorkingData()
			&& Mode.Settings->WorkingData.Vertices == Saved.Vertices && Mode.Settings->WorkingData.Indices == Saved.Indices);
		TestFalse(TEXT("Discard retires the obsolete empty-document job"), Mode.DocumentBuild->HasPending());
		if (!TestTrue(TEXT("Discard rebuild completes without a camera move"), Drain())) { return false; }
		TestTrue(TEXT("Discard publication retains exact saved area"), FMath::IsNearlyEqual(EditPreviewArea(EditPreviewComponents(*Mode.EditPreview)), SavedArea, 1.e-4));
		GEditor->RegisterForUndo(&Mode);
		GEditor->UndoTransaction();
		TestTrue(TEXT("Undo of Discard restores the edited empty document"), Mode.Settings->WorkingData.Indices.IsEmpty());
		Mode.AdvanceDocumentPreview();
		TestTrue(TEXT("Undo defers rebuilding to native work"), Mode.DocumentBuild->HasPending());
		GEditor->RedoTransaction();
		TestTrue(TEXT("Redo cancels obsolete Undo work and restores saved source"), !Mode.DocumentBuild->HasPending()
			&& Mode.Settings->WorkingData.Indices == Saved.Indices);
		GEditor->UnregisterForUndo(&Mode);
		if (!TestTrue(TEXT("Redo final fill completes"), Drain())) { return false; }
		Mode.Settings->WorkingData.Reset(); Mode.Settings->TouchDocument(); Mode.RefreshDocumentState();
		if (!TestTrue(TEXT("Empty restored document completes"), Drain())) { return false; }
		TestTrue(TEXT("Empty prepared update removes every stale tile"), EditPreviewComponents(*Mode.EditPreview).IsEmpty());
		Mode.Settings->WorkingData = Saved; Mode.Settings->TouchDocument(); Mode.RefreshDocumentState(); Mode.AdvanceDocumentPreview();
		Mode.BeginStroke();
		TestTrue(TEXT("Brush preserves resident loading without starting a publication stream"), Mode.bPainting
			&& Mode.DocumentBuild->IsResident() && !Mode.EditPreview->HasQueuedUpdates());
		Mode.FinishStroke(true);
		Mode.AdvanceDocumentPreview(); Mode.RemoveOrphanedTriangles();
		TestTrue(TEXT("Save compaction cancels snapshots even without a revision change"), !Mode.DocumentBuild->HasPending()
			&& Mode.bVisualizationDirty && Mode.bEditPreviewDirty && !Mode.EditPreview->HasQueuedUpdates());
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshProgressiveEditPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.ProgressiveEditPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProgressiveEditPreviewTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Progressive fixture World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		if (!TestNotNull(TEXT("Progressive fixture material exists"), GEngine ? GEngine->GeomMaterial.Get() : nullptr)) { return false; }
		TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
		Layers[0].DebugColor = FLinearColor(0.14f, 0.54f, 0.27f, 0.33f);
		FComposableCameraMeshSurfaceAuthoringData Data;
		Data.Vertices.Append({FVector3f(0, 0, 0), FVector3f(300, 0, 15), FVector3f(0, 300, 30)});
		Data.Indices.Append({0, 1, 2}); Data.TriangleLayerIds.Add(Layers[0].LayerId);
		AddEditPreviewTriangle(Data, Layers[0].LayerId, 1000);
		FResolvedSurfaceVisualization Expected; BuildAuthoringVisualization(Data, Layers, Expected);
		FMeshLayerEditPreview Preview, Reference;
		Reference.Update(*World->PersistentLevel, FTransform::Identity, Expected, Layers);
		FMeshLayerDocumentBuild Build; const FGuid Revision = FGuid::NewGuid();
		Build.Start(Data, Layers, Revision, nullptr, FVector2D(110, 110));
		FPreparedEditPreview First;
		const double Deadline = FPlatformTime::Seconds() + 10.0;
		while (!Build.TakeTile(Revision, First) && FPlatformTime::Seconds() < Deadline) { FPlatformProcess::Sleep(0.001f); }
		if (!TestFalse(TEXT("First region arrives before terminal document consumption"), First.Tiles.IsEmpty())) { return false; }
		TestTrue(TEXT("First delivered region belongs to the captured nearby view tile"), First.Tiles.Num() == 1 && First.Tiles[0].Tile == FIntPoint(0, 0));
		const FIntPoint FirstKey = First.Tiles[0].Tile;
		Preview.QueuePreparedUpdate(*World->PersistentLevel, MoveTemp(First), false);
		TestFalse(TEXT("Publication never waits for future document regions, including an explicit drain"),
			Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0));
		const auto FirstComponents = EditPreviewComponents(Preview);
		if (!TestFalse(TEXT("The first exact region is visible while the document remains attached"), FirstComponents.IsEmpty())) { return false; }
		const double FirstArea = EditPreviewArea(FirstComponents);
		TestTrue(TEXT("Initial mesh is a small real region, not a wait for whole-document assembly"), Build.HasPending()
			&& FirstArea > 0 && FirstArea < EditPreviewArea(EditPreviewComponents(Reference)) && Preview.IsReadyFor(World->PersistentLevel));
		const uint64 FirstRevision = FirstComponents[0]->GetGeometryRevision();
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		TestTrue(TEXT("An idle open stream retains the first fill without rebuilding or clearing it"), Preview.HasQueuedUpdates()
			&& FirstComponents[0]->GetGeometryRevision() == FirstRevision);
		FDocumentPreviewResult Result; bool bComplete = false, bExpandedFirst = false;
		while (!bComplete && FPlatformTime::Seconds() < Deadline)
		{
			FPreparedEditPreview Tile;
			while (Build.TakeTile(Revision, Tile))
			{
				for (const auto& Part : Tile.Tiles) { bExpandedFirst |= Part.Tile == FirstKey; }
				Preview.QueuePreparedUpdate(*World->PersistentLevel, MoveTemp(Tile), false);
			}
			Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
			bComplete = Build.Take(Revision, Result);
			if (!bComplete) { FPlatformProcess::Sleep(0.001f); }
		}
		if (!TestTrue(TEXT("All final regions and full source cache complete"), bComplete)) { return false; }
		FPreparedEditPreview Complete; Complete.CellSize = Result.Visualization.CellSize;
		Preview.QueuePreparedUpdate(*World->PersistentLevel, MoveTemp(Complete));
		TestTrue(TEXT("Terminal publication closes the stream"), Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0));
		const auto Actual = EditPreviewComponents(Preview), Original = EditPreviewComponents(Reference);
		TestTrue(TEXT("Remainder expands the first tile without erasing its initial region"), bExpandedFirst
			&& FMath::IsNearlyEqual(EditPreviewArea(Actual), EditPreviewArea(Original), 1.e-4));
		TestEqual(TEXT("Progressive publication retains original tile/component count"), Actual.Num(), Original.Num());
		for (const auto* Component : Actual)
		{
			const auto* Match = Original.FindByPredicate([Component](const auto* Candidate)
			{
				return Component->CalcBounds(FTransform::Identity).Origin.Equals(Candidate->CalcBounds(FTransform::Identity).Origin, 1.e-5);
			});
			if (!TestNotNull(TEXT("Corresponding final exact tile exists"), Match)) { return false; }
			TestTrue(TEXT("Final regions preserve every oriented triangle, normal, color and UV"),
				SameEditPreviewTriangles(*Component, **Match) && Component->GetFillColor() == (*Match)->GetFillColor());
		}
		FPreparedEditPreview Empty; Empty.CellSize = Expected.CellSize;
		Preview.QueuePreparedUpdate(*World->PersistentLevel, MoveTemp(Empty));
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		TestTrue(TEXT("An empty completed generation removes every older streamed tile"), EditPreviewComponents(Preview).IsEmpty());
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshRevisionPreviewHistoryTest,
	"ComposableCameraSystem.Editor.MeshCamera.RevisionEditPreviewHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshRevisionPreviewHistoryTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("History fixture World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.SetNum(2);
		for (auto& Layer : Layers) { Layer.LayerId = FGuid::NewGuid(); }
		Layers[0].DebugColor = FLinearColor(0.13791f, 0.54321f, 0.27123f, 0.32781f);
		Layers[1].DebugColor = FLinearColor(0.3f, 0.2f, 0.7f, 0.9f);
		FComposableCameraMeshSurfaceAuthoringData Base, Edited;
		AddEditPreviewTriangle(Base, Layers[0].LayerId, 0); AddEditPreviewTriangle(Base, Layers[0].LayerId, 1000);
		AddEditPreviewTriangle(Edited, Layers[1].LayerId, 15); AddEditPreviewTriangle(Edited, Layers[0].LayerId, 1000);
		FResolvedSurfaceVisualization Original, Changed; BuildAuthoringVisualization(Base, Layers, Original); BuildAuthoringVisualization(Edited, Layers, Changed);
		FMeshLayerEditPreview Preview, Reference;
		if (!TestTrue(TEXT("Base publication succeeds"), Preview.Update(*World->PersistentLevel, FTransform::Identity, Original, Layers))) { return false; }
		Reference.Update(*World->PersistentLevel, FTransform::Identity, Original, Layers);
		const FGuid Saved = FGuid::NewGuid(), EditedRevision = FGuid::NewGuid(), EmptyRevision = FGuid::NewGuid();
		TestTrue(TEXT("Complete checkpoint is remembered without source triangles"), Preview.RememberRevision(Saved, Saved));
		const auto BaseComponents = EditPreviewComponents(Preview);
		const auto* Far = BaseComponents.FindByPredicate([](const auto* Component) { return Component->GetVertices()[0].Position.X > 900; });
		if (!TestNotNull(TEXT("Unchanged remote tile exists"), Far)) { return false; }
		auto* Remote = *Far; const uint64 RemoteRevision = Remote->GetGeometryRevision();
		const auto RemoteGeometry = Remote->GetSharedGeometry();
		const auto* Near = BaseComponents.FindByPredicate([](const auto* Component) { return Component->GetVertices()[0].Position.X < 100; });
		if (!TestNotNull(TEXT("Checkpoint local tile exists"), Near)) { return false; }
		const auto NearGeometry = (*Near)->GetSharedGeometry();
		Preview.Update(*World->PersistentLevel, FTransform::Identity, Changed, Layers);
		TestTrue(TEXT("Edited revision is remembered"), Preview.RememberRevision(EditedRevision, Saved));
		TestTrue(TEXT("Restore only queues the changed spatial tile"), Preview.QueueRestoreRevision(*World->PersistentLevel, Saved)
			&& Preview.IsRestoringRevision() && Preview.GetQueuedTileCount() == 1);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		TestTrue(TEXT("Remote buffers and their component revision survive restoration"), Remote->GetSharedGeometry() == RemoteGeometry
			&& Remote->GetGeometryRevision() == RemoteRevision);
		const auto Actual = EditPreviewComponents(Preview), Expected = EditPreviewComponents(Reference);
		TestEqual(TEXT("Deleted/new Layer components restore to checkpoint count"), Actual.Num(), Expected.Num());
		for (const auto* Component : Actual)
		{
			const auto* Match = Expected.FindByPredicate([Component](const auto* Candidate)
			{ return Component->CalcBounds(FTransform::Identity).Origin.Equals(Candidate->CalcBounds(FTransform::Identity).Origin, 1.e-5); });
			if (!TestNotNull(TEXT("Checkpoint tile exists"), Match)) { return false; }
			TestTrue(TEXT("Restoration keeps exact fans, slope, normals, UVs and linear alpha"), SameEditPreviewTriangles(*Component, **Match)
				&& Component->GetFillColor() == (*Match)->GetFillColor());
			TestTrue(TEXT("Original immutable buffers are reused directly"), Component->GetSharedGeometry()
				== (Component->GetVertices()[0].Position.X < 100 ? NearGeometry : RemoteGeometry));
		}
		TestTrue(TEXT("Redo can restore the remembered edited revision"), Preview.QueueRestoreRevision(*World->PersistentLevel, EditedRevision));
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		FResolvedSurfaceVisualization Empty; BuildAuthoringVisualization({}, Layers, Empty);
		Preview.Update(*World->PersistentLevel, FTransform::Identity, Empty, Layers);
		TestTrue(TEXT("Empty display is a valid remembered revision"), Preview.RememberRevision(EmptyRevision, Saved));
		Preview.QueueRestoreRevision(*World->PersistentLevel, EditedRevision);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		Preview.QueueRestoreRevision(*World->PersistentLevel, EmptyRevision);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		TestTrue(TEXT("Empty restoration clears old mesh without any document worker"), EditPreviewComponents(Preview).IsEmpty());
		Preview.QueueRestoreRevision(*World->PersistentLevel, EditedRevision);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		FComposableCameraMeshSurfaceAuthoringData BothChanged, LatestSource;
		AddEditPreviewTriangle(BothChanged, Layers[1].LayerId, 15); AddEditPreviewTriangle(BothChanged, Layers[1].LayerId, 1000);
		AddEditPreviewTriangle(LatestSource, Layers[0].LayerId, 0); AddEditPreviewTriangle(LatestSource, Layers[0].LayerId, 1000);
		AddEditPreviewTriangle(LatestSource, Layers[1].LayerId, 80);
		FResolvedSurfaceVisualization BothVisualization, LatestVisualization;
		BuildAuthoringVisualization(BothChanged, Layers, BothVisualization); BuildAuthoringVisualization(LatestSource, Layers, LatestVisualization);
		Preview.Update(*World->PersistentLevel, FTransform::Identity, BothVisualization, Layers);
		Preview.QueueRestoreRevision(*World->PersistentLevel, Saved);
		TestEqual(TEXT("Both local and distant restore tiles are pending"), Preview.GetQueuedTileCount(), 2);
		const FBox2D LatestDirty(FVector2D(0, 0), FVector2D(140, 60));
		Preview.QueueUpdate(*World->PersistentLevel, LatestVisualization, &LatestDirty);
		TestEqual(TEXT("A local stroke supersedes its restore tile while retaining the distant restore"), Preview.GetQueuedTileCount(), 2);
		Preview.LaunchQueuedTileWork(Layers);
		TestEqual(TEXT("Stroke worker starts before untouched restore tiles finish publishing"), Preview.GetWaitingTileCount(), 0);
		Preview.AdvanceQueuedUpdates(*World->PersistentLevel, FTransform::Identity, Layers, 0.0);
		Reference.Update(*World->PersistentLevel, FTransform::Identity, LatestVisualization, Layers);
		const auto LatestActual = EditPreviewComponents(Preview), LatestExpected = EditPreviewComponents(Reference);
		TestEqual(TEXT("Immediate local editing preserves all restored distant Layer rows"), LatestActual.Num(), LatestExpected.Num());
		for (const auto* Component : LatestActual)
		{
			const auto* Match = LatestExpected.FindByPredicate([Component](const auto* Candidate)
			{ return Component->TranslucencySortPriority == Candidate->TranslucencySortPriority
				&& Component->CalcBounds(FTransform::Identity).Origin.Equals(Candidate->CalcBounds(FTransform::Identity).Origin, 1.e-5); });
			if (!TestNotNull(TEXT("Restoration plus stroke reference tile exists"), Match)) { return false; }
			TestTrue(TEXT("Merged restore/stroke queue keeps exact final geometry"), SameEditPreviewTriangles(*Component, **Match));
		}
		for (int32 Index = 0; Index < 35; ++Index) { Preview.RememberRevision(FGuid::NewGuid(), Saved); }
		TestFalse(TEXT("Old noncheckpoint revisions can be evicted"), Preview.QueueRestoreRevision(*World->PersistentLevel, EditedRevision));
		TestTrue(TEXT("Pinned Save checkpoint survives history eviction"), Preview.QueueRestoreRevision(*World->PersistentLevel, Saved));
		Preview.CancelUpdate();
		TestFalse(TEXT("An unknown revision never cancels into a false successful restoration"), Preview.QueueRestoreRevision(*World->PersistentLevel, FGuid::NewGuid()));
		TestFalse(TEXT("Canceled restoration has no remaining publication"), Preview.HasQueuedUpdates());
		Preview.Reset();
		TestFalse(TEXT("Reset/Level lifecycle removes all historical identities"), Preview.QueueRestoreRevision(*World->PersistentLevel, Saved));
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshRestorationDisplayTest,
	"ComposableCameraSystem.Editor.MeshCamera.ImmediateRestorationDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshRestorationDisplayTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Undo fixture editor exists"), GEditor)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Restoration display fixture World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		FComposableCameraMeshLayerEdMode Mode; Mode.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Mode.Settings->Layers.SetNum(2); Mode.Settings->NormalizeLayers();
		AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, 0);
		AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, 1000);
		Mode.Settings->TouchDocument(); Mode.CaptureSavedDocument();
		const auto Saved = Mode.Settings->WorkingData;
		BuildAuthoringVisualization(Saved, Mode.Settings->Layers, Mode.Visualization); Mode.bVisualizationDirty = false;
		if (!TestTrue(TEXT("Initial exact checkpoint is displayed/remembered"), Mode.UpdateCachedPreview())) { return false; }
		const auto Components = EditPreviewComponents(*Mode.EditPreview);
		const auto* Far = Components.FindByPredicate([](const auto* Component) { return Component->GetVertices()[0].Position.X > 900; });
		if (!TestNotNull(TEXT("Remote checkpoint component exists"), Far)) { return false; }
		auto* Remote = *Far; const uint64 RemoteRevision = Remote->GetGeometryRevision();
		{
			const FScopedTransaction Transaction(NSLOCTEXT("MeshCameraTests", "RestorationDisplayEdit", "Edit Mesh Layer Test"));
			Mode.Settings->Modify(); Mode.Settings->WorkingData.Reset();
			AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[1].LayerId, 15);
			AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, 1000);
			Mode.Settings->TouchDocument();
		}
		const FGuid EditedRevision = Mode.Settings->DocumentRevision;
		BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Visualization);
		Mode.bVisualizationDirty = false; Mode.UpdateCachedPreview();
		GEditor->RegisterForUndo(&Mode);
		auto Publish = [&]() { Mode.AdvancePainting(MAX_int32, 0.0); };
		auto CheckNearLayer = [&](int32 Layer)
		{
			const auto Visible = EditPreviewComponents(*Mode.EditPreview);
			const auto* Near = Visible.FindByPredicate([](const auto* Component) { return Component->GetVertices()[0].Position.X < 100; });
			return Near && (*Near)->TranslucencySortPriority == Layer && Remote->GetGeometryRevision() == RemoteRevision;
		};
		Mode.PrepareUndo(); GEditor->UndoTransaction();
		TestTrue(TEXT("Undo restores an editable base and queues exact buffers without any cache rebuild"), Mode.bHistoricalPreview && !Mode.bVisualizationDirty
			&& Mode.EditPreview->IsRestoringRevision() && !Mode.DocumentBuild->HasPending() && Mode.Settings->WorkingData.Vertices == Saved.Vertices
			&& Mode.Settings->WorkingData.TriangleLayerIds == Saved.TriangleLayerIds);
		Publish();
		TestTrue(TEXT("Undo changes visible Layer immediately, before background work is scheduled"), CheckNearLayer(0) && !Mode.DocumentBuild->HasPending());
		Mode.AdvanceDocumentPreview();
		TestTrue(TEXT("Restored coverage/index require no background reconstruction"), !Mode.DocumentBuild->HasPending()
			&& Mode.EditingCheckpoint && Mode.AuthoringIndex->IsCurrent(Saved));
		GEditor->RedoTransaction(); Publish();
		TestTrue(TEXT("Redo immediately restores edited display"), CheckNearLayer(1)
			&& !Mode.DocumentBuild->HasPending() && Mode.Settings->DocumentRevision == EditedRevision);
		TestTrue(TEXT("Discard restores the checkpoint source"), Mode.DiscardWorkingData()); Publish();
		TestTrue(TEXT("Discard display and editable coverage are both ready"), CheckNearLayer(0) && !Mode.bVisualizationDirty && !Mode.DocumentBuild->HasPending());
		GEditor->UndoTransaction(); Publish();
		TestTrue(TEXT("Undo of Discard immediately restores the edited mesh too"), CheckNearLayer(1) && Mode.Settings->DocumentRevision == EditedRevision);
		GEditor->RedoTransaction(); Publish();
		TestTrue(TEXT("Repeated restoration remains exact"), CheckNearLayer(0));
		GEditor->UnregisterForUndo(&Mode);
		const uint64 RestoredRevision = EditPreviewComponents(*Mode.EditPreview)[0]->GetGeometryRevision();
		Mode.AdvanceDocumentPreview();
		const double Deadline = FPlatformTime::Seconds() + 10.0;
		while (Mode.bVisualizationDirty && FPlatformTime::Seconds() < Deadline)
		{ Mode.AdvanceDocumentPreview(); Publish(); if (Mode.bVisualizationDirty) { FPlatformProcess::Sleep(0.001f); } }
		TestTrue(TEXT("Background completion installs current source index"), !Mode.bVisualizationDirty && Mode.AuthoringIndex->IsCurrent(Saved));
		TestTrue(TEXT("Background completion never replaces complete historical buffers with partial tiles"), CheckNearLayer(0)
			&& EditPreviewComponents(*Mode.EditPreview)[0]->GetGeometryRevision() == RestoredRevision);
		Mode.Settings->TouchDocument(); Mode.PostUndo(true);
		TestFalse(TEXT("PostUndo never relabels older visible buffers as an unremembered restored revision"), Mode.bHistoricalPreview);
		return true;
	}();
	World->DestroyWorld(false); return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshRestoredStrokeTest,
	"ComposableCameraSystem.Editor.MeshCamera.RestoredPreviewImmediateStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshRestoredStrokeTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transactions exist"), GEditor)
		|| !TestFalse(TEXT("No unrelated transaction is active"), GEditor->IsTransactionActive())) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Restored-stroke fixture exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AActor* Floor = World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Floor exists"), Floor) || !TestNotNull(TEXT("Floor mesh exists"), Cube)) { return false; }
		auto* Mesh = NewObject<UStaticMeshComponent>(Floor);
		Floor->AddInstanceComponent(Mesh); Floor->SetRootComponent(Mesh);
		Mesh->SetMobility(EComponentMobility::Movable); Mesh->SetStaticMesh(Cube);
		Mesh->SetRelativeScale3D(FVector(10, 10, 0.1)); Floor->SetActorLocation(FVector(0, 0, -Cube->GetBoundingBox().Max.Z * 0.1));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Block); Mesh->RegisterComponent();
		FRestoredStrokeFixtureModeTools Owner(*World);
		FComposableCameraMeshLayerEdMode Mode; Mode.Owner = &Owner; Mode.TargetLevel = World->PersistentLevel;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Mode.Settings->Layers.SetNum(2); Mode.Settings->NormalizeLayers();
		for (auto& Layer : Mode.Settings->Layers) { Layer.TraceChannel = ECC_Camera; }
		for (double X : {0.0, 1000.0, 2000.0}) { AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, X); }
		Mode.Settings->WorkingData.Vertices.Add(FVector3f(-9000, 0, -50)); // Save compaction must invalidate index counts only.
		Mode.Settings->TouchDocument(); Mode.CaptureSavedDocument();
		const auto SavedSource = Mode.Settings->WorkingData; const FGuid SavedRevision = Mode.SavedRevision;
		BuildAuthoringVisualization(SavedSource, Mode.Settings->Layers, Mode.Visualization); Mode.bVisualizationDirty = false;
		if (!TestTrue(TEXT("Saved editable checkpoint is published"), Mode.UpdateCachedPreview())) { return false; }
		const auto SavedCheckpoint = Mode.EditingCheckpoint;
		if (!TestTrue(TEXT("Checkpoint moves exact cells and keeps a current broad phase"), SavedCheckpoint
			&& !SavedCheckpoint->Visualization.Cells.IsEmpty() && SavedCheckpoint->Index.IsCurrent(SavedSource)
			&& Mode.Visualization.Cells.IsEmpty())) { return false; }
		const int32 SavedCells = SavedCheckpoint->Visualization.Cells.Num();
		const auto SavedPrepared = PrepareEditPreview(SavedCheckpoint->Visualization, Mode.Settings->Layers);
		{
			const FScopedTransaction Transaction(NSLOCTEXT("MeshCameraTests", "RestoredStrokeSeed", "Edit Restored Stroke Fixture"));
			Mode.Settings->Modify(); Mode.Settings->WorkingData.Reset();
			AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[1].LayerId, 15);
			AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[1].LayerId, 1000);
			AddEditPreviewTriangle(Mode.Settings->WorkingData, Mode.Settings->Layers[0].LayerId, 2000);
			Mode.Settings->TouchDocument();
		}
		BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Visualization);
		Mode.bVisualizationDirty = false; Mode.UpdateCachedPreview();
		if (!TestTrue(TEXT("Discard restores source"), Mode.DiscardWorkingData())) { return false; }
		TestTrue(TEXT("Discard restores the editable base before publishing even one tile"), Mode.EditingCheckpoint == SavedCheckpoint
			&& !Mode.bVisualizationDirty && Mode.AuthoringIndex->IsCurrent(SavedSource)
			&& Mode.EditPreview->IsRestoringRevision() && !Mode.DocumentBuild->HasPending());
		Mode.bHasHoverHit = true; Mode.HoverHit.ImpactNormal = FVector::UpVector;
		Mode.Settings->ProjectionDistance = 100.0;
		auto Stroke = [&](bool bErase)
		{
			const auto Base = Mode.EditingCheckpoint;
			const int32 Before = Mode.Settings->WorkingData.TriangleLayerIds.Num();
			Mode.Settings->ActiveLayerIndex = bErase ? 0 : 1;
			Mode.Settings->Tool = bErase ? EComposableCameraMeshDrawTool::Erase : EComposableCameraMeshDrawTool::Brush;
			Mode.Settings->BrushRadius = bErase ? 15.0 : 150.0;
			Mode.HoverHit.ImpactPoint = bErase ? FVector(15, 15, 0) : FVector(65, 25, 0);
			const bool bRestoring = Mode.EditPreview->IsRestoringRevision();
			Mode.BeginStroke();
			TestTrue(TEXT("A new stroke retains unpublished restored tiles and accepts its editable base"), !Mode.bVisualizationDirty
				&& !Mode.DocumentBuild->HasPending() && (!bRestoring || Mode.EditPreview->IsRestoringRevision()));
			Mode.QueuePaintAtHover(nullptr); Mode.AdvancePainting(MAX_int32, 1.0);
			TestTrue(TEXT("Source completes while only local coverage is pending"), !Mode.StrokeTask && Mode.QueuedStamps.IsEmpty()
				&& Mode.StrokeCoverage->HasPending() && !Mode.DocumentBuild->HasPending());
			if (!bErase)
			{
				TestEqual(TEXT("First Brush snapshots only appended triangles, never the whole saved document"),
					Mode.StrokeCoverage->GetLastSnapshotTriangleCount(), Mode.Settings->WorkingData.TriangleLayerIds.Num() - Before);
			}
			Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released);
			const double Deadline = FPlatformTime::Seconds() + 10.0;
			while ((Mode.bPainting || Mode.StrokeTask || !Mode.QueuedStamps.IsEmpty()
				|| Mode.StrokeCoverage->HasPending() || Mode.EditPreview->HasQueuedUpdates()) && FPlatformTime::Seconds() < Deadline)
			{
				Mode.AdvancePainting(MAX_int32, 0.004);
				if (Mode.bPainting || Mode.StrokeCoverage->HasPending() || Mode.EditPreview->HasQueuedUpdates()) { FPlatformProcess::Sleep(0.001f); }
			}
			TestTrue(TEXT("Finite Tick progression finishes new mesh without a document rebuild"), !Mode.bPainting
				&& !Mode.StrokeCoverage->HasPending() && !Mode.EditPreview->HasQueuedUpdates() && !Mode.DocumentBuild->HasPending());
			TestTrue(TEXT("Worker retains its immutable editable base"), Base && Base->Revision.IsValid() && !Base->Visualization.Cells.IsEmpty());
			FResolvedSurfaceVisualization Expected; BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Expected);
			FMeshLayerEditPreview Reference; Reference.Update(*World->PersistentLevel, FTransform::Identity, Expected, Mode.Settings->Layers);
			const auto Actual = EditPreviewComponents(*Mode.EditPreview), Complete = EditPreviewComponents(Reference);
			TestEqual(TEXT("Restored stroke retains all distant tiles and Layer rows"), Actual.Num(), Complete.Num());
			for (const auto* Component : Actual)
			{
				const auto* Match = Complete.FindByPredicate([Component](const auto* Candidate)
				{ return Component->TranslucencySortPriority == Candidate->TranslucencySortPriority
					&& Component->CalcBounds(FTransform::Identity).Origin.Equals(Candidate->CalcBounds(FTransform::Identity).Origin, 1.e-5); });
				if (!TestNotNull(TEXT("Exact reference tile exists"), Match)) { return false; }
				TestTrue(TEXT("Brush/Erase matches full-resolution geometry, priority, slope, normals and color"), SameEditPreviewTriangles(*Component, **Match)
					&& Component->GetFillColor() == (*Match)->GetFillColor());
			}
			return true;
		};
		if (!Stroke(false)) { return false; } // No publication/AdvanceDocumentPreview between Discard and Brush.
		GEditor->RegisterForUndo(&Mode);
		GEditor->UndoTransaction();
		TestTrue(TEXT("Undo returns the same editable saved checkpoint"), Mode.Settings->DocumentRevision == SavedRevision
			&& Mode.EditingCheckpoint == SavedCheckpoint && !Mode.bVisualizationDirty);
		bool bStrokesValid = Stroke(true); // Undo -> immediate Erase.
		GEditor->UndoTransaction(); GEditor->RedoTransaction();
		TestTrue(TEXT("Redo restores an editable cache before the next Brush"), Mode.EditingCheckpoint
			&& Mode.EditingCheckpoint->Revision == Mode.Settings->DocumentRevision && !Mode.bVisualizationDirty);
		bStrokesValid &= Stroke(false); // Redo -> immediate Brush.
		GEditor->UnregisterForUndo(&Mode);
		TestTrue(TEXT("Repeated Discard restores the pinned checkpoint"), Mode.DiscardWorkingData());
		Mode.RemoveOrphanedTriangles();
		TestTrue(TEXT("Geometry-preserving Save compaction keeps coverage while invalidating source-index counts"), !Mode.bVisualizationDirty
			&& Mode.EditingCheckpoint == SavedCheckpoint && !Mode.AuthoringIndex->IsCurrent(Mode.Settings->WorkingData));
		bStrokesValid &= Stroke(true); // Discard -> immediate Erase.
		TestTrue(TEXT("Saved cells/index stay immutable across all branches"), SavedCheckpoint->Visualization.Cells.Num() == SavedCells
			&& SavedCheckpoint->Index.IsCurrent(SavedSource));
		const auto SavedAfter = PrepareEditPreview(SavedCheckpoint->Visualization, Mode.Settings->Layers);
		TestEqual(TEXT("Historical coverage retains exact tile count"), SavedAfter.Tiles.Num(), SavedPrepared.Tiles.Num());
		for (int32 Tile = 0; Tile < FMath::Min(SavedAfter.Tiles.Num(), SavedPrepared.Tiles.Num()); ++Tile)
		{
			for (const auto& Pair : SavedPrepared.Tiles[Tile].Geometry)
			{
				const auto* After = SavedAfter.Tiles[Tile].Geometry.Find(Pair.Key);
				if (!TestNotNull(TEXT("Historical Layer geometry remains present"), After)) { return false; }
				TestTrue(TEXT("Historical fan indices and vertex counts remain unchanged"), After->Indices == Pair.Value.Indices
					&& After->Vertices.Num() == Pair.Value.Vertices.Num());
				for (int32 Vertex = 0; Vertex < FMath::Min(After->Vertices.Num(), Pair.Value.Vertices.Num()); ++Vertex)
				{
					TestTrue(TEXT("Worker never modifies saved positions or normals"), After->Vertices[Vertex].Position == Pair.Value.Vertices[Vertex].Position
						&& After->Vertices[Vertex].TangentX == Pair.Value.Vertices[Vertex].TangentX && After->Vertices[Vertex].TangentZ == Pair.Value.Vertices[Vertex].TangentZ);
				}
			}
		}
		return bStrokesValid;
	}();
	World->DestroyWorld(false); return bResult;
}

#endif
