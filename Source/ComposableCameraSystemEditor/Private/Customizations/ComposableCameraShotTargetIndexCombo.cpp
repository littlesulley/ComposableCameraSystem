// Copyright 2026 Sulley. All Rights Reserved.

#include "Customizations/ComposableCameraShotTargetIndexCombo.h"
#include "Editors/ComposableCameraShotSubjectLabel.h"
#include "ComposableCameraSystemEditorModule.h"

#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraShotTarget.h"
#include "DataAssets/ComposableCameraTargetInfo.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"

#include "Engine/SkeletalMesh.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "ISequencer.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShotTargetIndexCombo"

namespace
{
UMovieSceneComposableCameraShotSection* ResolveLabelSection(const TSharedPtr<IPropertyHandle>& Item,
	TWeakObjectPtr<UObject> Host)
{
	if (Host.IsValid()) return Cast<UMovieSceneComposableCameraShotSection>(Host.Get());
	TArray<UObject*> Outers;
	Item->GetOuterObjects(Outers);
	// Struct-only generators receive an explicit host. Never borrow another open
	// Shot Editor's global host, and never choose one arbitrary multi-edit source.
	return Outers.Num() == 1 ? Cast<UMovieSceneComposableCameraShotSection>(Outers[0]) : nullptr;
}

UMovieScene* ResolveBindingScene(UMovieSceneComposableCameraShotSection& Section, const FMovieSceneObjectBindingID& Binding)
{
	UMovieSceneSequence* Source = Section.GetTypedOuter<UMovieSceneSequence>();
	if (!Source) return nullptr;
	// A local binding's authored name survives closing Sequencer or despawning
	// its actor. Cross-sequence names must come from their actual sequence.
	if (Binding == FMovieSceneObjectBindingID(UE::MovieScene::FRelativeObjectBindingID(Binding.GetGuid())))
		return Source->GetMovieScene();
	const auto* Module = FModuleManager::GetModulePtr<FComposableCameraSystemEditorModule>(TEXT("ComposableCameraSystemEditor"));
	const auto Sequencer = Module ? Module->FindOpenSequencerForSequence(Source) : nullptr;
	if (!Sequencer) return nullptr;
	const FMovieSceneSequenceID SourceID = Sequencer->GetFocusedMovieSceneSequence() == Source
		? Sequencer->GetFocusedTemplateID() : MovieSceneSequenceID::Root;
	UMovieSceneSequence* BoundSequence = Sequencer->GetEvaluationTemplate().GetSequence(Binding.ResolveSequenceID(SourceID, *Sequencer));
	return BoundSequence ? BoundSequence->GetMovieScene() : nullptr;
}
}

TSharedPtr<IPropertyHandleArray> FShotTargetIndexCombo::ResolveTargetsArrayUpwards(const TSharedPtr<IPropertyHandle>& Start)
{
	// Walk parent chain. At each level, attempt to resolve a child handle
	// named "Targets" -> if it's an array handle, that's our shot. Walking
	// upward (rather than searching siblings) handles arbitrary embedding
	// depth: Shot.Placement.BasisActorIndex (depth 1), Shot.Aim.AimAnchor.TargetIndex
	// (depth 2), Shot.Placement.PlacementAnchor.WeightedTargets[i].TargetIndex
	// (depth 3 across an array element).
	TSharedPtr<IPropertyHandle> Cur = Start;
	while (Cur.IsValid())
	{
		TSharedPtr<IPropertyHandle> Maybe = Cur->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Targets));
		if (Maybe.IsValid())
		{
			TSharedPtr<IPropertyHandleArray> AsArr = Maybe->AsArray();
			if (AsArr.IsValid())
			{
				return AsArr;
			}
		}
		Cur = Cur->GetParentHandle();
	}
	return nullptr;
}

FText FShotTargetIndexCombo::FormatTargetEntryLabel(const TSharedPtr<IPropertyHandleArray>& TargetsArray, int32 Idx,
	TWeakObjectPtr<UObject> Host)
{
	if (!TargetsArray.IsValid())
	{
		return FText::Format(LOCTEXT("EntryUnreachable", "{0} - (unreachable)"),
			FText::AsNumber(Idx));
	}
	uint32 Num = 0;
	TargetsArray->GetNumElements(Num);
	if (Idx < 0 || static_cast<uint32>(Idx) >= Num)
	{
		return FText::Format(LOCTEXT("EntryInvalid", "{0} - (invalid)"),
			FText::AsNumber(Idx));
	}

	TSharedPtr<IPropertyHandle> ItemHandle = TargetsArray->GetElement(Idx);
	if (!ItemHandle.IsValid())
	{
		return FText::Format(LOCTEXT("EntryNullSlot", "{0} - (null slot)"),
			FText::AsNumber(Idx));
	}
	TSharedPtr<IPropertyHandle> InfoHandle = ItemHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraShotTarget, Target));
	TSharedPtr<IPropertyHandle> ActorHandle = InfoHandle.IsValid()
		? InfoHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, Actor))
		: nullptr;

	FText Identity = LOCTEXT("UnsetIdentity", "unset");
	bool bHasBinding = false;
	if (UMovieSceneComposableCameraShotSection* Section = ResolveLabelSection(ItemHandle, Host))
	{
		for (const auto& Override : Section->TargetActorOverrides)
		{
			if (Override.TargetIndex != Idx || !Override.Binding.IsValid()) continue;
			bHasBinding = true;
			Identity = LOCTEXT("UnresolvedBinding", "unresolved binding");
			if (UMovieScene* Scene = ResolveBindingScene(*Section, Override.Binding); Scene && Scene->FindBinding(Override.Binding.GetGuid()))
				Identity = Scene->GetObjectDisplayName(Override.Binding.GetGuid());
			break;
		}
	}
	FString ActorLabel;
	if (!bHasBinding && ActorHandle.IsValid())
	{
		void* RawPtr = nullptr;
		if (ActorHandle->GetValueData(RawPtr) == FPropertyAccess::Success && RawPtr)
		{
			const TSoftObjectPtr<AActor>* SoftPtr = static_cast<const TSoftObjectPtr<AActor>*>(RawPtr);
			if (SoftPtr)
			{
				if (AActor* Live = SoftPtr->Get())
				{
					ActorLabel = Live->GetActorLabel();
				}
				else
				{
					const FSoftObjectPath& Path = SoftPtr->ToSoftObjectPath();
					if (Path.IsValid())
					{
						const FString PathStr = Path.ToString();
						int32 LastDot = INDEX_NONE;
						if (PathStr.FindLastChar('.', LastDot) && LastDot < PathStr.Len() - 1)
						{
							ActorLabel = PathStr.Mid(LastDot + 1);
						}
						else
						{
							ActorLabel = FPaths::GetBaseFilename(PathStr);
						}
					}
				}
			}
		}
	}
	if (!bHasBinding && ActorLabel.IsEmpty() && InfoHandle.IsValid())
	{
#if WITH_EDITORONLY_DATA
		// Preview-only subjects also need a useful name before any actor is bound.
		const auto MeshHandle = InfoHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, EditorPreviewMesh));
		void* RawMesh = nullptr;
		if (MeshHandle && MeshHandle->GetValueData(RawMesh) == FPropertyAccess::Success && RawMesh)
		{
			const auto& Mesh = *static_cast<const TSoftObjectPtr<USkeletalMesh>*>(RawMesh);
			ActorLabel = Mesh.IsValid() ? Mesh.Get()->GetName() : Mesh.ToSoftObjectPath().GetAssetName();
		}
#endif
	}
	if (!bHasBinding && !ActorLabel.IsEmpty()) Identity = FText::FromString(ActorLabel);

	const FText Subject = ComposableCameraSystem::ShotSubjectLabels::ToText(Idx);
	return FText::Format(LOCTEXT("SubjectEntryFormat", "{0} - {1} ({2})"), FText::AsNumber(Idx), Subject, Identity);
}

FText FShotTargetIndexCombo::GetCurrentSelectionText(const TSharedPtr<IPropertyHandle>& IndexHandle,
	const TSharedPtr<IPropertyHandleArray>& TargetsArray, TWeakObjectPtr<UObject> Host)
{
	if (!IndexHandle.IsValid())
	{
		return LOCTEXT("NoHandle", "(no handle)");
	}
	int32 CurIdx = INDEX_NONE;
	if (IndexHandle->GetValue(CurIdx) != FPropertyAccess::Success)
	{
		return LOCTEXT("MultipleValues", "<multiple values>");
	}
	return FormatTargetEntryLabel(TargetsArray, CurIdx, Host);
}

void FShotTargetIndexCombo::BuildIndexMenu(FMenuBuilder& MenuBuilder,
	const TWeakPtr<IPropertyHandle>& WeakIndexHandle,
	const TSharedPtr<IPropertyHandleArray>& TargetsArray, TWeakObjectPtr<UObject> Host)
{
	if (!TargetsArray.IsValid())
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("MenuUnreachable", "(targets array unreachable in this context)"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
		return;
	}
	uint32 Num = 0;
	TargetsArray->GetNumElements(Num);
	if (Num == 0)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("MenuEmpty", "(no targets authored - add a Target to the Shot first)"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
		return;
	}

	for (uint32 i = 0; i < Num; ++i)
	{
		const int32 IdxToSet = static_cast<int32>(i);
		MenuBuilder.AddMenuEntry(FormatTargetEntryLabel(TargetsArray, IdxToSet, Host),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakIndexHandle, IdxToSet]()
				{
					if (TSharedPtr<IPropertyHandle> Pin = WeakIndexHandle.Pin())
					{
						Pin->SetValue(IdxToSet);
					}
				})));
	}
}

TSharedRef<SWidget> FShotTargetIndexCombo::Build(const TSharedRef<IPropertyHandle>& IndexHandle, TWeakObjectPtr<UObject> Host)
{
	// Capture IndexHandle STRONGLY so the combo widget keeps the property
	// handle alive past customization tear-down. `IPropertyHandle::GetChildHandle`
	// returns a fresh `TSharedPtr` each call and the property tree does
	// not retain a parallel reference once the customization's local
	// SharedPtr goes out of scope - a `TWeakPtr` capture here pins to
	// nullptr immediately after `CustomizeChildren` returns and the combo
	// renders "(no handle)". Same gotcha the existing
	// `FComposableCameraTargetInfoCustomization::ActorHandle` comment
	// calls out for sibling-handle storage.
	//
	// `BuildIndexMenu` takes a TWeakPtr so its menu actions don't extend
	// the handle's life past the menu - fine because the menu's own SharedPtr
	// (`StrongIndex` below) keeps the handle live for as long as the
	// combo widget exists.
	const TSharedPtr<IPropertyHandle> StrongIndex = IndexHandle;

	return SNew(SComboButton)
		.OnGetMenuContent_Lambda([StrongIndex, Host]() -> TSharedRef<SWidget>
		{
			TSharedPtr<IPropertyHandleArray> Arr = ResolveTargetsArrayUpwards(StrongIndex);
			FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
			BuildIndexMenu(MenuBuilder, StrongIndex, Arr, Host);
			return MenuBuilder.MakeWidget();
		})
		.ButtonContent()
		[SNew(STextBlock)
			.Text_Lambda([StrongIndex, Host]() -> FText
			{
				return GetCurrentSelectionText(StrongIndex, ResolveTargetsArrayUpwards(StrongIndex), Host);
			})];
}

#undef LOCTEXT_NAMESPACE
