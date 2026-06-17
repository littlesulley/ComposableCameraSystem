// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraRewindDebuggerTrack.h"

#include "GameFramework/Pawn.h"
#include "RewindDebuggerTrack.h"

#define LOCTEXT_NAMESPACE "ComposableCameraRewindDebuggerTrack"

namespace
{
	static const FName GComposableCameraRewindTrackName(TEXT("ComposableCameraSystem"));

	class FComposableCameraRewindDebuggerTrack : public RewindDebugger::FRewindDebuggerTrack
	{
	public:
		explicit FComposableCameraRewindDebuggerTrack(uint64 InObjectId)
			: ObjectId(InObjectId)
		{
		}

	private:
		virtual FName GetNameInternal() const override
		{
			return GComposableCameraRewindTrackName;
		}

		virtual FText GetDisplayNameInternal() const override
		{
			return LOCTEXT("TrackDisplayName", "Composable Camera");
		}

		virtual uint64 GetObjectIdInternal() const override
		{
			return ObjectId;
		}

	private:
		uint64 ObjectId = 0;
	};
}

FName FComposableCameraRewindDebuggerTrackCreator::GetTargetTypeNameInternal() const
{
	return APawn::StaticClass()->GetFName();
}

FName FComposableCameraRewindDebuggerTrackCreator::GetNameInternal() const
{
	return GComposableCameraRewindTrackName;
}

void FComposableCameraRewindDebuggerTrackCreator::GetTrackTypesInternal(
	TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const
{
	Types.Add({ GComposableCameraRewindTrackName, LOCTEXT("TrackTypeDisplayName", "Composable Camera") });
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FComposableCameraRewindDebuggerTrackCreator::CreateTrackInternal(
	uint64 ObjectId) const
{
	return MakeShared<FComposableCameraRewindDebuggerTrack>(ObjectId);
}

#undef LOCTEXT_NAMESPACE
