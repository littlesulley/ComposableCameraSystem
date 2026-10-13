// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Editors/ComposableCameraShotTemplates.h"

class ISequencer;
class FComposableCameraInstanceEditorSession;
class UMovieSceneComposableCameraInstanceTrack;
class UComposableCameraInstanceData;
class FScopedTransaction;
class UComposableCameraShotAsset;
class UCineCameraComponent;
class UMovieSceneComposableCameraShotSection;
class UMovieSceneSequence;
class FTransactionObjectEvent;
struct FPropertyChangedEvent;
struct FMovieSceneObjectBindingID;

/** Editor source adapter and gesture lifetime. No serialized parallel Shot model. */
class FComposableCameraShotAuthoringSession : public FGCObject
{
public:
	FComposableCameraShotAuthoringSession();
	~FComposableCameraShotAuthoringSession();
	void Bind(FComposableCameraShot* Shot, UObject* Host);
	/** Start an unsaved instance in the focused LS, without creating any camera/track. */
	bool StartSequenceDraft(TSharedRef<ISequencer> Sequencer, FString& Reason);
	/** Copy a template into a new draft. Null Sequencer creates a standalone draft. */
	bool StartTemplateDraft(const FComposableCameraShot& Shot, const FText& Name, TSharedPtr<ISequencer> Sequencer, FString& Reason);
	FText GetDraftName() const { return DraftName; }
	bool IsSequenceDraft() const;
	FComposableCameraShot* GetShot() const;
	bool CanEdit() const;
	void RequestPreview() { bPreviewPending = true; }
	UObject* GetHost() const { return Host.Get(); }
	UMovieSceneComposableCameraShotSection* GetSection() const;
	UMovieSceneComposableCameraInstanceTrack* GetInstanceTrack() const;
	UComposableCameraInstanceData* GetInstanceValues() const;
	bool IsGenericInstance() const;
	UMovieSceneSequence* GetSequence() const;
	TSharedPtr<ISequencer> GetSequencer() const;
	AActor* ResolveTarget(int32 Index) const;
	UCineCameraComponent* GetOutputCamera() const;
	/** Camera configuration remains usable outside an active Shot's playback range. */
	UCineCameraComponent* GetConfiguredCamera(bool bAllowTemplate = true) const;
	float GetPreviewAspectRatio() const;
	/** Scalar viewport release/commit, forwarded once without a structural refresh. */
	void NotifyViewportValueCommit();
	void BeginEdit(const FText& Description);
	bool IsEditing() const { return Transaction.IsValid(); }
	void EndEdit();
	void Changed(bool bStructural = false);
	/** Native property gestures keep value edits distinct from source/collection changes. */
	void NotifyNativePropertyChange(const FPropertyChangedEvent& Event);
	void Tick();
	bool CanAddTargets() const;
	/** Append without applying a template or changing existing subject roles. Null adds an empty slot. */
	bool AddTarget(AActor* Actor, FString& Reason);
	bool AppendTargets(TConstArrayView<AActor*> Actors, FString& Reason);
	bool AddSelectedTargets(FString& Reason);
	bool CanRemoveTarget(int32 Index) const;
	bool RemoveTarget(int32 Index, FString& Reason);
	bool SetTargetActor(int32 Index, AActor* Actor, FString& Reason);
	FMovieSceneObjectBindingID GetTargetBinding(int32 Index) const;
	/** Pick an existing actor binding without requiring its Spawnable to be alive. Invalid Guid clears it. */
	bool SetTargetBinding(int32 Index, const FGuid& BindingGuid, FString& Reason);
	bool MoveTarget(int32 From, int32 To);
	bool SwapSubjects();
	void Mirror();
	bool ApplyPreset(UComposableCameraShotAsset* Preset, FString& Reason);
	UComposableCameraShotAsset* SavePreset();
	bool bUseLevelWorld = true;
	bool bShowLookAtGuide = false; // View state only: Edit / LookAt selected.
	bool bShowFollowGuide = false; // View state only: Edit / Follow selected (screen handle or orbit).
	bool bShowSubjectGuide = false; // View state only: Create or Edit / Subjects selected.
	/** Structural flag asks the view to rebind after a gesture, never while dragging. */
	TFunction<void(bool)> OnChanged;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FComposableCameraShotAuthoringSession"); }
private:
	TWeakObjectPtr<UObject> Host;
	FName ShotProperty;
	FName EditingProperty;
	mutable TWeakPtr<ISequencer> CachedSequencer;
	FComposableCameraShot* LegacyShot = nullptr;
	TObjectPtr<UComposableCameraShotAsset> Scratch;
	TObjectPtr<UComposableCameraInstanceData> SequenceDraft;
	TObjectPtr<UMovieSceneSequence> DraftSequence;
	FText DraftName;
	TUniquePtr<FScopedTransaction> Transaction;
	TUniquePtr<FComposableCameraInstanceEditorSession> InstanceEditor;
	FDelegateHandle PropertyChangedHandle;
	FDelegateHandle ObjectTransactedHandle;
	FComposableCameraShot PreviewScratch;
	bool bPreviewPending = false;
	bool bGestureChanged = false;
	bool bStructuralChange = false;
	bool bNotifyingHost = false;
	void OnPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);
	void NotifyHost();
	void EnsureScratch();
	void AbortBindingEdit();
};
