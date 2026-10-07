// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Actions/ComposableCameraActionBase.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "MeshCamera/ComposableCameraMeshWorldSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Utils/ComposableCameraProjectSettings.h"

#include <limits>

namespace
{
	struct FMeshQueryTestWorld
	{
		TObjectPtr<UWorld> World = UWorld::CreateWorld(EWorldType::Game, false);
		FMeshQueryTestWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
		~FMeshQueryTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	UStaticMeshComponent* AddMeshQueryFloor(UWorld& World, UStaticMesh& Cube, double TopZ)
	{
		AActor* Actor = World.SpawnActor<AActor>();
		if (!Actor) return nullptr;
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
		Actor->AddInstanceComponent(Mesh);
		Actor->SetRootComponent(Mesh);
		Mesh->SetStaticMesh(&Cube);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldStatic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Mesh->RegisterComponentWithWorld(&World);
		Actor->SetActorScale3D(FVector(20, 20, 0.2));
		Actor->SetActorLocation(FVector(0, 0, TopZ - 10.0));
		return Mesh;
	}

	void AddMeshQueryPlane(FComposableCameraMeshSurfaceAuthoringData& Source, const FGuid& LayerId, float Z)
	{
		const int32 Base = Source.Vertices.Num();
		Source.Vertices.Append({FVector3f(-100, -100, Z), FVector3f(100, -100, Z),
			FVector3f(100, 100, Z), FVector3f(-100, 100, Z)});
		Source.Indices.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
		Source.TriangleLayerIds.Append({LayerId, LayerId});
	}

	FHitResult MeshQueryGroundAt(UPrimitiveComponent& Component, const FVector& Point)
	{
		FHitResult Hit(Component.GetOwner(), &Component, Point, FVector::UpVector);
		Hit.bBlockingHit = true;
		return Hit;
	}

	bool TraceMeshQueryGround(UWorld& World, const FVector& Origin, FHitResult& OutHit)
	{
		return World.LineTraceSingleByChannel(OutHit, Origin, Origin + FVector::DownVector * 300.0,
			ECC_Pawn, FCollisionQueryParams(SCENE_QUERY_STAT(CCS_MeshGroundTest), false));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshGroundHitQueryTest,
	"ComposableCameraSystem.MeshCamera.GroundHitQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshGroundHitQueryTest::RunTest(const FString&)
{
	FMeshQueryTestWorld TestWorld;
	FMeshQueryTestWorld OtherWorld;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto* Subsystem = TestWorld.World->GetSubsystem<UComposableCameraMeshWorldSubsystem>();
	if (!TestTrue(TEXT("Ground query world exists"), Cube && Subsystem)) return false;
	auto* Lower = AddMeshQueryFloor(*TestWorld.World, *Cube, 0.0);
	auto* Upper = AddMeshQueryFloor(*TestWorld.World, *Cube, 100.0);
	auto* Foreign = AddMeshQueryFloor(*OtherWorld.World, *Cube, 0.0);
	auto* Storage = TestWorld.World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
	if (!TestTrue(TEXT("Two Actor floors and foreign-world floor exist"), Lower && Upper && Foreign && Storage)) return false;
	Lower->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Upper->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	FComposableCameraMeshSurfaceAuthoringData Source;
	for (auto& Layer : Layers)
	{
		Layer.LayerId = FGuid::NewGuid();
		Layer.TraceChannel = ECC_Camera; // Authoring policy does not choose runtime ground.
		AddMeshQueryPlane(Source, Layer.LayerId, 0.0f);
	}
	Storage->SetAuthoringData(Layers, Source);
	Subsystem->RegisterStorageActor(Storage);
	FComposableCameraMeshGroundQueryParams Params;
	FComposableCameraMeshLayerQueryResults Results;
	FHitResult LowerGround, UpperGround;
	if (!TestTrue(TEXT("Business Pawn traces find both floors"),
		TraceMeshQueryGround(*TestWorld.World, FVector(0, 0, 50), LowerGround)
		&& TraceMeshQueryGround(*TestWorld.World, FVector(0, 0, 150), UpperGround))) return false;
	TestTrue(TEXT("Business hit identifies upper floor"), UpperGround.GetComponent() == Upper
		&& FMath::IsNearlyEqual(UpperGround.ImpactPoint.Z, 100.0, 0.01));
	TestFalse(TEXT("Unpainted upper ground cannot activate lower Layers without any scene query policy"),
		Subsystem->QueryMeshLayersInline(UpperGround, Params, Results));
	TestTrue(TEXT("Upper-floor miss clears output"), Results.IsEmpty());
	TestTrue(TEXT("Actual lower ground returns overlapping Layers"), Subsystem->QueryMeshLayersInline(LowerGround, Params, Results));
	if (!TestEqual(TEXT("Both lower Layers match"), Results.Num(), 2)) return false;
	TestTrue(TEXT("Layer order and exact saved point remain intact"), Results[0].LayerId == Layers[0].LayerId
		&& Results[0].SurfacePosition.Equals(FVector::ZeroVector, 0.01)
		&& FMath::IsNearlyZero(Results[0].VerticalDistance, 0.01));

	// Only GroundHit.ImpactPoint selects the sample; sweep capsule center may be far above it.
	FHitResult SweepGround;
	if (!TestTrue(TEXT("Business capsule sweep hits upper floor"), TestWorld.World->SweepSingleByChannel(
		SweepGround, FVector(0, 0, 250), FVector(0, 0, 150), FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(34.0f, 88.0f), FCollisionQueryParams(SCENE_QUERY_STAT(CCS_MeshGroundSweepTest), false))))
		return false;
	TestTrue(TEXT("Sweep center differs from physical contact"), SweepGround.Location.Z - SweepGround.ImpactPoint.Z > 80.0);
	Source.Reset();
	AddMeshQueryPlane(Source, Layers[0].LayerId, 100.0f);
	AddMeshQueryPlane(Source, Layers[1].LayerId, 0.0f);
	Storage->SetAuthoringData(Layers, Source);
	TestTrue(TEXT("Capsule contact matches painted upper floor"), Subsystem->QueryMeshLayersInline(SweepGround, Params, Results));
	TestEqual(TEXT("Lower floor excluded when both floors are painted"), Results.Num(), 1);
	if (Results.Num() == 1) TestTrue(TEXT("Upper result uses its saved triangle height"), Results[0].LayerId == Layers[0].LayerId
		&& FMath::IsNearlyEqual(Results[0].SurfacePosition.Z, 100.0, 0.01));

	Upper->SetCollisionResponseToAllChannels(ECR_Ignore);
	TestTrue(TEXT("Caller-provided ground is reused without a second scene trace"),
		Subsystem->QueryMeshLayersInline(UpperGround, Params, Results));
	FComposableCameraMeshLayerQueryResult Single;
	TArray<FComposableCameraMeshLayerQueryResult> BlueprintResults;
	TestTrue(TEXT("Single-result API uses ground match"), Subsystem->QueryMeshLayer(UpperGround, Params, Single));
	TestTrue(TEXT("Blueprint array API uses ground match"), Subsystem->QueryMeshLayers(UpperGround, Params, BlueprintResults));
	TestTrue(TEXT("All public forms agree"), BlueprintResults.Num() == Results.Num()
		&& Single.LayerId == Layers[0].LayerId);

	TestFalse(TEXT("Missing blocking ground returns empty"), Subsystem->QueryMeshLayersInline(FHitResult(), Params, Results));
	TestTrue(TEXT("Missing ground clears reused inline output"), Results.IsEmpty());
	TestFalse(TEXT("Single miss resets previous result"), Subsystem->QueryMeshLayer(FHitResult(), Params, Single));
	TestFalse(TEXT("Array miss resets previous result"), Subsystem->QueryMeshLayers(FHitResult(), Params, BlueprintResults));
	TestTrue(TEXT("All miss outputs reset"), !Single.LayerId.IsValid() && BlueprintResults.IsEmpty());
	FHitResult Invalid = UpperGround;
	Invalid.bStartPenetrating = true;
	TestFalse(TEXT("Penetrating ground is rejected"), Subsystem->QueryMeshLayersInline(Invalid, Params, Results));
	Invalid = UpperGround;
	Invalid.Component.Reset();
	TestFalse(TEXT("Expired ground component is rejected"), Subsystem->QueryMeshLayersInline(Invalid, Params, Results));
	TestFalse(TEXT("Foreign-world ground cannot activate this world's Layers"),
		Subsystem->QueryMeshLayersInline(MeshQueryGroundAt(*Foreign, FVector(0, 0, 100)), Params, Results));
	Params.SurfaceTolerance = -1.0;
	TestFalse(TEXT("Negative tolerance is rejected"), Subsystem->QueryMeshLayersInline(UpperGround, Params, Results));
	Params.SurfaceTolerance = std::numeric_limits<double>::infinity();
	TestFalse(TEXT("Nonfinite tolerance is rejected"), Subsystem->QueryMeshLayersInline(UpperGround, Params, Results));
	Params.SurfaceTolerance = TNumericLimits<double>::Max();
	TestFalse(TEXT("Overflowed query interval is rejected"), Subsystem->QueryMeshLayersInline(UpperGround, Params, Results));
	Params.SurfaceTolerance = 5.0;
	Invalid = UpperGround;
	Invalid.ImpactPoint.Z = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Nonfinite ground point is rejected"), Subsystem->QueryMeshLayersInline(Invalid, Params, Results));
	Upper->GetOwner()->Destroy();
	TestFalse(TEXT("Destroyed ground owner cannot reuse cached membership"), Subsystem->QueryMeshLayersInline(UpperGround, Params, Results));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshGroundHitGeometryTest,
	"ComposableCameraSystem.MeshCamera.GroundHitGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshGroundHitGeometryTest::RunTest(const FString&)
{
	FMeshQueryTestWorld TestWorld;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto* Subsystem = TestWorld.World->GetSubsystem<UComposableCameraMeshWorldSubsystem>();
	if (!TestTrue(TEXT("Geometry query world exists"), Cube && Subsystem)) return false;
	auto* Floor = AddMeshQueryFloor(*TestWorld.World, *Cube, 0.0);
	auto* Storage = TestWorld.World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
	auto* Instance = TestWorld.World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
	if (!TestTrue(TEXT("Geometry fixtures exist"), Floor && Storage && Instance)) return false;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(4);
	for (auto& Layer : Layers) Layer.LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Source;
	AddMeshQueryPlane(Source, Layers[0].LayerId, 5.0f);
	AddMeshQueryPlane(Source, Layers[1].LayerId, -5.0f);
	AddMeshQueryPlane(Source, Layers[2].LayerId, -5.25f);
	AddMeshQueryPlane(Source, Layers[3].LayerId, 5.25f);
	Storage->SetAuthoringData(Layers, Source);
	Subsystem->RegisterStorageActor(Storage);
	FComposableCameraMeshGroundQueryParams Params;
	FComposableCameraMeshLayerQueryResults Results;
	const FHitResult Ground = MeshQueryGroundAt(*Floor, FVector::ZeroVector);
	TestTrue(TEXT("Symmetric ground interval includes both tolerance endpoints"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	if (!TestEqual(TEXT("Only in-band Layers return"), Results.Num(), 2)) return false;
	TestTrue(TEXT("Each Layer preserves its own endpoint and separation"),
		Results[0].SurfacePosition.Equals(FVector(0, 0, 5), 0.01)
		&& Results[1].SurfacePosition.Equals(FVector(0, 0, -5), 0.01)
		&& FMath::IsNearlyEqual(Results[0].VerticalDistance, 5.0, 0.01)
		&& FMath::IsNearlyEqual(Results[1].VerticalDistance, 5.0, 0.01));
	Layers[0].bEnabled = false;
	Storage->SetAuthoringData(Layers, Source);
	TestTrue(TEXT("Enabled lower endpoint still matches"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	TestEqual(TEXT("Disabled Layer excluded"), Results.Num(), 1);
	Layers[0].bEnabled = true;

	Source.Reset();
	AddMeshQueryPlane(Source, Layers[0].LayerId, 0.0f);
	Storage->SetAuthoringData(Layers, Source);
	Params.SurfaceTolerance = 0.0;
	TestTrue(TEXT("Zero tolerance matches exact saved geometry"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	Source.Reset();
	AddMeshQueryPlane(Source, Layers[0].LayerId, 0.25f);
	Storage->SetAuthoringData(Layers, Source);
	TestFalse(TEXT("Zero tolerance rejects displaced geometry"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	Params.SurfaceTolerance = 5.0;

	// Inside the triangle AABB but outside the actual triangle must remain a miss.
	Source.Reset();
	Source.Vertices.Append({FVector3f(-100, -100, 0), FVector3f(100, -100, 0), FVector3f(100, 100, 0)});
	Source.Indices.Append({0, 1, 2});
	Source.TriangleLayerIds.Add(Layers[0].LayerId);
	Storage->SetAuthoringData(Layers, Source);
	TestFalse(TEXT("Bounds overlap does not replace exact triangle coverage"),
		Subsystem->QueryMeshLayersInline(MeshQueryGroundAt(*Floor, FVector(-75, 75, 0)), Params, Results));

	Source.Reset();
	AddMeshQueryPlane(Source, Layers[0].LayerId, 0.0f);
	for (auto& Vertex : Source.Vertices) Vertex.Z = Vertex.X * 0.2f;
	Storage->SetAuthoringData(Layers, Source);
	Params.SurfaceTolerance = 0.1;
	TestTrue(TEXT("Sloped triangle is evaluated at ground XY rather than centroid height"),
		Subsystem->QueryMeshLayersInline(MeshQueryGroundAt(*Floor, FVector(20, 0, 4)), Params, Results));
	if (Results.Num() == 1) TestTrue(TEXT("Slope result retains actual intersection"), Results[0].SurfacePosition.Equals(FVector(20, 0, 4), 0.01));

	Source.Reset();
	AddMeshQueryPlane(Source, Layers[0].LayerId, 0.0f);
	Storage->SetAuthoringData(Layers, Source);
	Instance->SetAuthoringData(Layers, Source);
	Subsystem->RegisterStorageActor(Instance);
	Params.SurfaceTolerance = 5.0;
	TestTrue(TEXT("Repeated Layer GUIDs in separate documents are distinct"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	TestEqual(TEXT("Both document identities retained"), Results.Num(), 2);
	Instance->SetActorTransform(FTransform(FRotator(15, 25, 0), FVector(1000, 700, 30), FVector(2, 3, 4)));
	const FVector InstancePoint = Instance->GetActorTransform().TransformPosition(FVector(20, 0, 0));
	TestTrue(TEXT("Ground matching respects translated rotated scaled documents"),
		Subsystem->QueryMeshLayersInline(MeshQueryGroundAt(*Floor, InstancePoint), Params, Results));
	TestEqual(TEXT("Remote original document excluded"), Results.Num(), 1);
	if (Results.Num() == 1) TestTrue(TEXT("Transformed result preserves document and point"),
		Results[0].StorageActor == Instance && Results[0].SurfacePosition.Equals(InstancePoint, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshNativeRayPrecisionTest,
	"ComposableCameraSystem.MeshCamera.NativeRayPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshNativeRayPrecisionTest::RunTest(const FString&)
{
	FComposableCameraMeshSurfaceAuthoringData Source;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(2);
	for (auto& Layer : Layers) Layer.LayerId = FGuid::NewGuid();
	AddMeshQueryPlane(Source, Layers[0].LayerId, 8.0f);
	AddMeshQueryPlane(Source, Layers[0].LayerId, 10.0f);
	AddMeshQueryPlane(Source, Layers[1].LayerId, 7.0f);
	for (int32 Index = 0; Index < 32; ++Index)
	{
		const int32 FirstVertex = Source.Vertices.Num();
		AddMeshQueryPlane(Source, Layers[0].LayerId, 10.0f);
		for (int32 Vertex = FirstVertex; Vertex < Source.Vertices.Num(); ++Vertex)
			Source.Vertices[Vertex].X += 1000.0f + 300.0f * Index;
	}
	FComposableCameraMeshSurfaceRuntimeData Linear;
	Linear.Vertices = Source.Vertices;
	Linear.Indices = Source.Indices;
	for (const auto& Id : Source.TriangleLayerIds) Linear.TriangleLayerIndices.Add(Id == Layers[0].LayerId ? 0 : 1);
	auto Indexed = Linear;
	Indexed.RebuildSpatialIndex();
	TArray<int32, TInlineAllocator<16>> IndexedLayers, LinearLayers;
	TArray<double, TInlineAllocator<16>> IndexedDistances, LinearDistances;
	FVector IndexedPosition, LinearPosition;
	double IndexedDistance = 0.0, LinearDistance = 0.0;
	int32 IndexedTests = 0, LinearTests = 0;
	TestTrue(TEXT("Indexed per-Layer ray intersections exist"), Indexed.QueryLocalRayLayers(
		FVector(0, 0, 20), FVector::DownVector, 30.0, 5.0, Layers, IndexedLayers, IndexedPosition, IndexedDistance, &IndexedTests, &IndexedDistances));
	TestTrue(TEXT("Linear per-Layer ray intersections exist"), Linear.QueryLocalRayLayers(
		FVector(0, 0, 20), FVector::DownVector, 30.0, 5.0, Layers, LinearLayers, LinearPosition, LinearDistance, &LinearTests, &LinearDistances));
	TestTrue(TEXT("Indexed and linear precision outputs agree"), IndexedLayers == LinearLayers && IndexedDistances == LinearDistances);
	TestTrue(TEXT("Precision output remains aligned with Layer order and nearest hits"),
		IndexedLayers.Num() == 2 && IndexedDistances.Num() == 2 && IndexedLayers[0] == 0 && IndexedLayers[1] == 1
		&& FMath::IsNearlyEqual(IndexedDistances[0], 10.0, 0.01) && FMath::IsNearlyEqual(IndexedDistances[1], 13.0, 0.01));
	TestTrue(TEXT("Per-Layer precision retains BVH pruning"), IndexedTests < LinearTests);
	IndexedLayers.Add(99);
	IndexedDistances.Add(99.0);
	TestFalse(TEXT("Precision miss reports no hits"), Indexed.QueryLocalRayLayers(
		FVector(-1000, 0, 20), FVector::DownVector, 30.0, 5.0, Layers, IndexedLayers, IndexedPosition, IndexedDistance, nullptr, &IndexedDistances));
	TestTrue(TEXT("Precision miss clears both aligned outputs"), IndexedLayers.IsEmpty() && IndexedDistances.IsEmpty());
	return true;
}

#if !UE_BUILD_SHIPPING
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshQueryDiagnosticsTest,
	"ComposableCameraSystem.MeshCamera.NextQueryDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshQueryDiagnosticsTest::RunTest(const FString&)
{
	FMeshQueryTestWorld TestWorld;
	FMeshQueryTestWorld OtherWorld;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto* Subsystem = TestWorld.World->GetSubsystem<UComposableCameraMeshWorldSubsystem>();
	auto* OtherSubsystem = OtherWorld.World->GetSubsystem<UComposableCameraMeshWorldSubsystem>();
	IConsoleObject* DebugObject = IConsoleManager::Get().FindConsoleObject(TEXT("CCS.MeshLayers.DebugNextQuery"));
	IConsoleCommand* Command = DebugObject ? DebugObject->AsCommand() : nullptr;
	if (!TestTrue(TEXT("Real diagnostic command and worlds exist"), Cube && Subsystem && OtherSubsystem && Command)) return false;
	auto* Floor = AddMeshQueryFloor(*TestWorld.World, *Cube, 0.0);
	auto* Storage = TestWorld.World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
	if (!TestTrue(TEXT("Diagnostic ground and document exist"), Floor && Storage)) return false;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(1);
	Layers[0].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Source;
	AddMeshQueryPlane(Source, Layers[0].LayerId, -7.070f);
	Storage->SetAuthoringData(Layers, Source);
	Subsystem->RegisterStorageActor(Storage);
	FComposableCameraMeshGroundQueryParams Params;
	FComposableCameraMeshLayerQueryResults Results;
	const FHitResult Ground = MeshQueryGroundAt(*Floor, FVector::ZeroVector);
	auto Arm = [&]() { return Command->Execute({}, TestWorld.World, *GLog); };
	AddExpectedMessagePlain(TEXT("Mesh Layer Query: Reason=NoLayerOnGround"), ELogVerbosity::Display,
		EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("NativeHit=1 NearestEnabledSourceZ=-7.070 DeltaValid=1 SourceMinusGroundCm=-7.070"), ELogVerbosity::Display,
		EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Mesh Layer Query: Reason=NoRegisteredDocuments"), ELogVerbosity::Display,
		EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Mesh Layer Query: Reason=MatchedGround World="), ELogVerbosity::Display,
		EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Mesh Layer Query: Reason=InvalidGroundHit"), ELogVerbosity::Display,
		EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Mesh Layer Query: Reason=InvalidUpdateOwner"), ELogVerbosity::Display,
		EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("Diagnostic arms without running a query"), Arm());
	TestFalse(TEXT("Other-world call leaves this request pending"), OtherSubsystem->QueryMeshLayersInline(Ground, Params, Results));
	TestFalse(TEXT("Actual mismatch reports supplied ground and native height"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	TestFalse(TEXT("Repeated mismatch stays silent"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	Subsystem->UnregisterStorageActor(Storage);
	TestTrue(TEXT("Unregistered-document request arms"), Arm());
	TestFalse(TEXT("Discovery never registers a document"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	TestFalse(TEXT("Later query remains unregistered"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	Source.Reset();
	AddMeshQueryPlane(Source, Layers[0].LayerId, 0.0f);
	Storage->SetAuthoringData(Layers, Source);
	Subsystem->RegisterStorageActor(Storage);
	TestTrue(TEXT("Successful-ground request arms"), Arm());
	TestTrue(TEXT("Matched ground is diagnosed once"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	TestTrue(TEXT("Later success stays silent"), Subsystem->QueryMeshLayersInline(Ground, Params, Results));
	TestTrue(TEXT("Missing-ground request arms"), Arm());
	TestFalse(TEXT("Invalid ground consumes its request"), Subsystem->QueryMeshLayersInline(FHitResult(), Params, Results));
	TestFalse(TEXT("Repeated invalid ground stays silent"), Subsystem->QueryMeshLayersInline(FHitResult(), Params, Results));
	TestTrue(TEXT("Invalid Update request arms"), Arm());
	TestFalse(TEXT("Invalid owner is diagnosed before querying"), Subsystem->UpdateMeshLayers(nullptr, Ground, Params));
	TestFalse(TEXT("Repeated invalid owner stays silent"), Subsystem->UpdateMeshLayers(nullptr, Ground, Params));
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshManualUpdateTest,
	"ComposableCameraSystem.MeshCamera.ManualUpdateAndClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshManualUpdateTest::RunTest(const FString&)
{
	TGuardValue<TArray<FName>> Contexts(GetMutableDefault<UComposableCameraProjectSettings>()->ContextNames,
		TArray<FName>{TEXT("MeshManualTest")});
	FMeshQueryTestWorld TestWorld;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UComposableCameraMeshWorldSubsystem* Subsystem = TestWorld.World->GetSubsystem<UComposableCameraMeshWorldSubsystem>();
	APlayerController* PC = TestWorld.World->SpawnActor<APlayerController>();
	AComposableCameraPlayerCameraManager* PCM = TestWorld.World->SpawnActor<AComposableCameraPlayerCameraManager>();
	if (!TestTrue(TEXT("Manual API setup exists"), Cube && Subsystem && PC && PCM)) return false;
	// This isolated world has no GameMode; BeginPlay must be dispatched explicitly
	// so Destroy exercises the real Actor EndPlay delegate, not only weak pointers.
	if (!PC->HasActorBegunPlay()) PC->DispatchBeginPlay();
	if (!PCM->HasActorBegunPlay()) PCM->DispatchBeginPlay();
	PC->PlayerCameraManager = PCM;
	if (!TestNotNull(TEXT("Gameplay camera exists"), PCM->ActivateNewCamera(AComposableCameraCameraBase::StaticClass(),
		static_cast<UComposableCameraTransitionDataAsset*>(nullptr), FComposableCameraActivateParams{},
		FOnCameraFinishConstructed{}, TEXT("MeshManualTest")))) return false;
	UStaticMeshComponent* Floor = AddMeshQueryFloor(*TestWorld.World, *Cube, 0.0);
	if (!TestNotNull(TEXT("Manual query floor exists"), Floor)) return false;
	Floor->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	UComposableCameraMeshProfile* Profile = NewObject<UComposableCameraMeshProfile>();
	Profile->Type = EComposableCameraMeshProfileType::Action;
	UComposableCameraActionTypeAsset* Asset = NewObject<UComposableCameraActionTypeAsset>();
	Asset->ActionTemplate = NewObject<UComposableCameraActionBase>(Asset);
	Asset->ActionTemplate->ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Manual);
	Profile->Action.ActionAsset = Asset;
	TArray<FComposableCameraMeshLayerDefinition> Layers;
	Layers.SetNum(1);
	Layers[0].LayerId = FGuid::NewGuid();
	Layers[0].Profile = Profile;
	FComposableCameraMeshSurfaceAuthoringData Source;
	AddMeshQueryPlane(Source, Layers[0].LayerId, 0.0f);
	AComposableCameraMeshSurfaceStorageActor* Storage = TestWorld.World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
	if (!TestNotNull(TEXT("Manual query document exists"), Storage)) return false;
	Storage->SetAuthoringData(Layers, Source);
	Subsystem->RegisterStorageActor(Storage);
	FComposableCameraMeshGroundQueryParams Params;
	FComposableCameraMeshLayerQueryResult Result;
	const FHitResult Ground = MeshQueryGroundAt(*Floor, FVector::ZeroVector);
	TestFalse(TEXT("Mesh subsystem has no automatic Tick"), Subsystem->IsA(UTickableWorldSubsystem::StaticClass()));
	TestTrue(TEXT("Read-only query returns the Profile"), Subsystem->QueryMeshLayer(Ground, Params, Result));
	TestEqual(TEXT("Query creates no player state"), Subsystem->PlayerStates.Num(), 0);
	TestEqual(TEXT("Query never dispatches effects"), PCM->GetCameraActions().Num(), 0);
	TestNull(TEXT("Manual Update does not require a possessed Pawn"), PC->GetPawn());
	TestTrue(TEXT("Explicit Update matches the supplied ground"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	if (!TestEqual(TEXT("Manual entry dispatches one Action"), PCM->GetCameraActions().Num(), 1)) return false;
	UComposableCameraActionBase* First = *PCM->GetCameraActions().CreateConstIterator();
	TestTrue(TEXT("Repeated Update still reports membership"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	TestTrue(TEXT("Stable membership preserves the exact Action"), PCM->GetCameraActions().Num() == 1 && PCM->GetCameraActions().Contains(First));
	UComposableCameraActionBase* External = PCM->AddCameraActionFromAsset(Asset, FComposableCameraParameterBlock{}, false);
	TestFalse(TEXT("Moving the explicit query point exits"), Subsystem->UpdateMeshLayers(PC, MeshQueryGroundAt(*Floor, FVector(300, 0, 0)), Params));
	TestTrue(TEXT("Exit preserves an external same-class Action"), PCM->GetCameraActions().Num() == 1 && PCM->GetCameraActions().Contains(External));
	TestTrue(TEXT("Caller can re-enter explicitly"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	Subsystem->ClearMeshLayers(PC);
	Subsystem->ClearMeshLayers(PC);
	TestEqual(TEXT("Clear is idempotent and removes player ownership"), Subsystem->PlayerStates.Num(), 0);
	TestTrue(TEXT("Clear preserves unrelated Action"), PCM->GetCameraActions().Num() == 1 && PCM->GetCameraActions().Contains(External));
	PCM->RemoveCameraAction(External);

	// An async completion cannot enter without another explicit Update.
	auto& Pending = Subsystem->EnsureProfilePreload(Profile);
	Pending.bResolved = false;
	Pending.Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Pending.AssetPaths, FStreamableDelegate(), FStreamableManager::DefaultAsyncLoadPriority,
		false, true, TEXT("CCS Manual Mesh Query Test"));
	if (!TestTrue(TEXT("Pending preload can be held"), Pending.Handle.IsValid())) return false;
	TestTrue(TEXT("Pending assets do not change geometric membership"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	TestEqual(TEXT("Pending entry owns no Action"), PCM->GetCameraActions().Num(), 0);
	Subsystem->ClearMeshLayers(PC);
	Pending.Handle->CancelHandle();
	Pending.Handle.Reset();
	TestTrue(TEXT("Query after readiness still does not apply effects"), Subsystem->QueryMeshLayer(Ground, Params, Result));
	TestEqual(TEXT("No callback resurrects cleared membership"), PCM->GetCameraActions().Num(), 0);
	TestTrue(TEXT("A new explicit Update enters ready Profile"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	TestEqual(TEXT("Ready manual entry dispatches once"), PCM->GetCameraActions().Num(), 1);
	FComposableCameraMeshGroundQueryParams Invalid;
	Invalid.SurfaceTolerance = -1.0;
	TestFalse(TEXT("Invalid tolerance exits old effects rather than leaving them active"), Subsystem->UpdateMeshLayers(PC, Ground, Invalid));
	TestEqual(TEXT("Invalid tolerance clears owned effects"), PCM->GetCameraActions().Num(), 0);
	TestTrue(TEXT("Ready membership can enter before losing ground"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	TestFalse(TEXT("Missing ground exits without searching for a lower painted floor"), Subsystem->UpdateMeshLayers(PC, FHitResult(), Params));
	TestEqual(TEXT("Missing ground removes owned effects"), PCM->GetCameraActions().Num(), 0);
	TestTrue(TEXT("Ready membership can enter again"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	Subsystem->UnregisterStorageActor(Storage);
	TestEqual(TEXT("Document unload releases owned effects without polling"), PCM->GetCameraActions().Num(), 0);
	Subsystem->RegisterStorageActor(Storage);
	TestTrue(TEXT("Reloaded document can enter explicitly"), Subsystem->UpdateMeshLayers(PC, Ground, Params));
	// Retain this independently spawned manager so the player's own EndPlay
	// cleanup can be checked, rather than reading an already-destroyed PCM.
	PC->PlayerCameraManager = nullptr;
	PC->Destroy();
	TestEqual(TEXT("Player EndPlay releases ownership without automatic Tick"), Subsystem->PlayerStates.Num(), 0);
	TestEqual(TEXT("Player EndPlay removes only its Action"), PCM->GetCameraActions().Num(), 0);
	APlayerController* OtherPC = TestWorld.World->SpawnActor<APlayerController>();
	AComposableCameraPlayerCameraManager* Replacement = TestWorld.World->SpawnActor<AComposableCameraPlayerCameraManager>();
	if (!TestTrue(TEXT("Replacement owner actors exist"), OtherPC && Replacement)) return false;
	if (!OtherPC->HasActorBegunPlay()) OtherPC->DispatchBeginPlay();
	if (!Replacement->HasActorBegunPlay()) Replacement->DispatchBeginPlay();
	OtherPC->PlayerCameraManager = PCM;
	TestTrue(TEXT("Another player can enter through the same explicit API"), Subsystem->UpdateMeshLayers(OtherPC, Ground, Params));
	OtherPC->PlayerCameraManager = Replacement;
	TestTrue(TEXT("Replacing PCM revalidates and enters against the new manager"), Subsystem->UpdateMeshLayers(OtherPC, Ground, Params));
	TestEqual(TEXT("Replacement releases old manager's owned Action"), PCM->GetCameraActions().Num(), 0);
	TestEqual(TEXT("Replacement installs one Action in the new manager"), Replacement->GetCameraActions().Num(), 1);
	PCM->Destroy();
	TestEqual(TEXT("Old PCM EndPlay is unbound and cannot clear the new owner"), Subsystem->PlayerStates.Num(), 1);
	Replacement->Destroy();
	TestEqual(TEXT("Current PCM EndPlay releases state without polling"), Subsystem->PlayerStates.Num(), 0);
	OtherPC->PlayerCameraManager = nullptr;
	return true;
}

#endif
