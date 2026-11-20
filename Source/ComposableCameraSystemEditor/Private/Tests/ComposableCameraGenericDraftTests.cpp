// Copyright 2026 Sulley. All Rights Reserved.
#include "Cameras/ComposableCameraCameraBase.h"
#include "Bindings/MovieSceneSpawnableActorBinding.h"
#include "CineCameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotSubjectBinding.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Interpolator/ComposableCameraIIRInterpolator.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequence.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneBindingReferences.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/ComposableCameraInstanceTime.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceParameterTrack.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraPatchSection.h"
#include "Nodes/ComposableCameraReceivePivotActorNode.h"
#include "Nodes/ComposableCameraPivotDampingNode.h"
#include "Nodes/ComposableCameraCameraOffsetNode.h"
#include "Nodes/ComposableCameraLensNode.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sequencer/ComposableCameraInstanceDragDropOp.h"
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/SShotEditorRoot.h"
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorSubjectPicker.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
UComposableCameraTypeAsset* MakeGenericDraftType()
{
	auto* Type = NewObject<UComposableCameraTypeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
	auto* Receive = NewObject<UComposableCameraReceivePivotActorNode>(Type);
	auto* Damp = NewObject<UComposableCameraPivotDampingNode>(Type);
	Damp->bMaintainCameraSpacePivotPosition = false;
	auto* Interpolator = NewObject<UComposableCameraIIRInterpolator>(Damp);
	Interpolator->Speed = 1.f; Interpolator->bUseFixedStep = false;
	Damp->ForwardInterpolator = Interpolator;
	auto* Offset = NewObject<UComposableCameraCameraOffsetNode>(Type);
	Offset->CameraOffset = FVector::ZeroVector;
	auto* Lens = NewObject<UComposableCameraLensNode>(Type); Lens->FocalLength = 65.f;
	Type->NodeTemplates = { Receive, Damp, Offset, Lens };
	Type->NodePinOverrides.SetNum(4);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto& Entry = Type->FullExecChain.AddDefaulted_GetRef();
		Entry.EntryType = EComposableCameraExecEntryType::CameraNode; Entry.CameraNodeIndex = Index;
		Type->ExecutionOrder.Add(Index);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto& Wire = Type->PinConnections.AddDefaulted_GetRef();
		Wire.SourceNodeIndex = Index; Wire.TargetNodeIndex = Index + 1;
		Wire.SourcePinName = Wire.TargetPinName = TEXT("PivotPosition");
	}
	auto& Actor = Type->ExposedParameters.AddDefaulted_GetRef();
	Actor.ParameterName = TEXT("Subject"); Actor.PinType = EComposableCameraPinType::Actor;
	Actor.TargetNodeIndex = 0; Actor.TargetPinName = TEXT("PivotActor"); Actor.bRequired = true;
	auto& Focal = Type->ExposedParameters.AddDefaulted_GetRef();
	Focal.ParameterName = TEXT("LensLength"); Focal.PinType = EComposableCameraPinType::Float;
	Focal.TargetNodeIndex = 3; Focal.TargetPinName = TEXT("FocalLength");
	return Type;
}
FComposableCameraInstanceField DraftField(UComposableCameraInstanceData& Values, FName Path)
{
	TArray<FComposableCameraInstanceField> Fields; Values.GatherFields(Fields);
	const auto* Field = Fields.FindByPredicate([Path](const auto& Item) { return Item.Path == Path; });
	return Field ? *Field : FComposableCameraInstanceField();
}
template<typename T> TSharedPtr<T> FindGenericWidget(const TSharedRef<SWidget>& Widget, FName Type)
{
	if (Widget->GetType() == Type) return StaticCastSharedRef<T>(Widget);
	for (int32 Index = 0; Index < Widget->GetChildren()->Num(); ++Index)
		if (const auto Found = FindGenericWidget<T>(Widget->GetChildren()->GetChildAt(Index), Type)) return Found;
	return nullptr;
}
bool GenericMenuHasText(const TSharedRef<SWidget>& Widget, const FString& Text)
{
	if (Widget->GetType() == FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Text)) return true;
	for (int32 Index = 0; Index < Widget->GetChildren()->Num(); ++Index)
		if (GenericMenuHasText(Widget->GetChildren()->GetChildAt(Index), Text)) return true;
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceInterfaceRetentionTest,
	"ComposableCameraSystem.CameraInstance.InterfaceRetention", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceInterfaceRetentionTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive()) return false;
	TStrongObjectPtr<UComposableCameraTypeAsset> Type(MakeGenericDraftType());
	TStrongObjectPtr<UComposableCameraInstanceData> Values(NewObject<UComposableCameraInstanceData>());
	Values->InitializeFromType(Type.Get());
	DraftField(*Values.Get(), TEXT("Parameters.LensLength")).WriteNumber(90);
	const FGuid Binding = FGuid::NewGuid();
	Values->Subjects[0].Binding = UE::MovieScene::FRelativeObjectBindingID(Binding);
	auto& Aperture = Type->ExposedParameters.AddDefaulted_GetRef();
	Aperture.ParameterName = TEXT("Aperture"); Aperture.PinType = EComposableCameraPinType::Float;
	Aperture.TargetNodeIndex = 3; Aperture.TargetPinName = TEXT("Aperture");
	auto& Added = Type->ExposedVariables.AddDefaulted_GetRef();
	Added.VariableName = TEXT("Gain"); Added.VariableType = EComposableCameraPinType::Float; Added.InitialValueString = TEXT("12.5");
	Type->Build(false);
	TestFalse(TEXT("Same type pointer invalidates after Build"), Values->IsTypeInterfaceCurrent());
	TestTrue(TEXT("Additive interface migration succeeds"), Values->RefreshTypeInterface());
	TestEqual(TEXT("Old override survives addition"), DraftField(*Values.Get(), TEXT("Parameters.LensLength")).ReadNumber(), 90.);
	TestTrue(TEXT("New parameter receives node default"), FMath::IsNearlyEqual(DraftField(*Values.Get(), TEXT("Parameters.Aperture")).ReadNumber(), 2.8, .001));
	TestEqual(TEXT("New variable receives source default"), DraftField(*Values.Get(), TEXT("Variables.Gain")).ReadNumber(), 12.5);
	const auto* OldLayout = Values->Camera.Parameters.GetPropertyBagStruct();
	{
		const FScopedTransaction Transaction(NSLOCTEXT("GenericDraftTest", "RemoveInput", "Remove Type Input"));
		Type->Modify(); Type->ExposedParameters.RemoveAt(1); Type->Build(false);
	}
	TestFalse(TEXT("Removed input blocks the instance"), Values->RefreshTypeInterface());
	TestTrue(TEXT("Failed migration retains the old bag"), Values->Camera.Parameters.GetPropertyBagStruct() == OldLayout);
	TestEqual(TEXT("Orphan override survives"), DraftField(*Values.Get(), TEXT("Parameters.LensLength")).ReadNumber(), 90.);
	Values->PostLoad(); // Exercise the load hook that used to migrate destructively.
	TestFalse(TEXT("Load also rejects destructive migration"), Values->IsTypeInterfaceValid());
	TestEqual(TEXT("Load retains orphan data"), DraftField(*Values.Get(), TEXT("Parameters.LensLength")).ReadNumber(), 90.);
	TestTrue(TEXT("Binding identity survives migration and load"), Values->Subjects[0].Binding.GetGuid() == Binding);
	TestTrue(TEXT("Source interface Undo is available"), GEditor->UndoTransaction());
	TestFalse(TEXT("Undo invalidates cached validation"), Values->IsTypeInterfaceCurrent());
	TestTrue(TEXT("Restoring source interface recovers the old override"), Values->RefreshTypeInterface());
	TestEqual(TEXT("Restored value is unchanged"), DraftField(*Values.Get(), TEXT("Parameters.LensLength")).ReadNumber(), 90.);
	Type->ExposedParameters[1].PinType = EComposableCameraPinType::Vector3D; Type->Build(false);
	TestFalse(TEXT("Type changes cannot reinterpret old bytes"), Values->RefreshTypeInterface());
	TestTrue(TEXT("Both bags remain intact on type change"), Values->Camera.Parameters.GetPropertyBagStruct() == OldLayout
		&& DraftField(*Values.Get(), TEXT("Variables.Gain")).ReadNumber() == 12.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceColdResetTest,
	"ComposableCameraSystem.CameraInstance.ColdReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceColdResetTest::RunTest(const FString&)
{
	FComposableCameraInstanceTime Clock;
	const FFrameRate Rate(24, 1);
	TestTrue(TEXT("First sample cold-starts"), Clock.Sample(FFrameTime(0), Rate, false, false).bReset);
	const auto Subframe = Clock.Sample(FFrameTime(FFrameNumber(0), .5f), Rate, false, false);
	TestTrue(TEXT("Subframe playback advances without reset"), !Subframe.bReset && FMath::IsNearlyEqual(Subframe.DeltaSeconds, 1.f / 48.f));
	TestFalse(TEXT("Repeated time never advances twice"), Clock.Sample(FFrameTime(FFrameNumber(0), .5f), Rate, true, false).bChanged);
	TestTrue(TEXT("Backward seek resets"), Clock.Sample(FFrameTime(0), Rate, false, false).bReset);
	TestTrue(TEXT("Forward jump resets"), Clock.Sample(FFrameTime(100), Rate, true, false).bReset);
	TestFalse(TEXT("Stopped/MRQ status alone is not a seek"), FComposableCameraInstanceTime::IsDiscontinuousStatus(EMovieScenePlayerStatus::Stopped));
	if (!GEngine) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	TStrongObjectPtr<UComposableCameraTypeAsset> Type(MakeGenericDraftType());
	TStrongObjectPtr<UComposableCameraInstanceData> Values(NewObject<UComposableCameraInstanceData>());
	Values->InitializeFromType(Type.Get());
	auto* Subject = World->SpawnActor<AStaticMeshActor>();
	auto* Camera = World->SpawnActor<AComposableCameraLevelSequenceActor>();
	if (!Subject || !Camera) return false;
	Subject->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	auto* Output = Camera->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	Output->bCameraInstanceDriven = true;
	DraftField(*Values.Get(), TEXT("Parameters.Subject")).WriteObject(Subject);
	Output->EvaluateCameraInstance(*Values.Get(), 0.f, true);
	const TWeakObjectPtr<AComposableCameraCameraBase> Initial = Output->GetInternalCamera();
	Subject->SetActorLocation(FVector(100, 0, 0));
	Output->EvaluateCameraInstance(*Values.Get(), .1f, false);
	TestTrue(TEXT("Continuous playback retains evaluator"), Initial.Get() == Output->GetInternalCamera());
	const float ContinuousX = Output->OutputCineCameraComponent->GetComponentLocation().X;
	TestTrue(TEXT("Real damping retains history"), ContinuousX > 0.f && ContinuousX < 100.f);
	TStrongObjectPtr<UMovieSceneComposableCameraPatchSection> Patch(NewObject<UMovieSceneComposableCameraPatchSection>());
	Patch->PatchAsset = NewObject<UComposableCameraPatchTypeAsset>(Patch.Get());
	Output->SetSequencerPatchOverlay(Patch.Get(), FComposableCameraParameterBlock(), 0.f);
	Output->EvaluateCameraInstance(*Values.Get(), 0.f, true);
	TestTrue(TEXT("Seek reconstructs stateful nodes"), Initial.Get() != Output->GetInternalCamera());
	TestTrue(TEXT("Cold start seeds the current Actor position"), FMath::IsNearlyEqual(Output->OutputCineCameraComponent->GetComponentLocation().X, 100.));
	TArray<FComposableCameraPatchSnapshot> Patches; Output->BuildSequencerPatchSnapshot(Patches);
	TestEqual(TEXT("Seek keeps Patch registration"), Patches.Num(), 1);
	Output->ReleaseCameraInstance();
	TestNull(TEXT("Release removes the evaluator"), Output->GetInternalCamera());
	TestFalse(TEXT("Release clears output validity"), Output->HasCameraInstanceResult());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceSubjectComponentRefreshTest,
	"ComposableCameraSystem.CameraInstance.SubjectComponentRefresh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceSubjectComponentRefreshTest::RunTest(const FString&)
{
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	Sequence->Initialize();
	auto* Scene = Sequence->GetMovieScene();
	Scene->SetTickResolutionDirectly(FFrameRate(24, 1)); Scene->SetDisplayRate(FFrameRate(24, 1)); Scene->SetPlaybackRange(0, 100);
	FSequencerInitParams Params; Params.RootSequence = Sequence.Get(); Params.PlaybackContext = World;
	Params.ViewParams.UniqueName = TEXT("SubjectComponentRefreshRegression");
	const auto Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
	const auto PreviousAuto = Sequencer->GetAutoChangeMode();
	const auto PreviousAllow = Sequencer->GetAllowEditsMode();
	Sequencer->SetAutoChangeMode(EAutoChangeMode::None); Sequencer->SetAllowEditsMode(EAllowEditsMode::AllEdits);
	ON_SCOPE_EXIT { Sequencer->SetAutoChangeMode(PreviousAuto); Sequencer->SetAllowEditsMode(PreviousAllow); Sequencer->Close(); };
	auto* Subject = World->SpawnActor<AStaticMeshActor>();
	if (!Subject) return false;
	Subject->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	auto* Mesh = NewObject<USkeletalMeshComponent>(Subject, NAME_None, RF_Transactional);
	Subject->AddInstanceComponent(Mesh); Mesh->SetupAttachment(Subject->GetRootComponent()); Mesh->RegisterComponent();
	const FGuid SubjectBinding = Scene->AddPossessable(TEXT("Component Subject"), Subject->GetClass());
	Sequence->BindPossessableObject(SubjectBinding, *Subject, World);
	TStrongObjectPtr<UComposableCameraTypeAsset> Type(MakeGenericDraftType());
	// NAME_None uses the skeletal component's transform, exercising the socket path
	// without loading a content asset or changing the Actor's root transform.
	CastChecked<UComposableCameraReceivePivotActorNode>(Type->NodeTemplates[0])->bUseBoneForPivot = true;
	FString Error;
	auto* Track = ComposableCameraSystem::CameraInstanceAuthoring::Create(Sequencer, Type.Get(), 0, Error, 3.);
	if (!Track || !ComposableCameraSystem::CameraInstanceAuthoring::SetSubjectBinding(*Track, TEXT("Parameters.Subject"), SubjectBinding)) return false;
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	Sequencer->SetLocalTimeDirectly(FFrameTime(0)); Sequencer->ForceEvaluate();
	UComposableCameraLevelSequenceComponent* Output = nullptr;
	for (const auto Object : Track->OutputBinding.ResolveBoundObjects(Sequencer->GetFocusedTemplateID(), *Sequencer))
		if (auto* Actor = Cast<AActor>(Object.Get())) Output = Actor->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
	if (!TestNotNull(TEXT("Native instance output exists without Shot Editor"), Output)
		|| !TestNotNull(TEXT("Initial evaluator exists"), Output->GetInternalCamera())) return false;
	const TWeakObjectPtr<AComposableCameraCameraBase> InitialEvaluator = Output->GetInternalCamera();
	Mesh->SetRelativeLocation(FVector(100, 0, 0));
	Sequencer->SetLocalTimeDirectly(FFrameTime(1)); Sequencer->ForceEvaluate();
	const double DampedX = Output->OutputCineCameraComponent->GetComponentLocation().X;
	TestTrue(TEXT("Advancing time retains damping history"), DampedX > 0. && DampedX < 100. && Output->GetInternalCamera() == InitialEvaluator.Get());
	Sequencer->ForceEvaluate();
	TestTrue(TEXT("Unchanged repeated sample neither rebuilds nor advances damping"), Output->GetInternalCamera() == InitialEvaluator.Get()
		&& FMath::IsNearlyEqual(Output->OutputCineCameraComponent->GetComponentLocation().X, DampedX));
	const FGuid SceneSignature = Scene->GetSignature();
	const FTransform ActorTransform = Subject->GetActorTransform();
	FProperty* Location = FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeLocationPropertyName());
	if (!Location) return false;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("GenericDraftTest", "ComponentMove", "Move Subject Component"));
		Mesh->Modify(); Mesh->PreEditChange(Location);
		Mesh->SetRelativeLocation(FVector(200, 0, 0));
		FPropertyChangedEvent Event(Location, EPropertyChangeType::ValueSet); Event.MemberProperty = Location;
		Mesh->PostEditChangeProperty(Event);
	}
	TestTrue(TEXT("Component edit changes neither Actor root nor Sequence signature"), Subject->GetActorTransform().Equals(ActorTransform)
		&& Scene->GetSignature() == SceneSignature && !Sequencer->GetHandleToObject(Mesh, false).IsValid());
	Sequencer->ForceEvaluate();
	TestTrue(TEXT("Fixed-time component edit cold-samples the new socket position"), FMath::IsNearlyEqual(Output->OutputCineCameraComponent->GetComponentLocation().X, 200.)
		&& Output->GetInternalCamera() != InitialEvaluator.Get());
	const TWeakObjectPtr<AComposableCameraCameraBase> RefreshedEvaluator = Output->GetInternalCamera();
	Sequencer->ForceEvaluate();
	TestTrue(TEXT("Consumed property invalidation does not repeat"), Output->GetInternalCamera() == RefreshedEvaluator.Get());
	TestTrue(TEXT("Component edit Undo"), GEditor->UndoTransaction()); Sequencer->ForceEvaluate();
	TestTrue(TEXT("Undo refreshes the same-time socket position"), FMath::IsNearlyEqual(Output->OutputCineCameraComponent->GetComponentLocation().X, 100.));
	TestTrue(TEXT("Component edit Redo"), GEditor->RedoTransaction()); Sequencer->ForceEvaluate();
	TestTrue(TEXT("Redo refreshes the same-time socket position"), FMath::IsNearlyEqual(Output->OutputCineCameraComponent->GetComponentLocation().X, 200.));
	// The callback must use currently resolved inputs, not retain an old Subject.
	auto* Replacement = World->SpawnActor<AStaticMeshActor>(); if (!Replacement) return false;
	const FGuid ReplacementBinding = Scene->AddPossessable(TEXT("Replacement Subject"), Replacement->GetClass());
	Sequence->BindPossessableObject(ReplacementBinding, *Replacement, World);
	if (!ComposableCameraSystem::CameraInstanceAuthoring::SetSubjectBinding(*Track, TEXT("Parameters.Subject"), ReplacementBinding)) return false;
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged); Sequencer->ForceEvaluate();
	const TWeakObjectPtr<AComposableCameraCameraBase> ReplacementEvaluator = Output->GetInternalCamera();
	FPropertyChangedEvent OldSubjectEvent(Location, EPropertyChangeType::ValueSet);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Mesh, OldSubjectEvent);
	Sequencer->ForceEvaluate();
	TestTrue(TEXT("An old Subject's component no longer invalidates the camera"), Output->GetInternalCamera() == ReplacementEvaluator.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceGenericDraftTest,
	"ComposableCameraSystem.CameraInstance.GenericDraft", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceGenericDraftTest::RunTest(const FString&)
{
	if (!GEditor || !GEngine || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized()) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL()); World->BeginPlay();
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	Sequence->Initialize();
	auto* Scene = Sequence->GetMovieScene();
	Scene->SetTickResolutionDirectly(FFrameRate(24, 1)); Scene->SetDisplayRate(FFrameRate(24, 1)); Scene->SetPlaybackRange(0, 100);
	FSequencerInitParams Params; Params.RootSequence = Sequence.Get(); Params.PlaybackContext = World;
	Params.ViewParams.UniqueName = TEXT("GenericDraftRegression");
	const auto Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
	const auto PreviousMode = Sequencer->GetAutoChangeMode(); Sequencer->SetAutoChangeMode(EAutoChangeMode::AutoKey);
	ON_SCOPE_EXIT { Sequencer->SetAutoChangeMode(PreviousMode); Sequencer->Close(); };
	auto* Template = World->SpawnActor<AStaticMeshActor>();
	if (!Template) return false;
	const FGuid Subject = Sequencer->MakeNewSpawnable(*Template, nullptr, false); Template->Destroy();
	if (!Subject.IsValid()) return false;
	Scene->SetObjectDisplayName(Subject, FText::FromString(TEXT("Generic Draft Hero")));
	auto* Spawn = Scene->FindTrack<UMovieSceneSpawnTrack>(Subject);
	if (!Spawn) Spawn = Scene->AddTrack<UMovieSceneSpawnTrack>(Subject);
	auto* SpawnSection = CastChecked<UMovieSceneBoolSection>(Spawn->CreateNewSection());
	SpawnSection->SetRange(TRange<FFrameNumber>(0, 100)); SpawnSection->GetChannel().SetDefault(true); Spawn->AddSection(*SpawnSection);
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	Sequencer->SetLocalTimeDirectly(FFrameTime(0)); Sequencer->ForceEvaluate();
	const int32 BindingsBefore = Scene->GetBindings().Num();
	const int32 TracksBefore = Scene->GetTracks().Num();
	TStrongObjectPtr<UComposableCameraTypeAsset> Type(MakeGenericDraftType());
	const auto Root = SNew(SShotEditorRoot).PersistViewState(false);
	const auto Session = Root->AuthoringSession;
	const auto Panel = Root->AuthoringPanel;
	FString Error;
	TestFalse(TEXT("Ordinary offline drafts are outside first scope"), Session->StartTypeDraft(Type.Get(), nullptr, Error));
	// Anchor the shared Create page to this fixture, even when a user's other
	// Sequencer tabs are open. The production Create button then replaces it.
	if (!Session->StartSequenceDraft(Sequencer, Error)) return false;
	Panel->SelectedType = Type.Get();
	Panel->CreateTypeDraft(); Root->Tick(FGeometry(), 0., 0.f); Panel->Tick(FGeometry(), 0., 0.f);
	if (!TestTrue(TEXT("Production Create uses the shared draft session"), Session->IsSequenceDraft() && Session->IsGenericInstance())) return false;
	TestNull(TEXT("Generic draft has no fabricated Shot"), Session->GetShot());
	TestNull(TEXT("Required Actor missing withholds output"), Session->GetOutputCamera());
	TestTrue(TEXT("Shared Edit opens real Actor inputs"), Panel->ActivePage == SShotEditorAuthoringPanel::EPage::Edit && Panel->GenericInputs.Contains(TEXT("Parameters.Subject")));
	const auto InspectInput = [&]() { return ComposableCameraSystem::ShotSubjectBinding::Inspect(*Session, TEXT("Parameters.Subject"), INDEX_NONE, AActor::StaticClass(), true); };
	TestTrue(TEXT("Input diagnostic distinguishes required unbound"), InspectInput().Status == EShotSubjectBindingStatus::RequiredUnbound);
	TestTrue(TEXT("Optional unbound is not an error"), ComposableCameraSystem::ShotSubjectBinding::Inspect(*Session, TEXT("Parameters.Subject"), INDEX_NONE, AActor::StaticClass(), false).Status == EShotSubjectBindingStatus::Unbound);
	const auto Picker = FindGenericWidget<SShotEditorSubjectPicker>(Panel->GenericSubjectRows.ToSharedRef(), TEXT("SShotEditorSubjectPicker"));
	if (!Picker) return false;
	TestTrue(TEXT("Picker lists binding metadata"), GenericMenuHasText(Picker->BuildMenuForTesting(), TEXT("Generic Draft Hero")));
	TestTrue(TEXT("Picker binds real exposed input"), Picker->ChooseBindingForTesting(Subject)); Session->Tick();
	TestTrue(TEXT("Input diagnostic resolves live binding"), InspectInput().Status == EShotSubjectBindingStatus::Resolved && InspectInput().Actor.IsValid());
	TestTrue(TEXT("Input diagnostic catches wrong actor class"), ComposableCameraSystem::ShotSubjectBinding::Inspect(*Session, TEXT("Parameters.Subject"), INDEX_NONE,
		AComposableCameraLevelSequenceActor::StaticClass(), true).Status == EShotSubjectBindingStatus::ClassMismatch);
	const auto SavedBinding = Session->GetInstanceValues()->Subjects[0].Binding;
	Session->GetInstanceValues()->Subjects[0].Binding = UE::MovieScene::FFixedObjectBindingID(Subject, FMovieSceneSequenceID(123456));
	TestTrue(TEXT("Foreign Sequence ID never aliases a same-GUID local binding"), InspectInput().Status == EShotSubjectBindingStatus::SequenceUnavailable && !InspectInput().bCanLocateBinding && !InspectInput().Actor.IsValid());
	Session->GetInstanceValues()->Subjects[0].Binding = UE::MovieScene::FRelativeObjectBindingID(FGuid::NewGuid());
	TestTrue(TEXT("Missing binding is distinct from inactive spawnable"), InspectInput().Status == EShotSubjectBindingStatus::MissingBinding && !InspectInput().bCanLocateBinding);
	const FGuid Unresolved = Scene->AddPossessable(TEXT("Unavailable Actor"), AActor::StaticClass());
	Session->GetInstanceValues()->Subjects[0].Binding = UE::MovieScene::FRelativeObjectBindingID(Unresolved);
	TestTrue(TEXT("Unresolved Possessable is not mislabeled unspawned"), InspectInput().Status == EShotSubjectBindingStatus::Unresolved && InspectInput().bCanLocateBinding);
	UMovieSceneSequence* BindingSequence = Sequence.Get();
	auto* References = BindingSequence->GetBindingReferences();
	if (!TestNotNull(TEXT("Diagnostic fixture has binding references"), References)) return false;
	auto* CustomBinding = NewObject<UMovieSceneSpawnableActorBinding>(Scene);
	CustomBinding->SetObjectTemplate(NewObject<AActor>(Scene, NAME_None, RF_ArchetypeObject | RF_Transient));
	References->AddBinding(Unresolved, CustomBinding);
	TestTrue(TEXT("UE5.6 custom Spawnable is not mistaken for a missing Possessable"), InspectInput().Status == EShotSubjectBindingStatus::Unspawned);
	References->RemoveBinding(Unresolved);
	Session->GetInstanceValues()->Subjects[0].Binding = SavedBinding; Scene->RemovePossessable(Unresolved);
	if (!TestNotNull(TEXT("Draft evaluates before insertion"), Session->GetOutputCamera())) return false;
	const TWeakObjectPtr<AActor> TemporaryCamera = Session->GetOutputCamera()->GetOwner();
	TestTrue(TEXT("Preview camera is transient"), TemporaryCamera->HasAnyFlags(RF_Transient));
	TestEqual(TEXT("Real Type default reaches preview"), Session->GetOutputCamera()->CurrentFocalLength, 65.f);
	Session->BeginEdit(NSLOCTEXT("GenericDraftTest", "DraftLens", "Change Draft Lens"));
	DraftField(*Session->GetInstanceValues(), TEXT("Parameters.LensLength")).WriteNumber(85);
	Session->Changed(); Session->EndEdit(); Session->Tick();
	TestEqual(TEXT("Draft parameter edits preview immediately"), Session->GetOutputCamera()->CurrentFocalLength, 85.f);
	TestTrue(TEXT("Modified draft is guarded against discard"), Session->HasModifiedDraft());
	TestEqual(TEXT("Draft creates no camera binding even with Auto Key"), Scene->GetBindings().Num(), BindingsBefore);
	TestEqual(TEXT("Draft creates no root track"), Scene->GetTracks().Num(), TracksBefore);
	TestNull(TEXT("Draft creates no Camera Cuts"), Scene->GetCameraCutTrack());
	TestTrue(TEXT("Clear uses the same binding path"), Session->SetInputBinding(TEXT("Parameters.Subject"), FGuid(), Error)); Session->Tick();
	TestNull(TEXT("Clear withholds required-input output"), Session->GetOutputCamera());
	TestTrue(TEXT("Binding edit is undoable"), GEditor->UndoTransaction()); Session->Tick();
	TestTrue(TEXT("Undo restores identity"), Session->GetInputBinding(TEXT("Parameters.Subject")).GetGuid() == Subject);
	TestTrue(TEXT("Binding clear can be redone"), GEditor->RedoTransaction()); Session->Tick();
	TestFalse(TEXT("Redo restores empty binding"), Session->GetInputBinding(TEXT("Parameters.Subject")).IsValid());
	TestTrue(TEXT("Undo restores the bound preview again"), GEditor->UndoTransaction()); Session->Tick();
	const TWeakObjectPtr<AActor> PreviousActor = Session->ResolveInputActor(TEXT("Parameters.Subject"));
	const SWidget* EditPage = Panel->GetEditPageForTesting();
	Sequencer->SetLocalTimeDirectly(FFrameTime(110)); Sequencer->ForceEvaluate(); Session->Tick(); Panel->Tick(FGeometry(), 0., 0.f);
	TestNull(TEXT("Inactive Spawnable withholds required output"), Session->GetOutputCamera());
	TestTrue(TEXT("Inactive spawnable has a specific diagnosis"), InspectInput().Status == EShotSubjectBindingStatus::Unspawned && !InspectInput().Actor.IsValid());
	Scene->SetReadOnly(true);
	TestTrue(TEXT("Inactive spawnable can be located in Sequencer"), ComposableCameraSystem::ShotSubjectBinding::Locate(*Session, TEXT("Parameters.Subject"), INDEX_NONE, AActor::StaticClass(), true, false, Error));
	Scene->SetReadOnly(false);
	TArray<FGuid> SelectedBindings; Sequencer->GetSelectedObjects(SelectedBindings);
	TestTrue(TEXT("Locate selects saved binding"), SelectedBindings.Contains(Subject));
	TestEqual(TEXT("Locate does not seek or force spawn"), Sequencer->GetLocalTime().Time.FloorToFrame().Value, 110);
	TestTrue(TEXT("Locate never changes the authored input ID"), Session->GetInputBinding(TEXT("Parameters.Subject")) == SavedBinding);
	TestFalse(TEXT("Inactive Actor navigation is rejected"), ComposableCameraSystem::ShotSubjectBinding::Locate(*Session, TEXT("Parameters.Subject"), INDEX_NONE, AActor::StaticClass(), true, true, Error));
	TestTrue(TEXT("Inactive Spawnable remains selectable"), GenericMenuHasText(Picker->BuildMenuForTesting(), TEXT("Generic Draft Hero")));
	Sequencer->SetLocalTimeDirectly(FFrameTime(24)); Sequencer->ForceEvaluate(); Session->Tick();
	TestNotNull(TEXT("Respawn resumes preview"), Session->GetOutputCamera());
	TestTrue(TEXT("Diagnostic recovers after respawn"), InspectInput().Status == EShotSubjectBindingStatus::Resolved && InspectInput().Actor.Get() == Session->ResolveInputActor(TEXT("Parameters.Subject")));
	TestTrue(TEXT("Respawn resolves a fresh Actor"), !PreviousActor.IsValid() || PreviousActor.Get() != Session->ResolveInputActor(TEXT("Parameters.Subject")));
	TestTrue(TEXT("Seeking retains shared Edit widgets"), EditPage == Panel->GetEditPageForTesting());
	const auto ActorField = DraftField(*Session->GetInstanceValues(), TEXT("Parameters.Subject"));
	TestNull(TEXT("Resolved Actor is never stored in draft"), CastFieldChecked<FObjectPropertyBase>(ActorField.Property)->GetObjectPropertyValue(ActorField.Value));
	const auto Drag = FComposableCameraInstanceDragDropOp::New(Session.ToSharedRef(), 2., Error);
	if (!Drag) return false;
	auto* Track = Drag->Drop(Sequencer, FFrameNumber(24));
	if (!TestNotNull(TEXT("Shared drop creates a camera instance"), Track)) return false;
	TestTrue(TEXT("Insertion transfers to sampled editing"), !Session->IsSequenceDraft() && Session->GetInstanceTrack() == Track);
	TestFalse(TEXT("Insertion destroys temporary output"), TemporaryCamera.IsValid());
	TestEqual(TEXT("Exactly one native top-level binding"), Scene->GetBindings().Num(), BindingsBefore + 1);
	TestEqual(TEXT("Source node default is unchanged"), CastChecked<UComposableCameraLensNode>(Type->NodeTemplates[3])->FocalLength, 65.f);
	TestTrue(TEXT("Input bindings transfer without reinterpreting identity"), Track->Instance->Subjects[0].Binding.GetGuid() == Subject);
	if (auto* Actor = Session->ResolveInputActor(TEXT("Parameters.Subject")))
	{
		Actor->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		Actor->SetActorLocation(FVector(200, 0, 0)); Sequencer->ForceEvaluate(); Session->Tick();
		if (auto* Camera = Session->GetOutputCamera())
		{
			TestTrue(TEXT("Bound same-time Actor changes refresh preview"), FMath::IsNearlyEqual(Camera->GetComponentLocation().X, 200.));
			auto* Component = Camera->GetOwner()->FindComponentByClass<UComposableCameraLevelSequenceComponent>();
			const TWeakObjectPtr<AComposableCameraCameraBase> Evaluator = Component->GetInternalCamera();
			Sequencer->ForceEvaluate();
			TestTrue(TEXT("Unchanged repeated time retains evaluator"), Component->GetInternalCamera() == Evaluator.Get());
		}
		else AddError(TEXT("Bound preview must recover after moving the Actor."));
	}
	else AddError(TEXT("Inserted instance lost its Subject."));
	TestFalse(TEXT("A draft menu cannot write into the inserted instance"), Picker->ChooseBindingForTesting(FGuid()));
	Session->BeginEdit(NSLOCTEXT("GenericDraftTest", "InstanceLens", "Key Instance Lens"));
	DraftField(*Session->GetInstanceValues(), TEXT("Parameters.LensLength")).WriteNumber(95);
	Session->Changed(); Session->EndEdit();
	auto* Lane = ComposableCameraSystem::CameraInstanceAuthoring::FindParameterTrack(*Track, TEXT("Parameters.LensLength"));
	if (!TestNotNull(TEXT("Inserted generic uses existing Auto Key lane"), Lane)) return false;
	TestTrue(TEXT("Parameter lane shares the instance binding"), Lane->FindObjectBindingGuid() == Track->FindObjectBindingGuid());
	TestTrue(TEXT("First parameter section is infinite"), Lane->GetAllSections()[0]->GetRange() == TRange<FFrameNumber>::All());
	TestTrue(TEXT("Activation stays finite"), Track->GetAllSections()[0]->GetRange() == TRange<FFrameNumber>(24, 72));
	auto& NewInput = Type->ExposedVariables.AddDefaulted_GetRef();
	NewInput.VariableName = TEXT("Gain"); NewInput.VariableType = EComposableCameraPinType::Float; NewInput.InitialValueString = TEXT("7");
	auto& OtherInput = Type->ExposedVariables.AddDefaulted_GetRef();
	OtherInput.VariableName = TEXT("Blend"); OtherInput.VariableType = EComposableCameraPinType::Float; OtherInput.InitialValueString = TEXT("9");
	Type->Build(false); Session->Tick();
	TestEqual(TEXT("Bound editor receives additive source defaults"), DraftField(*Session->GetInstanceValues(), TEXT("Variables.Gain")).ReadNumber(), 7.);
	Session->BeginEdit(NSLOCTEXT("GenericDraftTest", "NewInput", "Key Added Input"));
	DraftField(*Session->GetInstanceValues(), TEXT("Variables.Gain")).WriteNumber(14);
	DraftField(*Session->GetInstanceValues(), TEXT("Variables.Blend")).WriteNumber(18);
	Session->Changed(); Session->EndEdit();
	TestNotNull(TEXT("Added source input can create its first native lane"), ComposableCameraSystem::CameraInstanceAuthoring::FindParameterTrack(*Track, TEXT("Variables.Gain")));
	TestEqual(TEXT("Auto Key retains the new authored default"), DraftField(*Track->Instance, TEXT("Variables.Gain")).ReadNumber(), 7.);
	TestNotNull(TEXT("Same edit can add a second lane without invalidating defaults"), ComposableCameraSystem::CameraInstanceAuthoring::FindParameterTrack(*Track, TEXT("Variables.Blend")));
	TestEqual(TEXT("Second new default survives the multi-field key"), DraftField(*Track->Instance, TEXT("Variables.Blend")).ReadNumber(), 9.);
	Type->ExposedParameters[0].ParameterName = TEXT("RenamedSubject"); Type->Build(false);
	TestTrue(TEXT("Stale interface diagnosis precedes Actor resolution"), InspectInput().Status == EShotSubjectBindingStatus::InterfaceChanged);
	return true;
}
#endif
