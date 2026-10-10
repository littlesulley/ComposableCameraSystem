// Copyright 2026 Sulley. All Rights Reserved.
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editor.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PreviewScene.h"
#include "PrimitiveDrawInterface.h"
#include "SceneView.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/SShotEditorViewport.h"
#include "Widgets/ComposableCameraShotSubjectGizmoUtils.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
class FSubjectGuidePDI final : public FPrimitiveDrawInterface
{
public:
	explicit FSubjectGuidePDI(const FSceneView* View = nullptr) : FPrimitiveDrawInterface(View) {}
	int32 Lines = 0, Points = 0;
	virtual bool IsHitTesting() override { return false; }
	virtual void SetHitProxy(HHitProxy*) override {}
	virtual void RegisterDynamicResource(FDynamicPrimitiveResource*) override {}
	virtual void AddReserveLines(uint8, int32, bool, bool) override {}
	virtual void DrawSprite(const FVector&, float, float, const FTexture*, const FLinearColor&, uint8, float, float, float, float, uint8, float) override {}
	virtual void DrawLine(const FVector&, const FVector&, const FLinearColor&, uint8, float, float, bool) override { ++Lines; }
	virtual void DrawTranslucentLine(const FVector&, const FVector&, const FLinearColor&, uint8, float, float, bool) override { ++Lines; }
	virtual void DrawPoint(const FVector&, const FLinearColor&, float, uint8) override { ++Points; }
	virtual int32 DrawMesh(const FMeshBatch&) override { return 0; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotEditorSubjectGizmosTest,
	"ComposableCameraSystem.ShotEditor.SubjectGizmos",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotEditorSubjectGizmosTest::RunTest(const FString&)
{
	using namespace ComposableCameraSystem::ShotSubjectGizmo;
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	FPreviewScene Scene(FPreviewScene::ConstructionValues{});
	const auto Spawn = [&](FVector Location)
	{
		AActor* Actor = Scene.GetWorld()->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root); Root->RegisterComponent(); Actor->SetActorLocation(Location);
		return Actor;
	};
	AActor* A = Spawn(FVector(100, 0, 0));
	AActor* B = Spawn(FVector(300, 50, 90));
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(B, TEXT("Pivot"));
	Component->SetupAttachment(B->GetRootComponent()); Component->RegisterComponent();
	Component->SetRelativeLocationAndRotation(FVector(20, 30, 40), FRotator(0, 90, 0));
	Component->Bounds = FBoxSphereBounds(Component->GetComponentLocation(), FVector(30, 40, 60), 80);
	TStrongObjectPtr<UComposableCameraShotAsset> Asset(NewObject<UComposableCameraShotAsset>(GetTransientPackage(), NAME_None, RF_Transactional));
	Asset->Shot.Targets.SetNum(2); Asset->Shot.Targets[0].Target.Actor = A;
	const auto CurrentSubject = [&]() -> FComposableCameraShotTarget& { return Asset->Shot.Targets[1]; };
	CurrentSubject().Target.Actor = B; CurrentSubject().Target.ComponentName = Component->GetFName();
	CurrentSubject().Target.bOffsetInLocalSpace = true; CurrentSubject().Target.Offset = FVector(10, 20, 30);
	CurrentSubject().BoundsShape = EShotTargetBoundsShape::ManualExtent; CurrentSubject().ManualBoundsExtent = FVector(50, 60, 70);
	CurrentSubject().BoundsContributionWeight = 0.f;
	auto Session = MakeShared<FComposableCameraShotAuthoringSession>(); Session->Bind(&Asset->Shot, Asset.Get()); Session->bUseLevelWorld = false;
	const auto Authoring = SNew(SShotEditorAuthoringPanel).Session(Session);
	Authoring->SelectSubjectsForTesting();
	const auto Parameters = SNew(SShotEditorParameterPanel).Session(Session).Section(EShotEditorParameterSection::Subject).SubjectIndex(1);
	const auto Preview = SNew(SShotEditorViewport);
	Preview->SetAuthoringSession(Session); Preview->SetActiveShot(&Asset->Shot, Asset.Get());
	const auto Client = Preview->GetClientForTesting();
	if (!TestTrue(TEXT("Production viewport exists"), Client.IsValid() && Client->Viewport)) return false;
	Client->bUseLevelWorld = false;
	FSubjectGeometry Geometry;
	TestTrue(TEXT("Subject component geometry resolves"), Resolve(CurrentSubject().Target, Geometry));
	FVector RuntimePoint; CurrentSubject().Target.ResolveWorldPoint(RuntimePoint);
	TestTrue(TEXT("Gizmo pivot equals actual runtime point"), Geometry.Pivot.Equals(RuntimePoint));
	TestTrue(TEXT("Local edit axis follows selected component"), Geometry.OffsetRotation.RotateVector(Axis(0)).Equals(FVector::RightVector));
	TestTrue(TEXT("Heading basis matches selected component"), Geometry.BasisRotation.Equals(Component->GetComponentQuat()));
	const auto Paint = [&]()
	{
		Client->bEffectiveShotCacheValid = false;
		FSubjectGuidePDI PDI; Client->DrawSubjectGuides(&PDI);
		return FIntPoint(PDI.Lines, PDI.Points);
	};
	const FIntPoint Manual = Paint();
	TestTrue(TEXT("Manual FOV / zero weight still displays authored bounds and pivots"), Manual.X > 4 && Manual.Y >= 4);
	CurrentSubject().BoundsShape = EShotTargetBoundsShape::None;
	const FIntPoint WithoutBounds = Paint();
	TestTrue(TEXT("None removes bounds, retaining subject pivots"), WithoutBounds.X < Manual.X && WithoutBounds.Y == Manual.Y);
	CurrentSubject().BoundsShape = EShotTargetBoundsShape::AutoFromComponentBounds;
	TestTrue(TEXT("Automatic component bounds are visible at Manual FOV / zero weight"), Paint().X > WithoutBounds.X);
	TestTrue(TEXT("Painting never writes authored transient bounds"), CurrentSubject().CachedAutoBoundsExtent.IsZero());
	CurrentSubject().BoundsShape = EShotTargetBoundsShape::ManualExtent;
	Authoring->SelectLookAtForTesting(false);
	TestTrue(TEXT("Other pages suppress subject-only guides at Manual FOV"), Paint() == FIntPoint::ZeroValue);
	Authoring->SelectCreateForTesting();
	TestTrue(TEXT("Create subjects display same geometry"), Paint() == Manual);
	Client->SetShowCompositionGuides(false);
	TestTrue(TEXT("Guides off hides all subject geometry"), Paint() == FIntPoint::ZeroValue);
	Client->SetShowCompositionGuides(true); Authoring->SelectSubjectsForTesting();
	{
		// Exercise production 3D drawing, excluding primitives owned by the native viewport.
		const auto BeforeAnchor = Asset->Shot.Placement.PlacementAnchor;
		const auto BeforeMode = Asset->Shot.Placement.Mode;
		const auto BeforeFOVMode = Asset->Shot.Lens.FOVMode;
		Asset->Shot.Placement.Mode = EShotPlacementMode::AnchorOrbit;
		Asset->Shot.Lens.FOVMode = EShotFOVMode::Manual;
		Authoring->SelectLookAtForTesting(false);
		FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(nullptr, Scene.GetWorld()->Scene, Client->EngineShowFlags).SetTime(FGameTime()));
		FSceneViewInitOptions Options; Options.SetViewRectangle(FIntRect(0, 0, 64, 64)); Options.ViewFamily = &Family;
		FSceneView View(Options);
		const auto PaintOrbitPoints = [&]()
		{
			Client->bEffectiveShotCacheValid = false;
			FSubjectGuidePDI Native(&View), Actual(&View);
			Client->FEditorViewportClient::Draw(&View, &Native);
			Client->Draw(&View, &Actual);
			return Actual.Points - Native.Points;
		};
		for (const EShotEditorMode Mode : { EShotEditorMode::Drag, EShotEditorMode::Free })
		{
			Client->SetMode(Mode);
			auto& Anchor = Asset->Shot.Placement.PlacementAnchor;
			Anchor.Mode = EShotAnchorMode::SingleTarget; Anchor.TargetIndex = 1;
			TestEqual(TEXT("Follow Orbit adds no 3D point for component/local-offset anchor"), PaintOrbitPoints(), 0);
			Anchor.Mode = EShotAnchorMode::WeightedWorldCentroid; Anchor.WeightedTargets.SetNum(2);
			Anchor.WeightedTargets[0].TargetIndex = 0; Anchor.WeightedTargets[0].Weight = 1.f;
			Anchor.WeightedTargets[1].TargetIndex = 1; Anchor.WeightedTargets[1].Weight = .5f;
			TestEqual(TEXT("Follow Orbit adds no 3D point for weighted anchor"), PaintOrbitPoints(), 0);
			Anchor.Mode = EShotAnchorMode::FixedWorldPosition; Anchor.WorldPosition = FVector::ZeroVector;
			TestEqual(TEXT("Follow Orbit adds no 3D point for fixed world origin"), PaintOrbitPoints(), 0);
			TestTrue(TEXT("Canvas orbit control remains available on Follow"), Client->ShowOrbitControl());
		}
		Client->SetMode(EShotEditorMode::Drag);
		Asset->Shot.Placement.PlacementAnchor = BeforeAnchor; Asset->Shot.Placement.Mode = BeforeMode;
		Asset->Shot.Lens.FOVMode = BeforeFOVMode;
		Client->bEffectiveShotCacheValid = false;
		Authoring->SelectSubjectsForTesting();
	}
	const auto MakeHit = [&](auto Type, int32 AxisIndex = 0, float Sign = 1.f)
	{
		FComposableCameraShotEditorViewportClient::FHandleScreenPosCache Hit;
		Hit.Type = Type; Hit.SubjectIndex = 1; Hit.SubjectCount = Asset->Shot.Targets.Num();
		Hit.AxisIndex = AxisIndex; Hit.FaceSign = Sign; Hit.PixelsPerUnit = FVector2D(2., -1.);
		Hit.SubjectRenderRect = Client->GetRenderRect();
		Hit.HitArea = FBox2D(FVector2D(90, 90), FVector2D(110, 110));
		Hit.SubjectIdentity.Capture(CurrentSubject().Target); Hit.ResolvedSubjectActor = B;
		Hit.AuthoredOffset = CurrentSubject().Target.Offset; Hit.AuthoredExtent = CurrentSubject().ManualBoundsExtent;
		return Hit;
	};
	using EHandle = FComposableCameraShotEditorViewportClient::EHandleType;
	const FVector BeforeOffset = CurrentSubject().Target.Offset;
	const FVector BeforeExtent = CurrentSubject().ManualBoundsExtent;
	const uint32 BeforeRows = Parameters->GetRebuildCountForTesting();
	int32 Commits = 0, Modified = 0, Structural = 0;
	Session->OnChanged = [&](bool bStructural) { if (bStructural) { ++Structural; Authoring->Refresh(); Parameters->RefreshSource(); } };
	const auto CommitHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([&](UObject* Object, FPropertyChangedEvent&) { if (Object == Asset.Get()) ++Commits; });
	const auto ModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddLambda([&](UObject* Object) { if (Object == Asset.Get()) ++Modified; });
	auto Hit = MakeHit(EHandle::SubjectOffsetAxis);
	Client->StartHandleDrag(Hit, 100, 100); Client->ApplyDragToShot(120, 90); Client->ApplyDragToShot(140, 80);
	TestTrue(TEXT("Local offset writes selected authored axis"), CurrentSubject().Target.Offset.Equals(BeforeOffset + FVector(20, 0, 0)));
	Resolve(CurrentSubject().Target, Geometry);
	TestTrue(TEXT("Local X drag moves pivot along component world Y"), Geometry.Pivot.Equals(RuntimePoint + FVector(0, 20, 0)));
	TestTrue(TEXT("Wheel cannot nest inside subject drag"), !Client->TryAdjustDistanceFromMouseWheel(true));
	TestEqual(TEXT("Live drag emits no host commits"), Commits, 0);
	Client->EndDrag(); Authoring->Tick(FGeometry(), 0., 0.f); Parameters->Tick(FGeometry(), 0., 0.f);
	TestEqual(TEXT("Subject release emits one host commit"), Commits, 1);
	TestEqual(TEXT("Subject gesture emits no OnObjectModified"), Modified, 0);
	TestEqual(TEXT("Subject gesture does not refresh source"), Structural, 0);
	TestEqual(TEXT("Subject native rows survive drag"), Parameters->GetRebuildCountForTesting(), BeforeRows);
	TestTrue(TEXT("One Undo restores entire offset gesture"), GEditor->UndoTransaction());
	TestTrue(TEXT("Offset restored"), CurrentSubject().Target.Offset.Equals(BeforeOffset));
	TestTrue(TEXT("Redo restores full offset gesture"), GEditor->RedoTransaction());
	TestTrue(TEXT("Offset redo value restored"), CurrentSubject().Target.Offset.Equals(BeforeOffset + FVector(20, 0, 0)));
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		for (const float Sign : { -1.f, 1.f })
		{
			Hit = MakeHit(EHandle::SubjectBoundsFace, AxisIndex, Sign);
			Client->StartHandleDrag(Hit, 100, 100); Client->ApplyDragToShot(100 + int32(20 * Sign), 100 - int32(10 * Sign)); Client->EndDrag();
			FVector Expected = BeforeExtent; Expected[AxisIndex] += 10.;
			TestTrue(TEXT("Either face increases only corresponding half-extent"), CurrentSubject().ManualBoundsExtent.Equals(Expected));
			TestTrue(TEXT("Bounds resize supports one-step Undo"), GEditor->UndoTransaction());
			TestTrue(TEXT("Bounds Undo restores all axes"), CurrentSubject().ManualBoundsExtent.Equals(BeforeExtent));
		}
	Hit = MakeHit(EHandle::SubjectBoundsFace);
	Client->StartHandleDrag(Hit, 100, 100); Client->ApplyDragToShot(-1000, 650); Client->EndDrag();
	TestEqual(TEXT("Extent cannot become negative"), CurrentSubject().ManualBoundsExtent.X, 0.);
	TestTrue(TEXT("Clamped bounds gesture supports Undo"), GEditor->UndoTransaction());
	Commits = 0; Hit = MakeHit(EHandle::SubjectOffsetAxis);
	Client->StartHandleDrag(Hit, 100, 100); Client->ApplyDragToShot(100, 100); Client->EndDrag();
	TestEqual(TEXT("Click without movement produces no commit"), Commits, 0);
	Client->CachedHandles.Reset(); Client->CachedHandles.Add(Hit);
	FComposableCameraShotEditorViewportClient::FHandleScreenPosCache Found;
	TestTrue(TEXT("Subject handle participates in production hit testing"), Client->HitTestHandles(100, 100, Found));
	Hit.SubjectRenderRect.Max += FIntPoint(1, 0);
	TestFalse(TEXT("Resize rejects a stale painted subject hit"), Client->IsSubjectHandleValid(Hit));
	Client->StartHandleDrag(Hit, 100, 100); TestFalse(TEXT("Resize-invalidated projection cannot start drag"), Client->IsEditingGesture());
	Hit = MakeHit(EHandle::SubjectOffsetAxis);
	Authoring->SelectLookAtForTesting(true);
	TestFalse(TEXT("Switching page rejects stale subject hits before repaint"), Client->HitTestHandles(100, 100, Found));
	Client->StartHandleDrag(Hit, 100, 100); TestFalse(TEXT("Hidden subject handle cannot start drag"), Client->IsEditingGesture());
	Authoring->SelectSubjectsForTesting();
	CurrentSubject().Target.bOffsetInLocalSpace = false;
	TestFalse(TEXT("Changing offset frame invalidates old axis hit"), Client->IsSubjectHandleValid(Hit));
	const FVector BeforeWorldOffset = CurrentSubject().Target.Offset;
	Hit = MakeHit(EHandle::SubjectOffsetAxis);
	Client->StartHandleDrag(Hit, 100, 100); Client->ApplyDragToShot(120, 90); Client->EndDrag();
	TestTrue(TEXT("World drag writes only the selected world-axis offset"), CurrentSubject().Target.Offset.Equals(BeforeWorldOffset + FVector(10, 0, 0)));
	TestTrue(TEXT("World offset edits retain world-axis semantics"), Resolve(CurrentSubject().Target, Geometry)
		&& Geometry.Pivot.Equals(Component->GetComponentLocation() + CurrentSubject().Target.Offset));
	Hit = MakeHit(EHandle::SubjectBoundsFace); CurrentSubject().BoundsShape = EShotTargetBoundsShape::AutoFromComponentBounds;
	Client->StartHandleDrag(Hit, 100, 100); TestFalse(TEXT("Auto bounds cannot be resized through stale manual handle"), Client->IsEditingGesture());
	Hit = MakeHit(EHandle::SubjectOffsetAxis); Client->SetMode(EShotEditorMode::Free);
	Client->StartHandleDrag(Hit, 100, 100); TestFalse(TEXT("Inspect mode makes subject handles read-only"), Client->IsEditingGesture());
	Client->SetMode(EShotEditorMode::Drag); Asset->Shot.Targets.AddDefaulted();
	TestFalse(TEXT("Array changes invalidate old subject indices"), Client->IsSubjectHandleValid(Hit));
	TestTrue(TEXT("Camera-facing axis safely rejects drag"), !CanDrag(FVector2D::ZeroVector) && DragDelta(FVector2D(10, 20), FVector2D::ZeroVector) == 0.);
	TestEqual(TEXT("Mouse/axis scaling preserves world delta at 200 percent DPI"), DragDelta(FVector2D(40, -20), FVector2D(4, -2)), DragDelta(FVector2D(20, -10), FVector2D(2, -1)));
	TestTrue(TEXT("First subject stays untouched"), Asset->Shot.Targets[0].Target.Offset.IsZero());
	TestEqual(TEXT("Gizmo/navigation changes retain task pages"), Authoring->GetPageBuildCountForTesting(), 1u);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(CommitHandle);
	FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);
	Session->OnChanged = nullptr;
	return true;
}
#endif
