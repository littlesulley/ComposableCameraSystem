// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"

#include "ComposableCameraSystemEditorModule.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "MeshCamera/ComposableCameraMeshLayerModeToolkit.h"
#include "MeshCamera/ComposableCameraMeshLayerRendering.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "LevelUtils.h"
#include "Misc/MessageDialog.h"
#include "PrimitiveDrawingUtils.h"
#include "Toolkits/ToolkitManager.h"

#define LOCTEXT_NAMESPACE "ComposableCameraMeshLayerEdMode"

const FEditorModeID FComposableCameraMeshLayerEdMode::ModeId =
	TEXT("EM_ComposableCameraMeshLayers");

void FComposableCameraMeshLayerEdMode::Enter()
{
	FEdMode::Enter();
	bExiting = false;

	Settings = NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage());
	Settings->OnLayerDataChanged.BindRaw(this, &FComposableCameraMeshLayerEdMode::MarkLayerDataDirty);
	if (!InitializeWorkingDocument())
	{
		RequestDeletion();
		return;
	}

	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShared<FComposableCameraMeshLayerModeToolkit>(this);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void FComposableCameraMeshLayerEdMode::Exit()
{
	bExiting = true;
	if (bDirty)
	{
		const EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT("SaveBeforeExit", "Mesh Camera Layer data changed. Save before closing the tool?"));
		if (Response == EAppReturnType::Yes)
		{
			SaveWorkingData();
		}
	}

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	if (Settings)
	{
		Settings->OnLayerDataChanged.Unbind();
		Settings = nullptr;
	}

	bPainting = false;
	bHasHoverHit = false;
	Visualization.Cells.Reset();
	FEdMode::Exit();
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FComposableCameraMeshLayerEdMode::RequestCloseFromToolkit()
{
	if (!bExiting)
	{
		RequestDeletion();
	}
}

void FComposableCameraMeshLayerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Settings);
	FEdMode::AddReferencedObjects(Collector);
}

bool FComposableCameraMeshLayerEdMode::InitializeWorkingDocument()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	TargetLevel = World->GetCurrentLevel();
	if (!TargetLevel.IsValid())
	{
		return false;
	}

	StorageActor = FindStorageActor();
	if (AComposableCameraMeshSurfaceStorageActor* ExistingActor = StorageActor.Get())
	{
		AnchorTransform = ExistingActor->GetActorTransform();
		Settings->Layers = ExistingActor->GetLayers();
		WorkingData = ExistingActor->GetAuthoringData();
	}
	else
	{
		AnchorTransform = FTransform::Identity;
		if (ULevelStreaming* StreamingLevel = FLevelUtils::FindStreamingLevel(TargetLevel.Get()))
		{
			AnchorTransform = StreamingLevel->LevelTransform;
		}
		WorkingData.Reset();
	}

	if (Settings->Layers.IsEmpty())
	{
		Settings->Layers.AddDefaulted();
	}
	Settings->NormalizeLayers();
	bDirty = false;
	bVisualizationDirty = true;
	return true;
}

AComposableCameraMeshSurfaceStorageActor*
FComposableCameraMeshLayerEdMode::FindStorageActor() const
{
	const ULevel* Level = TargetLevel.Get();
	if (!Level)
	{
		return nullptr;
	}

	for (AActor* Actor : Level->Actors)
	{
		if (AComposableCameraMeshSurfaceStorageActor* Storage =
			Cast<AComposableCameraMeshSurfaceStorageActor>(Actor))
		{
			return Storage;
		}
	}
	return nullptr;
}

AComposableCameraMeshSurfaceStorageActor*
FComposableCameraMeshLayerEdMode::FindOrCreateStorageActor()
{
	if (AComposableCameraMeshSurfaceStorageActor* ExistingActor = StorageActor.Get())
	{
		return ExistingActor;
	}

	ULevel* Level = TargetLevel.Get();
	UWorld* World = GetWorld();
	if (!Level || !World || FLevelUtils::IsLevelLocked(Level))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = Level;
	SpawnParameters.ObjectFlags = RF_Transactional;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = MakeUniqueObjectName(
		Level,
		AComposableCameraMeshSurfaceStorageActor::StaticClass(),
		TEXT("CCS_MeshSurfaceData"));

	AComposableCameraMeshSurfaceStorageActor* NewActor =
		World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>(
			AComposableCameraMeshSurfaceStorageActor::StaticClass(),
			AnchorTransform,
			SpawnParameters);
	if (!NewActor)
	{
		return nullptr;
	}

	if (Level->IsUsingExternalActors() && !NewActor->IsPackageExternal())
	{
		NewActor->SetPackageExternal(true);
	}
	StorageActor = NewActor;
	return NewActor;
}

bool FComposableCameraMeshLayerEdMode::SaveWorkingData()
{
	if (!Settings || !TargetLevel.IsValid())
	{
		return false;
	}

	Settings->NormalizeLayers();
	RemoveOrphanedTriangles();
	AComposableCameraMeshSurfaceStorageActor* Actor = FindOrCreateStorageActor();
	if (!Actor)
	{
		UE_LOG(LogComposableCameraSystemEditor, Error,
			TEXT("Mesh layer save failed: current Level is unavailable or locked."));
		return false;
	}

	Actor->Modify();
	Actor->SetAuthoringData(Settings->Layers, WorkingData);
	Actor->MarkPackageDirty();
	TargetLevel->MarkPackageDirty();
	ULevel::LevelDirtiedEvent.Broadcast();

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.AddUnique(TargetLevel->GetPackage());
	PackagesToSave.AddUnique(Actor->GetPackage());
	const FEditorFileUtils::EPromptReturnCode Result =
		FEditorFileUtils::PromptForCheckoutAndSave(
			PackagesToSave,
			false,
			false);
	if (Result != FEditorFileUtils::PR_Success)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("Mesh layer data was applied but its package was not saved."));
		return false;
	}

	bDirty = false;
	UE_LOG(LogComposableCameraSystemEditor, Log,
		TEXT("Saved %d mesh layers and %d authored triangles to Level '%s'."),
		Settings->Layers.Num(),
		WorkingData.Indices.Num() / 3,
		*TargetLevel->GetOutermost()->GetName());
	return true;
}

bool FComposableCameraMeshLayerEdMode::InputKey(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	FKey Key,
	EInputEvent Event)
{
	if (Key == EKeys::LeftMouseButton && ViewportClient && !ViewportClient->IsAltPressed())
	{
		if (Event == IE_Pressed)
		{
			bPainting = true;
			bHasLastPaintPosition = false;
			UpdateHoverHit(ViewportClient);
			PaintAtHover(ViewportClient);
			return true;
		}
		if (Event == IE_Released)
		{
			bPainting = false;
			bHasLastPaintPosition = false;
			return true;
		}
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FComposableCameraMeshLayerEdMode::MouseMove(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	int32 X,
	int32 Y)
{
	UpdateHoverHit(ViewportClient);
	if (bPainting)
	{
		PaintAtHover(ViewportClient);
		return true;
	}
	return false;
}

bool FComposableCameraMeshLayerEdMode::CapturedMouseMove(
	FEditorViewportClient* ViewportClient,
	FViewport* Viewport,
	int32 X,
	int32 Y)
{
	return MouseMove(ViewportClient, Viewport, X, Y);
}

bool FComposableCameraMeshLayerEdMode::UpdateHoverHit(FEditorViewportClient* ViewportClient)
{
	bHasHoverHit = false;
	if (!ViewportClient || !GetWorld())
	{
		return false;
	}

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerBrush), true);
	QueryParams.bTraceComplex = true;
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get())
	{
		QueryParams.AddIgnoredActor(Actor);
	}

	bHasHoverHit = GetWorld()->LineTraceSingleByChannel(
		HoverHit,
		Cursor.GetOrigin(),
		Cursor.GetOrigin() + Cursor.GetDirection() * HALF_WORLD_MAX,
		ECC_Visibility,
		QueryParams);
	const FVector DocumentUp = AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	if (bHasHoverHit
		&& FVector::DotProduct(HoverHit.ImpactNormal, DocumentUp) < Settings->MinimumFloorNormalZ)
	{
		bHasHoverHit = false;
	}
	ViewportClient->Invalidate(false, false);
	return bHasHoverHit;
}

bool FComposableCameraMeshLayerEdMode::PaintAtHover(FEditorViewportClient* ViewportClient)
{
	if (!bHasHoverHit || !Settings || !Settings->Layers.IsValidIndex(Settings->ActiveLayerIndex))
	{
		return false;
	}

	const double MinimumSpacing = Settings->BrushRadius * 0.35;
	if (bHasLastPaintPosition
		&& FVector::Distance(LastPaintWorldPosition, HoverHit.ImpactPoint) < MinimumSpacing)
	{
		return false;
	}

	const FGuid LayerId = Settings->Layers[Settings->ActiveLayerIndex].LayerId;
	const bool bTemporaryErase = ViewportClient && ViewportClient->IsShiftPressed();
	const bool bChanged = Settings->bErase || bTemporaryErase
		? EraseBrushStamp(HoverHit, LayerId)
		: AddProjectedBrushStamp(HoverHit, LayerId);
	if (bChanged)
	{
		LastPaintWorldPosition = HoverHit.ImpactPoint;
		bHasLastPaintPosition = true;
		bDirty = true;
		bVisualizationDirty = true;
	}
	return bChanged;
}

bool FComposableCameraMeshLayerEdMode::AddProjectedBrushStamp(
	const FHitResult& CenterHit,
	const FGuid& LayerId)
{
	UWorld* World = GetWorld();
	if (!World || !LayerId.IsValid())
	{
		return false;
	}

	const FVector Normal = CenterHit.ImpactNormal.GetSafeNormal();
	FVector TangentX = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	if (TangentX.IsNearlyZero())
	{
		TangentX = FVector::ForwardVector;
	}
	const FVector TangentY = FVector::CrossProduct(Normal, TangentX).GetSafeNormal();
	const int32 SegmentCount = FMath::Clamp(Settings->BrushSegments, 6, 32);

	TArray<FVector, TInlineAllocator<32>> RingPoints;
	TArray<bool, TInlineAllocator<32>> ValidPoints;
	RingPoints.SetNum(SegmentCount);
	ValidPoints.Init(false, SegmentCount);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CCSMeshLayerProjection), true);
	QueryParams.bTraceComplex = true;
	if (AComposableCameraMeshSurfaceStorageActor* Actor = StorageActor.Get())
	{
		QueryParams.AddIgnoredActor(Actor);
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * SegmentIndex / SegmentCount;
		const FVector Desired = CenterHit.ImpactPoint
			+ TangentX * (FMath::Cos(Angle) * Settings->BrushRadius)
			+ TangentY * (FMath::Sin(Angle) * Settings->BrushRadius);

		FHitResult ProjectionHit;
		if (World->LineTraceSingleByChannel(
			ProjectionHit,
			Desired + Normal * Settings->ProjectionDistance,
			Desired - Normal * Settings->ProjectionDistance,
			ECC_Visibility,
			QueryParams)
			&& FVector::DotProduct(
				ProjectionHit.ImpactNormal,
				AnchorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal())
				>= Settings->MinimumFloorNormalZ)
		{
			RingPoints[SegmentIndex] = ProjectionHit.ImpactPoint;
			ValidPoints[SegmentIndex] = true;
		}
	}

	int32 AddedTriangleCount = 0;
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const int32 NextIndex = (SegmentIndex + 1) % SegmentCount;
		if (!ValidPoints[SegmentIndex] || !ValidPoints[NextIndex])
		{
			continue;
		}

		const int32 Index0 = WorkingData.Vertices.Add(
			FVector3f(AnchorTransform.InverseTransformPosition(CenterHit.ImpactPoint)));
		const int32 Index1 = WorkingData.Vertices.Add(
			FVector3f(AnchorTransform.InverseTransformPosition(RingPoints[SegmentIndex])));
		const int32 Index2 = WorkingData.Vertices.Add(
			FVector3f(AnchorTransform.InverseTransformPosition(RingPoints[NextIndex])));
		WorkingData.Indices.Add(Index0);
		WorkingData.Indices.Add(Index1);
		WorkingData.Indices.Add(Index2);
		WorkingData.TriangleLayerIds.Add(LayerId);
		++AddedTriangleCount;
	}

	return AddedTriangleCount > 0;
}

bool FComposableCameraMeshLayerEdMode::EraseBrushStamp(
	const FHitResult& CenterHit,
	const FGuid& LayerId)
{
	if (!WorkingData.IsConsistent() || !LayerId.IsValid())
	{
		return false;
	}

	FComposableCameraMeshSurfaceAuthoringData RemainingData;
	RemainingData.Vertices.Reserve(WorkingData.Vertices.Num());
	RemainingData.Indices.Reserve(WorkingData.Indices.Num());
	RemainingData.TriangleLayerIds.Reserve(WorkingData.TriangleLayerIds.Num());
	int32 RemovedCount = 0;

	const int32 TriangleCount = WorkingData.Indices.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32 Offset = TriangleIndex * 3;
		const int32 SourceIndex0 = WorkingData.Indices[Offset];
		const int32 SourceIndex1 = WorkingData.Indices[Offset + 1];
		const int32 SourceIndex2 = WorkingData.Indices[Offset + 2];
		if (!WorkingData.Vertices.IsValidIndex(SourceIndex0)
			|| !WorkingData.Vertices.IsValidIndex(SourceIndex1)
			|| !WorkingData.Vertices.IsValidIndex(SourceIndex2))
		{
			continue;
		}

		const FVector World0 = AnchorTransform.TransformPosition(FVector(WorkingData.Vertices[SourceIndex0]));
		const FVector World1 = AnchorTransform.TransformPosition(FVector(WorkingData.Vertices[SourceIndex1]));
		const FVector World2 = AnchorTransform.TransformPosition(FVector(WorkingData.Vertices[SourceIndex2]));
		const FVector Centroid = (World0 + World1 + World2) / 3.0;
		const FVector Delta = Centroid - CenterHit.ImpactPoint;
		const double NormalDistance = FVector::DotProduct(Delta, CenterHit.ImpactNormal);
		const FVector SurfaceDelta = Delta - CenterHit.ImpactNormal * NormalDistance;
		const bool bInsideBrush = SurfaceDelta.Length() <= Settings->BrushRadius
			&& FMath::Abs(NormalDistance) <= Settings->ProjectionDistance;
		if (WorkingData.TriangleLayerIds[TriangleIndex] == LayerId && bInsideBrush)
		{
			++RemovedCount;
			continue;
		}

		const int32 TargetIndex0 = RemainingData.Vertices.Add(WorkingData.Vertices[SourceIndex0]);
		const int32 TargetIndex1 = RemainingData.Vertices.Add(WorkingData.Vertices[SourceIndex1]);
		const int32 TargetIndex2 = RemainingData.Vertices.Add(WorkingData.Vertices[SourceIndex2]);
		RemainingData.Indices.Add(TargetIndex0);
		RemainingData.Indices.Add(TargetIndex1);
		RemainingData.Indices.Add(TargetIndex2);
		RemainingData.TriangleLayerIds.Add(WorkingData.TriangleLayerIds[TriangleIndex]);
	}

	if (RemovedCount > 0)
	{
		WorkingData = MoveTemp(RemainingData);
		return true;
	}
	return false;
}

void FComposableCameraMeshLayerEdMode::RemoveOrphanedTriangles()
{
	if (!Settings || !WorkingData.IsConsistent())
	{
		return;
	}

	TSet<FGuid> ValidLayerIds;
	for (const FComposableCameraMeshLayerDefinition& Layer : Settings->Layers)
	{
		ValidLayerIds.Add(Layer.LayerId);
	}

	FComposableCameraMeshSurfaceAuthoringData CleanData;
	CleanData.Vertices.Reserve(WorkingData.Vertices.Num());
	CleanData.Indices.Reserve(WorkingData.Indices.Num());
	CleanData.TriangleLayerIds.Reserve(WorkingData.TriangleLayerIds.Num());
	const int32 TriangleCount = WorkingData.Indices.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		if (!ValidLayerIds.Contains(WorkingData.TriangleLayerIds[TriangleIndex]))
		{
			continue;
		}

		const int32 Offset = TriangleIndex * 3;
		const int32 SourceIndex0 = WorkingData.Indices[Offset];
		const int32 SourceIndex1 = WorkingData.Indices[Offset + 1];
		const int32 SourceIndex2 = WorkingData.Indices[Offset + 2];
		if (!WorkingData.Vertices.IsValidIndex(SourceIndex0)
			|| !WorkingData.Vertices.IsValidIndex(SourceIndex1)
			|| !WorkingData.Vertices.IsValidIndex(SourceIndex2))
		{
			continue;
		}

		const int32 TargetIndex0 = CleanData.Vertices.Add(WorkingData.Vertices[SourceIndex0]);
		const int32 TargetIndex1 = CleanData.Vertices.Add(WorkingData.Vertices[SourceIndex1]);
		const int32 TargetIndex2 = CleanData.Vertices.Add(WorkingData.Vertices[SourceIndex2]);
		CleanData.Indices.Add(TargetIndex0);
		CleanData.Indices.Add(TargetIndex1);
		CleanData.Indices.Add(TargetIndex2);
		CleanData.TriangleLayerIds.Add(WorkingData.TriangleLayerIds[TriangleIndex]);
	}
	WorkingData = MoveTemp(CleanData);
}

void FComposableCameraMeshLayerEdMode::MarkLayerDataDirty()
{
	bDirty = true;
	bVisualizationDirty = true;
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void FComposableCameraMeshLayerEdMode::Render(
	const FSceneView* View,
	FViewport* Viewport,
	FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
	if (Settings)
	{
		if (bVisualizationDirty)
		{
			UE::ComposableCamera::MeshEditor::BuildAuthoringVisualization(
				WorkingData,
				Settings->Layers,
				Visualization);
			bVisualizationDirty = false;
		}
		UE::ComposableCamera::MeshEditor::DrawVisualization(
			PDI,
			AnchorTransform,
			Visualization,
			Settings->Layers);
	}

	if (bHasHoverHit && Settings)
	{
		const FVector Normal = HoverHit.ImpactNormal.GetSafeNormal();
		FVector TangentX = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		if (TangentX.IsNearlyZero())
		{
			TangentX = FVector::ForwardVector;
		}
		const FVector TangentY = FVector::CrossProduct(Normal, TangentX).GetSafeNormal();
		const FLinearColor Color = Settings->bErase ? FLinearColor::Red : FLinearColor::White;
		DrawCircle(PDI,
			HoverHit.ImpactPoint + Normal * 2.0,
			TangentX,
			TangentY,
			Color,
			Settings->BrushRadius,
			32,
			SDPG_Foreground,
			1.5f);
	}
}

FText FComposableCameraMeshLayerEdMode::GetStatusText() const
{
	const FString LevelName = TargetLevel.IsValid()
		? TargetLevel->GetOutermost()->GetName()
		: TEXT("<no level>");
	return FText::Format(
		LOCTEXT("Status", "Level: {0}\nLayers: {1}  Triangles: {2}\nState: {3}"),
		FText::FromString(LevelName),
		FText::AsNumber(Settings ? Settings->Layers.Num() : 0),
		FText::AsNumber(WorkingData.Indices.Num() / 3),
		bDirty ? LOCTEXT("Dirty", "Unsaved") : LOCTEXT("Saved", "Saved"));
}

#undef LOCTEXT_NAMESPACE
