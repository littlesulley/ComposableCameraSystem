// Copyright Sulley. All rights reserved.

#include "Customizations/ComposableCameraTargetInfoCustomization.h"

#include "ComposableCameraSystemEditorModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/ComposableCameraTargetInfo.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Actor.h"
#include "IDetailChildrenBuilder.h"
#include "ISequencer.h"
#include "MovieScene.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieSceneSequence.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "ReferenceSkeleton.h"
#include "SSearchableComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComposableCameraTargetInfoCustomization"

namespace
{
	/** Sentinel string for the "clear bone" entry. Treated as `NAME_None`
	 * on commit so the BoneName UPROPERTY ends up empty. */
	static const FString GNoneOptionString = TEXT("(none)");

	/** Hint shown inside the combo when no SkelMesh resolves. The combo is
	 * also disabled in that state, so this is informational only. */
	static const FString GNoSkelMeshOptionString = TEXT("(no skeletal mesh found)");

	AActor* ResolveActorFromHandle(const TSharedPtr<IPropertyHandle>& Handle, const TCHAR* DebugLabel)
	{
		if (!Handle.IsValid() || !Handle->IsValidHandle())
		{
			UE_LOG(LogComposableCameraSystemEditor, Verbose,
				TEXT("[BonePicker] %s handle invalid"),
				DebugLabel);
			return nullptr;
		}

		UObject* ResolvedObject = nullptr;
		const FPropertyAccess::Result Res = Handle->GetValue(ResolvedObject);
		if (Res != FPropertyAccess::Success)
		{
			UE_LOG(LogComposableCameraSystemEditor, Verbose,
				TEXT("[BonePicker] %s GetValue failed (Result=%d)"),
				DebugLabel,
				static_cast<int32>(Res));
		}

		if (!ResolvedObject)
		{
			void* RawData = nullptr;
			if (Handle->GetValueData(RawData) == FPropertyAccess::Success && RawData)
			{
				FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(RawData);
				ResolvedObject = SoftPtr->Get();
				if (!ResolvedObject && !SoftPtr->IsNull())
				{
					ResolvedObject = SoftPtr->LoadSynchronous();
				}
				UE_LOG(LogComposableCameraSystemEditor, Verbose,
					TEXT("[BonePicker] %s soft-ptr fallback: path='%s' resolved=%s"),
					DebugLabel,
					*SoftPtr->ToString(),
					ResolvedObject ? *ResolvedObject->GetName() : TEXT("null"));
			}
		}

		if (!ResolvedObject)
		{
			UE_LOG(LogComposableCameraSystemEditor, Verbose,
				TEXT("[BonePicker] %s resolved no actor"),
				DebugLabel);
			return nullptr;
		}

		AActor* Actor = Cast<AActor>(ResolvedObject);
		if (!Actor)
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("[BonePicker] %s resolved object '%s' is not an AActor (class=%s)"),
				DebugLabel,
				*ResolvedObject->GetName(),
				ResolvedObject->GetClass() ? *ResolvedObject->GetClass()->GetName() : TEXT("null"));
		}
		return Actor;
	}

	USkeletalMeshComponent* ResolveFirstSkelMesh(AActor* Actor, const TCHAR* DebugLabel)
	{
		if (!Actor)
		{
			return nullptr;
		}

		USkeletalMeshComponent* SkelComp = Actor->FindComponentByClass<USkeletalMeshComponent>();
		UE_LOG(LogComposableCameraSystemEditor, Verbose,
			TEXT("[BonePicker] %s actor '%s' SkelMeshComp=%s"),
			DebugLabel,
			*Actor->GetName(),
			SkelComp ? *SkelComp->GetName() : TEXT("none"));
		return SkelComp;
	}

	USkeletalMesh* ResolveSkeletalMeshFromHandle(const TSharedPtr<IPropertyHandle>& Handle,
		const TCHAR* DebugLabel)
	{
		if (!Handle.IsValid() || !Handle->IsValidHandle())
		{
			return nullptr;
		}

		UObject* ResolvedObject = nullptr;
		Handle->GetValue(ResolvedObject);
		if (!ResolvedObject)
		{
			void* RawData = nullptr;
			if (Handle->GetValueData(RawData) == FPropertyAccess::Success && RawData)
			{
				FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(RawData);
				ResolvedObject = SoftPtr->Get();
				if (!ResolvedObject && !SoftPtr->IsNull())
				{
					ResolvedObject = SoftPtr->LoadSynchronous();
				}
			}
		}

		USkeletalMesh* Mesh = Cast<USkeletalMesh>(ResolvedObject);
		UE_LOG(LogComposableCameraSystemEditor, Verbose,
			TEXT("[BonePicker] %s preview mesh=%s"),
			DebugLabel,
			Mesh ? *Mesh->GetName() : TEXT("none"));
		return Mesh;
	}
}

TSharedRef<IPropertyTypeCustomization> FComposableCameraTargetInfoCustomization::MakeInstance()
{
	return MakeShared<FComposableCameraTargetInfoCustomization>();
}

void FComposableCameraTargetInfoCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FComposableCameraTargetInfo::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FComposableCameraTargetInfoCustomization::MakeInstance));
}

void FComposableCameraTargetInfoCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FComposableCameraTargetInfo::StaticStruct()->GetFName());
	}
}

void FComposableCameraTargetInfoCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	StructHandle = StructPropertyHandle;

	// Default-shape header: just the struct's name. ValueContent is left
	// untouched so the engine's default expand caret renders normally - 
	// this struct has too many fields to summarize inline anyway.
	HeaderRow.NameContent()
		[StructPropertyHandle->CreatePropertyNameWidget()];
}

void FComposableCameraTargetInfoCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& StructBuilder,
	IPropertyTypeCustomizationUtils& /*StructCustomizationUtils*/)
{
	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);

	// Walk children in declaration order, customizing BoneName's row inline
	// when reached. Actor comes first in `FComposableCameraTargetInfo`'s
	// declaration so its handle is captured before BoneName is encountered;
	// BuildBoneCombo() reads `WeakActorHandle` at construction.
	//
	// Customizing inline (rather than storing a pointer to the returned
	// IDetailPropertyRow& and applying CustomWidget after the loop) avoids
	// any chance of a stale row reference if the children builder's storage
	// reallocates during subsequent AddProperty calls.

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> Child = StructPropertyHandle->GetChildHandle(i);
		if (!Child.IsValid() || !Child->GetProperty())
		{
			continue;
		}

		const FName ChildName = Child->GetProperty()->GetFName();

		if (ChildName == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, Actor))
		{
			ActorHandle = Child;

			StructBuilder.AddProperty(Child.ToSharedRef());

			// Re-fire the bone scan whenever the actor reference is
			// reassigned in the Details panel. The combo widget refreshes
			// its options list inline via `BoneCombo->RefreshOptions`.
			Child->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FComposableCameraTargetInfoCustomization::RefreshBoneOptions));

			// Phase E LS Section override surface - read-only label that
			// shows the *effective* actor (bound via the Section's right-click
			// "Bind Target Actors " menu) when an override is active. Same
			// override the runtime + preview viewport already see; surfacing
			// it in Details closes the loop so designers know which actor
			// the bone picker is sourcing bones from.
			StructBuilder.AddCustomRow(LOCTEXT("EffectiveActorRow", "Effective Actor"))
				.NameContent()
				[SNew(STextBlock)
					.Text(LOCTEXT("EffectiveActorLabel", "Effective Actor"))
					.ToolTipText(LOCTEXT("EffectiveActorTooltip",
						"Resolved target actor - the LS Section's TargetActorOverrides "
						"binding when active (set via the section's right-click \"Bind "
						"Target Actors\" menu), else the directly-authored Actor field. "
						"This is what the runtime solver and the bone picker actually use. "
						"Format when an override is active: \"<Actor> (LS: "
						"<SequenceName> -> <BindingName>)\" - the LS asset and the "
						"binding's outliner name in the Sequencer it was authored in."))
					.Font(IDetailLayoutBuilder::GetDetailFont())]
				.ValueContent()
				.MinDesiredWidth(180.f)
				.MaxDesiredWidth(420.f)
				[BuildEffectiveActorLabel()];
		}
#if WITH_EDITORONLY_DATA
		else if (ChildName == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, EditorPreviewMesh))
		{
			EditorPreviewMeshHandle = Child;

			StructBuilder.AddProperty(Child.ToSharedRef());

			Child->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FComposableCameraTargetInfoCustomization::RefreshBoneOptions));
		}
#endif
		else if (ChildName == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, BoneName))
		{
			BoneNameHandle = Child;

			// Initial population happens here - both Actor and BoneName
			// handles are now captured, so RefreshBoneOptions can resolve
			// the SkelMesh and BuildBoneCombo can read the seed list.
			RefreshBoneOptions();

			// Hide the default BoneName row entirely - using
			// `AddProperty(...).CustomWidget(...)` triggered an
			// `ensure(bTickable)` in `FDetailItemNode::Tick` under
			// IStructureDetailsView (Shot Editor host). Replacing the row
			// outright via `AddCustomRow` sidesteps the property-row tick
			// gating: the custom row is never registered as tickable
			// because none of the gating conditions
			// (`PropertyRow::RequiresTick`, `WidgetDecl->VisibilityAttr.IsBound`)
			// are met by our static-shape custom widget.
			StructBuilder.AddProperty(Child.ToSharedRef())
				.Visibility(EVisibility::Collapsed);

			StructBuilder.AddCustomRow(Child->GetPropertyDisplayName())
				.NameContent()
				[SNew(STextBlock)
					.Text(Child->GetPropertyDisplayName())
					.ToolTipText(Child->GetToolTipText())
					.Font(IDetailLayoutBuilder::GetDetailFont())]
				.ValueContent()
				.MinDesiredWidth(180.f)
				.MaxDesiredWidth(420.f)
				[BuildBoneCombo()];
		}
		else
		{
			StructBuilder.AddProperty(Child.ToSharedRef());
		}
	}
}

void FComposableCameraTargetInfoCustomization::RefreshBoneOptions()
{
	TArray<TSharedPtr<FString>>& Options = *BoneOptions;
	Options.Reset();

	// First entry always "(none)" so designers can clear the BoneName field
	// without disabling `bUseBoneAsPivot`. Treated as NAME_None on commit.
	Options.Add(MakeShared<FString>(GNoneOptionString));

	if (USkeletalMesh* SkelMesh = ResolveSkeletalMesh())
	{
		// Bone names from the reference skeleton.
		const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();
		const int32 NumBones = RefSkeleton.GetNum();
		TArray<FName> Bones;
		Bones.Reserve(NumBones);
		for (int32 i = 0; i < NumBones; ++i)
		{
			Bones.Add(RefSkeleton.GetBoneName(i));
		}

		TArray<FName> Sockets;
		for (const USkeletalMeshSocket* Socket: SkelMesh->GetActiveSocketList())
		{
			if (Socket)
			{
				Sockets.Add(Socket->SocketName);
			}
		}

		// Merge + dedup (a few sockets share names with their parent
		// bones in some assets - Engine convention) + sort
		// case-insensitively for reading order.
		TSet<FName> Unique;
		Unique.Append(Bones);
		Unique.Append(Sockets);
		TArray<FName> Sorted = Unique.Array();
		Sorted.Sort([](const FName& A, const FName& B)
		{
			return A.Compare(B) < 0;
		});

		Options.Reserve(Options.Num() + Sorted.Num());
		for (const FName& N: Sorted)
		{
			if (!N.IsNone())
			{
				Options.Add(MakeShared<FString>(N.ToString()));
			}
		}
	}
	else
	{
		// No SkelMesh resolvable - surface a hint entry. The combo itself is
		// disabled in this state so the entry is informational only.
		Options.Add(MakeShared<FString>(GNoSkelMeshOptionString));
	}

	if (BoneCombo.IsValid())
	{
		BoneCombo->RefreshOptions();
	}

	UE_LOG(LogComposableCameraSystemEditor, Verbose,
		TEXT("[BonePicker] RefreshBoneOptions complete - %d entries"),
		Options.Num());
}

int32 FComposableCameraTargetInfoCustomization::ResolveTargetIndex() const
{
	if (!StructHandle.IsValid())
	{
		return INDEX_NONE;
	}
	// FComposableCameraTargetInfo is a member named `Target` on
	// FComposableCameraShotTarget. The struct handle's parent is the
	// containing FShotTarget. That FShotTarget is itself an array element
	// in `FComposableCameraShot::Targets`, so its property handle has a
	// valid `GetIndexInArray()`. Walking just one parent reaches that.
	const TSharedPtr<IPropertyHandle> ShotTargetHandle = StructHandle->GetParentHandle();
	return ShotTargetHandle.IsValid() ? ShotTargetHandle->GetIndexInArray() : INDEX_NONE;
}

AActor* FComposableCameraTargetInfoCustomization::ResolveLSOverrideActor() const
{
	FText UnusedSeq, UnusedBinding;
	return ResolveLSOverrideContext(UnusedSeq, UnusedBinding);
}

AActor* FComposableCameraTargetInfoCustomization::ResolveLSOverrideContext(FText& OutSequenceDisplayName,
	FText& OutBindingDisplayName) const
{
	OutSequenceDisplayName = FText::GetEmpty();
	OutBindingDisplayName = FText::GetEmpty();

	const int32 TargetIndex = ResolveTargetIndex();
	if (TargetIndex == INDEX_NONE)
	{
		return nullptr;
	}

	// Two paths to find the host Section:
	// (1) Sequencer Section Details - `IPropertyHandle::GetOuterObjects`
	// returns the Section directly (the Section UObject owns the
	// `InlineShot` UPROPERTY this struct lives inside).
	// (2) Shot Editor `IStructureDetailsView` - the view wraps the Shot
	// in a raw `FStructOnScope(StaticStruct, uint8*)` which carries
	// no host UObject, so GetOuterObjects returns empty. Fall back
	// to the editor's static "currently live host" lookup.
	UMovieSceneComposableCameraShotSection* Section = nullptr;

	if (StructHandle.IsValid())
	{
		TArray<UObject*> OuterObjects;
		StructHandle->GetOuterObjects(OuterObjects);
		for (UObject* Outer: OuterObjects)
		{
			if (UMovieSceneComposableCameraShotSection* Found =
				Cast<UMovieSceneComposableCameraShotSection>(Outer))
			{
				Section = Found;
				break;
			}
		}
	}

	if (!Section)
	{
		Section = Cast<UMovieSceneComposableCameraShotSection>(FComposableCameraShotEditor::GetCurrentLiveHost());
	}

	if (!Section)
	{
		return nullptr;
	}

	// Linear scan - TargetActorOverrides is small (one entry per overridden
	// target index, capped by Shot's target count which is typically 1-4).
	const FComposableCameraShotTargetActorOverride* MatchingOverride = nullptr;
	for (const FComposableCameraShotTargetActorOverride& O: Section->TargetActorOverrides)
	{
		if (O.TargetIndex == TargetIndex && O.Binding.IsValid())
		{
			MatchingOverride = &O;
			break;
		}
	}
	if (!MatchingOverride)
	{
		return nullptr;
	}

	UMovieSceneSequence* Sequence = Section->GetTypedOuter<UMovieSceneSequence>();
	if (!Sequence)
	{
		return nullptr;
	}

	// Sequence display name = the LS asset's object name (matches the tab
	// title in the Sequencer editor). FName route avoids any localization
	// detour through `UObject::GetDisplayNameText` which can return the
	// class name on certain assets.
	OutSequenceDisplayName = FText::FromName(Sequence->GetFName());

	// Binding display name = the name shown in the Sequencer outliner for
	// this binding (e.g. "Hero" for a possessable, "MainCamSpawn" for a
	// spawnable). Resolved from the MovieScene's authored display map; falls
	// back to the binding GUID as a last resort.
	if (UMovieScene* MovieScene = Sequence->GetMovieScene())
	{
		const FGuid BindingGuid = MatchingOverride->Binding.GetGuid();
		const FText DisplayName = MovieScene->GetObjectDisplayName(BindingGuid);
		OutBindingDisplayName = DisplayName.IsEmpty()
			? FText::FromString(BindingGuid.ToString(EGuidFormats::DigitsWithHyphens))
			: DisplayName;
	}

	// `FindOpenSequencerForSequence` is the editor-only registry that maps
	// running Sequencer instances to their MovieSceneSequence. Without an
	// open Sequencer there's no playback context to resolve binding GUIDs
	// against - we silently fall through to "no override" so the customization
	// picks up the directly-authored Actor field instead. Note: sequence /
	// binding names ABOVE are still populated even when no Sequencer is open,
	// so future code that wants to surface the override metadata in a
	// disabled state can read them; current label hides everything when
	// the actor doesn't resolve.
	FComposableCameraSystemEditorModule& EditorModule =
		FModuleManager::GetModuleChecked<FComposableCameraSystemEditorModule>(TEXT("ComposableCameraSystemEditor"));
	const TSharedPtr<ISequencer> OpenSequencer =
		EditorModule.FindOpenSequencerForSequence(Sequence);
	if (!OpenSequencer.IsValid())
	{
		return nullptr;
	}

	// Possessables resolve immediately to their level-world actor;
	// Spawnables only while their binding's section is active in the
	// timeline (designer must scrub the playhead inside the spawnable's
	// range to see it bound). FocusedTemplateID handles sub-sequences.
	const TArrayView<TWeakObjectPtr<>> Bound = OpenSequencer->FindBoundObjects(MatchingOverride->Binding.GetGuid(),
		OpenSequencer->GetFocusedTemplateID());
	for (const TWeakObjectPtr<UObject>& Weak: Bound)
	{
		if (AActor* Actor = Cast<AActor>(Weak.Get()))
		{
			return Actor;
		}
	}
	return nullptr;
}

USkeletalMesh* FComposableCameraTargetInfoCustomization::ResolveSkeletalMesh() const
{
	// Phase E LS Section override path takes priority: when the section's
	// right-click "Bind Target Actors" menu has bound this Target index to
	// a Sequencer binding, the *bound* actor is what runtime + preview both
	// see, so the bone picker should list bones from that actor, not from
	// the directly-authored Actor field.
	if (AActor* OverrideActor = ResolveLSOverrideActor())
	{
		if (USkeletalMeshComponent* SkelComp =
			ResolveFirstSkelMesh(OverrideActor, TEXT("LS-override")))
		{
			return SkelComp->GetSkeletalMeshAsset();
		}
	}

	if (USkeletalMeshComponent* SkelComp =
		ResolveFirstSkelMesh(ResolveActorFromHandle(ActorHandle, TEXT("Actor")), TEXT("Actor")))
	{
		return SkelComp->GetSkeletalMeshAsset();
	}

#if WITH_EDITORONLY_DATA
	if (USkeletalMesh* PreviewMesh =
		ResolveSkeletalMeshFromHandle(EditorPreviewMeshHandle, TEXT("EditorPreviewMesh")))
	{
		return PreviewMesh;
	}
#endif

	return nullptr;
}

TSharedRef<SWidget> FComposableCameraTargetInfoCustomization::BuildEffectiveActorLabel()
{
	TWeakPtr<FComposableCameraTargetInfoCustomization> WeakSelf =
		StaticCastSharedRef<FComposableCameraTargetInfoCustomization>(AsShared());

	return SNew(STextBlock)
		.Text_Lambda([WeakSelf]() -> FText
		{
			const TSharedPtr<FComposableCameraTargetInfoCustomization> Self = WeakSelf.Pin();
			if (!Self.IsValid())
			{
				return FText::GetEmpty();
			}
			FText SequenceName, BindingName;
			AActor* Effective = Self->ResolveLSOverrideContext(SequenceName, BindingName);
			if (!Effective)
			{
				return LOCTEXT("EffectiveActorNoOverride", "(no override)");
			}
			// Prefer the four-part format when both names resolve so designers
			// can tell apart overrides sourced from different open LSs at a
			// glance. Falls back gracefully if MovieScene lookup couldn't
			// find a binding name (extremely rare - guid mismatch from a
			// stale override; we still show the actor + sequence).
			if (!SequenceName.IsEmpty() && !BindingName.IsEmpty())
			{
				return FText::Format(LOCTEXT("EffectiveActorLSBoundFullFmt",
						"{0} (LS: {1} -> {2})"),
					FText::FromString(Effective->GetActorNameOrLabel()),
					SequenceName,
					BindingName);
			}
			if (!SequenceName.IsEmpty())
			{
				return FText::Format(LOCTEXT("EffectiveActorLSBoundSeqFmt",
						"{0} (LS: {1})"),
					FText::FromString(Effective->GetActorNameOrLabel()),
					SequenceName);
			}
			return FText::Format(LOCTEXT("EffectiveActorLSBoundFmt", "{0} (LS-bound)"),
				FText::FromString(Effective->GetActorNameOrLabel()));
		})
		.ColorAndOpacity_Lambda([WeakSelf]() -> FSlateColor
		{
			const TSharedPtr<FComposableCameraTargetInfoCustomization> Self = WeakSelf.Pin();
			// Dim the label when no override is active - reads as a hint
			// rather than competing with the authored Actor field above it.
			// Highlighted (warm cyan) when bound so the override is immediately
			// scannable at a glance.
			if (Self.IsValid() && Self->ResolveLSOverrideActor())
			{
				return FSlateColor(FLinearColor(0.55f, 0.85f, 1.0f));
			}
			return FSlateColor::UseSubduedForeground();
		})
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

TSharedRef<SWidget> FComposableCameraTargetInfoCustomization::BuildBoneCombo()
{
	// Capture the BoneOptions SharedRef in every callback so the array
	// outlives the customization - Slate may dispatch combo events / paint
	// frames after the customization has been released by a Details panel
	// rebuild. The raw `&Options` pointer passed to OptionsSource resolves
	// through the captured SharedRef each time, never dangling.
	TSharedRef<TArray<TSharedPtr<FString>>> OptionsKeepAlive = BoneOptions;

	// Capture the BoneName property handle by SharedPtr so widget callbacks
	// can read / write the property even after the customization is released.
	// `IPropertyHandle::GetChildHandle` returns a fresh SharedPtr per call
	// and the property tree does not always retain a parallel reference; a
	// TWeakPtr would Pin to null after the loop's local SharedPtr falls
	// out of scope. Always check `IsValidHandle()` before deref since the
	// underlying FPropertyNode can still be torn down externally.
	TSharedPtr<IPropertyHandle> CapturedBoneHandle = BoneNameHandle;

	// Self-pointer for OnComboBoxOpening - refresh the bone list right
	// before the dropdown opens so a late-resolved Actor (or one toggled
	// into bone mode after construction) still surfaces its bones.
	TWeakPtr<FComposableCameraTargetInfoCustomization> WeakSelf =
		StaticCastSharedRef<FComposableCameraTargetInfoCustomization>(AsShared());

	return SAssignNew(BoneCombo, SSearchableComboBox)
		.OptionsSource(&OptionsKeepAlive.Get())
		.IsEnabled_Lambda([CapturedBoneHandle]() -> bool
		{
			// Enable iff the BoneName property handle is currently editable
			// (catches the EditCondition gating on `bUseBoneAsPivot`).
			return CapturedBoneHandle.IsValid()
				&& CapturedBoneHandle->IsValidHandle()
				&& CapturedBoneHandle->IsEditable();
		})
		.OnComboBoxOpening_Lambda([WeakSelf]()
		{
			if (TSharedPtr<FComposableCameraTargetInfoCustomization> Self = WeakSelf.Pin())
			{
				Self->RefreshBoneOptions();
			}
		})
		.OnSelectionChanged_Lambda(
			[CapturedBoneHandle, OptionsKeepAlive]
			(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
			{
				if (!NewSelection.IsValid() || SelectInfo == ESelectInfo::OnNavigation)
				{
					return;
				}
				const FString& Sel = *NewSelection;
				if (Sel == GNoSkelMeshOptionString)
				{
					return; // hint-only entry, ignore
				}
				if (!CapturedBoneHandle.IsValid() || !CapturedBoneHandle->IsValidHandle())
				{
					return;
				}
				// SetValue routes through the property handle's
				// NotifyPreChange / NotifyPostChange chain, giving us proper
				// undo / redo + PostEditChangeProperty broadcast on the host
				// UObject.
				CapturedBoneHandle->SetValue(
					(Sel == GNoneOptionString) ? NAME_None: FName(*Sel));
			})
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
		{
			return SNew(STextBlock)
				.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
				.Font(IDetailLayoutBuilder::GetDetailFont());
		})
		.Content()
		[SNew(STextBlock)
			.Text_Lambda([CapturedBoneHandle]() -> FText
			{
				if (!CapturedBoneHandle.IsValid() || !CapturedBoneHandle->IsValidHandle())
				{
					return FText::FromString(GNoneOptionString);
				}
				FName Current = NAME_None;
				const FPropertyAccess::Result Result = CapturedBoneHandle->GetValue(Current);
				if (Result == FPropertyAccess::MultipleValues)
				{
					return LOCTEXT("MultipleValues", "<multiple values>");
				}
				return Current.IsNone()
					? FText::FromString(GNoneOptionString)
					: FText::FromName(Current);
			})
			.Font(IDetailLayoutBuilder::GetDetailFont())];
}

#undef LOCTEXT_NAMESPACE
