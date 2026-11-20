// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Editors/ComposableCameraShotSubjectBinding.h"

class FComposableCameraShotAuthoringSession;
class FMenuBuilder;
class AActor;

DECLARE_DELEGATE_OneParam(FOnShotSubjectPickerError, const FString&);

/** Section subjects pick authored bindings; standalone subjects retain the native actor picker. */
class SShotEditorSubjectPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShotEditorSubjectPicker) : _SubjectIndex(INDEX_NONE), _ActorClass(nullptr), _RequiredInput(false) {}
		SLATE_ARGUMENT(TSharedPtr<FComposableCameraShotAuthoringSession>, Session)
		SLATE_ARGUMENT(int32, SubjectIndex)
		SLATE_ARGUMENT(FName, InputPath)
		SLATE_ARGUMENT(const UClass*, ActorClass)
		SLATE_ARGUMENT(bool, RequiredInput)
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
	FName InputPath;
	TWeakObjectPtr<const UClass> ExpectedActorClass;
	bool bRequiredInput = false;
	uint32 MenuTypeRevision = 0;
	bool IsCurrentMenu() const;
	TSharedRef<SWidget> BuildMenu();
	void BuildLevelActorMenu(FMenuBuilder& Menu);
	FText GetLabel() const;
	FShotSubjectBindingDiagnostic InspectBinding() const;
	FReply LocateBinding(bool bActor);
	bool CanPick() const;
	bool AssignBinding(const FGuid& Guid);
	void PickBinding(FGuid Guid);
	void PickActor(AActor* Actor);
	void UseSelectedActor();
	void CloseMenu();
};
