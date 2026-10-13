// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FComposableCameraShotAuthoringSession;
class FMenuBuilder;
class AActor;

DECLARE_DELEGATE_OneParam(FOnShotSubjectPickerError, const FString&);

/** Section subjects pick authored bindings; standalone subjects retain the native actor picker. */
class SShotEditorSubjectPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShotEditorSubjectPicker) : _SubjectIndex(INDEX_NONE) {}
		SLATE_ARGUMENT(TSharedPtr<FComposableCameraShotAuthoringSession>, Session)
		SLATE_ARGUMENT(int32, SubjectIndex)
		SLATE_EVENT(FOnShotSubjectPickerError, OnError)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
#if WITH_DEV_AUTOMATION_TESTS
	TSharedRef<SWidget> BuildMenuForTesting() { return BuildMenu(); }
	bool ChooseBindingForTesting(const FGuid& Guid) { return AssignBinding(Guid); }
#endif
private:
	TWeakPtr<FComposableCameraShotAuthoringSession> WeakSession;
	TWeakObjectPtr<UObject> MenuHost;
	FOnShotSubjectPickerError OnError;
	int32 SubjectIndex = INDEX_NONE;
	TSharedRef<SWidget> BuildMenu();
	void BuildLevelActorMenu(FMenuBuilder& Menu);
	FText GetLabel() const;
	bool CanPick() const;
	bool AssignBinding(const FGuid& Guid);
	void PickBinding(FGuid Guid);
	void PickActor(AActor* Actor);
	void UseSelectedActor();
	void CloseMenu();
};
