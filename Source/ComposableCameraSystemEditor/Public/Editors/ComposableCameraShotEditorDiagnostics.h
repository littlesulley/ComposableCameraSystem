// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

struct FComposableCameraShot;

enum class EShotEditorDiagnosticIssue : uint8
{
	None, SourceUnavailable, FollowAnchor, AimAnchor, CoincidentAnchors, PoseUnavailable,
	BasisFallback, FocusAnchor, NoFramingBounds, FOVMinimum, FOVMaximum
};
enum class EShotEditorDiagnosticPage : uint8 { Follow, Aim, Lens, Subjects };

/** Allocation-free preview status; text is formatted only when status changes. */
struct FShotEditorDiagnostic
{
	EShotEditorDiagnosticIssue Issue = EShotEditorDiagnosticIssue::None;
	EShotEditorDiagnosticPage Page = EShotEditorDiagnosticPage::Follow;
	int32 TargetIndex = INDEX_NONE;
	bool bPoseHeld = false;
	bool operator==(const FShotEditorDiagnostic& Other) const
	{
		return Issue == Other.Issue && Page == Other.Page && TargetIndex == Other.TargetIndex && bPoseHeld == Other.bPoseHeld;
	}
	bool HasIssue() const { return Issue != EShotEditorDiagnosticIssue::None; }
	COMPOSABLECAMERASYSTEMEDITOR_API FText ToText() const;
	COMPOSABLECAMERASYSTEMEDITOR_API const TCHAR* PropertyPath() const;
};

/** Reads the already resolved preview Shot. Does not run another solver or mutate source data. */
COMPOSABLECAMERASYSTEMEDITOR_API FShotEditorDiagnostic DiagnoseShotPreview(
	const FComposableCameraShot& Shot, bool bPoseValid, bool bPoseHeld, float SolvedFOV, bool bNativeOutput = false);
