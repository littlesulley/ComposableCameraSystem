// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Editor.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "PropertyHandle.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "IDetailsView.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyPath.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerModeToolkit.h"
#include "MeshCamera/ComposableCameraMeshLayerShapes.h"
#include "MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"
#include "UObject/GCObject.h"

namespace
{
	double ShapeArea(const FComposableCameraMeshSurfaceAuthoringData& Data)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Data.Indices.Num(); Index += 3)
		{
			const FVector A(Data.Vertices[Data.Indices[Index]]), B(Data.Vertices[Data.Indices[Index + 1]]), C(Data.Vertices[Data.Indices[Index + 2]]);
			Area += FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
		}
		return Area;
	}

	bool ShapeCoversPoint(const FComposableCameraMeshSurfaceAuthoringData& Data, const FGuid& LayerId, const FVector2D& Point)
	{
		FComposableCameraMeshSurfaceRuntimeData Runtime;
		Runtime.Vertices = Data.Vertices;
		Runtime.Indices = Data.Indices;
		for (const FGuid& Id : Data.TriangleLayerIds) { Runtime.TriangleLayerIndices.Add(Id == LayerId ? 0 : 1); }
		for (const FVector3f& Vertex : Data.Vertices) { Runtime.LocalBounds += FVector(Vertex); }
		TArray<FComposableCameraMeshLayerDefinition> Layers;
		Layers.AddDefaulted();
		Layers[0].LayerId = LayerId;
		Layers.AddDefaulted();
		Layers[1].bEnabled = false;
		TArray<int32, TInlineAllocator<16>> Hits;
		FVector Position;
		double Distance = 0.0;
		return Runtime.QueryLocalRayLayers(FVector(Point.X, Point.Y, 1000.0), -FVector::UpVector,
			2000.0, 5.0, Layers, Hits, Position, Distance);
	}

	bool ProjectShapeSlope(const FVector2D& Point, FVector& OutPosition)
	{
		OutPosition = FVector(Point.X, Point.Y, Point.X * 0.1 + Point.Y * 0.2);
		return true;
	}

	struct FMeshSelectionDetailsRefreshState : FGCObject
	{
		TObjectPtr<UComposableCameraMeshLayerSelection> Selection = NewObject<UComposableCameraMeshLayerSelection>();
		TSharedPtr<IDetailsView> View;
		TSharedPtr<FComposableCameraMeshLayerModeToolkit> Toolkit;
		int32 LayoutBuilds = 0;
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObject(Selection); }
		virtual FString GetReferencerName() const override { return TEXT("FMeshSelectionDetailsRefreshState"); }
	};

	struct FMeshToolSliderState : FGCObject
	{
		TObjectPtr<UComposableCameraMeshLayerToolSettings> Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		TSharedPtr<IDetailsView> View;
		TSharedPtr<ISinglePropertyView> NumericView;
		TSharedPtr<IPropertyHandle> Handle;
		TSharedPtr<FComposableCameraMeshLayerModeToolkit> Toolkit;
		int32 LayoutBuilds = 0;
		int32 Notifications = 0;
		bool bOwnsSliderTransaction = false;
		~FMeshToolSliderState()
		{
			Settings->OnToolSettingsChanged.Unbind();
			if (bOwnsSliderTransaction && GEditor) { GEditor->EndTransaction(); }
		}
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObject(Settings); }
		virtual FString GetReferencerName() const override { return TEXT("FMeshToolSliderState"); }
	};

	class FMeshToolSliderCustomization : public IDetailCustomization
	{
	public:
		explicit FMeshToolSliderCustomization(TWeakPtr<FMeshToolSliderState> InState) : State(InState) {}
		virtual void CustomizeDetails(IDetailLayoutBuilder&) override
		{
			if (const auto Pinned = State.Pin()) { ++Pinned->LayoutBuilds; }
		}
	private:
		TWeakPtr<FMeshToolSliderState> State;
	};

	class FMeshToolSliderCommand : public IAutomationLatentCommand
	{
	public:
		FMeshToolSliderCommand(FAutomationTestBase* InTest, TSharedRef<FMeshToolSliderState> InState)
			: Test(InTest), State(InState) {}
		bool BeginPhase()
		{
			State->Settings->Tool = Phase == 0 ? EComposableCameraMeshDrawTool::Brush
				: Phase == 1 ? EComposableCameraMeshDrawTool::Select : EComposableCameraMeshDrawTool::Erase;
			const FName Property = Phase == 1 ? TEXT("ShapeGridSize") : TEXT("BrushRadius");
			FPropertyEditorModule& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			State->NumericView = Properties.CreateSingleProperty(State->Settings, Property, FSinglePropertyParams());
			State->Handle = State->NumericView.IsValid() ? State->NumericView->GetPropertyHandle() : nullptr;
			if (!Test->TestTrue(TEXT("Tool numeric property handle exists"), State->Handle.IsValid())) { return false; }
			PreviousBuilds = State->LayoutBuilds;
			PreviousNotifications = State->Notifications;
			// Request before capture: the queued ticker must also protect this case.
			State->Toolkit->RefreshSelectionDetails();
			GEditor->BeginTransaction(NSLOCTEXT("MeshCameraTests", "SliderTransaction", "Mesh Tool Slider"));
			State->bOwnsSliderTransaction = true;
			const EPropertyValueSetFlags::Type Flags = EPropertyValueSetFlags::InteractiveChange | EPropertyValueSetFlags::NotTransactable;
			Test->TestTrue(TEXT("First interactive slider value applies"), State->Handle->SetValue(Phase == 1 ? 20.0 : 200.0, Flags) == FPropertyAccess::Success);
			FinalValue = Phase == 1 ? 30.0 : 220.0;
			Test->TestTrue(TEXT("Continued slider movement applies"), State->Handle->SetValue(FinalValue, Flags) == FPropertyAccess::Success);
			Frames = 0;
			bWaitingForCommitRefresh = false;
			StartTime = FPlatformTime::Seconds();
			return true;
		}
		virtual bool Update() override
		{
			if (!bWaitingForCommitRefresh)
			{
				if (++Frames < 3) { return false; }
				Test->TestEqual(TEXT("Details survives consecutive editor ticks during slider capture"), State->LayoutBuilds, PreviousBuilds);
				Test->TestEqual(TEXT("Interactive movement does not dispatch a tool refresh"), State->Notifications, PreviousNotifications);
				// Same finish path as SPropertyEditorNumeric: SetValue closes the slider transaction.
				Test->TestTrue(TEXT("Slider final commit applies"), State->Handle->SetValue(FinalValue) == FPropertyAccess::Success);
				if (GEditor->IsTransactionActive())
				{
					Test->AddError(TEXT("Tool slider left an editor transaction open; Undo would be blocked"));
					return true;
				}
				State->bOwnsSliderTransaction = false;
				Test->TestEqual(TEXT("Slider commit dispatches one settings notification"), State->Notifications, PreviousNotifications + 1);
				bWaitingForCommitRefresh = true;
				StartTime = FPlatformTime::Seconds();
				return false;
			}
			if (State->LayoutBuilds <= PreviousBuilds)
			{
				if (FPlatformTime::Seconds() - StartTime < 5.0) { return false; }
				Test->AddError(TEXT("Tool Details did not refresh after slider release"));
				return true;
			}
			Test->TestEqual(TEXT("Pending slider refresh coalesces into one final rebuild"), State->LayoutBuilds, PreviousBuilds + 1);
			if (++Phase == 3) { return true; }
			return !BeginPhase();
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<FMeshToolSliderState> State;
		int32 Phase = 0;
		int32 Frames = 0;
		int32 PreviousBuilds = 0;
		int32 PreviousNotifications = 0;
		double FinalValue = 0.0;
		double StartTime = 0.0;
		bool bWaitingForCommitRefresh = false;
	};

	class FMeshSelectionRefreshTestCustomization : public IDetailCustomization
	{
	public:
		explicit FMeshSelectionRefreshTestCustomization(TWeakPtr<FMeshSelectionDetailsRefreshState> InState) : State(InState) {}
		virtual void CustomizeDetails(IDetailLayoutBuilder&) override
		{
			if (const auto Pinned = State.Pin()) { ++Pinned->LayoutBuilds; }
		}
	private:
		TWeakPtr<FMeshSelectionDetailsRefreshState> State;
	};

	class FMeshSelectionRefreshCompleteCommand : public IAutomationLatentCommand
	{
	public:
		FMeshSelectionRefreshCompleteCommand(FAutomationTestBase* InTest,
			TSharedRef<FMeshSelectionDetailsRefreshState> InState, int32 InPreviousBuilds)
			: Test(InTest), State(InState), PreviousBuilds(InPreviousBuilds), StartTime(FPlatformTime::Seconds()) {}
		virtual bool Update() override
		{
			if (State->LayoutBuilds <= PreviousBuilds)
			{
				if (FPlatformTime::Seconds() - StartTime < 5.0) { return false; }
				Test->AddError(TEXT("Queued Mesh selection refresh did not rebuild its Details view"));
				return true;
			}
			Test->TestEqual(TEXT("Repeated requests coalesce into one Details rebuild"), State->LayoutBuilds, PreviousBuilds + 1);
			// Queue again, then close before the deferred callback. Ownership must release.
			State->Toolkit->RefreshSelectionDetails();
			const TWeakPtr<FComposableCameraMeshLayerModeToolkit> WeakToolkit = State->Toolkit;
			State->Toolkit.Reset();
			Test->TestFalse(TEXT("Queued refresh does not keep a closed toolkit alive"), WeakToolkit.IsValid());
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<FMeshSelectionDetailsRefreshState> State;
		int32 PreviousBuilds;
		double StartTime;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshShapeGeometryTest,
	"ComposableCameraSystem.Editor.MeshCamera.ShapeGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshShapeGeometryTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	const FGuid LayerId = FGuid::NewGuid();
	TArray<FVector2D> Outline;
	FComposableCameraMeshSurfaceAuthoringData Data;
	BuildRectangleOutline(FVector2D(100.0, 60.0), FVector2D::ZeroVector, Outline);
	TestTrue(TEXT("Rectangle projects successfully in either drag direction"),
		BuildProjectedShape(Outline, 40.0, LayerId, ProjectShapeSlope, Data) == EShapeBuildResult::Success);
	TestTrue(TEXT("Projected rectangle has a consistent authoring mesh"), Data.IsConsistent() && !Data.Indices.IsEmpty());
	TestTrue(TEXT("Subdivision preserves rectangle area exactly"), FMath::IsNearlyEqual(ShapeArea(Data), 6000.0, 0.01));
	for (const FVector3f& Vertex : Data.Vertices)
	{
		TestTrue(TEXT("Projection follows the floor slope"), FMath::IsNearlyEqual(double(Vertex.Z), Vertex.X * 0.1 + Vertex.Y * 0.2, 0.001));
	}
	for (const FGuid& Id : Data.TriangleLayerIds) { TestTrue(TEXT("Every triangle owns the selected Layer GUID"), Id == LayerId); }
	TestTrue(TEXT("Rectangle boundary is included by the runtime ray query"), ShapeCoversPoint(Data, LayerId, FVector2D(100.0, 30.0)));
	TestFalse(TEXT("Rectangle excludes points beyond its exact edge"), ShapeCoversPoint(Data, LayerId, FVector2D(101.0, 30.0)));

	BuildCircleOutline(FVector2D::ZeroVector, 100.0, 64, Outline);
	TestTrue(TEXT("Circle triangulates without filling beyond its radius"),
		BuildProjectedShape(Outline, 40.0, LayerId, ProjectShapeSlope, Data) == EShapeBuildResult::Success);
	TestTrue(TEXT("Circle area stays within one percent of analytic area"), FMath::Abs(ShapeArea(Data) - UE_DOUBLE_PI * 10000.0) < UE_DOUBLE_PI * 100.0);
	TestTrue(TEXT("Circle covers its center"), ShapeCoversPoint(Data, LayerId, FVector2D::ZeroVector));
	TestFalse(TEXT("Circle excludes bounding-box corners"), ShapeCoversPoint(Data, LayerId, FVector2D(90.0, 90.0)));

	Outline = {{0.0, 0.0}, {100.0, 0.0}, {100.0, 50.0}, {50.0, 50.0}, {50.0, 100.0}, {0.0, 100.0}};
	for (int32 Winding = 0; Winding < 2; ++Winding)
	{
		TestTrue(TEXT("Concave polygon works with either winding"),
			BuildProjectedShape(Outline, 40.0, LayerId, ProjectShapeSlope, Data) == EShapeBuildResult::Success);
		TestTrue(TEXT("Concave polygon preserves its area"), FMath::IsNearlyEqual(ShapeArea(Data), 7500.0, 0.01));
		TestFalse(TEXT("Concave notch stays empty in the runtime query"), ShapeCoversPoint(Data, LayerId, FVector2D(80.0, 80.0)));
		TestTrue(TEXT("Concave arm remains covered"), ShapeCoversPoint(Data, LayerId, FVector2D(25.0, 75.0)));
		Algo::Reverse(Outline);
	}
	TestTrue(TEXT("Shape points snap to a shared document grid"), SnapShapePoint(FVector2D(23.0, -37.0), 10.0).Equals(FVector2D(20.0, -40.0)));
	TestTrue(TEXT("Zero grid size preserves precise unsnapped coordinates"), SnapShapePoint(FVector2D(23.25, -37.5), 0.0).Equals(FVector2D(23.25, -37.5)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshShapeProjectionTest,
	"ComposableCameraSystem.Editor.MeshCamera.ShapeProjectionAndLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshShapeProjectionTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	const FGuid LayerId = FGuid::NewGuid();
	TArray<FVector2D> Outline;
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(100.0, 100.0), Outline);
	FComposableCameraMeshSurfaceAuthoringData Data;
	TestTrue(TEXT("Missing floor samples yield a partial surface"), BuildProjectedShape(Outline, 20.0, LayerId,
		[](const FVector2D& Point, FVector& Position)
		{
			Position = FVector(Point.X, Point.Y, 0.0);
			return Point.X < 40.0 || Point.X > 60.0;
		}, Data) == EShapeBuildResult::PartialSurface);
	TestFalse(TEXT("Shape does not bridge the sampled floor gap"), ShapeCoversPoint(Data, LayerId, FVector2D(50.0, 50.0)));
	TestTrue(TEXT("Compatible floor beside the gap remains covered"), ShapeCoversPoint(Data, LayerId, FVector2D(10.0, 10.0)));
	if (!TestFalse(TEXT("Partial surface contains triangles"), Data.Vertices.IsEmpty())) { return false; }
	const int32 OriginalTriangles = Data.TriangleLayerIds.Num();
	const FVector3f OriginalVertex = Data.Vertices[0];
	Outline = {{0.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}, {100.0, 0.0}};
	TestTrue(TEXT("Self-intersecting polygon is rejected"), BuildProjectedShape(Outline, 20.0, LayerId, ProjectShapeSlope, Data) == EShapeBuildResult::InvalidOutline);
	TestEqual(TEXT("Invalid outline preserves prior data"), Data.TriangleLayerIds.Num(), OriginalTriangles);
	Outline = {{0.0, 0.0}, {100.0, 0.0}, {200.0, 0.0}};
	TestTrue(TEXT("Zero-area polygon is rejected"), BuildProjectedShape(Outline, 20.0, LayerId, ProjectShapeSlope, Data) == EShapeBuildResult::InvalidOutline);
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(100.0, 100.0), Outline);
	TestTrue(TEXT("An entirely missing floor is rejected"), BuildProjectedShape(Outline, 20.0, LayerId,
		[](const FVector2D&, FVector&) { return false; }, Data) == EShapeBuildResult::NoSurface);
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(100000.0, 100000.0), Outline);
	int32 ProjectionCalls = 0;
	TestTrue(TEXT("Excessive surface density is bounded"), BuildProjectedShape(Outline, 10.0, LayerId,
		[&ProjectionCalls](const FVector2D& Point, FVector& Position) { ++ProjectionCalls; return ProjectShapeSlope(Point, Position); }, Data) == EShapeBuildResult::TooComplex);
	TestEqual(TEXT("Rejected density performs no collision projections"), ProjectionCalls, 0);
	TestEqual(TEXT("Rejected projections preserve prior triangle data"), Data.TriangleLayerIds.Num(), OriginalTriangles);
	TestTrue(TEXT("Rejected projections preserve prior vertex data"), Data.Vertices[0] == OriginalVertex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshShapeProjectionBudgetTest,
	"ComposableCameraSystem.Editor.MeshCamera.ShapeProjectionBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshShapeProjectionBudgetTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FVector2D> Outline;
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(240, 160), Outline);
	FProjectedShapeBuild Build;
	TestTrue(TEXT("Outline prepares without collision work"), Build.Begin(Outline, 20.0, FGuid::NewGuid()) == EShapeBuildResult::Success);
	TestFalse(TEXT("Dense projection remains resumable after preparation"), Build.IsFinished());
	TestEqual(TEXT("Immediate rectangle fill needs only two outline triangles"), Build.GetOutlineIndices().Num(), 6);
	int32 StepQueries = 0, Steps = 0;
	bool bRepeatedQuery = false;
	TSet<FVector2D> Queried;
	auto Project = [&StepQueries, &Queried, &bRepeatedQuery](const FVector2D& Point, FVector& Position)
	{
		++StepQueries; bRepeatedQuery |= Queried.Contains(Point); Queried.Add(Point);
		if (FMath::Abs(Point.X - 120.0) < 12.0 && FMath::Abs(Point.Y - 80.0) < 12.0) { return false; }
		return ProjectShapeSlope(Point, Position);
	};
	while (!Build.IsFinished() && Steps < 10000)
	{
		StepQueries = 0; Build.Advance(Project, 3); ++Steps;
		if (!TestTrue(TEXT("Each resume obeys its query budget, including partial leaves"), StepQueries <= 3)) { return false; }
	}
	if (!TestTrue(TEXT("Budgeted projection completes across multiple frames"), Build.IsFinished() && Steps > 1)) { return false; }
	TestFalse(TEXT("Cached successes and failures are never queried twice"), bRepeatedQuery);
	TestTrue(TEXT("Missing floor is reported after incremental projection"), Build.GetResult() == EShapeBuildResult::PartialSurface);
	const auto Data = Build.TakeData();
	TestTrue(TEXT("Resumed output retains consistent ownership"), Data.IsConsistent());
	if (!TestFalse(TEXT("Compatible samples emit triangles"), Data.TriangleLayerIds.IsEmpty())) { return false; }
	TestFalse(TEXT("Refinement preserves the sampled floor gap"), ShapeCoversPoint(Data, Data.TriangleLayerIds[0], FVector2D(120, 80)));
	TestTrue(TEXT("Compatible floor remains beside the gap"), ShapeCoversPoint(Data, Data.TriangleLayerIds[0], FVector2D(10, 10)));
	for (const FVector3f& Vertex : Data.Vertices)
	{
		FVector Expected; ProjectShapeSlope(FVector2D(Vertex.X, Vertex.Y), Expected);
		if (!TestTrue(TEXT("Paused leaves preserve projected slope heights"), FMath::IsNearlyEqual(double(Vertex.Z), Expected.Z, 1.e-4))) { return false; }
	}
	FProjectedShapeBuild TooDense;
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(100000, 100000), Outline);
	TestTrue(TEXT("Excessive density is rejected before incremental queries"), TooDense.Begin(Outline, 10.0, FGuid::NewGuid()) == EShapeBuildResult::TooComplex && TooDense.IsFinished());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshShapeCurvatureTest,
	"ComposableCameraSystem.Editor.MeshCamera.ShapeCurvatureAndSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshShapeCurvatureTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	TArray<FVector2D> Outline;
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(100, 100), Outline);
	const FGuid LayerId = FGuid::NewGuid();
	auto Height = [](double X, double Y) { return 0.015 * X * X + 0.01 * Y * Y; };
	FProjectedShapeBuild Build;
	Build.Begin(Outline, 100.0, LayerId);
	TSet<FVector2D> Queried;
	int32 Queries = 0, Steps = 0;
	bool bRepeatedQuery = false;
	while (!Build.IsFinished() && Steps++ < 10000)
	{
		Queries = 0;
		Build.Advance([&](const FVector2D& Point, FVector& Position)
		{
			++Queries; bRepeatedQuery |= Queried.Contains(Point); Queried.Add(Point);
			Position = FVector(Point.X, Point.Y, Height(Point.X, Point.Y)); return true;
		}, 3);
		if (!TestTrue(TEXT("Adaptive refinement respects every resume's collision-query budget"), Queries <= 3)) { return false; }
	}
	if (!TestTrue(TEXT("Curved ground completes without dropping compatible leaves"),
		Build.IsFinished() && Build.GetResult() == EShapeBuildResult::Success)) { return false; }
	TestFalse(TEXT("Adaptive/shared-edge samples never repeat collision work"), bRepeatedQuery);
	const auto Data = Build.TakeData();
	TestTrue(TEXT("Curved ground conserves the complete rectangle footprint"),
		Data.IsConsistent() && FMath::IsNearlyEqual(ShapeArea(Data), 10000.0, 0.01));
	FComposableCameraMeshSurfaceRuntimeData Runtime;
	Runtime.Vertices = Data.Vertices; Runtime.Indices = Data.Indices;
	Runtime.TriangleLayerIndices.Init(0, Data.TriangleLayerIds.Num()); Runtime.RebuildSpatialIndex();
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.AddDefaulted(); Layers[0].LayerId = LayerId;
	for (int32 X = 5; X < 100; X += 10)
	{
		for (int32 Y = 5; Y < 100; Y += 10)
		{
			TArray<int32, TInlineAllocator<16>> Hits;
			FVector Position; double Distance = 0.0;
			if (!TestTrue(TEXT("Interior runtime probes find saved coverage at actual curved-ground height"),
				Runtime.QueryLocalRayLayers(FVector(X, Y, Height(X, Y) + 2.0), -FVector::UpVector, 4.0, 4.0,
					Layers, Hits, Position, Distance) && FMath::Abs(Position.Z - Height(X, Y)) <= 1.01)) { return false; }
		}
	}
	// Every emitted edge vertex must agree with the opposite mesh side, including
	// coarse/fine neighbors. A midpoint on a different-height chord forms a crack.
	bool bCrack = false;
	for (int32 Triangle = 0; Triangle < Data.Indices.Num() && !bCrack; Triangle += 3)
	{
		for (int32 Edge = 0; Edge < 3 && !bCrack; ++Edge)
		{
			const FVector A(Data.Vertices[Data.Indices[Triangle + Edge]]);
			const FVector B(Data.Vertices[Data.Indices[Triangle + (Edge + 1) % 3]]);
			const FVector2D AB(B.X - A.X, B.Y - A.Y);
			if (AB.SizeSquared() <= 1.e-8) { continue; }
			for (const FVector3f& Vertex : Data.Vertices)
			{
				const FVector P(Vertex);
				const FVector2D AP(P.X - A.X, P.Y - A.Y);
				const double T = FVector2D::DotProduct(AP, AB) / AB.SizeSquared();
				if (T > 1.e-5 && T < 1.0 - 1.e-5 && FMath::Abs(AB.X * AP.Y - AB.Y * AP.X) <= 1.e-5
					&& FMath::Abs(P.Z - FMath::Lerp(A.Z, B.Z, T)) > 1.e-3) { bCrack = true; break; }
			}
		}
	}
	TestFalse(TEXT("Curved coarse/fine boundaries contain no different-height T junctions"), bCrack);
	FComposableCameraMeshSurfaceAuthoringData Scaled;
	TestTrue(TEXT("Scaled-document error converts from world centimeters before refinement"),
		BuildProjectedShape(Outline, 100.0, LayerId, [&](const FVector2D& Point, FVector& Position)
		{
			Position = FVector(Point.X, Point.Y, Height(Point.X, Point.Y)); return true;
		}, Scaled, 16384, 0.25) == EShapeBuildResult::Success);
	Runtime.Vertices = Scaled.Vertices; Runtime.Indices = Scaled.Indices;
	Runtime.TriangleLayerIndices.Init(0, Scaled.TriangleLayerIds.Num()); Runtime.RebuildSpatialIndex();
	for (int32 X = 15; X < 100; X += 20)
	{
		TArray<int32, TInlineAllocator<16>> Hits; FVector Position; double Distance = 0.0;
		TestTrue(TEXT("Four-times scaled saved surface retains a one-world-centimeter fitting bound"),
			Runtime.QueryLocalRayLayers(FVector(X, 45, Height(X, 45) + 0.5), -FVector::UpVector, 1.0, 1.0,
				Layers, Hits, Position, Distance) && FMath::Abs(Position.Z - Height(X, 45)) * 4.0 <= 1.01);
	}
	FComposableCameraMeshSurfaceAuthoringData Preserved = Data;
	TestTrue(TEXT("Bounded curved generation reports density failure"), BuildProjectedShape(Outline, 100.0, LayerId,
		[&](const FVector2D& Point, FVector& Position) { Position = FVector(Point.X, Point.Y, Height(Point.X, Point.Y)); return true; }, Preserved, 4)
		== EShapeBuildResult::TooComplex);
	TestTrue(TEXT("Density failure preserves earlier source instead of publishing a partial mesh"), Preserved.Vertices == Data.Vertices && Preserved.Indices == Data.Indices);
	FComposableCameraMeshSurfaceAuthoringData Partial;
	TestTrue(TEXT("One unsupported interior strip refines a coarse leaf instead of discarding its entire supported neighborhood"),
		BuildProjectedShape(Outline, 100.0, LayerId, [](const FVector2D& Point, FVector& Position)
		{
			Position = FVector(Point.X, Point.Y, 0.0);
			return Point.X < 48.0 || Point.X > 52.0;
		}, Partial) == EShapeBuildResult::PartialSurface);
	TestTrue(TEXT("Both sides of a tiny real floor gap remain painted"),
		ShapeCoversPoint(Partial, LayerId, FVector2D(40, 50)) && ShapeCoversPoint(Partial, LayerId, FVector2D(60, 50)));
	TestFalse(TEXT("Refinement never fills the actual unsupported floor gap"), ShapeCoversPoint(Partial, LayerId, FVector2D(50, 50)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshShapeCreationTest,
	"ComposableCameraSystem.Editor.MeshCamera.ShapeCreationPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshShapeCreationTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	FComposableCameraMeshLayerEdMode Mode;
	Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers(); Mode.Settings->TouchDocument();
	Mode.SavedRevision = Mode.Settings->DocumentRevision;
	TestTrue(TEXT("Clean footer omits Saved state"), Mode.GetStatusText().IsEmpty());
	for (const auto Type : {EComposableCameraMeshShapeType::Rectangle, EComposableCameraMeshShapeType::Circle, EComposableCameraMeshShapeType::Polygon})
	{
		FComposableCameraMeshAuthoredShape Shape;
		Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = Mode.Settings->GetActiveLayerId(); Shape.Type = Type;
		Shape.ControlPoints = {{0, 0}, {240, 160}};
		if (Type == EComposableCameraMeshShapeType::Polygon) { Shape.ControlPoints = {{0, 0}, {240, 0}, {120, 80}, {240, 160}, {0, 160}}; }
		Shape.SampleSpacing = 20.0;
		if (!TestTrue(TEXT("Each Shape queues a fill immediately without collision or source mutation"), Mode.QueueShapeCreation(Shape, nullptr))) { return false; }
		const auto& Preview = Mode.GetPendingShapePreview(Mode.PendingShapes.Num() - 1);
		TestTrue(TEXT("Immediate preview has a filled triangulated outline"), !Preview.Indices.IsEmpty() && Preview.Indices.Num() == (Preview.LocalVertices.Num() - 2) * 3);
	}
	TestTrue(TEXT("Consecutive shapes retain their independent pending previews"), Mode.IsCreatingShapes() && Mode.PendingShapes.Num() == 3);
	TestTrue(TEXT("Pending fill never enters saved source or a transaction"), Mode.Settings->WorkingData.Indices.IsEmpty() && !Mode.IsDirty());
	Mode.HandleToolSettingsChanged();
	TestTrue(TEXT("Captured released shapes survive tool/option changes"), Mode.PendingShapes.Num() == 3);
	Mode.BeginStroke();
	TestFalse(TEXT("Brush/Erase cannot mutate a source snapshot before pending regions finish"), Mode.bPainting);
	TestFalse(TEXT("Save cannot persist an incomplete region"), Mode.SaveWorkingData());
	Mode.LostFocus(nullptr, nullptr);
	TestTrue(TEXT("Released regions continue while focus moves to a panel"), Mode.IsCreatingShapes());
	TestTrue(TEXT("Escape cancels released pending fills too"), Mode.InputKey(nullptr, nullptr, EKeys::Escape, IE_Pressed));
	TestFalse(TEXT("Cancelled work leaves no preview or authored coverage"), Mode.IsCreatingShapes() || !Mode.Settings->WorkingData.Indices.IsEmpty());
	Mode.bDirty = true;
	TestTrue(TEXT("Dirty footer also omits standalone Unsaved state"), Mode.GetStatusText().IsEmpty());
	Mode.ShapeFeedback = FText::FromString(TEXT("Required shape feedback"));
	TestEqual(TEXT("Removing state leaves feedback without a leading newline"), Mode.GetStatusText().ToString(), FString(TEXT("Required shape feedback")));
	FComposableCameraMeshAuthoredShape CompletedShape;
	CompletedShape.ShapeId = FGuid::NewGuid(); CompletedShape.LayerId = Mode.Settings->GetActiveLayerId(); CompletedShape.ControlPoints = {{0, 0}, {200, 200}};
	TestTrue(TEXT("Missing-world fixture can queue a pure outline"), Mode.QueueShapeCreation(CompletedShape, nullptr));
	Mode.AdvanceShapeCreation();
	TestFalse(TEXT("Lost World discards work without accessing an absent mode owner"), Mode.IsCreatingShapes());
	if (!TestNotNull(TEXT("Editor transactions available for atomic completion"), GEditor)) { return false; }
	Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>(GetTransientPackage(), NAME_None, RF_Transactional);
	TArray<FVector2D> Outline; FComposableCameraMeshSurfaceAuthoringData Geometry, Source;
	BuildShapeOutline(CompletedShape, Outline); BuildProjectedShape(Outline, 100.0, CompletedShape.LayerId, ProjectShapeSlope, Geometry);
	AppendShapeGeometry(Source, Geometry, CompletedShape.ShapeId); Source.Shapes.Add(CompletedShape);
	FResolvedSurfaceVisualization Resolved; BuildAuthoringVisualization(Source, Mode.Settings->Layers, Resolved);
	GEditor->RegisterForUndo(&Mode);
	Mode.ApplyCreatedShape(MoveTemp(Source), MoveTemp(Resolved), CompletedShape.ShapeId, false);
	TestTrue(TEXT("Completion installs source and ready coverage together"), Mode.Settings->WorkingData.Shapes.Num() == 1 && !Mode.bVisualizationDirty && !Mode.Visualization.Cells.IsEmpty());
	GEditor->UndoTransaction();
	TestTrue(TEXT("One Undo removes the completed creation and restores the clean checkpoint"), Mode.Settings->WorkingData.Shapes.IsEmpty() && Mode.Settings->WorkingData.Indices.IsEmpty() && !Mode.IsDirty());
	GEditor->RedoTransaction();
	TestTrue(TEXT("Redo restores the completed Shape identity and projected source"), Mode.Settings->WorkingData.Shapes.Num() == 1 && Mode.Settings->WorkingData.Shapes[0].ShapeId == CompletedShape.ShapeId && Mode.IsDirty());
	GEditor->UnregisterForUndo(&Mode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshShapeInteractionTest,
	"ComposableCameraSystem.Editor.MeshCamera.ShapeInteraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshShapeInteractionTest::RunTest(const FString&)
{
	FComposableCameraMeshLayerEdMode Mode;
	Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>();
	Mode.Settings->AddLayer();
	Mode.Settings->AddLayer();
	Mode.Settings->SelectLayer(Mode.Settings->Layers[0].LayerId);
	Mode.Settings->OnToolSettingsChanged.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::CancelInteraction);
	Mode.Settings->OnLayerDataChanged.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::MarkLayerDataDirty);
	Mode.Settings->ShapeGridSize = 10.0;
	Mode.Settings->Tool = EComposableCameraMeshDrawTool::Rectangle;
	Mode.AnchorTransform = FTransform(FRotator(0.0, 90.0, 0.0), FVector(1000.0, -500.0, 25.0));
	Mode.HoverHit.ImpactPoint = Mode.AnchorTransform.TransformPosition(FVector(23.0, 37.0, 0.0));
	Mode.HoverHit.ImpactNormal = FVector::UpVector;
	Mode.bHasHoverHit = true;
	Mode.BeginShape();
	TestTrue(TEXT("Draft captures its selected Layer"), Mode.ShapeLayerId == Mode.Settings->GetActiveLayerId());
	TestTrue(TEXT("First corner snaps in document coordinates"), Mode.ShapeStart.Equals(FVector2D(20.0, 40.0)));
	Mode.HoverHit.ImpactPoint = Mode.AnchorTransform.TransformPosition(FVector(84.0, 121.0, 0.0));
	Mode.UpdateShapePreview();
	TestEqual(TEXT("Drag previews four rectangle corners"), Mode.ShapePreview.Num(), 4);
	TestTrue(TEXT("Opposite corner snaps to the same document grid"), Mode.ShapeEnd.Equals(FVector2D(80.0, 120.0)));
	TestTrue(TEXT("Outline follows the Level document anchor"), Mode.GetShapePlanePosition(Mode.ShapeEnd).Equals(Mode.AnchorTransform.TransformPosition(FVector(80.0, 120.0, 0.0)), 0.001));
	TestFalse(TEXT("Preview does not dirty or write the working mesh"), Mode.IsDirty() || !Mode.Settings->WorkingData.Indices.IsEmpty());
	TestTrue(TEXT("Escape consumes and cancels the shape draft"), Mode.InputKey(nullptr, nullptr, EKeys::Escape, IE_Pressed));
	TestFalse(TEXT("Cancelled shape no longer owns a draft"), Mode.bDrawingShape);
	TestTrue(TEXT("Cancelled preview releases its vertices"), Mode.ShapePreview.IsEmpty());

	Mode.Settings->Tool = EComposableCameraMeshDrawTool::Polygon;
	Mode.BeginShape();
	Mode.ShapePoints.Add(FVector2D(100.0, 100.0));
	TestTrue(TEXT("Backspace removes a polygon point"), Mode.InputKey(nullptr, nullptr, EKeys::BackSpace, IE_Pressed));
	TestEqual(TEXT("Backspace retains the first point"), Mode.ShapePoints.Num(), 1);
	Mode.InputKey(nullptr, nullptr, EKeys::BackSpace, IE_Pressed);
	TestFalse(TEXT("Removing the first point cancels the draft"), Mode.bDrawingShape);
	Mode.BeginShape();
	Mode.Settings->SelectLayer(Mode.Settings->Layers[1].LayerId);
	TestFalse(TEXT("Selecting another Layer cancels the old draft"), Mode.bDrawingShape);
	TestFalse(TEXT("Selection and cancelled drafts do not dirty geometry"), Mode.IsDirty());
	Mode.BeginShape();
	Mode.Settings->Tool = EComposableCameraMeshDrawTool::Circle;
	FPropertyChangedEvent ChangedTool(FindFProperty<FProperty>(UComposableCameraMeshLayerToolSettings::StaticClass(), TEXT("Tool")));
	Mode.Settings->PostEditChangeProperty(ChangedTool);
	TestFalse(TEXT("Tool changes cancel pending shapes"), Mode.bDrawingShape);
	TestFalse(TEXT("Tool changes do not dirty geometry"), Mode.IsDirty());
	Mode.BeginShape();
	Mode.ShapeFeedback = FText::FromString(TEXT("Draft validation error"));
	TestTrue(TEXT("Validation feedback stays visible beside draft measurements"), Mode.GetStatusText().ToString().Contains(TEXT("Draft validation error")));
	Mode.LostFocus(nullptr, nullptr);
	TestFalse(TEXT("Losing viewport focus cancels the pending drag"), Mode.bDrawingShape);

	Mode.Settings->Tool = EComposableCameraMeshDrawTool::Brush;
	Mode.bPainting = true;
	TestTrue(TEXT("Brush release clears captured state without depending on modifier keys"), Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released));
	TestFalse(TEXT("Released brush cannot continue painting"), Mode.bPainting);
	Mode.Settings->OnToolSettingsChanged.Unbind();
	Mode.Settings->OnLayerDataChanged.Unbind();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshShapeEditingTest,
	"ComposableCameraSystem.Editor.MeshCamera.ShapeEditingAndErase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshShapeEditingTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	FComposableCameraMeshLayerEdMode Mode;
	Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.Settings->Layers.AddDefaulted();
	Mode.Settings->NormalizeLayers();
	Mode.Settings->TouchDocument();
	Mode.SavedRevision = Mode.Settings->DocumentRevision;
	FComposableCameraMeshAuthoredShape Shape;
	Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = Mode.Settings->GetActiveLayerId();
	Shape.ControlPoints = {{0.0, 0.0}, {200.0, 200.0}};
	TArray<FVector2D> Outline;
	FComposableCameraMeshSurfaceAuthoringData Geometry;
	BuildShapeOutline(Shape, Outline);
	BuildProjectedShape(Outline, 1000.0, Shape.LayerId, ProjectShapeSlope, Geometry);
	Mode.ApplyShapeGeometry(Shape, Geometry, false);
	TestTrue(TEXT("Completed region retains selectable identity"), Mode.HasSelectedShape());
	TestTrue(TEXT("Mesh picking resolves owning Shape"), FindShapeOnRay(Mode.Settings->WorkingData, Shape.LayerId, FVector(100.0, 100.0, 1000.0), -FVector::UpVector) == Shape.ShapeId);
	TestTrue(TEXT("Pick outside region yields no Shape"), !FindShapeOnRay(Mode.Settings->WorkingData, Shape.LayerId, FVector(300.0, 100.0, 1000.0), -FVector::UpVector).IsValid());
	TestTrue(TEXT("Details shows exact rectangle dimensions"), Mode.SelectionEditor->Size.Equals(FVector2D(200.0, 200.0)));

	// An eraser fully inside two very large triangles must cut a hole, not remove whole triangles.
	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Center = FVector(100.0, 100.0, 30.0); Stamp.Radius = 25.0; Stamp.Depth = 100.0;
	const FGuid OtherLayer = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData OtherGeometry = Geometry;
	for (FGuid& Id : OtherGeometry.TriangleLayerIds) { Id = OtherLayer; }
	AppendShapeGeometry(Mode.Settings->WorkingData, OtherGeometry, FGuid());
	TestTrue(TEXT("Eraser clips large triangles even when centroids lie outside its radius"), EraseShapeGeometry(Mode.Settings->WorkingData, Shape.LayerId, Stamp));
	TestFalse(TEXT("Erased center is absent from runtime coverage"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(100.0, 100.0)));
	TestTrue(TEXT("Coverage outside small eraser survives"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(80.0, 50.0)));
	TestTrue(TEXT("Other Layer survives overlapping erasure"), ShapeCoversPoint(Mode.Settings->WorkingData, OtherLayer, FVector2D(100.0, 100.0)));
	TestEqual(TEXT("Shape retains erase mask"), Mode.Settings->WorkingData.Shapes[0].Erasures.Num(), 1);
	TestFalse(TEXT("Repeating identical erasure is a no-op"), EraseShapeGeometry(Mode.Settings->WorkingData, Shape.LayerId, Stamp));
	TestTrue(TEXT("Ownership stays consistent after fragment generation"), Mode.Settings->WorkingData.IsConsistent());

	// Serialize the actual reflected source and restore it as a reopened document.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FObjectAndNameAsStringProxyArchive WriteArchive(Writer, false);
	FComposableCameraMeshSurfaceAuthoringData::StaticStruct()->SerializeItem(WriteArchive, &Mode.Settings->WorkingData, nullptr);
	FMemoryReader Reader(Bytes);
	FObjectAndNameAsStringProxyArchive ReadArchive(Reader, false);
	FComposableCameraMeshSurfaceAuthoringData Reopened;
	FComposableCameraMeshSurfaceAuthoringData::StaticStruct()->SerializeItem(ReadArchive, &Reopened, nullptr);
	if (!TestTrue(TEXT("Reopened source retains Shape GUID and ownership"), Reopened.Shapes.Num() == 1 && Reopened.Shapes[0].ShapeId == Shape.ShapeId && Reopened.TriangleShapeIds == Mode.Settings->WorkingData.TriangleShapeIds)) { return false; }
	TestEqual(TEXT("Reopened source retains erasure"), Reopened.Shapes[0].Erasures.Num(), 1);
	Mode.Settings->WorkingData = MoveTemp(Reopened);
	Shape = Mode.Settings->WorkingData.Shapes[0];
	Shape.ControlPoints[1] = FVector2D(250.0, 250.0);
	BuildShapeOutline(Shape, Outline);
	BuildProjectedShape(Outline, 1000.0, Shape.LayerId, ProjectShapeSlope, Geometry);
	Mode.ApplyShapeGeometry(Shape, Geometry, false);
	TestFalse(TEXT("Editing reopened Shape does not resurrect its erased hole"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(100.0, 100.0)));
	TestTrue(TEXT("Resizing adds coverage at its new boundary"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(240.0, 240.0)));
	TestEqual(TEXT("Editing replaces rather than duplicates source record"), Mode.Settings->WorkingData.Shapes.Num(), 1);

	Mode.bEditingShape = true; Mode.EditControlIndex = 1;
	Mode.HoverHit.ImpactPoint = FVector(300.0, 300.0, 0.0); Mode.bHasHoverHit = true;
	Mode.UpdateEditPreview();
	TestTrue(TEXT("Rectangle corner preview moves the selected control"), Mode.EditShape.ControlPoints[1].Equals(FVector2D(300.0, 300.0)));
	TestTrue(TEXT("Preview preserves committed Shape"), Mode.Settings->WorkingData.Shapes[0].ControlPoints[1].Equals(FVector2D(250.0, 250.0)));
	Mode.InputKey(nullptr, nullptr, EKeys::Escape, IE_Pressed);
	TestFalse(TEXT("Esc cancels edit drag"), Mode.bEditingShape);
	TestTrue(TEXT("Existing Shape replacement queues before deletion"), Mode.QueueShapeCreation(Shape, nullptr));
	Mode.DeleteSelectedShape();
	TestFalse(TEXT("Delete cancels pending replacement so it cannot resurrect the Shape"), Mode.IsCreatingShapes());
	TestFalse(TEXT("Deleting Shape removes its entire region"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(240.0, 240.0)));
	TestTrue(TEXT("Deleting Shape preserves other Layer geometry"), ShapeCoversPoint(Mode.Settings->WorkingData, OtherLayer, FVector2D(100.0, 100.0)));
	Shape.ShapeId = FGuid::NewGuid(); Shape.Type = EComposableCameraMeshShapeType::Circle;
	Shape.Erasures.Reset(); Shape.ControlPoints = {{300.0, 300.0}, {350.0, 300.0}};
	BuildShapeOutline(Shape, Outline); BuildProjectedShape(Outline, 100.0, Shape.LayerId, ProjectShapeSlope, Geometry);
	Mode.ApplyShapeGeometry(Shape, Geometry, false);
	Mode.bEditingShape = true; Mode.EditControlIndex = 0; Mode.HoverHit.ImpactPoint = FVector(400.0, 400.0, 0.0);
	Mode.UpdateEditPreview();
	TestTrue(TEXT("Moving circle center preserves radius"), Mode.EditShape.ControlPoints[0].Equals(FVector2D(400.0, 400.0)) && FMath::IsNearlyEqual((Mode.EditShape.ControlPoints[1] - Mode.EditShape.ControlPoints[0]).Size(), 50.0));
	Mode.CancelInteraction();
	Shape.ShapeId = FGuid::NewGuid(); Shape.Type = EComposableCameraMeshShapeType::Polygon;
	Shape.ControlPoints = {{0.0, 0.0}, {200.0, 0.0}, {0.0, 200.0}};
	BuildShapeOutline(Shape, Outline); BuildProjectedShape(Outline, 100.0, Shape.LayerId, ProjectShapeSlope, Geometry);
	Mode.ApplyShapeGeometry(Shape, Geometry, false);
	Mode.bEditingShape = true; Mode.EditControlIndex = 1; Mode.HoverHit.ImpactPoint = FVector(250.0, 0.0, 0.0);
	Mode.UpdateEditPreview();
	TestTrue(TEXT("Polygon drag adjusts one vertex"), Mode.EditShape.ControlPoints[1].Equals(FVector2D(250.0, 0.0)) && Mode.EditShape.ControlPoints[0] == Shape.ControlPoints[0]);
	Mode.EditControlIndex = INDEX_NONE; Mode.EditDragStart = FVector2D::ZeroVector; Mode.HoverHit.ImpactPoint = FVector(30.0, 40.0, 0.0);
	Mode.UpdateEditPreview();
	TestTrue(TEXT("Interior drag translates whole polygon"), Mode.EditShape.ControlPoints[0].Equals(FVector2D(30.0, 40.0)) && Mode.EditShape.ControlPoints[1].Equals(FVector2D(230.0, 40.0)));
	Mode.CancelInteraction();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshEraseBroadPhaseTest,
	"ComposableCameraSystem.Editor.MeshCamera.EraseBroadPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshEraseBroadPhaseTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	const FGuid Layer = FGuid::NewGuid();
	TArray<FVector2D> Outline;
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(200.0, 200.0), Outline);
	FComposableCameraMeshSurfaceAuthoringData Near;
	BuildProjectedShape(Outline, 1000.0, Layer, ProjectShapeSlope, Near);
	FComposableCameraMeshSurfaceAuthoringData Dense = Near;
	for (int32 Index = 0; Index < 128; ++Index)
	{
		auto Far = Near;
		for (FVector3f& Vertex : Far.Vertices) { Vertex.X += 1000.0f + Index * 250.0f; }
		AppendShapeGeometry(Dense, Far, FGuid());
	}
	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Center = FVector(100.0, 100.0, 30.0); Stamp.Radius = 25.0; Stamp.Depth = 100.0;
	FEraseGeometryStats Stats;
	FBox2D ChangedBounds(ForceInit);
	const double Before = ShapeArea(Dense);
	const double NearBefore = ShapeArea(Near);
	Dense.Vertices.Reserve(Dense.Vertices.Num() + 8192); // Enough for bounded candidate fragments.
	const FVector3f* VertexBuffer = Dense.Vertices.GetData();
	const TArray<FVector3f> OriginalVertices = Dense.Vertices;
	TestTrue(TEXT("Dense fixture cuts the near region"), EraseShapeGeometry(Dense, Layer, Stamp, false, &Stats, &ChangedBounds));
	TestTrue(TEXT("Reference fixture cuts the same region"), EraseShapeGeometry(Near, Layer, Stamp, false));
	TestEqual(TEXT("Only the two near triangles enter polygon clipping"), Stats.ClippedTriangles, 2);
	TestEqual(TEXT("Far geometry uses the cheap rejection path"), Stats.RejectedTriangles, 256);
	TestTrue(TEXT("Remote geometry preserves area"), FMath::IsNearlyEqual(Before - ShapeArea(Dense), NearBefore - ShapeArea(Near), 0.1));
	TestTrue(TEXT("Changed bounds cover the cut, not the original large triangles"), ChangedBounds.bIsValid
		&& ChangedBounds.Min.X >= 75.0 - 1.e-4 && ChangedBounds.Max.X <= 125.0 + 1.e-4
		&& ChangedBounds.Min.Y >= 75.0 - 1.e-4 && ChangedBounds.Max.Y <= 125.0 + 1.e-4);
	TestTrue(TEXT("Erase reuses the source vertex buffer instead of replacing the whole document"), Dense.Vertices.GetData() == VertexBuffer);
	bool bOriginalVerticesUnchanged = Dense.Vertices.Num() >= OriginalVertices.Num();
	for (int32 Index = 0; Index < OriginalVertices.Num() && bOriginalVerticesUnchanged; ++Index)
	{
		bOriginalVerticesUnchanged &= Dense.Vertices[Index] == OriginalVertices[Index];
	}
	TestTrue(TEXT("Untouched and shared source vertices keep their coordinates and indices"), bOriginalVerticesUnchanged);
	TestTrue(TEXT("Swap-removed and appended triangle ownership stays consistent"), Dense.IsConsistent());
	const int32 VerticesAfterCut = Dense.Vertices.Num(), IndicesAfterCut = Dense.Indices.Num();
	Stamp.Center = FVector(-10000.0, -10000.0, 0.0);
	TestFalse(TEXT("Distant erasure does not rewrite source"), EraseShapeGeometry(Dense, Layer, Stamp, false, nullptr, &ChangedBounds));
	TestTrue(TEXT("No-op erasure preserves buffers, counts and invalid dirty bounds"), Dense.Vertices.GetData() == VertexBuffer
		&& Dense.Vertices.Num() == VerticesAfterCut && Dense.Indices.Num() == IndicesAfterCut && !ChangedBounds.bIsValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshEraseInteriorTest,
	"ComposableCameraSystem.Editor.MeshCamera.EraseInteriorFastPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComposableCameraMeshEraseInteriorTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	const FGuid Layer = FGuid::NewGuid(), OtherLayer = FGuid::NewGuid(), Owner = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	auto AddTriangle = [&](const FVector& A, const FVector& B, const FVector& C, const FGuid& LayerId)
	{
		const int32 First = Data.Vertices.Num(); Data.Vertices.Append({FVector3f(A), FVector3f(B), FVector3f(C)});
		Data.Indices.Append({First, First + 1, First + 2}); Data.TriangleLayerIds.Add(LayerId);
		Data.TriangleShapeIds.Add(LayerId == Layer ? Owner : FGuid());
	};
	AddTriangle(FVector(-1, -1, -0.3), FVector(1, -1, -0.1), FVector(0, 1, 0.2), Layer);
	const double Angle = UE_DOUBLE_PI / 32, Apothem = 10 * FMath::Cos(Angle);
	const FVector Normal(FMath::Cos(Angle), FMath::Sin(Angle), 0), Tangent(-Normal.Y, Normal.X, 0);
	// These vertices lie inside the radius-10 circle, but outside one face of the exact 32-gon.
	AddTriangle(Normal * (Apothem + 0.02) - Tangent * 0.04, Normal * (Apothem + 0.04) + Tangent * 0.04,
		Normal * (Apothem + 0.02) + Tangent * 0.04, Layer);
	AddTriangle(Normal * (Apothem - 0.05) - Tangent * 0.2, Normal * (Apothem + 0.04) - Tangent * 0.2,
		Normal * (Apothem + 0.04) + Tangent * 0.2, Layer);
	AddTriangle(FVector(-1, -1, -20), FVector(1, -1, -20), FVector(0, 1, -20), OtherLayer);
	auto& Shape = Data.Shapes.AddDefaulted_GetRef(); Shape.ShapeId = Owner; Shape.LayerId = Layer;
	FComposableCameraMeshEraseStamp Stamp; Stamp.Radius = 10; Stamp.Depth = 1;
	FEraseGeometryStats Stats; FBox2D Dirty(ForceInit);
	TestTrue(TEXT("Erase removes safe interior and clips boundary"), EraseShapeGeometry(Data, Layer, Stamp, true, &Stats, &Dirty));
	TestEqual(TEXT("Only the safely contained triangle skips plane splitting"), Stats.InteriorTriangles, 1);
	TestFalse(TEXT("Interior coverage disappears"), ShapeCoversPoint(Data, Layer, FVector2D::ZeroVector));
	const FVector OutsideCirclePolygon = Normal * (Apothem + 0.025);
	TestTrue(TEXT("Circle-only containment cannot erase the polygon's outside sliver"), ShapeCoversPoint(Data, Layer,
		FVector2D(OutsideCirclePolygon.X, OutsideCirclePolygon.Y)));
	const FVector BoundaryOutside = Normal * (Apothem + 0.02), BoundaryInside = Normal * (Apothem - 0.02) - Tangent * 0.12;
	TestTrue(TEXT("Boundary splitting keeps the exact outside part"), ShapeCoversPoint(Data, Layer, FVector2D(BoundaryOutside.X, BoundaryOutside.Y)));
	TestFalse(TEXT("Boundary splitting removes the exact inside part"), ShapeCoversPoint(Data, Layer, FVector2D(BoundaryInside.X, BoundaryInside.Y)));
	TestTrue(TEXT("Other Layer remains queryable with valid ownership and one retained mask"), Data.IsConsistent()
		&& ShapeCoversPoint(Data, OtherLayer, FVector2D::ZeroVector) && Data.Shapes[0].Erasures.Num() == 1 && Dirty.bIsValid);
	const auto Once = Data;
	TestFalse(TEXT("Repeated identical mask does not retessellate retained fragments"), EraseShapeGeometry(Data, Layer, Stamp));
	TestTrue(TEXT("Repeated cut preserves exact source and ownership"), Data.Vertices == Once.Vertices && Data.Indices == Once.Indices
		&& Data.TriangleShapeIds == Once.TriangleShapeIds && Data.Shapes[0].Erasures.Num() == 1);

	FComposableCameraMeshSurfaceAuthoringData Affine;
	Affine.Vertices = {FVector3f(-1, -1, 0), FVector3f(1, -1, 0), FVector3f(0, 1, 0),
		FVector3f(100, 100, 0), FVector3f(101, 100, 0), FVector3f(100, 101, 0)};
	Affine.Indices = {0, 1, 2, 3, 4, 5}; Affine.TriangleLayerIds = {Layer, OtherLayer};
	Stamp.AxisX = FVector(2, 0.4, 0.1); Stamp.AxisY = FVector(0.1, 0.7, 0.2); Stamp.AxisZ = FVector(0.3, 0.2, 1.3);
	FMeshLayerAuthoringIndex Index; Index.Build(Affine);
	TestTrue(TEXT("Conservative interior test supports scaled/sheared document axes"), EraseShapeGeometry(Affine, Layer, Stamp, false, &Stats, nullptr, &Index)
		&& Stats.InteriorTriangles == 1 && Affine.Indices.Num() == 3 && Affine.TriangleLayerIds[0] == OtherLayer && Index.IsCurrent(Affine));
	Stamp.Center = FVector(-1000, -1000, 0);
	FEraseGeometryBuild Empty;
	TestFalse(TEXT("Empty indexed candidates finish at Begin"), Empty.Begin(Affine, Layer, Stamp, true, &Index));
	TestTrue(TEXT("Rejected empty operation has no mutation, work or changed bounds"), Empty.Advance(Affine, &Index)
		&& !Empty.HasChanged() && !Empty.GetChangedBounds().bIsValid && Empty.GetStats().ConsideredTriangles == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshErasePickingOrderTest,
	"ComposableCameraSystem.Editor.MeshCamera.ErasePickingOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshErasePickingOrderTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	const FGuid Layer = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Data;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FComposableCameraMeshAuthoredShape Shape;
		Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = Layer;
		Shape.ControlPoints = {FVector2D(Index * 50.0, Index * 50.0), FVector2D(200.0 + Index * 50.0, 200.0 + Index * 50.0)};
		TArray<FVector2D> Outline; FComposableCameraMeshSurfaceAuthoringData Geometry;
		BuildShapeOutline(Shape, Outline);
		BuildProjectedShape(Outline, 1000.0, Layer, ProjectShapeSlope, Geometry);
		AppendShapeGeometry(Data, Geometry, Shape.ShapeId);
		Data.Shapes.Add(MoveTemp(Shape));
	}
	const FGuid LaterShape = Data.Shapes[1].ShapeId;
	const FVector Origin(100, 100, 300);
	TestEqual(TEXT("Same-surface overlap initially selects the later Shape"), FindShapeOnRay(Data, Layer, Origin, -FVector::UpVector), LaterShape);
	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Center = FVector(150, 150, 45); Stamp.Radius = 10.0; Stamp.Depth = 100.0;
	TestTrue(TEXT("Fixture cuts both overlapping Shapes"), EraseShapeGeometry(Data, Layer, Stamp));
	TestEqual(TEXT("Retessellation preserves later-Shape picking at untouched overlap"), FindShapeOnRay(Data, Layer, Origin, -FVector::UpVector), LaterShape);
	TestFalse(TEXT("Unused vertices in the erased hole cannot be picked"), FindShapeOnRay(Data, Layer, FVector(150, 150, 300), -FVector::UpVector).IsValid());
	TestTrue(TEXT("Both Shapes retain their cuts and consistent triangle ownership"), Data.IsConsistent()
		&& Data.Shapes[0].Erasures.Num() == 1 && Data.Shapes[1].Erasures.Num() == 1);
	FComposableCameraMeshAuthoredShape Below = Data.Shapes[1];
	Below.ShapeId = FGuid::NewGuid(); Below.Erasures.Reset();
	TArray<FVector2D> BelowOutline; FComposableCameraMeshSurfaceAuthoringData BelowGeometry;
	BuildShapeOutline(Below, BelowOutline);
	BuildProjectedShape(BelowOutline, 1000.0, Layer, ProjectShapeSlope, BelowGeometry);
	for (FVector3f& Vertex : BelowGeometry.Vertices) { Vertex.Z -= 100.0f; }
	AppendShapeGeometry(Data, BelowGeometry, Below.ShapeId); Data.Shapes.Add(MoveTemp(Below));
	TestEqual(TEXT("A newer Shape on a lower floor cannot override the nearer surface"), FindShapeOnRay(Data, Layer, Origin, -FVector::UpVector), LaterShape);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshStrokeRefreshTest,
	"ComposableCameraSystem.Editor.MeshCamera.StrokeVisualizationRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshStrokeRefreshTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transactions available"), GEditor)) { return false; }
	using namespace UE::ComposableCamera::MeshEditor;
	FComposableCameraMeshLayerEdMode Mode;
	Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers(); Mode.Settings->TouchDocument();
	Mode.SavedRevision = Mode.Settings->DocumentRevision;
	TArray<FVector2D> Outline;
	BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(200.0, 200.0), Outline);
	BuildProjectedShape(Outline, 1000.0, Mode.Settings->GetActiveLayerId(), ProjectShapeSlope, Mode.Settings->WorkingData);
	BuildAuthoringVisualization(Mode.Settings->WorkingData, Mode.Settings->Layers, Mode.Visualization);
	Mode.bVisualizationDirty = false;
	Mode.Settings->SetToolMode(EComposableCameraMeshToolMode::Erase);
	Mode.Settings->BrushRadius = 25.0;
	Mode.bHasHoverHit = true; Mode.HoverHit.ImpactNormal = FVector::UpVector;
	Mode.HoverHit.ImpactPoint = FVector(100.0, 100.0, 30.0);
	Mode.BeginStroke();
	TestTrue(TEXT("Actual Erase stroke modifies its document"), Mode.PaintAtHover(nullptr));
	TestFalse(TEXT("A stamp leaves the already updated visualization clean"), Mode.bVisualizationDirty);
	TestTrue(TEXT("Stamp updates saved-revision dirty state immediately"), Mode.IsDirty());
	const auto* CellsBeforeRelease = Mode.GetVisualization().Cells.GetData();
	const int32 CellsBeforeReleaseCount = Mode.GetVisualization().Cells.Num();
	TestTrue(TEXT("Mouse release completes the real stroke input path"), Mode.InputKey(nullptr, nullptr, EKeys::LeftMouseButton, IE_Released));
	TestFalse(TEXT("Release retains already current coverage without a full rebuild"), Mode.bVisualizationDirty);
	TestTrue(TEXT("Release keeps the coverage cache and its cells"), CellsBeforeRelease == Mode.GetVisualization().Cells.GetData()
		&& CellsBeforeReleaseCount == Mode.GetVisualization().Cells.Num());
	TestFalse(TEXT("Release closes its editor transaction"), GEditor->IsTransactionActive());
	const auto BeforeCancel = Mode.Settings->WorkingData;
	const FGuid BeforeCancelRevision = Mode.Settings->DocumentRevision;
	Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(150.0, 100.0, 35.0);
	TestTrue(TEXT("Another stroke cuts different coverage"), Mode.PaintAtHover(nullptr));
	Mode.FinishStroke(true);
	TestTrue(TEXT("Cancelled stroke restores source and revision"), Mode.Settings->WorkingData.Indices == BeforeCancel.Indices
		&& Mode.Settings->WorkingData.Vertices == BeforeCancel.Vertices && Mode.Settings->DocumentRevision == BeforeCancelRevision);
	TestTrue(TEXT("Cancellation invalidates changed preview for restored source"), Mode.bVisualizationDirty);
	TestFalse(TEXT("Cancellation leaves no active transaction"), GEditor->IsTransactionActive());
	Mode.BeginStroke();
	Mode.HoverHit.ImpactPoint = FVector(10000.0, 10000.0, 30.0);
	TestFalse(TEXT("Empty-region erasing is a no-op"), Mode.PaintAtHover(nullptr));
	const FVector LastAttempt = Mode.LastPaintWorldPosition;
	Mode.HoverHit.ImpactPoint.X += 1.0;
	TestFalse(TEXT("Nearby mouse movement does not retry the no-op stamp"), Mode.PaintAtHover(nullptr));
	TestTrue(TEXT("Spacing applies to attempted stamps, not only successful cuts"), Mode.LastPaintWorldPosition.Equals(LastAttempt));
	Mode.FinishStroke();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshDocumentUndoTest,
	"ComposableCameraSystem.Editor.MeshCamera.DocumentUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshDocumentUndoTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transactions available"), GEditor)) { return false; }
	using namespace UE::ComposableCamera::MeshEditor;
	FComposableCameraMeshLayerEdMode Mode;
	Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers(); Mode.Settings->TouchDocument();
	Mode.SavedRevision = Mode.Settings->DocumentRevision;
	Mode.Settings->OnLayerDataChanged.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::MarkLayerDataDirty);
	Mode.SelectionEditor->OnBeforeEdit.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::BeforeSelectionEdit);
	Mode.SelectionEditor->OnLayerEdited.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::ApplySelectedLayer);
	Mode.RefreshSelectionEditor();
	GEditor->RegisterForUndo(&Mode);
	FComposableCameraMeshAuthoredShape Shape;
	Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = Mode.Settings->GetActiveLayerId(); Shape.ControlPoints = {{0.0, 0.0}, {200.0, 200.0}};
	TArray<FVector2D> Outline; FComposableCameraMeshSurfaceAuthoringData Geometry;
	BuildShapeOutline(Shape, Outline); BuildProjectedShape(Outline, 100.0, Shape.LayerId, ProjectShapeSlope, Geometry);
	Mode.ApplyShapeGeometry(Shape, Geometry, true);
	TestTrue(TEXT("Drawing dirties document"), Mode.IsDirty());
	GEditor->UndoTransaction();
	TestTrue(TEXT("Undo restores source and clean checkpoint together"), Mode.Settings->WorkingData.Shapes.IsEmpty() && Mode.Settings->WorkingData.Indices.IsEmpty() && !Mode.IsDirty());
	GEditor->RedoTransaction();
	TestTrue(TEXT("Redo restores Shape identity and geometry"), Mode.Settings->WorkingData.Shapes.Num() == 1 && Mode.Settings->WorkingData.Shapes[0].ShapeId == Shape.ShapeId && Mode.IsDirty());
	// Simulate a successful Save checkpoint, then undo/redo around that checkpoint.
	Mode.SavedRevision = Mode.Settings->DocumentRevision; Mode.RefreshDocumentState();
	GEditor->UndoTransaction(); TestTrue(TEXT("Undo away from saved source enables Save"), Mode.IsDirty());
	GEditor->RedoTransaction(); TestFalse(TEXT("Redo back to saved revision clears dirty state"), Mode.IsDirty());

	Mode.Settings->SetToolMode(EComposableCameraMeshToolMode::Erase);
	Mode.BeginStroke();
	FHitResult Hit; Hit.ImpactPoint = FVector(100.0, 100.0, 30.0); Hit.ImpactNormal = FVector::UpVector;
	Mode.Settings->BrushRadius = 25.0;
	Mode.HoverHit = Hit; Mode.bHasHoverHit = true;
	TestTrue(TEXT("Actual Erase painting path changes source and revision"), Mode.PaintAtHover(nullptr));
	Hit.ImpactPoint = FVector(150.0, 150.0, 45.0);
	Mode.HoverHit = Hit;
	TestTrue(TEXT("Continued Erase drag adds another stamp"), Mode.PaintAtHover(nullptr));
	Mode.FinishStroke(); Mode.RefreshDocumentState();
	GEditor->UndoTransaction();
	TestTrue(TEXT("One Undo restores whole multi-stamp stroke"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(100.0, 100.0)) && ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(150.0, 150.0)));
	TestFalse(TEXT("Undo stroke restores saved checkpoint"), Mode.IsDirty());
	GEditor->RedoTransaction();
	TestFalse(TEXT("Redo erases first stamp"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(100.0, 100.0)));
	TestFalse(TEXT("Redo erases second stamp"), ShapeCoversPoint(Mode.Settings->WorkingData, Shape.LayerId, FVector2D(150.0, 150.0)));
	Mode.Settings->AddLayer();
	const FGuid AddedLayer = Mode.Settings->GetActiveLayerId();
	GEditor->UndoTransaction(); TestEqual(TEXT("Layer creation undoes"), Mode.Settings->Layers.Num(), 1);
	GEditor->RedoTransaction(); TestTrue(TEXT("Layer creation redoes with stable GUID"), Mode.Settings->Layers.Num() == 2 && Mode.Settings->Layers[1].LayerId == AddedLayer);
	Mode.Settings->SelectLayer(Shape.LayerId); Mode.Settings->RemoveActiveLayer();
	TestTrue(TEXT("Layer deletion prunes geometry and Shape records together"), Mode.Settings->WorkingData.Shapes.IsEmpty() && Mode.Settings->WorkingData.Indices.IsEmpty());
	GEditor->UndoTransaction();
	TestTrue(TEXT("Layer deletion Undo restores geometry, masks and GUIDs"), Mode.Settings->WorkingData.Shapes.Num() == 1 && Mode.Settings->WorkingData.Shapes[0].ShapeId == Shape.ShapeId && Mode.Settings->WorkingData.Shapes[0].Erasures.Num() == 2);

	FPropertyEditorModule& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	const TSharedPtr<ISinglePropertyView> LayerView = Properties.CreateSingleProperty(Mode.SelectionEditor, TEXT("Layer"), FSinglePropertyParams());
	const TSharedPtr<IPropertyHandle> NameHandle = LayerView.IsValid() ? LayerView->GetPropertyHandle()->GetChildHandle(TEXT("Name")) : nullptr;
	if (TestTrue(TEXT("Actual Details proxy exposes Layer Name"), NameHandle.IsValid()))
	{
		const FName OriginalName = Mode.Settings->Layers[Mode.Settings->ActiveLayerIndex].Name;
		TestTrue(TEXT("Details commits Layer name"), NameHandle->SetValue(FName(TEXT("UndoProxyName"))) == FPropertyAccess::Success);
		TestEqual(TEXT("Details updates document owner"), Mode.Settings->Layers[Mode.Settings->ActiveLayerIndex].Name, FName(TEXT("UndoProxyName")));
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo Details restores document and visible proxy"), Mode.SelectionEditor->Layer.Name, OriginalName);
		GEditor->RedoTransaction();
		TestEqual(TEXT("Redo Details restores document owner"), Mode.Settings->Layers[Mode.Settings->ActiveLayerIndex].Name, FName(TEXT("UndoProxyName")));
	}
	const TSharedPtr<IPropertyHandle> ChannelHandle = LayerView.IsValid() ? LayerView->GetPropertyHandle()->GetChildHandle(TEXT("TraceChannel")) : nullptr;
	if (TestTrue(TEXT("Actual Layer Details exposes Channel"), ChannelHandle.IsValid()))
	{
		TestTrue(TEXT("Details commits Layer Channel"), ChannelHandle->SetValue(static_cast<uint8>(ECC_Camera)) == FPropertyAccess::Success);
		TestEqual(TEXT("Channel edit updates the document"), Mode.Settings->Layers[Mode.Settings->ActiveLayerIndex].TraceChannel.GetValue(), ECC_Camera);
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo Channel restores the visible proxy"), Mode.SelectionEditor->Layer.TraceChannel.GetValue(), ECC_Visibility);
		GEditor->RedoTransaction();
		TestTrue(TEXT("Redo Channel restores value without changing Layer identity"),
			Mode.Settings->Layers[Mode.Settings->ActiveLayerIndex].TraceChannel == ECC_Camera
			&& Mode.Settings->GetActiveLayerId() == Shape.LayerId);
	}
	// Select edits share this final geometry replacement path after collision validation.
	FComposableCameraMeshAuthoredShape Edited = Mode.Settings->WorkingData.Shapes[0];
	Edited.ControlPoints = {{50.0, 50.0}, {250.0, 250.0}};
	BuildShapeOutline(Edited, Outline); BuildProjectedShape(Outline, 100.0, Edited.LayerId, ProjectShapeSlope, Geometry);
	Mode.ApplyShapeGeometry(Edited, Geometry, true);
	GEditor->UndoTransaction();
	TestTrue(TEXT("Select geometry Undo restores controls and cuts"), Mode.Settings->WorkingData.Shapes[0].ControlPoints[0].Equals(FVector2D::ZeroVector) && Mode.Settings->WorkingData.Shapes[0].Erasures.Num() == 2);
	GEditor->RedoTransaction();
	TestTrue(TEXT("Select geometry Redo restores adjusted controls"), Mode.Settings->WorkingData.Shapes[0].ControlPoints[0].Equals(FVector2D(50.0, 50.0)));
	Mode.DeleteSelectedShape();
	TestTrue(TEXT("Selected Shape deletion removes retained source"), Mode.Settings->WorkingData.Shapes.IsEmpty());
	GEditor->UndoTransaction();
	TestTrue(TEXT("Deletion Undo restores edited Shape with cuts"), Mode.Settings->WorkingData.Shapes.Num() == 1 && Mode.Settings->WorkingData.Shapes[0].ShapeId == Shape.ShapeId && Mode.Settings->WorkingData.Shapes[0].Erasures.Num() == 2);
	GEditor->RedoTransaction();
	TestTrue(TEXT("Deletion Redo removes the same Shape"), Mode.Settings->WorkingData.Shapes.IsEmpty());
	GEditor->UndoTransaction();
	for (EComposableCameraMeshShapeType Type : { EComposableCameraMeshShapeType::Circle, EComposableCameraMeshShapeType::Polygon })
	{
		FComposableCameraMeshAuthoredShape Added;
		Added.ShapeId = FGuid::NewGuid(); Added.LayerId = Shape.LayerId; Added.Type = Type;
		Added.ControlPoints = Type == EComposableCameraMeshShapeType::Circle
			? TArray<FVector2D>{{300.0, 300.0}, {350.0, 300.0}}
			: TArray<FVector2D>{{300.0, 300.0}, {350.0, 300.0}, {325.0, 350.0}};
		BuildShapeOutline(Added, Outline);
		TestTrue(TEXT("Circle/Polygon test geometry validates and projects"), BuildProjectedShape(Outline, 100.0, Added.LayerId, ProjectShapeSlope, Geometry) == EShapeBuildResult::Success);
		Mode.ApplyShapeGeometry(Added, Geometry, true);
		GEditor->UndoTransaction();
		TestEqual(TEXT("Circle/Polygon creation undoes independently"), Mode.Settings->WorkingData.Shapes.Num(), 1);
		GEditor->RedoTransaction();
		TestTrue(TEXT("Circle/Polygon creation redoes with stable identity"), Mode.Settings->WorkingData.Shapes.Num() == 2 && Mode.Settings->WorkingData.Shapes[1].ShapeId == Added.ShapeId);
		GEditor->UndoTransaction();
	}
	TestFalse(TEXT("Draw / Select / Erase operations leave no active transaction"), GEditor->IsTransactionActive());
	Mode.Settings->OnLayerDataChanged.Unbind();
	Mode.SelectionEditor->OnBeforeEdit.Unbind();
	Mode.SelectionEditor->OnLayerEdited.Unbind();
	GEditor->UnregisterForUndo(&Mode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshDiscardTest,
	"ComposableCameraSystem.Editor.MeshCamera.DiscardWorkingDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshDiscardTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transactions available"), GEditor)) { return false; }
	using namespace UE::ComposableCamera::MeshEditor;
	FComposableCameraMeshLayerEdMode Mode;
	Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers(); Mode.Settings->TouchDocument();
	Mode.Settings->Layers[0].Name = TEXT("OpeningLayer");
	Mode.Settings->Layers[0].TraceChannel = ECC_Camera;
	Mode.Settings->Layers[0].Profile = NewObject<UComposableCameraMeshProfile>();
	Mode.CaptureSavedDocument();
	const FGuid OpeningLayer = Mode.Settings->GetActiveLayerId();
	const FGuid OpeningRevision = Mode.SavedRevision;
	Mode.Settings->OnLayerDataChanged.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::MarkLayerDataDirty);
	GEditor->RegisterForUndo(&Mode);
	TestFalse(TEXT("Clean document disables Discard"), Mode.CanDiscardWorkingData());
	TestFalse(TEXT("Checkpoint stays outside editor transactions"), Mode.SavedDocument->HasAnyFlags(RF_Transactional));

	FComposableCameraMeshAuthoredShape Shape;
	Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = OpeningLayer; Shape.ControlPoints = {{0.0, 0.0}, {200.0, 200.0}};
	TArray<FVector2D> Outline; FComposableCameraMeshSurfaceAuthoringData Geometry;
	BuildShapeOutline(Shape, Outline); BuildProjectedShape(Outline, 100.0, Shape.LayerId, ProjectShapeSlope, Geometry);
	Mode.ApplyShapeGeometry(Shape, Geometry, true);
	Mode.Settings->AddLayer();
	{
		const FScopedTransaction Transaction(NSLOCTEXT("MeshDiscardTest", "EditLayer", "Edit Layer before Discard"));
		Mode.Settings->Modify();
		Mode.Settings->Layers[0].Name = TEXT("UnsavedLayer");
		Mode.Settings->Layers[0].TraceChannel = ECC_Visibility;
		Mode.Settings->Layers[0].DebugColor = FLinearColor::Red;
		Mode.Settings->Layers[0].Profile = nullptr;
		Mode.Settings->NotifyLayerDataChanged();
	}
	TestTrue(TEXT("Committed changes enable Discard"), Mode.CanDiscardWorkingData());
	TestTrue(TEXT("Discard restores initial checkpoint before any Save"), Mode.DiscardWorkingData());
	TestEqual(TEXT("Discard restores the checkpoint's Layer Channel"), Mode.Settings->Layers[0].TraceChannel.GetValue(), ECC_Camera);
	TestTrue(TEXT("Discard restores Layer identity, properties and reflected asset reference"), Mode.Settings->Layers.Num() == 1
		&& Mode.Settings->Layers[0].LayerId == OpeningLayer && Mode.Settings->Layers[0].Name == FName(TEXT("OpeningLayer"))
		&& Mode.Settings->Layers[0].Profile == Mode.SavedDocument->Layers[0].Profile
		&& Mode.Settings->Layers[0].DebugColor == Mode.SavedDocument->Layers[0].DebugColor);
	TestTrue(TEXT("Discard restores empty source and clean revision"), Mode.Settings->WorkingData.Shapes.IsEmpty()
		&& Mode.Settings->WorkingData.Indices.IsEmpty() && Mode.Settings->DocumentRevision == OpeningRevision && !Mode.IsDirty());
	GEditor->UndoTransaction();
	TestTrue(TEXT("One Undo restores all discarded committed changes"), Mode.Settings->Layers.Num() == 2
		&& Mode.Settings->Layers[0].Name == FName(TEXT("UnsavedLayer")) && Mode.Settings->WorkingData.Shapes.Num() == 1 && Mode.IsDirty());
	GEditor->RedoTransaction();
	TestTrue(TEXT("Redo Discard returns to checkpoint"), Mode.Settings->Layers.Num() == 1 && Mode.Settings->WorkingData.Shapes.IsEmpty() && !Mode.CanDiscardWorkingData());
	GEditor->UndoTransaction();
	Mode.Settings->SelectLayer(OpeningLayer);
	Mode.Settings->SetToolMode(EComposableCameraMeshToolMode::Erase);
	Mode.Settings->BrushRadius = 25.0;
	Mode.HoverHit.ImpactNormal = FVector::UpVector; Mode.HoverHit.ImpactPoint = FVector(60.0, 60.0, 18.0); Mode.bHasHoverHit = true;
	Mode.BeginStroke();
	TestTrue(TEXT("Checkpoint includes a real retained erase stamp"), Mode.PaintAtHover(nullptr));
	Mode.FinishStroke();
	// Simulate the successful-save branch without opening checkout/package UI.
	Mode.CaptureSavedDocument(); Mode.RefreshDocumentState();
	const auto CheckpointSource = Mode.Settings->WorkingData;
	const FGuid CheckpointRevision = Mode.SavedRevision;
	Mode.BeginStroke(); Mode.HoverHit.ImpactPoint = FVector(150.0, 150.0, 45.0);
	TestTrue(TEXT("Later Erase modifies different coverage"), Mode.PaintAtHover(nullptr)); Mode.FinishStroke();
	Mode.Settings->AddLayer();
	Mode.Settings->BrushRadius = 42.0; Mode.Settings->ShapeGridSize = 11.0;
	Mode.SelectedShapeId = Shape.ShapeId; Mode.ShapeFeedback = FText::FromString(TEXT("Old feedback"));
	TestTrue(TEXT("Discard restores most recent successful checkpoint"), Mode.DiscardWorkingData());
	TestTrue(TEXT("Latest checkpoint retains source indices, vertices and ownership"), Mode.Settings->WorkingData.Vertices == CheckpointSource.Vertices
		&& Mode.Settings->WorkingData.Indices == CheckpointSource.Indices && Mode.Settings->WorkingData.TriangleLayerIds == CheckpointSource.TriangleLayerIds
		&& Mode.Settings->WorkingData.TriangleShapeIds == CheckpointSource.TriangleShapeIds);
	TestTrue(TEXT("Latest checkpoint retains Shape controls and erasures"), Mode.Settings->WorkingData.Shapes.Num() == 1
		&& Mode.Settings->WorkingData.Shapes[0].ShapeId == Shape.ShapeId && Mode.Settings->WorkingData.Shapes[0].ControlPoints == Shape.ControlPoints
		&& Mode.Settings->WorkingData.Shapes[0].Erasures.Num() == 1);
	TestTrue(TEXT("Saved cut remains, unsaved cut disappears"), !ShapeCoversPoint(Mode.Settings->WorkingData, OpeningLayer, FVector2D(60.0, 60.0))
		&& ShapeCoversPoint(Mode.Settings->WorkingData, OpeningLayer, FVector2D(150.0, 150.0)));
	TestTrue(TEXT("Discard preserves tool preferences and clears stale selection/feedback"), Mode.Settings->BrushRadius == 42.0
		&& Mode.Settings->ShapeGridSize == 11.0 && !Mode.SelectedShapeId.IsValid() && Mode.GetStatusText().IsEmpty()
		&& !Mode.SelectionEditor->bHasShape && Mode.bVisualizationDirty && Mode.Settings->DocumentRevision == CheckpointRevision);
	GEditor->UndoTransaction();
	TestTrue(TEXT("Undo latest Discard restores Layer creation and both erasures"), Mode.Settings->Layers.Num() == 3
		&& Mode.Settings->WorkingData.Shapes.Num() == 1 && Mode.Settings->WorkingData.Shapes[0].Erasures.Num() == 2 && Mode.IsDirty());
	GEditor->RedoTransaction();
	TestFalse(TEXT("Redo clears dirty state again"), Mode.IsDirty());

	FComposableCameraMeshAuthoredShape PendingShape = Shape;
	PendingShape.ShapeId = FGuid::NewGuid();
	TestTrue(TEXT("Released Shape can queue against a clean checkpoint"), Mode.QueueShapeCreation(PendingShape, nullptr));
	Mode.bDrawingShape = true; Mode.ShapePoints = {{0.0, 0.0}, {100.0, 100.0}};
	Mode.ShapeMeasurement = FText::FromString(TEXT("Draft"));
	TestTrue(TEXT("Pending creation and draft enable Discard"), Mode.CanDiscardWorkingData());
	TestTrue(TEXT("Discard cancels queued creation and active draft"), Mode.DiscardWorkingData());
	TestTrue(TEXT("Cancelled jobs cannot publish and source stays at checkpoint"), !Mode.IsCreatingShapes() && !Mode.bDrawingShape
		&& Mode.ShapePoints.IsEmpty() && Mode.GetStatusText().IsEmpty() && Mode.Settings->WorkingData.Indices == CheckpointSource.Indices && !Mode.IsDirty());
	GEditor->UndoTransaction();
	TestTrue(TEXT("Draft-only Discard adds no Undo step"), Mode.Settings->Layers.Num() == 3 && Mode.Settings->WorkingData.Shapes.Num() == 1
		&& Mode.Settings->WorkingData.Shapes[0].Erasures.Num() == 2);
	GEditor->RedoTransaction();
	Mode.bHasHoverHit = true; Mode.HoverHit.ImpactPoint = FVector(150.0, 150.0, 45.0);
	Mode.BeginStroke(); TestTrue(TEXT("Unfinished Erase changes source"), Mode.PaintAtHover(nullptr));
	TestTrue(TEXT("Discard reverts unfinished stroke and closes its transaction"), Mode.DiscardWorkingData());
	TestTrue(TEXT("Stroke cancellation restores exact source without leaving active Undo"), Mode.Settings->WorkingData.Indices == CheckpointSource.Indices
		&& Mode.Settings->WorkingData.Vertices == CheckpointSource.Vertices && !Mode.bPainting && !Mode.StrokeTransaction && !GEditor->IsTransactionActive());

	// Save applies actor data before package IO. A failed save must not advance
	// the independent checkpoint, even when that actor already holds unsaved data.
	AComposableCameraMeshSurfaceStorageActor* Actor = NewObject<AComposableCameraMeshSurfaceStorageActor>(GetTransientPackage(), NAME_None, RF_Transactional | RF_Transient);
	Actor->SetAuthoringData(Mode.SavedDocument->Layers, CheckpointSource);
	{
		const FScopedTransaction Transaction(NSLOCTEXT("MeshDiscardTest", "FailedSaveEdit", "Edit Layer before failed Save"));
		Mode.Settings->Modify(); Mode.Settings->Layers[0].Name = TEXT("FailedSaveName"); Mode.Settings->NotifyLayerDataChanged();
	}
	Actor->SetAuthoringData(Mode.Settings->Layers, Mode.Settings->WorkingData);
	Mode.StorageActor = Actor; Mode.PendingSaveActor = Actor;
	Mode.bPendingSaveNeedsDiscard = true;
	GEditor->UndoTransaction();
	TestTrue(TEXT("Undo source alone cannot hide actor data applied by failed Save"), Mode.Settings->DocumentRevision == CheckpointRevision
		&& Mode.IsDirty() && Mode.CanDiscardWorkingData());
	GEditor->RedoTransaction();
	TestTrue(TEXT("Discard restores checkpoint after a failed package save"), Mode.DiscardWorkingData());
	TestTrue(TEXT("Working document and applied actor return to checkpoint together"), Actor->GetLayers()[0].Name == Mode.SavedDocument->Layers[0].Name
		&& Mode.Settings->Layers[0].Name == Mode.SavedDocument->Layers[0].Name && !Mode.IsDirty());
	GEditor->UndoTransaction();
	TestTrue(TEXT("Undo Discard restores failed-save document and actor"), Actor->GetLayers()[0].Name == FName(TEXT("FailedSaveName"))
		&& Mode.Settings->Layers[0].Name == FName(TEXT("FailedSaveName")) && Mode.IsDirty());
	TestTrue(TEXT("Repeated Discard after Undo also restores actor"), Mode.DiscardWorkingData());
	TestEqual(TEXT("Actor never replaces successful checkpoint"), Actor->GetLayers()[0].Name, Mode.SavedDocument->Layers[0].Name);
	TestFalse(TEXT("Discard disables itself after failed-save actor rollback"), Mode.CanDiscardWorkingData());
	Mode.bHasHoverHit = true; Mode.HoverHit.ImpactPoint = FVector(150.0, 150.0, 45.0);
	Mode.BeginStroke(); TestTrue(TEXT("Source-only failed Save fixture erases coverage"), Mode.PaintAtHover(nullptr)); Mode.FinishStroke();
	Actor->SetAuthoringData(Mode.Settings->Layers, Mode.Settings->WorkingData); Mode.bPendingSaveNeedsDiscard = true;
	GEditor->UndoTransaction();
	TestTrue(TEXT("Final Undo detects failed-save geometry with unchanged Layer properties"), Mode.Settings->DocumentRevision == CheckpointRevision && Mode.CanDiscardWorkingData());
	TestTrue(TEXT("Discard can restore only the failed-save actor when source is clean"), Mode.DiscardWorkingData());
	TestTrue(TEXT("Actor-only rollback restores full source"), Actor->GetAuthoringData().Indices == CheckpointSource.Indices
		&& Actor->GetAuthoringData().Vertices == CheckpointSource.Vertices && !Mode.CanDiscardWorkingData());
	GEditor->UndoTransaction(); TestTrue(TEXT("Actor-only Discard is undoable"), Mode.CanDiscardWorkingData());
	GEditor->RedoTransaction(); TestFalse(TEXT("Actor-only Discard redo clears rollback state"), Mode.CanDiscardWorkingData());
	Mode.Settings->OnLayerDataChanged.Unbind();
	GEditor->UnregisterForUndo(&Mode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshToolPanelsTest,
	"ComposableCameraSystem.Editor.MeshCamera.ToolPanels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshToolPanelsTest::RunTest(const FString&)
{
	FComposableCameraMeshLayerEdMode Mode;
	Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>(GetTransientPackage(), NAME_None, RF_Transactional);
	Mode.Settings->Layers.AddDefaulted(); Mode.Settings->NormalizeLayers(); Mode.Settings->TouchDocument();
	Mode.SavedRevision = Mode.Settings->DocumentRevision;
	Mode.Settings->SetDrawTool(EComposableCameraMeshDrawTool::Circle);
	auto& Shape = Mode.Settings->WorkingData.Shapes.AddDefaulted_GetRef();
	Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = Mode.Settings->GetActiveLayerId();
	Shape.Type = EComposableCameraMeshShapeType::Circle; Shape.ControlPoints = {{0.0, 0.0}, {100.0, 0.0}};
	Mode.SelectedShapeId = Shape.ShapeId;
	Mode.RefreshSelectionEditor();
	Mode.Settings->OnToolSettingsChanged.BindRaw(&Mode, &FComposableCameraMeshLayerEdMode::HandleToolSettingsChanged);
	const FGuid Revision = Mode.Settings->DocumentRevision;
	const FGuid SelectedId = Mode.SelectedShapeId;
	const auto Toolkit = MakeShared<FComposableCameraMeshLayerModeToolkit>(&Mode);
	Toolkit->InitializeDetailsViews();
	const auto Panel = Toolkit->BuildToolPanel();
	const auto ContainsProperty = [](const TSharedPtr<IDetailsView>& View, FName Name)
	{
		for (const FPropertyPath& Path : View->GetPropertiesInOrderDisplayed())
		{
			const FProperty* Property = Path.GetLeafMostProperty().Property.Get();
			if (Property && Property->GetFName() == Name) { return true; }
		}
		return false;
	};
	TestTrue(TEXT("Draw panel exposes grid for the remembered Circle tool"), ContainsProperty(Toolkit->ToolDetailsView, TEXT("ShapeGridSize")));
	TestFalse(TEXT("Mixed Tool enum is absent from the options"), ContainsProperty(Toolkit->ToolDetailsView, TEXT("Tool")));
	TestFalse(TEXT("Layer Details does not contain Shape parameters"), ContainsProperty(Toolkit->LayerDetailsView, TEXT("Radius")));
	TestFalse(TEXT("Shape Details does not contain Layer parameters"), ContainsProperty(Toolkit->ShapeDetailsView, TEXT("Name")));
	for (FName Name : { FName(TEXT("ShapeType")), FName(TEXT("Position")), FName(TEXT("Size")), FName(TEXT("Radius")), FName(TEXT("Vertices")) })
	{
		const FProperty* Property = FindFProperty<FProperty>(UComposableCameraMeshLayerSelection::StaticClass(), Name);
		TestTrue(TEXT("Shape selection state never exposes an edit-condition checkbox"), Property && Property->HasMetaData(TEXT("HideEditConditionToggle")));
	}
	TestTrue(TEXT("Draw type selector is visible"), Panel->GetChildren()->GetChildAt(1)->GetVisibility() == EVisibility::Visible);
	TestTrue(TEXT("Shape panel is hidden outside Select"), Panel->GetChildren()->GetChildAt(3)->GetVisibility() == EVisibility::Collapsed);

	Mode.bDrawingShape = true;
	Toolkit->HandleToolModeChanged(EComposableCameraMeshToolMode::Select);
	TestFalse(TEXT("Mode change cancels an unfinished drawing"), Mode.bDrawingShape);
	// Rebuild directly outside property callbacks to inspect the actual filtering.
	// SelectionDetailsRefresh separately exercises the deferred ticker lifecycle.
	Toolkit->ToolDetailsView->ForceRefresh();
	Toolkit->ShapeDetailsView->ForceRefresh();
	TestTrue(TEXT("Select exposes edit snapping"), ContainsProperty(Toolkit->ToolDetailsView, TEXT("ShapeGridSize")));
	TestFalse(TEXT("Select omits drawing projection options"), ContainsProperty(Toolkit->ToolDetailsView, TEXT("ProjectionDistance")));
	TestTrue(TEXT("Select shows retained Circle parameters"), ContainsProperty(Toolkit->ShapeDetailsView, TEXT("Radius")));
	TestTrue(TEXT("Draw selector hides in Select"), Panel->GetChildren()->GetChildAt(1)->GetVisibility() == EVisibility::Collapsed);
	TestTrue(TEXT("Select shows Shape panel"), Panel->GetChildren()->GetChildAt(3)->GetVisibility() == EVisibility::Visible);

	Toolkit->HandleToolModeChanged(EComposableCameraMeshToolMode::Erase);
	Toolkit->ToolDetailsView->ForceRefresh();
	TestTrue(TEXT("Erase exposes its brush radius"), ContainsProperty(Toolkit->ToolDetailsView, TEXT("BrushRadius")));
	TestFalse(TEXT("Erase omits Shape snapping"), ContainsProperty(Toolkit->ToolDetailsView, TEXT("ShapeGridSize")));
	TestTrue(TEXT("Erase hides Shape editing panel"), Panel->GetChildren()->GetChildAt(3)->GetVisibility() == EVisibility::Collapsed);
	Toolkit->HandleToolModeChanged(EComposableCameraMeshToolMode::Draw);
	TestTrue(TEXT("Returning to Draw restores Circle instead of Brush"), Mode.Settings->Tool == EComposableCameraMeshDrawTool::Circle);
	TestTrue(TEXT("Mode changes preserve selected Shape and source revision"), Mode.SelectedShapeId == SelectedId && Mode.Settings->DocumentRevision == Revision && !Mode.IsDirty());
	Mode.Settings->OnToolSettingsChanged.Unbind();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshToolSliderTest,
	"ComposableCameraSystem.Editor.MeshCamera.ToolSliderTransactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshToolSliderTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor transactions available"), GEditor)) { return false; }
	if (!TestFalse(TEXT("Slider test starts outside an existing editor transaction"), GEditor->IsTransactionActive())) { return false; }
	const auto State = MakeShared<FMeshToolSliderState>();
	FPropertyEditorModule& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	State->View = Properties.CreateDetailView(FDetailsViewArgs());
	const TWeakPtr<FMeshToolSliderState> WeakState = State;
	State->View->RegisterInstancedCustomPropertyLayout(UComposableCameraMeshLayerToolSettings::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([WeakState]() -> TSharedRef<IDetailCustomization>
		{
			return MakeShared<FMeshToolSliderCustomization>(WeakState);
		}));
	State->View->SetObject(State->Settings.Get());
	if (!TestTrue(TEXT("Tool Details builds its initial layout"), State->LayoutBuilds > 0)) { return false; }
	State->Toolkit = MakeShared<FComposableCameraMeshLayerModeToolkit>(nullptr);
	State->Toolkit->ToolDetailsView = State->View;
	State->Settings->OnToolSettingsChanged.BindLambda([WeakState]()
	{
		if (const auto Pinned = WeakState.Pin()) { ++Pinned->Notifications; Pinned->Toolkit->RefreshSelectionDetails(); }
	});
	const auto Command = MakeShared<FMeshToolSliderCommand>(this, State);
	if (!Command->BeginPhase()) { return false; }
	FAutomationTestFramework::Get().EnqueueLatentCommand(Command);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshSelectionDetailsRefreshTest,
	"ComposableCameraSystem.Editor.MeshCamera.SelectionDetailsRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshSelectionDetailsRefreshTest::RunTest(const FString&)
{
	const TSharedRef<FMeshSelectionDetailsRefreshState> State = MakeShared<FMeshSelectionDetailsRefreshState>();
	FPropertyEditorModule& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	State->View = Properties.CreateDetailView(FDetailsViewArgs());
	const TWeakPtr<FMeshSelectionDetailsRefreshState> WeakState = State;
	State->View->RegisterInstancedCustomPropertyLayout(UComposableCameraMeshLayerSelection::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([WeakState]() -> TSharedRef<IDetailCustomization>
		{
			return MakeShared<FMeshSelectionRefreshTestCustomization>(WeakState);
		}));
	State->View->SetObject(State->Selection.Get());
	if (!TestTrue(TEXT("Selection Details builds its initial layout"), State->LayoutBuilds > 0)) { return false; }
	State->Toolkit = MakeShared<FComposableCameraMeshLayerModeToolkit>(nullptr);
	State->Toolkit->LayerDetailsView = State->View;
	const int32 PreviousBuilds = State->LayoutBuilds;
	State->Toolkit->RefreshSelectionDetails();
	State->Toolkit->RefreshSelectionDetails();
	TestEqual(TEXT("Refresh stays deferred beyond the request call"), State->LayoutBuilds, PreviousBuilds);
	ADD_LATENT_AUTOMATION_COMMAND(FMeshSelectionRefreshCompleteCommand(this, State, PreviousBuilds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshIndexedEraseEquivalenceTest,
	"ComposableCameraSystem.Editor.MeshCamera.IndexedEraseEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshIndexedEraseEquivalenceTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	const FGuid LayerId = FGuid::NewGuid(), ShapeId = FGuid::NewGuid();
	TArray<FVector2D> Outline; BuildRectangleOutline(FVector2D::ZeroVector, FVector2D(200, 200), Outline);
	FComposableCameraMeshSurfaceAuthoringData Near; BuildProjectedShape(Outline, 1000.0, LayerId, ProjectShapeSlope, Near);
	FComposableCameraMeshSurfaceAuthoringData Indexed;
	AppendShapeGeometry(Indexed, Near, ShapeId);
	FComposableCameraMeshAuthoredShape Shape; Shape.ShapeId = ShapeId; Shape.LayerId = LayerId; Indexed.Shapes.Add(Shape);
	for (int32 Copy = 0; Copy < 1024; ++Copy)
	{
		auto Far = Near;
		for (auto& Vertex : Far.Vertices) { Vertex.X += 1000.0f + Copy * 250.0f; }
		AppendShapeGeometry(Indexed, Far, FGuid());
	}
	auto Reference = Indexed;
	FMeshLayerAuthoringIndex Index; Index.Build(Indexed);
	FComposableCameraMeshEraseStamp Stamp;
	Stamp.Radius = 25.0; Stamp.Depth = 100.0;
	Stamp.AxisX = FVector(2.0, 0.35, 0.0); Stamp.AxisY = FVector(-0.2, 1.5, 0.0); Stamp.AxisZ = FVector(-0.1, -0.2, 1.0);
	auto SameSource = [&]()
	{
		return Indexed.Vertices == Reference.Vertices && Indexed.Indices == Reference.Indices
			&& Indexed.TriangleLayerIds == Reference.TriangleLayerIds && Indexed.TriangleShapeIds == Reference.TriangleShapeIds;
	};
	for (int32 Round = 0; Round < 6; ++Round)
	{
		const double X = Round == 0 ? 100.0 : 1100.0 + (Round - 1) * 250.0;
		Stamp.Center = FVector(X, 100.0, 30.0);
		FEraseGeometryStats Stats; FBox2D Changed(ForceInit), ReferenceChanged(ForceInit);
		const bool bChanged = EraseShapeGeometry(Indexed, LayerId, Stamp, true, &Stats, &Changed, &Index);
		const bool bReferenceChanged = EraseShapeGeometry(Reference, LayerId, Stamp, true, nullptr, &ReferenceChanged);
		TestTrue(TEXT("Transformed-prism erase changes the fixture"), bChanged);
		TestEqual(TEXT("Indexed and legacy erase agree on change status"), bChanged, bReferenceChanged);
		TestTrue(TEXT("Indexed erasure preserves exact source order, vertices and ownership"), SameSource());
		TestTrue(TEXT("Indexed dirty bounds equal exact removed-polygon bounds"), Changed.bIsValid == ReferenceChanged.bIsValid
			&& (!Changed.bIsValid || (Changed.Min.Equals(ReferenceChanged.Min) && Changed.Max.Equals(ReferenceChanged.Max))));
		TestTrue(TEXT("Erase considers nearby blocks rather than the entire document"), Stats.ConsideredTriangles < Indexed.TriangleLayerIds.Num() / 2);
		TestTrue(TEXT("Every swap/tail fragment keeps the cached index current"), Index.IsCurrent(Indexed));
	}
	TestEqual(TEXT("Shape erasure masks are retained exactly"), Indexed.Shapes[0].Erasures.Num(), Reference.Shapes[0].Erasures.Num());
	TestEqual(TEXT("Only the near owned Shape records its cut"), Indexed.Shapes[0].Erasures.Num(), 1);
	Stamp.Center = FVector(100, 100, 30);
	TestFalse(TEXT("Identical remembered Shape erase is still a no-op"), EraseShapeGeometry(Indexed, LayerId, Stamp, true, nullptr, nullptr, &Index));
	TestFalse(TEXT("Reference remembers the same no-op"), EraseShapeGeometry(Reference, LayerId, Stamp, true));
	TestTrue(TEXT("A remembered no-op keeps the source identical"), SameSource());
	Stamp.Center = FVector::ZeroVector; Stamp.Radius = Stamp.Depth = 10000000.0;
	TestTrue(TEXT("Indexed erasure can clear every source triangle"), EraseShapeGeometry(Indexed, LayerId, Stamp, true, nullptr, nullptr, &Index));
	EraseShapeGeometry(Reference, LayerId, Stamp, true);
	TestTrue(TEXT("Clearing geometry keeps original empty-document behavior"), SameSource() && Indexed.Vertices.IsEmpty()
		&& Index.IsCurrent(Indexed));
	return true;
}

#endif
