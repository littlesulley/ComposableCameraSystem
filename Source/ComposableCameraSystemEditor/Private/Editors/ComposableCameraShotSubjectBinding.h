// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

class AActor;
class UMovieSceneSequence;
class FComposableCameraShotAuthoringSession;

enum class EShotSubjectBindingStatus : uint8
{
	Unbound, RequiredUnbound, Resolved, MissingBinding, Unspawned, Unresolved,
	ClassMismatch, SequenceUnavailable, InterfaceChanged
};

/** Read-only, short-lived editor inspection. Never stored in an asset or evaluator. */
struct FShotSubjectBindingDiagnostic
{
	EShotSubjectBindingStatus Status = EShotSubjectBindingStatus::Unbound;
	FText Label;
	FGuid BindingGuid;
	TWeakObjectPtr<AActor> Actor;
	bool bCanLocateBinding = false;
	FText GetMessage() const;
};

namespace ComposableCameraSystem::ShotSubjectBinding
{
	/** Class/required metadata comes from the retained input row, rebuilt on interface changes. */
	FShotSubjectBindingDiagnostic Inspect(const FComposableCameraShotAuthoringSession& Session,
		FName Input, int32 SubjectIndex, const UClass* ExpectedClass, bool bRequired);
	/** Reinspect on click; navigation does not modify bindings or seek time. */
	bool Locate(const FComposableCameraShotAuthoringSession& Session, FName Input, int32 SubjectIndex,
		const UClass* ExpectedClass, bool bRequired, bool bActor, FString& Error);
}
