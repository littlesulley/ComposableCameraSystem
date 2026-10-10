// Copyright 2026 Sulley. All Rights Reserved.
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CineCameraComponent.h"
#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Actor.h"
#include "Math/ComposableCameraShotSolver.h"
#include "LevelSequence/ComposableCameraLevelSequenceShotActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Transitions/ComposableCameraLinearTransition.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FAuthoringTestWorld
{
	UWorld* World;
	FAuthoringTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
	}
	~FAuthoringTestWorld() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	AActor* Actor(FVector Location)
	{
		AActor* Result = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Result, TEXT("Root"));
		Result->SetRootComponent(Root); Root->RegisterComponent(); Result->SetActorLocation(Location);
		return Result;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotTwoTargetAxisTest,
	"ComposableCameraSystem.ShotAuthoring.TwoTargetAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotTwoTargetAxisTest::RunTest(const FString&)
{
	FAuthoringTestWorld Fixture;
	AActor* A = Fixture.Actor(FVector(20, 30, 10));
	AActor* B = Fixture.Actor(FVector(20, 330, 200));
	FComposableCameraShot Shot;
	Shot.Targets.SetNum(2); Shot.Targets[0].Target.Actor = A; Shot.Targets[1].Target.Actor = B;
	Shot.Placement.BasisFrame = EShotPlacementBasisFrame::TwoTargetAxis;
	const auto Basis = [&]() { return ComposableCameraSystem::ShotSolver::ResolvePlacementBasis(Shot); };
	TestTrue(TEXT("Horizontal A -> B defines forward"), Basis().GetForwardVector().Equals(FVector::YAxisVector));
	TestTrue(TEXT("World up stays fixed despite subject height difference"), Basis().GetUpVector().Equals(FVector::UpVector));
	A->SetActorRotation(FRotator(25, 130, 40)); B->SetActorRotation(FRotator(-20, -50, 80));
	TestTrue(TEXT("Turning subjects does not rotate the pair composition"), Basis().GetForwardVector().Equals(FVector::YAxisVector));
	Swap(Shot.Placement.BasisActorIndex, Shot.Placement.BasisSecondaryTargetIndex);
	TestTrue(TEXT("B -> A reverses forward"), Basis().GetForwardVector().Equals(-FVector::YAxisVector));
	B->SetActorLocation(A->GetActorLocation());
	TestTrue(TEXT("Coincident pair safely falls back to world"), Basis().Equals(FQuat::Identity));
	Shot.Placement.BasisSecondaryTargetIndex = 99;
	TestTrue(TEXT("Missing slot safely falls back to world"), Basis().Equals(FQuat::Identity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotNamedComponentTest,
	"ComposableCameraSystem.ShotAuthoring.ComponentPivotAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotNamedComponentTest::RunTest(const FString&)
{
	FAuthoringTestWorld Fixture;
	AActor* Actor = Fixture.Actor(FVector(10, 20, 30));
	UStaticMeshComponent* First = NewObject<UStaticMeshComponent>(Actor, TEXT("Body"));
	First->SetupAttachment(Actor->GetRootComponent()); First->RegisterComponent();
	UStaticMeshComponent* Second = NewObject<UStaticMeshComponent>(Actor, TEXT("Prop"));
	Second->SetupAttachment(Actor->GetRootComponent()); Second->RegisterComponent();
	Second->SetRelativeLocationAndRotation(FVector(100, 0, 0), FRotator(0, 90, 0));
	First->Bounds = FBoxSphereBounds(First->GetComponentLocation(), FVector(10, 20, 30), 40);
	Second->Bounds = FBoxSphereBounds(Second->GetComponentLocation(), FVector(40, 50, 60), 90);
	FComposableCameraShotTarget Target;
	Target.Target.Actor = Actor; Target.BoundsShape = EShotTargetBoundsShape::AutoFromComponentBounds;
	Target.Target.ComponentName = Second->GetFName(); Target.Target.bOffsetInLocalSpace = true; Target.Target.Offset = FVector(10, 0, 0);
	FVector Pivot; FQuat Basis;
	TestTrue(TEXT("Selected component resolves"), Target.Target.ResolveWorldPoint(Pivot));
	TestTrue(TEXT("Local offset uses selected component rotation"), Pivot.Equals(Second->GetComponentLocation() + FVector(0, 10, 0)));
	TestTrue(TEXT("Selected component basis resolves"), Target.Target.ResolveBasisQuat(Basis));
	TestTrue(TEXT("Basis uses selected component"), Basis.Equals(Second->GetComponentQuat()));
	Target.RefreshAutoBoundsCache();
	TestTrue(TEXT("Bounds belong to selected mesh"), Target.GetEffectiveBoundsExtent().Equals(FVector(40, 50, 60)));
	Target.Target.ComponentName = NAME_None; Target.RefreshAutoBoundsCache();
	TestTrue(TEXT("Clearing selection restores auto mesh, not cached explicit mesh"), Target.CachedBoundsMeshComponent.Get() == Actor->FindComponentByClass<UStaticMeshComponent>());
	Target.Target.ComponentName = TEXT("MissingComponent"); Target.RefreshAutoBoundsCache();
	TestFalse(TEXT("Missing explicit component never uses unrelated actor pivot"), Target.Target.ResolveWorldPoint(Pivot));
	TestFalse(TEXT("Missing explicit component never uses unrelated actor basis"), Target.Target.ResolveBasisQuat(Basis));
	TestTrue(TEXT("Missing component contributes no stale bounds"), Target.GetEffectiveBoundsExtent().IsZero());
	Target.Target.ComponentName = NAME_None; Target.Target.Offset = FVector::ZeroVector;
	Target.Target.ResolveWorldPoint(Pivot);
	TestTrue(TEXT("None preserves legacy actor pivot"), Pivot.Equals(Actor->GetActorLocation()));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotPivotTransformTest,
	"ComposableCameraSystem.ShotAuthoring.PivotTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotPivotTransformTest::RunTest(const FString&)
{
	FAuthoringTestWorld Fixture;
	AActor* Actor = Fixture.Actor(FVector(10, 20, 30));
	Actor->SetActorRotation(FRotator(0, 90, 0)); Actor->SetActorScale3D(FVector(3));
	USceneComponent* PivotComponent = NewObject<USceneComponent>(Actor, TEXT("Pivot"));
	PivotComponent->SetupAttachment(Actor->GetRootComponent()); PivotComponent->RegisterComponent();
	PivotComponent->SetRelativeLocationAndRotation(FVector(10, 0, 20), FRotator(20, 40, 10));
	FComposableCameraTargetInfo Target;
	Target.Actor = Actor; Target.Offset = FVector(12, 34, 56);
	for (const FName Component : { FName(), PivotComponent->GetFName() })
		for (const bool bLocal : { false, true })
		{
			Target.ComponentName = Component; Target.bOffsetInLocalSpace = bLocal;
			FTransform Frame; FVector Point; bool bUsedBone = true;
			TestTrue(TEXT("Shared pivot frame resolves"), Target.ResolvePivotTransform(Frame, &bUsedBone));
			const FVector Base = Component.IsNone() ? Actor->GetActorLocation() : PivotComponent->GetComponentLocation();
			const FQuat Rotation = Component.IsNone() ? Actor->GetActorQuat() : PivotComponent->GetComponentQuat();
			TestTrue(TEXT("Frame preserves actor/component origin"), Frame.GetLocation().Equals(Base));
			TestTrue(TEXT("Frame preserves actor/component rotation"), Frame.GetRotation().Equals(Rotation));
			TestTrue(TEXT("Offset frame intentionally has unit scale"), Frame.GetScale3D().Equals(FVector::OneVector));
			TestFalse(TEXT("Actor/component path clears bone result"), bUsedBone);
			TestTrue(TEXT("Existing point resolver remains available"), Target.ResolveWorldPoint(Point));
			TestTrue(TEXT("Existing world/local offset semantics survive refactor"), Point.Equals(Base + (bLocal ? Rotation.RotateVector(Target.Offset) : Target.Offset)));
		}
	Target.bUseBoneAsPivot = true; Target.BoneName = TEXT("MissingBone");
	FTransform Frame; bool bUsedBone = true;
	TestTrue(TEXT("Missing bone retains selected component fallback"), Target.ResolvePivotTransform(Frame, &bUsedBone));
	TestFalse(TEXT("Fallback never claims a bone pivot"), bUsedBone);
	TestTrue(TEXT("Fallback frame matches selected component"), Frame.GetLocation().Equals(PivotComponent->GetComponentLocation()));
#if WITH_EDITOR
	// A named socket exercises the same native transform query used by loaded character skeletons.
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Actor);
	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Mesh);
	Socket->SocketName = TEXT("GizmoSocket"); Socket->BoneName = TEXT("Root"); Mesh->AddSocket(Socket);
	USkeletalMeshComponent* Skel = NewObject<USkeletalMeshComponent>(Actor, TEXT("Skeleton"));
	Skel->SetupAttachment(Actor->GetRootComponent()); Skel->SetSkeletalMeshAsset(Mesh); Skel->RegisterComponent();
	Skel->SetRelativeLocationAndRotation(FVector(5, 8, 12), FRotator(10, -90, 20));
	Target.ComponentName = Skel->GetFName(); Target.BoneName = Socket->SocketName;
	TestTrue(TEXT("Named socket resolves through shared path"), Target.ResolvePivotTransform(Frame, &bUsedBone));
	TestTrue(TEXT("Socket path reports bone pivot"), bUsedBone);
	const FTransform SocketFrame = Skel->GetSocketTransform(Socket->SocketName, RTS_World);
	TestTrue(TEXT("Frame equals native socket location/rotation"), Frame.GetLocation().Equals(SocketFrame.GetLocation()) && Frame.GetRotation().Equals(SocketFrame.GetRotation()));
	FVector Point;
	TestTrue(TEXT("Existing bone point still resolves"), Target.ResolveWorldPoint(Point));
	TestTrue(TEXT("Bone local offset uses unit-scale shared socket frame"), Point.Equals(Frame.GetLocation() + Frame.GetRotation().RotateVector(Target.Offset)));
#endif
	const FTransform Sentinel(FRotator(10, 20, 30), FVector(123, 456, 789));
	Frame = Sentinel; Target.ComponentName = TEXT("MissingComponent");
	TestFalse(TEXT("Missing explicit component fails"), Target.ResolvePivotTransform(Frame, &bUsedBone));
	TestTrue(TEXT("Failed resolution preserves caller's frame"), Frame.Equals(Sentinel));
	Target.Actor.Reset(); Frame = Sentinel;
	TestFalse(TEXT("Missing actor fails"), Target.ResolvePivotTransform(Frame));
	TestTrue(TEXT("Missing actor preserves caller's frame"), Frame.Equals(Sentinel));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotPreviewRefreshTest,
	"ComposableCameraSystem.ShotAuthoring.ActivePreviewPreservesBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShotPreviewRefreshTest::RunTest(const FString&)
{
	FAuthoringTestWorld Fixture;
	auto* Actor = Fixture.World->SpawnActor<AComposableCameraLevelSequenceShotActor>();
	auto* Component = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	auto* A = NewObject<UMovieSceneComposableCameraShotSection>(Component);
	auto* B = NewObject<UMovieSceneComposableCameraShotSection>(Component);
	auto* C = NewObject<UMovieSceneComposableCameraShotSection>(Component);
	auto* Transition = NewObject<UComposableCameraTransitionDataAsset>(Component);
	Transition->Transition = NewObject<UComposableCameraLinearTransition>(Transition);
	const auto EntryAt = [](double X, int32 Row)
	{
		FComposableCameraSequencerShotEntry Entry;
		Entry.RowIndex = Row;
		Entry.Shot.Placement.Mode = EShotPlacementMode::FixedWorldPosition;
		Entry.Shot.Placement.FixedWorldPosition = FVector(X, 0, 0);
		Entry.Shot.Placement.PlacementAnchor.Mode = EShotAnchorMode::FixedWorldPosition;
		Entry.Shot.Aim.Mode = EShotAimMode::NoOp;
		Entry.Shot.Aim.AimAnchor.Mode = EShotAnchorMode::FixedWorldPosition;
		Entry.Shot.Focus.Mode = EShotFocusMode::Manual;
		return Entry;
	};
	FComposableCameraSequencerShotEntry Outgoing = EntryAt(100, 0);
	FComposableCameraSequencerShotEntry Incoming = EntryAt(300, 3);
	Incoming.EnterTransition = Transition; Incoming.BlendAlpha = .25f;
	TestFalse(TEXT("Inactive section cannot be registered by preview"), Component->RefreshShotEditorPreview(B, Incoming.Shot));
	Component->SetSequencerShotOverride(A, Outgoing);
	Component->SetSequencerShotOverride(B, Incoming);
	Component->SetSequencerShotOverride(C, EntryAt(900, 7));
	Incoming.Shot.Placement.FixedWorldPosition.X = 500;
	TestTrue(TEXT("Existing active section refreshes immediately"), Component->RefreshShotEditorPreview(B, Incoming.Shot));
	const double Weight = Transition->Transition->GetBlendWeightAt(.25f);
	TestTrue(TEXT("Preview retains row priority, transition, alpha and ignores third section"),
		FMath::IsNearlyEqual(Component->OutputCineCameraComponent->GetComponentLocation().X, 100.0 + 400.0 * Weight, .01));
	Component->RemoveSequencerShotOverride(B);
	TestFalse(TEXT("Removed section cannot be revived by authoring"), Component->RefreshShotEditorPreview(B, Incoming.Shot));
	return true;
}
#endif
#endif
