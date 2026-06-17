// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraTrace.h"

#if UE_COMPOSABLE_CAMERA_TRACE

#include "HAL/IConsoleManager.h"
#include "ObjectTrace.h"
#include "Trace/Trace.h"

bool GComposableCameraDebugTrace = false;
static FAutoConsoleVariableRef CVarComposableCameraDebugTrace(
	TEXT("CCS.Debug.Trace"),
	GComposableCameraDebugTrace,
	TEXT("(Default: false. Enables background tracing of ComposableCameraSystem debug info."));

// Channel name must match FComposableCameraTrace::ChannelName.
UE_TRACE_CHANNEL(ComposableCameraSystemChannel)

// Logger and event names must match FComposableCameraTrace static names.
UE_TRACE_EVENT_BEGIN(ComposableCameraSystem, ActiveCamera)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, WorldId)
	UE_TRACE_EVENT_FIELD(uint64, PlayerControllerId)
	UE_TRACE_EVENT_FIELD(uint64, PawnId)
	UE_TRACE_EVENT_FIELD(uint64, PlayerCameraManagerId)
	UE_TRACE_EVENT_FIELD(uint64, ViewTargetActorId)
	UE_TRACE_EVENT_FIELD(uint64, CameraComponentId)
	UE_TRACE_EVENT_FIELD(uint8, SourceKind)
	UE_TRACE_EVENT_FIELD(double, LocationX)
	UE_TRACE_EVENT_FIELD(double, LocationY)
	UE_TRACE_EVENT_FIELD(double, LocationZ)
	UE_TRACE_EVENT_FIELD(double, RotationPitch)
	UE_TRACE_EVENT_FIELD(double, RotationYaw)
	UE_TRACE_EVENT_FIELD(double, RotationRoll)
	UE_TRACE_EVENT_FIELD(float, FieldOfView)
	UE_TRACE_EVENT_FIELD(uint8, ProjectionMode)
	UE_TRACE_EVENT_FIELD(float, OrthoWidth)
	UE_TRACE_EVENT_FIELD(float, OrthoNearClipPlane)
	UE_TRACE_EVENT_FIELD(float, OrthoFarClipPlane)
	UE_TRACE_EVENT_FIELD(bool, bConstrainAspectRatio)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(ComposableCameraSystem, CCSEvaluation)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, WorldId)
	UE_TRACE_EVENT_FIELD(uint64, SourceObjectId)
	UE_TRACE_EVENT_FIELD(uint64, OwnerPawnId)
	UE_TRACE_EVENT_FIELD(uint64, PlayerControllerId)
	UE_TRACE_EVENT_FIELD(uint64, ViewTargetActorId)
	UE_TRACE_EVENT_FIELD(uint8, SourceKind)
	UE_TRACE_EVENT_FIELD(uint8, ProjectionStatus)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, CameraTypeAssetName)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, ContextName)
	UE_TRACE_EVENT_FIELD(double, LocationX)
	UE_TRACE_EVENT_FIELD(double, LocationY)
	UE_TRACE_EVENT_FIELD(double, LocationZ)
	UE_TRACE_EVENT_FIELD(double, RotationPitch)
	UE_TRACE_EVENT_FIELD(double, RotationYaw)
	UE_TRACE_EVENT_FIELD(double, RotationRoll)
	UE_TRACE_EVENT_FIELD(float, FieldOfView)
	UE_TRACE_EVENT_FIELD(uint8[], SerializedPrimitives)
UE_TRACE_EVENT_END()

FString FComposableCameraTrace::ChannelName(TEXT("ComposableCameraSystemChannel"));
FString FComposableCameraTrace::LoggerName(TEXT("ComposableCameraSystem"));
FString FComposableCameraTrace::ActiveCameraEventName(TEXT("ActiveCamera"));
FString FComposableCameraTrace::EvaluationEventName(TEXT("CCSEvaluation"));

bool FComposableCameraTrace::IsTraceEnabled()
{
	return GComposableCameraDebugTrace || UE_TRACE_CHANNELEXPR_IS_ENABLED(ComposableCameraSystemChannel);
}

void FComposableCameraTrace::TraceActiveCamera(UWorld* World, const FComposableCameraActiveTraceFrame& Frame)
{
	if (!IsTraceEnabled())
	{
		return;
	}

	const FComposableCameraTracePose& Pose = Frame.RenderedPose;
	const uint64 Cycle = Frame.FrameCycle != 0 ? Frame.FrameCycle : FPlatformTime::Cycles64();
	const double RecordingTime = Frame.RecordingTime != 0.0
		? Frame.RecordingTime
		: FObjectTrace::GetWorldElapsedTime(World);

	UE_TRACE_LOG(ComposableCameraSystem, ActiveCamera, ComposableCameraSystemChannel)
		<< ActiveCamera.Cycle(Cycle)
		<< ActiveCamera.RecordingTime(RecordingTime)
		<< ActiveCamera.WorldId(Frame.WorldId)
		<< ActiveCamera.PlayerControllerId(Frame.PlayerControllerId)
		<< ActiveCamera.PawnId(Frame.PawnId)
		<< ActiveCamera.PlayerCameraManagerId(Frame.PlayerCameraManagerId)
		<< ActiveCamera.ViewTargetActorId(Frame.ViewTargetActorId)
		<< ActiveCamera.CameraComponentId(Frame.CameraComponentId)
		<< ActiveCamera.SourceKind(static_cast<uint8>(Frame.SourceKind))
		<< ActiveCamera.LocationX(Pose.Location.X)
		<< ActiveCamera.LocationY(Pose.Location.Y)
		<< ActiveCamera.LocationZ(Pose.Location.Z)
		<< ActiveCamera.RotationPitch(Pose.Rotation.Pitch)
		<< ActiveCamera.RotationYaw(Pose.Rotation.Yaw)
		<< ActiveCamera.RotationRoll(Pose.Rotation.Roll)
		<< ActiveCamera.FieldOfView(Pose.FieldOfView)
		<< ActiveCamera.ProjectionMode(static_cast<uint8>(Pose.ProjectionMode.GetValue()))
		<< ActiveCamera.OrthoWidth(Pose.OrthoWidth)
		<< ActiveCamera.OrthoNearClipPlane(Pose.OrthoNearClipPlane)
		<< ActiveCamera.OrthoFarClipPlane(Pose.OrthoFarClipPlane)
		<< ActiveCamera.bConstrainAspectRatio(Pose.bConstrainAspectRatio);
}

void FComposableCameraTrace::TraceEvaluation(UWorld* World, const FComposableCameraEvaluationTraceFrame& Frame)
{
	if (!IsTraceEnabled())
	{
		return;
	}

	TArray<uint8> SerializedPrimitives;
	if (!SerializeComposableCameraDebugPrimitives(Frame.Primitives, SerializedPrimitives))
	{
		SerializedPrimitives.Reset();
	}

	TCHAR CameraTypeAssetName[FName::StringBufferSize];
	TCHAR ContextName[FName::StringBufferSize];
	const uint32 CameraTypeAssetNameLength = Frame.CameraTypeAssetName.ToString(CameraTypeAssetName);
	const uint32 ContextNameLength = Frame.ContextName.ToString(ContextName);

	const FComposableCameraTracePose& Pose = Frame.CCSPose;
	const uint64 Cycle = Frame.FrameCycle != 0 ? Frame.FrameCycle : FPlatformTime::Cycles64();
	const double RecordingTime = Frame.RecordingTime != 0.0
		? Frame.RecordingTime
		: FObjectTrace::GetWorldElapsedTime(World);

	UE_TRACE_LOG(ComposableCameraSystem, CCSEvaluation, ComposableCameraSystemChannel)
		<< CCSEvaluation.Cycle(Cycle)
		<< CCSEvaluation.RecordingTime(RecordingTime)
		<< CCSEvaluation.WorldId(Frame.WorldId)
		<< CCSEvaluation.SourceObjectId(Frame.SourceObjectId)
		<< CCSEvaluation.OwnerPawnId(Frame.OwnerPawnId)
		<< CCSEvaluation.PlayerControllerId(Frame.PlayerControllerId)
		<< CCSEvaluation.ViewTargetActorId(Frame.ViewTargetActorId)
		<< CCSEvaluation.SourceKind(static_cast<uint8>(Frame.SourceKind))
		<< CCSEvaluation.ProjectionStatus(static_cast<uint8>(Frame.ProjectionStatus))
		<< CCSEvaluation.CameraTypeAssetName(CameraTypeAssetName, CameraTypeAssetNameLength)
		<< CCSEvaluation.ContextName(ContextName, ContextNameLength)
		<< CCSEvaluation.LocationX(Pose.Location.X)
		<< CCSEvaluation.LocationY(Pose.Location.Y)
		<< CCSEvaluation.LocationZ(Pose.Location.Z)
		<< CCSEvaluation.RotationPitch(Pose.Rotation.Pitch)
		<< CCSEvaluation.RotationYaw(Pose.Rotation.Yaw)
		<< CCSEvaluation.RotationRoll(Pose.Rotation.Roll)
		<< CCSEvaluation.FieldOfView(Pose.FieldOfView)
		<< CCSEvaluation.SerializedPrimitives(SerializedPrimitives.GetData(), SerializedPrimitives.Num());
}

#endif // UE_COMPOSABLE_CAMERA_TRACE
