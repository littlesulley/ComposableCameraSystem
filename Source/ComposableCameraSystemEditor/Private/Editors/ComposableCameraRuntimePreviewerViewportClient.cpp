// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraRuntimePreviewerViewportClient.h"

#include "AdvancedPreviewScene.h"
#include "Animation/SkeletalMeshActor.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PreviewScene.h"
#include "SceneManagement.h"
#include "SEditorViewport.h"

#define LOCTEXT_NAMESPACE "ComposableCameraRuntimePreviewerViewportClient"

namespace
{
	const FVector kFallbackPawnScale(0.7f, 0.7f, 1.8f);
	const TCHAR* const kFallbackPawnMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	const FLinearColor kCameraColor(0.15f, 0.75f, 1.0f, 1.0f);
	const FLinearColor kFrustumColor(0.15f, 0.85f, 1.0f, 0.9f);
	const FLinearColor kMovementColor(0.2f, 1.0f, 0.35f, 1.0f);

	float GetViewportAspectRatio(const FViewport* Viewport)
	{
		if (!Viewport)
		{
			return 16.0f / 9.0f;
		}
		const FIntPoint Size = Viewport->GetSizeXY();
		return Size.Y > 0
			? static_cast<float>(Size.X) / static_cast<float>(Size.Y)
			: 16.0f / 9.0f;
	}
}

FText RuntimePreviewerStatusToText(ERuntimePreviewerStatus Status)
{
	switch (Status)
	{
	case ERuntimePreviewerStatus::NoPIE:
		return LOCTEXT("RuntimePreviewerNoPIE", "Start PIE to preview runtime camera relation.");
	case ERuntimePreviewerStatus::NoCamera:
		return LOCTEXT("RuntimePreviewerNoCamera", "No matching runtime camera selected.");
	case ERuntimePreviewerStatus::NoPawn:
		return LOCTEXT("RuntimePreviewerNoPawn", "Runtime camera has no controlled pawn.");
	case ERuntimePreviewerStatus::Live:
		return LOCTEXT("RuntimePreviewerLive", "Live runtime preview.");
	default:
		return LOCTEXT("RuntimePreviewerUnknown", "Runtime preview unavailable.");
	}
}

namespace ComposableCameraSystem::RuntimePreviewer
{
	FTransform MakeSubjectRelativeTransform(const FTransform& SourceWorldTransform,
		const FTransform& SubjectWorldTransform)
	{
		return SourceWorldTransform.GetRelativeTransform(SubjectWorldTransform);
	}

	FTransform MakeTranslationRelativeTransform(const FTransform& SourceWorldTransform,
		const FTransform& SubjectWorldTransform)
	{
		const FTransform SubjectOriginTransform(FQuat::Identity,
			SubjectWorldTransform.GetLocation(),
			FVector::OneVector);
		return SourceWorldTransform.GetRelativeTransform(SubjectOriginTransform);
	}

	FTransform MakeCameraPreviewTransform(const FTransform& CameraWorldTransform,
		const FTransform& SubjectWorldTransform)
	{
		return MakeTranslationRelativeTransform(CameraWorldTransform,
			SubjectWorldTransform);
	}

	FTransform MakeSkeletalSubjectWorldTransform(
		const FTransform& ComponentWorldTransform,
		const TArray<FTransform>& ComponentSpaceTransforms)
	{
		FTransform SubjectWorldTransform = ComponentWorldTransform;
		if (ComponentSpaceTransforms.Num() > 0)
		{
			SubjectWorldTransform = ComponentSpaceTransforms[0] * ComponentWorldTransform;
		}
		SubjectWorldTransform.SetScale3D(FVector::OneVector);
		return SubjectWorldTransform;
	}

	float ComputeFloorOffsetForBounds(const FBox& Bounds)
	{
		return Bounds.IsValid
			? FMath::Max(0.0f, static_cast<float>(-Bounds.Min.Z))
			: 0.0f;
	}
}

FComposableCameraRuntimePreviewerViewportClient::FComposableCameraRuntimePreviewerViewportClient(
	FAdvancedPreviewScene* InPreviewScene,
	const TSharedRef<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(nullptr, InPreviewScene, InEditorViewportWidget)
	, PreviewScene(InPreviewScene)
{
	SetViewMode(VMI_Lit);
	EngineShowFlags.SetGrid(true);
	SetRealtime(true);

	SetViewLocation(FVector(-350.0f, -350.0f, 180.0f));
	SetViewRotation(FRotator(-18.0f, 45.0f, 0.0f));
	ViewFOV = 60.0f;
	bSetListenerPosition = false;
}

FComposableCameraRuntimePreviewerViewportClient::~FComposableCameraRuntimePreviewerViewportClient()
{
	ProxyActor.Reset();
}

void FComposableCameraRuntimePreviewerViewportClient::SetPreviewData(
	const FComposableCameraRuntimePreviewData& InData)
{
	PreviewData = InData;
	if (!PreviewData.bHasValidCameraPose)
	{
		ClearPreviewData(ERuntimePreviewerStatus::NoCamera);
	}
	else if (!PreviewData.ControlledPawn.IsValid())
	{
		ClearPreviewData(ERuntimePreviewerStatus::NoPawn);
	}
	else
	{
		Status = ERuntimePreviewerStatus::Live;
		RefreshPreviewNow();
	}
}

void FComposableCameraRuntimePreviewerViewportClient::ClearPreviewData(ERuntimePreviewerStatus InStatus)
{
	PreviewData = FComposableCameraRuntimePreviewData();
	Status = InStatus;
	LastSourcePawn.Reset();
	LastSkeletalMesh.Reset();
	LastStaticMesh.Reset();
	bProxyUsesFallback = false;
	bSkeletalPoseCopyFailed = false;
	DestroyPawnProxy();
	Invalidate(false, false);
}

void FComposableCameraRuntimePreviewerViewportClient::ReleaseSceneResources()
{
	ClearPreviewData(ERuntimePreviewerStatus::NoPIE);
}

void FComposableCameraRuntimePreviewerViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	if (Viewport == nullptr)
	{
		return;
	}

	RefreshPreviewNow();
}

void FComposableCameraRuntimePreviewerViewportClient::RefreshPreviewNow()
{
	if (Status != ERuntimePreviewerStatus::Live)
	{
		if (ProxyActor.IsValid())
		{
			DestroyPawnProxy();
		}
		Invalidate(false, false);
		return;
	}

	if (NeedsProxyRebuild())
	{
		RebuildPawnProxy();
	}

	SyncPawnProxy();
	UpdateFloorOffsetForProxy();
	Invalidate(false, false);
}

void FComposableCameraRuntimePreviewerViewportClient::Draw(const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	if (Status != ERuntimePreviewerStatus::Live)
	{
		return;
	}

	DrawRuntimeCamera(View, PDI);
	DrawMovementArrow(PDI);
}

void FComposableCameraRuntimePreviewerViewportClient::DrawCanvas(FViewport& InViewport,
	FSceneView& View,
	FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	auto DrawLine = [&Canvas, Font](int32 LineIdx, const FString& Text,
		const FLinearColor& Color)
	{
		FCanvasTextItem Item(FVector2D(8.0f, 8.0f + LineIdx * 14.0f),
			FText::FromString(Text),
			Font,
			Color);
		Item.EnableShadow(FLinearColor::Black);
		Canvas.DrawItem(Item);
	};

	const FLinearColor White(0.92f, 0.96f, 1.0f, 1.0f);
	const FLinearColor Gray(0.68f, 0.72f, 0.78f, 1.0f);
	const FLinearColor Warn(1.0f, 0.62f, 0.35f, 1.0f);

	int32 Line = 0;
	DrawLine(Line++, RuntimePreviewerStatusToText(Status).ToString(),
		Status == ERuntimePreviewerStatus::Live ? White : Warn);

	if (Status != ERuntimePreviewerStatus::Live)
	{
		return;
	}

	AActor* Pawn = PreviewData.ControlledPawn.Get();
	DrawLine(Line++, FString::Printf(TEXT("Pawn: %s"),
		Pawn ? *Pawn->GetName() : TEXT("<none>")), Gray);

	const FTransform CameraTransform = GetCameraPreviewTransform();
	const FVector CameraLoc = CameraTransform.GetLocation();
	const FRotator CameraRot = CameraTransform.Rotator();
	DrawLine(Line++, FString::Printf(TEXT("Camera Offset: %.1f, %.1f, %.1f"),
		CameraLoc.X, CameraLoc.Y, CameraLoc.Z), Gray);
	DrawLine(Line++, FString::Printf(TEXT("Camera Rotation: P %.1f  Y %.1f  R %.1f"),
		CameraRot.Pitch, CameraRot.Yaw, CameraRot.Roll), Gray);
	DrawLine(Line++, FString::Printf(TEXT("FOV: %.2f deg"),
		PreviewData.CameraFieldOfView), Gray);
	DrawLine(Line++, FString::Printf(TEXT("Context: %s%s"),
		*PreviewData.ContextName.ToString(),
		PreviewData.bIsActiveCamera ? TEXT(" (active)") : TEXT("")), Gray);
}

bool FComposableCameraRuntimePreviewerViewportClient::NeedsProxyRebuild() const
{
	AActor* Pawn = PreviewData.ControlledPawn.Get();
	if (!Pawn)
	{
		return ProxyActor.IsValid();
	}

	if (!ProxyActor.IsValid() || LastSourcePawn.Get() != Pawn)
	{
		return true;
	}

	if (USkeletalMeshComponent* SourceSK = Pawn->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (USkeletalMesh* Mesh = SourceSK->GetSkeletalMeshAsset())
		{
			if (bSkeletalPoseCopyFailed)
			{
				return LastSkeletalMesh.Get() != Mesh || !bProxyUsesFallback;
			}
			return LastSkeletalMesh.Get() != Mesh
				|| LastStaticMesh.IsValid()
				|| bProxyUsesFallback;
		}
	}

	if (UStaticMeshComponent* SourceSM = Pawn->FindComponentByClass<UStaticMeshComponent>())
	{
		if (UStaticMesh* Mesh = SourceSM->GetStaticMesh())
		{
			return LastStaticMesh.Get() != Mesh
				|| LastSkeletalMesh.IsValid()
				|| bProxyUsesFallback;
		}
	}

	return !bProxyUsesFallback || LastSkeletalMesh.IsValid() || LastStaticMesh.IsValid();
}

void FComposableCameraRuntimePreviewerViewportClient::RebuildPawnProxy()
{
	DestroyPawnProxy();

	AActor* Pawn = PreviewData.ControlledPawn.Get();
	if (!Pawn)
	{
		LastSourcePawn.Reset();
		return;
	}

	ProxyActor = SpawnProxyForPawn(Pawn);
	LastSourcePawn = Pawn;
}

void FComposableCameraRuntimePreviewerViewportClient::DestroyPawnProxy()
{
	if (AActor* Proxy = ProxyActor.Get())
	{
		Proxy->Destroy();
	}
	ProxyActor.Reset();
	LastSkeletalMesh.Reset();
	LastStaticMesh.Reset();
	bProxyUsesFallback = false;
	bSkeletalPoseCopyFailed = false;
	UpdateFloorOffsetForProxy();
}

void FComposableCameraRuntimePreviewerViewportClient::SyncPawnProxy()
{
	AActor* Pawn = PreviewData.ControlledPawn.Get();
	AActor* Proxy = ProxyActor.Get();
	if (!Pawn || !Proxy)
	{
		return;
	}

	using namespace ComposableCameraSystem::RuntimePreviewer;

	FTransform SourcePreviewTransform = MakeTranslationRelativeTransform(
		Pawn->GetActorTransform(),
		PreviewData.SubjectWorldTransform);

	if (USkeletalMeshComponent* SourceSK = Pawn->FindComponentByClass<USkeletalMeshComponent>())
	{
		SourcePreviewTransform = MakeTranslationRelativeTransform(
			SourceSK->GetComponentTransform(),
			PreviewData.SubjectWorldTransform);

		if (USkeletalMeshComponent* ProxySK = Proxy->FindComponentByClass<USkeletalMeshComponent>())
		{
			const TArray<FTransform>& SourceCST = SourceSK->GetComponentSpaceTransforms();
			TArray<FTransform>& ProxyCST = ProxySK->GetEditableComponentSpaceTransforms();
			if (SourceCST.Num() > 0 && SourceCST.Num() == ProxyCST.Num())
			{
				ProxyCST = SourceCST;
				ProxySK->ApplyEditedComponentSpaceTransforms();
				ProxySK->UpdateBounds();
				ProxySK->MarkRenderTransformDirty();
			}
			else
			{
				bSkeletalPoseCopyFailed = true;
				DestroyPawnProxy();
				bSkeletalPoseCopyFailed = true;
				ProxyActor = SpawnProxyForPawn(Pawn);
				LastSourcePawn = Pawn;
				return;
			}
		}
	}
	else if (UStaticMeshComponent* SourceSM = Pawn->FindComponentByClass<UStaticMeshComponent>())
	{
		SourcePreviewTransform = MakeTranslationRelativeTransform(
			SourceSM->GetComponentTransform(),
			PreviewData.SubjectWorldTransform);
	}

	if (bProxyUsesFallback)
	{
		Proxy->SetActorLocationAndRotation(SourcePreviewTransform.GetLocation(),
			SourcePreviewTransform.GetRotation());
	}
	else
	{
		Proxy->SetActorTransform(SourcePreviewTransform);
	}
}

AActor* FComposableCameraRuntimePreviewerViewportClient::SpawnProxyForPawn(AActor* SourcePawn)
{
	if (!PreviewScene || !SourcePawn)
	{
		return nullptr;
	}

	UWorld* PreviewWorld = PreviewScene->GetWorld();
	if (!PreviewWorld)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bNoFail = true;

	using namespace ComposableCameraSystem::RuntimePreviewer;

	if (!bSkeletalPoseCopyFailed)
	{
		if (USkeletalMeshComponent* SourceSK = SourcePawn->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (USkeletalMesh* Mesh = SourceSK->GetSkeletalMeshAsset())
			{
				ASkeletalMeshActor* Proxy = PreviewWorld->SpawnActor<ASkeletalMeshActor>(Params);
				if (Proxy && Proxy->GetSkeletalMeshComponent())
				{
					Proxy->SetActorTransform(MakeTranslationRelativeTransform(
						SourceSK->GetComponentTransform(),
						PreviewData.SubjectWorldTransform));

					USkeletalMeshComponent* ProxySK = Proxy->GetSkeletalMeshComponent();
					ProxySK->SetSkeletalMeshAsset(Mesh);
					ProxySK->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					ProxySK->SetComponentTickEnabled(false);
					Proxy->SetActorTickEnabled(false);
					ProxySK->UpdateBounds();
					ProxySK->MarkRenderTransformDirty();
					ProxySK->MarkRenderStateDirty();

					LastSkeletalMesh = Mesh;
					LastStaticMesh.Reset();
					bProxyUsesFallback = false;
					return Proxy;
				}
			}
		}

		if (UStaticMeshComponent* SourceSM = SourcePawn->FindComponentByClass<UStaticMeshComponent>())
		{
			if (UStaticMesh* Mesh = SourceSM->GetStaticMesh())
			{
				AStaticMeshActor* Proxy = PreviewWorld->SpawnActor<AStaticMeshActor>(Params);
				if (Proxy && Proxy->GetStaticMeshComponent())
				{
					Proxy->SetActorTransform(MakeTranslationRelativeTransform(
						SourceSM->GetComponentTransform(),
						PreviewData.SubjectWorldTransform));
					Proxy->GetStaticMeshComponent()->SetStaticMesh(Mesh);

					LastSkeletalMesh.Reset();
					LastStaticMesh = Mesh;
					bProxyUsesFallback = false;
					return Proxy;
				}
			}
		}
	}

	UStaticMesh* FallbackMesh = LoadObject<UStaticMesh>(nullptr, kFallbackPawnMeshPath);
	AStaticMeshActor* Proxy = PreviewWorld->SpawnActor<AStaticMeshActor>(Params);
	if (Proxy && Proxy->GetStaticMeshComponent())
	{
		if (FallbackMesh)
		{
			Proxy->GetStaticMeshComponent()->SetStaticMesh(FallbackMesh);
		}
		const FTransform PreviewTransform = MakeTranslationRelativeTransform(
			SourcePawn->GetActorTransform(),
			PreviewData.SubjectWorldTransform);
		Proxy->SetActorLocationAndRotation(PreviewTransform.GetLocation(),
			PreviewTransform.GetRotation());
		Proxy->SetActorScale3D(kFallbackPawnScale);
	}

	LastSkeletalMesh.Reset();
	LastStaticMesh.Reset();
	bProxyUsesFallback = true;
	if (USkeletalMeshComponent* SourceSK = SourcePawn->FindComponentByClass<USkeletalMeshComponent>())
	{
		LastSkeletalMesh = SourceSK->GetSkeletalMeshAsset();
	}
	return Proxy;
}

FTransform FComposableCameraRuntimePreviewerViewportClient::GetCameraPreviewTransform() const
{
	using namespace ComposableCameraSystem::RuntimePreviewer;

	const FTransform CameraWorldTransform(PreviewData.CameraRotation,
		PreviewData.CameraPosition);
	return MakeCameraPreviewTransform(CameraWorldTransform,
		PreviewData.SubjectWorldTransform);
}

void FComposableCameraRuntimePreviewerViewportClient::DrawRuntimeCamera(
	const FSceneView* /*View*/,
	FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !PreviewData.bHasValidCameraPose)
	{
		return;
	}

	const FTransform CameraTransform = GetCameraPreviewTransform();
	const FVector CameraLoc = CameraTransform.GetLocation();
	const FQuat CameraQuat = CameraTransform.GetRotation();
	const FVector Forward = CameraQuat.GetForwardVector();
	const FVector Right = CameraQuat.GetRightVector();
	const FVector Up = CameraQuat.GetUpVector();

	PDI->DrawPoint(CameraLoc, kCameraColor, 12.0f, SDPG_Foreground);
	PDI->DrawLine(CameraLoc, CameraLoc + Forward * 55.0f,
		FLinearColor::Red, SDPG_Foreground, 2.0f);
	PDI->DrawLine(CameraLoc, CameraLoc + Right * 35.0f,
		FLinearColor::Green, SDPG_Foreground, 1.5f);
	PDI->DrawLine(CameraLoc, CameraLoc + Up * 35.0f,
		FLinearColor::Blue, SDPG_Foreground, 1.5f);

	const float Aspect = GetViewportAspectRatio(Viewport);
	const float FOVDegrees = static_cast<float>(PreviewData.CameraFieldOfView);
	const float FrustumDistance = 160.0f;
	const float HalfWidth = FMath::Tan(FMath::DegreesToRadians(FOVDegrees * 0.5f)) * FrustumDistance;
	const float HalfHeight = Aspect > KINDA_SMALL_NUMBER ? HalfWidth / Aspect : HalfWidth;
	const FVector Center = CameraLoc + Forward * FrustumDistance;

	const FVector TopRight = Center + Right * HalfWidth + Up * HalfHeight;
	const FVector TopLeft = Center - Right * HalfWidth + Up * HalfHeight;
	const FVector BottomRight = Center + Right * HalfWidth - Up * HalfHeight;
	const FVector BottomLeft = Center - Right * HalfWidth - Up * HalfHeight;

	PDI->DrawLine(CameraLoc, TopRight, kFrustumColor, SDPG_Foreground, 1.0f);
	PDI->DrawLine(CameraLoc, TopLeft, kFrustumColor, SDPG_Foreground, 1.0f);
	PDI->DrawLine(CameraLoc, BottomRight, kFrustumColor, SDPG_Foreground, 1.0f);
	PDI->DrawLine(CameraLoc, BottomLeft, kFrustumColor, SDPG_Foreground, 1.0f);
	PDI->DrawLine(TopRight, TopLeft, kFrustumColor, SDPG_Foreground, 1.0f);
	PDI->DrawLine(TopLeft, BottomLeft, kFrustumColor, SDPG_Foreground, 1.0f);
	PDI->DrawLine(BottomLeft, BottomRight, kFrustumColor, SDPG_Foreground, 1.0f);
	PDI->DrawLine(BottomRight, TopRight, kFrustumColor, SDPG_Foreground, 1.0f);
}

void FComposableCameraRuntimePreviewerViewportClient::DrawMovementArrow(
	FPrimitiveDrawInterface* PDI) const
{
	if (!PDI || PreviewData.PawnVelocity.IsNearlyZero(1.0f))
	{
		return;
	}

	const FVector LocalVelocity =
		PreviewData.SubjectWorldTransform.InverseTransformVectorNoScale(PreviewData.PawnVelocity);
	if (LocalVelocity.IsNearlyZero(1.0f))
	{
		return;
	}

	const float Speed = LocalVelocity.Size();
	const FVector Direction = LocalVelocity.GetSafeNormal();
	const float Length = FMath::Clamp(Speed * 0.05f, 35.0f, 180.0f);
	const FVector Start(0.0f, 0.0f, 8.0f);
	const FVector End = Start + Direction * Length;

	PDI->DrawLine(Start, End, kMovementColor, SDPG_Foreground, 3.0f);
	PDI->DrawPoint(End, kMovementColor, 8.0f, SDPG_Foreground);
}

void FComposableCameraRuntimePreviewerViewportClient::FramePreviewSubject()
{
	FBox Bounds(ForceInit);
	Bounds += FVector::ZeroVector;
	Bounds += FVector(0.0f, 0.0f, 120.0f);

	if (ProxyActor.IsValid())
	{
		Bounds += ProxyActor->GetComponentsBoundingBox(true);
	}

	if (PreviewData.bHasValidCameraPose)
	{
		Bounds += GetCameraPreviewTransform().GetLocation();
	}

	if (!Bounds.IsValid)
	{
		Bounds = FBox(FVector(-100.0f, -100.0f, 0.0f),
			FVector(100.0f, 100.0f, 180.0f));
	}

	const FVector Center = Bounds.GetCenter();
	const float Radius = FMath::Max(250.0f, Bounds.GetExtent().Size() + 160.0f);
	const FVector ViewLocation = Center + FVector(-Radius, -Radius, Radius * 0.65f);

	SetViewLocation(ViewLocation);
	SetViewRotation((Center - ViewLocation).Rotation());
	Invalidate(false, false);
}

void FComposableCameraRuntimePreviewerViewportClient::UpdateFloorOffsetForProxy()
{
	if (!PreviewScene)
	{
		return;
	}

	FBox Bounds(ForceInit);
	if (AActor* Proxy = ProxyActor.Get())
	{
		Bounds = Proxy->GetComponentsBoundingBox(true);
	}

	PreviewScene->SetFloorOffset(
		ComposableCameraSystem::RuntimePreviewer::ComputeFloorOffsetForBounds(Bounds));
}

#undef LOCTEXT_NAMESPACE
