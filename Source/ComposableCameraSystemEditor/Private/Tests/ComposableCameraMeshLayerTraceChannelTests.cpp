// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"

namespace
{
	class FMeshLayerTraceModeTools : public FEditorModeTools
	{
	public:
		explicit FMeshLayerTraceModeTools(UWorld& World) : FixtureWorld(&World) {}
		virtual UWorld* GetWorld() const override { return FixtureWorld.Get(); }
	private:
		TWeakObjectPtr<UWorld> FixtureWorld;
	};

	// Write tagged data without the new field, reproducing an older Layer record.
	class FMeshLayerLegacyChannelWriter : public FObjectAndNameAsStringProxyArchive
	{
	public:
		explicit FMeshLayerLegacyChannelWriter(FArchive& Archive) : FObjectAndNameAsStringProxyArchive(Archive, false) {}
		virtual bool ShouldSkipProperty(const FProperty* Property) const override
		{
			return Property->GetFName() == GET_MEMBER_NAME_CHECKED(FComposableCameraMeshLayerDefinition, TraceChannel)
				|| FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerTraceChannelTest,
	"ComposableCameraSystem.Editor.MeshCamera.LayerTraceChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerTraceChannelTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Editor is available"), GEditor)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("Isolated trace World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!TestNotNull(TEXT("StaticMesh floor fixture loads"), Cube)) { return false; }
		auto AddFloor = [World, Cube](double Height, bool bCustomSurface)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor) { return static_cast<UStaticMeshComponent*>(nullptr); }
			auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
			Actor->AddInstanceComponent(Mesh);
			Actor->SetRootComponent(Mesh);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetRelativeScale3D(FVector(4, 4, 0.1));
			Actor->SetActorLocation(FVector(0, 0, Height - Cube->GetBoundingBox().Max.Z * 0.1));
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Mesh->SetCollisionObjectType(ECC_WorldStatic);
			Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
			Mesh->SetCollisionResponseToChannel(ECC_Visibility, bCustomSurface ? ECR_Ignore : ECR_Block);
			Mesh->SetCollisionResponseToChannel(ECC_Camera, bCustomSurface ? ECR_Block : ECR_Ignore);
			Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1, bCustomSurface ? ECR_Block : ECR_Ignore);
			Mesh->RegisterComponent();
			return Mesh;
		};
		const auto* Lower = AddFloor(-20.0, false);
		const auto* Upper = AddFloor(0.0, true);
		if (!TestNotNull(TEXT("Visibility floor exists"), Lower)
			|| !TestNotNull(TEXT("Visibility-ignoring custom floor exists"), Upper)) { return false; }
		FMeshLayerTraceModeTools Owner(*World);
		FComposableCameraMeshLayerEdMode Mode;
		Mode.Owner = &Owner;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage(), NAME_None, RF_Transactional);
		Mode.SelectionEditor = NewObject<UComposableCameraMeshLayerSelection>();
		Mode.Settings->Layers.SetNum(2);
		Mode.Settings->NormalizeLayers();
		const FGuid FirstId = Mode.Settings->Layers[0].LayerId;
		const FGuid SecondId = Mode.Settings->Layers[1].LayerId;
		TestEqual(TEXT("New and legacy Layers default to Visibility"), Mode.Settings->Layers[0].TraceChannel.GetValue(), ECC_Visibility);
		Mode.Settings->Layers[1].TraceChannel = ECC_Camera;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CCSMeshLayerTraceChannelTest), true);
		const FVector Start(0, 0, 100), End(0, 0, -100);
		FHitResult Hit;
		TestTrue(TEXT("Default Layer traces through the ignored slab to the Visibility floor"),
			Mode.TraceLayerSurface(*World, FirstId, Start, End, Params, Hit) && Hit.GetComponent() == Lower);
		TestTrue(TEXT("A different Layer uses its own channel despite the first row being active"),
			Mode.TraceLayerSurface(*World, SecondId, Start, End, Params, Hit) && Hit.GetComponent() == Upper);
		TestFalse(TEXT("Unknown Layer cannot fall back to Visibility"),
			Mode.TraceLayerSurface(*World, FGuid::NewGuid(), Start, End, Params, Hit));

		Mode.Settings->BrushRadius = 30.0;
		for (ECollisionChannel Channel : {ECC_Visibility, ECC_Camera, ECC_GameTraceChannel1})
		{
			Mode.Settings->Layers[0].TraceChannel = Channel;
			Mode.Settings->WorkingData.Reset();
			if (!TestTrue(TEXT("Layer channel finds its brush center"), Mode.TraceLayerSurface(*World, FirstId, Start, End, Params, Hit))) { return false; }
			if (!TestTrue(TEXT("Real brush ring uses the same Layer channel"), Mode.AddProjectedBrushStamp(Hit, FirstId))) { return false; }
			const double ExpectedZ = Channel == ECC_Visibility ? -20.0 : 0.0;
			for (const FVector3f& Vertex : Mode.Settings->WorkingData.Vertices)
			{
				TestTrue(TEXT("Brush center and ring stay on the selected surface"), FMath::Abs(double(Vertex.Z) - ExpectedZ) < 1.e-4);
			}
		}
		Mode.Settings->Layers[0].TraceChannel = ECC_GameTraceChannel2;
		TestFalse(TEXT("An unblocked channel does not silently use Visibility"), Mode.TraceLayerSurface(*World, FirstId, Start, End, Params, Hit));
		Mode.Settings->Layers[0].TraceChannel = ECC_MAX;
		TestFalse(TEXT("Invalid serialized channels fail before issuing a physics query"), Mode.TraceLayerSurface(*World, FirstId, Start, End, Params, Hit));
		Mode.Settings->Layers[0].TraceChannel = ECC_GameTraceChannel1;
		Mode.Settings->WorkingData.Reset();
		for (EComposableCameraMeshShapeType Type : {EComposableCameraMeshShapeType::Rectangle,
			EComposableCameraMeshShapeType::Circle, EComposableCameraMeshShapeType::Polygon})
		{
			FComposableCameraMeshAuthoredShape Shape;
			Shape.ShapeId = FGuid::NewGuid(); Shape.LayerId = FirstId; Shape.Type = Type;
			Shape.ControlPoints = Type == EComposableCameraMeshShapeType::Polygon
				? TArray<FVector2D>{{-40, -40}, {40, -40}, {0, 40}}
				: Type == EComposableCameraMeshShapeType::Circle
					? TArray<FVector2D>{{0, 0}, {40, 0}} : TArray<FVector2D>{{-40, -40}, {40, 40}};
			if (!TestTrue(TEXT("Actual Shape projection accepts a custom-channel StaticMesh"), Mode.CommitEditedShape(Shape, false))) { return false; }
			for (const FVector3f& Vertex : Mode.Settings->WorkingData.Vertices)
			{
				TestTrue(TEXT("Rectangle/Circle/Polygon vertices remain on the custom floor"), FMath::Abs(double(Vertex.Z)) < 1.e-4);
			}
		}

		auto* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("Saved document fixture exists"), Storage)) { return false; }
		Storage->SetAuthoringData(Mode.Settings->Layers, Mode.Settings->WorkingData);
		TestEqual(TEXT("Writing the document preserves the Layer Channel"), Storage->GetLayers()[0].TraceChannel.GetValue(), ECC_GameTraceChannel1);
		FComposableCameraMeshLayerQueryResults NativeLayers;
		TestTrue(TEXT("Custom-channel geometry remains queryable by the runtime triangle path"),
			Storage->QueryLayers(FVector(0, 0, 50), 100.0, 1.0, NativeLayers));
		Mode.Settings->Layers[0].TraceChannel = ECC_Visibility;
		Storage->SetAuthoringData(Mode.Settings->Layers, Mode.Settings->WorkingData);
		if (TestTrue(TEXT("Changing only Channel preserves baked camera coverage"),
			Storage->QueryLayers(FVector(0, 0, 50), 100.0, 1.0, NativeLayers))
			&& TestEqual(TEXT("The same Layer remains queryable"), NativeLayers.Num(), 1))
		{
			TestTrue(TEXT("Channel changes never reproject existing mesh heights"), FMath::IsNearlyZero(NativeLayers[0].SurfacePosition.Z));
		}
		Mode.Settings->Layers[0].TraceChannel = ECC_GameTraceChannel1;
		auto SavedLayer = Mode.Settings->Layers[0];
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		FObjectAndNameAsStringProxyArchive Save(Writer, false);
		FComposableCameraMeshLayerDefinition::StaticStruct()->SerializeItem(Save, &SavedLayer, nullptr);
		FMemoryReader Reader(Bytes);
		FObjectAndNameAsStringProxyArchive Load(Reader, false);
		FComposableCameraMeshLayerDefinition Reopened;
		FComposableCameraMeshLayerDefinition::StaticStruct()->SerializeItem(Load, &Reopened, nullptr);
		TestTrue(TEXT("Saved Layer retains channel and stable identity"), Reopened.TraceChannel == ECC_GameTraceChannel1 && Reopened.LayerId == FirstId);
		TArray<uint8> LegacyBytes;
		FMemoryWriter LegacyWriter(LegacyBytes);
		FMeshLayerLegacyChannelWriter LegacySave(LegacyWriter);
		FComposableCameraMeshLayerDefinition::StaticStruct()->SerializeItem(LegacySave, &SavedLayer, nullptr);
		FMemoryReader LegacyReader(LegacyBytes);
		FObjectAndNameAsStringProxyArchive LegacyLoad(LegacyReader, false);
		FComposableCameraMeshLayerDefinition LegacyLayer;
		FComposableCameraMeshLayerDefinition::StaticStruct()->SerializeItem(LegacyLoad, &LegacyLayer, nullptr);
		TestTrue(TEXT("An old record without Channel retains identity and loads as Visibility"),
			LegacyLayer.LayerId == FirstId && LegacyLayer.TraceChannel == ECC_Visibility);
		return true;
	}();
	World->DestroyWorld(false);
	return bResult;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshUnevenBrushTest,
	"ComposableCameraSystem.Editor.MeshCamera.UnevenBrushProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshUnevenBrushTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	if (!TestNotNull(TEXT("Isolated projection World exists"), World)) { return false; }
	const bool bResult = [this, World]()
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		AActor* Floor = World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Curved StaticMesh loads"), Sphere) || !TestNotNull(TEXT("Curved floor Actor exists"), Floor)) { return false; }
		const FVector Anchor(2000000, 2000000, 2000000);
		auto* Mesh = NewObject<UStaticMeshComponent>(Floor);
		Floor->AddInstanceComponent(Mesh); Floor->SetRootComponent(Mesh);
		Mesh->SetMobility(EComponentMobility::Movable); Mesh->SetStaticMesh(Sphere);
		Mesh->SetRelativeScale3D(FVector(4, 4, 1)); Floor->SetActorLocation(Anchor);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block); Mesh->RegisterComponent();
		FMeshLayerTraceModeTools Owner(*World);
		FComposableCameraMeshLayerEdMode Mode;
		Mode.Owner = &Owner;
		Mode.Settings = NewObject<UComposableCameraMeshLayerToolSettings>();
		Mode.Settings->Layers.SetNum(1); Mode.Settings->NormalizeLayers();
		Mode.Settings->BrushRadius = 100.0; Mode.AnchorTransform = FTransform(FQuat::Identity, Anchor, FVector(2, 3, 4));
		const FGuid LayerId = Mode.Settings->Layers[0].LayerId;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CCSMeshUnevenBrushTest), true);
		FHitResult Center;
		if (!TestTrue(TEXT("Complex trace finds curved brush center"),
			Mode.TraceLayerSurface(*World, LayerId, Anchor + FVector(0, 0, 150), Anchor, Params, Center))) { return false; }
		if (!TestTrue(TEXT("Actual Brush builds interior samples on uneven ground"), Mode.AddProjectedBrushStamp(Center, LayerId))) { return false; }
		auto* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("Saved brush document exists"), Storage)) { return false; }
		Storage->SetActorTransform(Mode.AnchorTransform); Storage->SetAuthoringData(Mode.Settings->Layers, Mode.Settings->WorkingData);
		bool bDetailed = false;
		for (const FVector3f& Vertex : Mode.Settings->WorkingData.Vertices)
		{
			const double Radius = Mode.AnchorTransform.TransformVector(FVector(Vertex.X, Vertex.Y, 0)).Size();
			bDetailed |= Radius > 1.0 && Radius < 70.0;
		}
		TestTrue(TEXT("Brush retains projected interior vertices instead of only a center/rim fan"), bDetailed);
		for (int32 X = -60; X <= 60; X += 30)
		{
			for (int32 Y = -60; Y <= 60; Y += 30)
			{
				FHitResult Ground;
				const FVector Start = Anchor + FVector(X, Y, 150);
				if (!TestTrue(TEXT("Interior physical floor trace succeeds"),
					Mode.TraceLayerSurface(*World, LayerId, Start, Anchor + FVector(X, Y, 0), Params, Ground))) { return false; }
				FComposableCameraMeshLayerQueryResults Hits;
				if (!TestTrue(TEXT("Saved brush follows actual curved ground within fitting tolerance, including far-origin precision"),
					Storage->QueryLayers(Ground.ImpactPoint + FVector(0, 0, 2), 4.0, 4.0, Hits)
					&& Hits.Num() == 1 && FMath::Abs(Hits[0].SurfacePosition.Z - Ground.ImpactPoint.Z) <= 1.1)) { return false; }
			}
		}
		return true;
	}();
	World->DestroyWorld(false);
	return bResult;
}

#endif
