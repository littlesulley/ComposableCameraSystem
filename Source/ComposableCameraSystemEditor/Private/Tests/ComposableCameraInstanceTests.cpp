// Copyright 2026 Sulley. All Rights Reserved.
#include "Channels/MovieSceneChannelProxy.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "ISequencerTrackEditor.h"
#include "LevelSequence.h"
#include "LevelSequence/ComposableCameraLevelSequenceActor.h"
#include "LevelSequence/ComposableCameraLevelSequenceShotActor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene/MovieSceneComposableCameraShotTrack.h"
#include "Nodes/ComposableCameraLensNode.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FCameraInstanceFixture
{
	TStrongObjectPtr<ULevelSequence> Sequence{NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional)};
	UMovieSceneComposableCameraInstanceTrack* Track;
	UMovieSceneComposableCameraInstanceSection* Section;
	FCameraInstanceFixture()
	{
		Sequence->Initialize();
		Track = Sequence->GetMovieScene()->AddTrack<UMovieSceneComposableCameraInstanceTrack>();
		FComposableCameraShot Shot;
		Shot.Targets.SetNum(2);
		Track->Instance->InitializeFromShot(Shot);
		Section = CastChecked<UMovieSceneComposableCameraInstanceSection>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>(0, 100));
		Track->AddSection(*Section);
	}
};

FComposableCameraInstanceField* FindField(TArray<FComposableCameraInstanceField>& Fields, FName Path)
{
	return Fields.FindByPredicate([Path](const auto& Field) { return Field.Path == Path; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceChannelsTest,
	"ComposableCameraSystem.CameraInstance.NativeChannels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceChannelsTest::RunTest(const FString&)
{
	FCameraInstanceFixture Fixture;
	TStrongObjectPtr<UComposableCameraInstanceData> Sample(NewObject<UComposableCameraInstanceData>());
	Sample->CopyConfiguration(*Fixture.Track->Instance);
	TArray<FComposableCameraInstanceField> Fields;
	Sample->GatherFields(Fields);
	auto* FOV = FindField(Fields, TEXT("Shot.Lens.ManualFOV"));
	auto* Mode = FindField(Fields, TEXT("Shot.Lens.FOVMode"));
	if (!TestNotNull(TEXT("FOV is animatable"), FOV) || !TestNotNull(TEXT("Enum is animatable"), Mode)) return false;
	FOV->WriteNumber(40);
	TestTrue(TEXT("First native numeric key"), Fixture.Section->AddKey(*FOV, 0));
	FOV->WriteNumber(80);
	Fixture.Section->AddKey(*FOV, 10);
	for (auto& Key : Fixture.Section->Channels[0].Number.GetData().GetValues()) Key.InterpMode = RCIM_Linear;
	Mode->WriteNumber(0);
	Fixture.Section->AddKey(*Mode, 0);
	Mode->WriteNumber(1);
	Fixture.Section->AddKey(*Mode, 10);
	Fixture.Section->Evaluate(FFrameTime(FFrameNumber(5), .5f), Fields);
	TestEqual(TEXT("Subframe sampling retains fractional time"), FOV->ReadNumber(), 62.0);
	TestEqual(TEXT("Enum interpolation is discrete"), Mode->ReadNumber(), 0.0);
	TestEqual(TEXT("Sampling never overwrites instance defaults"), Fixture.Track->Instance->Shot.Lens.ManualFOV, 79.f);
	Fixture.Section->MoveSection(100);
	Fixture.Section->Evaluate(FFrameTime(FFrameNumber(105), .5f), Fields);
	TestEqual(TEXT("Native section movement carries keys with instance"), FOV->ReadNumber(), 62.0);
	TestEqual(TEXT("Numeric channel is exposed to Sequencer/curve editor"), Fixture.Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>().Num(), 1);
	TestEqual(TEXT("Enum uses native integer channel"), Fixture.Section->GetChannelProxy().GetChannels<FMovieSceneIntegerChannel>().Num(), 1);
	const FName BoolPath(*(TEXT("Shot.Targets.") + Sample->Shot.Targets[0].SubjectId.ToString(EGuidFormats::Digits) + TEXT(".Target.bOffsetInLocalSpace")));
	const auto* Boolean = FindField(Fields, BoolPath);
	if (!TestNotNull(TEXT("Boolean input can be keyed"), Boolean)) return false;
	Boolean->WriteNumber(0);
	Fixture.Section->AddKey(*Boolean, 100);
	Boolean->WriteNumber(1);
	Fixture.Section->AddKey(*Boolean, 110);
	Fixture.Section->Evaluate(FFrameTime(109), Fields);
	TestEqual(TEXT("Bool remains discrete between keys"), Boolean->ReadNumber(), 0.0);
	Fixture.Section->Evaluate(FFrameTime(110), Fields);
	TestEqual(TEXT("Bool changes at the authored key"), Boolean->ReadNumber(), 1.0);
	Fixture.Section->SetIsLocked(true);
	TestFalse(TEXT("Locked sections reject authoring"), Fixture.Section->AddKey(*FOV, 110));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceSubjectIdentityTest,
	"ComposableCameraSystem.CameraInstance.SubjectIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceSubjectIdentityTest::RunTest(const FString&)
{
	FCameraInstanceFixture Fixture;
	auto* Data = Fixture.Track->Instance.Get();
	const FGuid Subject = Data->Shot.Targets[0].SubjectId;
	const FString Prefix = TEXT("Shot.Targets.") + Subject.ToString(EGuidFormats::Digits);
	const FName OffsetPath(*(Prefix + TEXT(".Target.Offset.X")));
	const FName ActorPath(*(Prefix + TEXT(".Target.Actor")));
	TArray<FComposableCameraInstanceField> Fields;
	Data->GatherFields(Fields);
	auto* Offset = FindField(Fields, OffsetPath);
	if (!TestNotNull(TEXT("Vector component can be keyed"), Offset)) return false;
	Offset->WriteNumber(123);
	Fixture.Section->AddKey(*Offset, 5);
	Data->Shot.Targets[0].Target.Offset.X = 0;
	Data->Shot.Targets.Swap(0, 1);
	Data->GatherFields(Fields); // Explicit layout invalidation after authoring reorder.
	Fixture.Section->Evaluate(FFrameTime(5), Fields);
	TestEqual(TEXT("Reordered subject retains its own curve"), Data->Shot.Targets[1].Target.Offset.X, 123.0);
	TestEqual(TEXT("Other subject does not inherit the curve"), Data->Shot.Targets[0].Target.Offset.X, 0.0);
	UMovieScene* Scene = Fixture.Sequence->GetMovieScene();
	TStrongObjectPtr<AActor> Template(NewObject<AActor>(Scene, NAME_None, RF_Transient | RF_ArchetypeObject));
	const FGuid Binding = Scene->AddSpawnable(TEXT("Inactive Hero"), *Template.Get());
	TestTrue(TEXT("Binding assignment needs no live spawned actor"), ComposableCameraSystem::CameraInstanceAuthoring::SetSubjectBinding(*Fixture.Track, ActorPath, Binding));
	const auto* Slot = Data->Subjects.FindByPredicate([&](const auto& Value) { return Value.Input == ActorPath; });
	TestTrue(TEXT("Sequence identity is saved on instance"), Slot && Slot->Binding.GetGuid() == Binding);
	TestTrue(TEXT("Actor templates are never written into instance config"), Data->Shot.Targets[1].Target.Actor.IsNull());
	Scene->SetReadOnly(true);
	TestFalse(TEXT("Read-only sequences reject binding changes"), ComposableCameraSystem::CameraInstanceAuthoring::SetSubjectBinding(*Fixture.Track, ActorPath, FGuid()));
	Scene->SetReadOnly(false);
	Data->Shot.Targets.Add(Data->Shot.Targets[1]);
	Data->EnsureSubjectIdentities();
	TestTrue(TEXT("Duplicating a subject allocates a new identity"), Data->Shot.Targets[2].SubjectId != Subject);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceDuplicateTest,
	"ComposableCameraSystem.CameraInstance.DuplicateOwnsData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceDuplicateTest::RunTest(const FString&)
{
	FCameraInstanceFixture Fixture;
	TArray<FComposableCameraInstanceField> Fields;
	Fixture.Track->Instance->GatherFields(Fields);
	const auto* Field = FindField(Fields, TEXT("Shot.Lens.ManualFOV"));
	if (!Field) return false;
	Fixture.Section->AddKey(*Field, 0);
	auto* Copy = DuplicateObject<UMovieSceneComposableCameraInstanceTrack>(Fixture.Track, Fixture.Sequence->GetMovieScene());
	Fixture.Sequence->GetMovieScene()->AddGivenTrack(Copy);
	TestTrue(TEXT("Track duplicate owns a distinct configuration object"), Copy->Instance != Fixture.Track->Instance);
	TestTrue(TEXT("Built-in composition type duplicates with instance"), Copy->Instance->Camera.TypeAsset != Fixture.Track->Instance->Camera.TypeAsset);
	auto* CopySection = CastChecked<UMovieSceneComposableCameraInstanceSection>(Copy->GetAllSections()[0]);
	TestEqual(TEXT("Duplicated channel proxy rebuilds from copied storage"), CopySection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>().Num(), 1);
	Copy->Instance->Shot.Lens.ManualFOV = 110;
	Copy->Instance->GatherFields(Fields);
	CopySection->AddKey(*FindField(Fields, TEXT("Shot.Lens.ManualFOV")), 0);
	double Original = 0;
	Fixture.Section->Channels[0].Number.Evaluate(FFrameTime(0), Original);
	TestEqual(TEXT("Original keys remain independent"), Original, 79.0);
	TStrongObjectPtr<UComposableCameraInstanceData> Draft(NewObject<UComposableCameraInstanceData>());
	Draft->CopyForAuthoring(*Fixture.Track->Instance);
	TestTrue(TEXT("Draft insertion owns its built-in type"), Draft->Camera.TypeAsset->GetOuter() == Draft.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceGenericDefaultsTest,
	"ComposableCameraSystem.CameraInstance.GenericDefaultsAndInputs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceGenericDefaultsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UComposableCameraTypeAsset> Type(NewObject<UComposableCameraTypeAsset>());
	auto* Lens = NewObject<UComposableCameraLensNode>(Type.Get());
	Lens->FocalLength = 65.f;
	Type->NodeTemplates.Add(Lens);
	Type->NodePinOverrides.AddDefaulted();
	auto& Parameter = Type->ExposedParameters.AddDefaulted_GetRef();
	Parameter.ParameterName = TEXT("LensLength");
	Parameter.PinType = EComposableCameraPinType::Float;
	Parameter.TargetNodeIndex = 0;
	Parameter.TargetPinName = TEXT("FocalLength");
	auto& Variable = Type->ExposedVariables.AddDefaulted_GetRef();
	Variable.VariableName = TEXT("Strength");
	Variable.VariableType = EComposableCameraPinType::Float;
	Variable.InitialValueString = TEXT("12.5");
	TStrongObjectPtr<UComposableCameraInstanceData> Data(NewObject<UComposableCameraInstanceData>());
	Data->InitializeFromType(Type.Get());
	TArray<FComposableCameraInstanceField> Fields;
	Data->GatherFields(Fields);
	const auto* FocalLength = FindField(Fields, TEXT("Parameters.LensLength"));
	const auto* Strength = FindField(Fields, TEXT("Variables.Strength"));
	if (!TestNotNull(TEXT("Generic parameter is reflected"), FocalLength) || !TestNotNull(TEXT("Generic variable is reflected"), Strength)) return false;
	TestEqual(TEXT("Type input default is copied, not zero initialized"), FocalLength->ReadNumber(), 65.0);
	TestEqual(TEXT("Exposed variable default is copied"), Strength->ReadNumber(), 12.5);
	FocalLength->WriteNumber(85);
	TestEqual(TEXT("Instance override leaves source node untouched"), Lens->FocalLength, 65.f);
	FComposableCameraRuntimeDataBlock Block;
	Block.Storage.SetNumZeroed(2 * sizeof(float));
	Block.ExposedParameterOffsets.Add(TEXT("LensLength"), 0);
	Block.InternalVariableOffsets.Add(TEXT("Strength"), sizeof(float));
	FComposableCameraRuntimeDataBlock::FSlotShape Shape;
	Shape.PinType = EComposableCameraPinType::Float;
	Shape.Size = sizeof(float);
	Block.SlotShapes.Add(0, Shape);
	Block.SlotShapes.Add(sizeof(float), Shape);
	Data->ApplyInputs(Block);
	TestEqual(TEXT("Parameter reaches runtime data block"), Block.ReadValue<float>(0), 85.f);
	TestEqual(TEXT("Variable reaches runtime data block"), Block.ReadValue<float>(sizeof(float)), 12.5f);
	Block.SlotShapes[0].PinType = EComposableCameraPinType::Int32;
	FocalLength->WriteNumber(95);
	Data->ApplyInputs(Block);
	Block.SlotShapes[0].PinType = EComposableCameraPinType::Float;
	TestEqual(TEXT("Stale input type cannot corrupt a different runtime slot"), Block.ReadValue<float>(0), 85.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraInstanceAssetDropOwnershipTest,
	"ComposableCameraSystem.CameraInstance.AssetDropOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraInstanceAssetDropOwnershipTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->IsTransactionActive() || !FSlateApplication::IsInitialized())
	{
		AddError(TEXT("Run in an initialized editor outside an active transaction."));
		return false;
	}
	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	const FGuid Possessed = Scene->AddPossessable(TEXT("Existing LS Camera"), AComposableCameraLevelSequenceActor::StaticClass());
	const FGuid ShotActor = Scene->AddPossessable(TEXT("Existing Shot Camera"), AComposableCameraLevelSequenceShotActor::StaticClass());
	TStrongObjectPtr<AComposableCameraLevelSequenceActor> Template(NewObject<AComposableCameraLevelSequenceActor>(Scene, NAME_None, RF_Transient | RF_ArchetypeObject));
	const FGuid Spawned = Scene->AddSpawnable(TEXT("Inactive LS Camera"), *Template.Get());
	// The fixture deliberately has no playback world. Keep this binding inactive
	// so Sequencer's default null spawn register is never asked to spawn it.
	auto* SpawnTrack = Scene->AddTrack<UMovieSceneSpawnTrack>(Spawned);
	SpawnTrack->SetObjectId(Spawned);
	auto* SpawnSection = CastChecked<UMovieSceneSpawnSection>(SpawnTrack->CreateNewSection());
	SpawnSection->SetRange(TRange<FFrameNumber>::All());
	SpawnSection->GetChannel().SetDefault(false);
	SpawnTrack->AddSection(*SpawnSection);
	auto* LegacyTrack = Scene->AddTrack<UMovieSceneComposableCameraShotTrack>(Possessed);
	auto* ExistingSection = CastChecked<UMovieSceneComposableCameraShotSection>(LegacyTrack->CreateNewSection());
	ExistingSection->InlineShot.Lens.ManualFOV = 57.f;
	LegacyTrack->AddSection(*ExistingSection);
	TStrongObjectPtr<UComposableCameraShotAsset> Preset(NewObject<UComposableCameraShotAsset>());
	TStrongObjectPtr<UComposableCameraTypeAsset> Type(NewObject<UComposableCameraTypeAsset>());
	TStrongObjectPtr<UComposableCameraPatchTypeAsset> Patch(NewObject<UComposableCameraPatchTypeAsset>());
	FSequencerInitParams Params;
	Params.RootSequence = Sequence.Get();
	Params.ViewParams.UniqueName = TEXT("CameraInstanceAssetDropRegression");
	const auto Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(Params);
	ON_SCOPE_EXIT { Sequencer->Close(); };
	const auto LegacyEditor = Sequencer->GetTrackEditor(LegacyTrack);
	TStrongObjectPtr<UMovieSceneComposableCameraInstanceTrack> InstanceType(NewObject<UMovieSceneComposableCameraInstanceTrack>(Scene));
	const auto InstanceEditor = Sequencer->GetTrackEditor(InstanceType.Get());
	if (!TestTrue(TEXT("Both current and legacy track editors remain registered"), LegacyEditor.IsValid() && InstanceEditor.IsValid())) return false;

	// Invoke the real registered legacy handler on an editable Sequence. Before the
	// fix it returned true for LSActor targets and appended/created a legacy Section,
	// becoming a second consumer after the Instance editor in Sequencer's dispatch.
	const FGuid Targets[] = { FGuid(), Possessed, ShotActor, Spawned };
	const int32 BindingCount = Scene->GetBindings().Num();
	const int32 RootTrackCount = Scene->GetTracks().Num();
	for (const FGuid Target : Targets)
	{
		TestFalse(TEXT("Legacy editor never consumes a ShotAsset drop"), LegacyEditor->HandleAssetAdded(Preset.Get(), Target));
		TestFalse(TEXT("Legacy editor never consumes a CameraType drop"), LegacyEditor->HandleAssetAdded(Type.Get(), Target));
		TestFalse(TEXT("Legacy editor leaves Patch drops to Patch editor"), LegacyEditor->HandleAssetAdded(Patch.Get(), Target));
		TestFalse(TEXT("Instance editor also leaves Patch subclasses to Patch editor"), InstanceEditor->HandleAssetAdded(Patch.Get(), Target));
	}
	TestEqual(TEXT("Drops leave existing legacy Sections intact"), LegacyTrack->GetAllSections().Num(), 1);
	TestEqual(TEXT("Legacy configuration stays editable and unchanged"), ExistingSection->InlineShot.Lens.ManualFOV, 57.f);
	TestNull(TEXT("Drop does not create a legacy Track on another Possessable"), Scene->FindTrack<UMovieSceneComposableCameraShotTrack>(ShotActor));
	TestNull(TEXT("Drop does not create a legacy Track on an inactive Spawnable"), Scene->FindTrack<UMovieSceneComposableCameraShotTrack>(Spawned));
	TestEqual(TEXT("Declined handlers do not create bindings"), Scene->GetBindings().Num(), BindingCount);
	TestEqual(TEXT("Declined handlers do not create root tracks"), Scene->GetTracks().Num(), RootTrackCount);
	return true;
}
#endif
