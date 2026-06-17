// Copyright 2026 Sulley. All Rights Reserved.

#include "Trace/ComposableCameraTraceAnalyzer.h"

#include "Debug/ComposableCameraTraceTypes.h"
#include "Trace/ComposableCameraTraceProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace
{
	static FName ReadTraceName(const UE::Trace::IAnalyzer::FEventData& EventData, const ANSICHAR* FieldName)
	{
		FString Value;
		if (!EventData.GetString(FieldName, Value))
		{
			return NAME_None;
		}
		return FName(*Value);
	}
}

FComposableCameraTraceAnalyzer::FComposableCameraTraceAnalyzer(
	TraceServices::IAnalysisSession& InSession,
	FComposableCameraTraceProvider& InProvider)
	: Session(InSession)
	, Provider(InProvider)
{
}

void FComposableCameraTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	Context.InterfaceBuilder.RouteEvent(RouteActiveCamera, "ComposableCameraSystem", "ActiveCamera");
	Context.InterfaceBuilder.RouteEvent(RouteCCSEvaluation, "ComposableCameraSystem", "CCSEvaluation");
}

bool FComposableCameraTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	if (Style == EStyle::LeaveScope)
	{
		return true;
	}

	TraceServices::FAnalysisSessionEditScope EditScope(Session);

	const uint64 Cycle = Context.EventData.GetValue<uint64>("Cycle");
	const double EventTime = Context.EventTime.AsSeconds(Cycle);
	const double RecordingTime = Context.EventData.GetValue<double>("RecordingTime");

	if (RouteId == RouteActiveCamera)
	{
		FComposableCameraActiveTraceFrame Frame;
		Frame.FrameCycle = Cycle;
		Frame.RecordingTime = RecordingTime;
		Frame.WorldId = Context.EventData.GetValue<uint64>("WorldId");
		Frame.PlayerControllerId = Context.EventData.GetValue<uint64>("PlayerControllerId");
		Frame.PawnId = Context.EventData.GetValue<uint64>("PawnId");
		Frame.PlayerCameraManagerId = Context.EventData.GetValue<uint64>("PlayerCameraManagerId");
		Frame.ViewTargetActorId = Context.EventData.GetValue<uint64>("ViewTargetActorId");
		Frame.CameraComponentId = Context.EventData.GetValue<uint64>("CameraComponentId");
		Frame.SourceKind = static_cast<EComposableCameraTraceSourceKind>(Context.EventData.GetValue<uint8>("SourceKind"));
		Frame.RenderedPose.Location.X = Context.EventData.GetValue<double>("LocationX");
		Frame.RenderedPose.Location.Y = Context.EventData.GetValue<double>("LocationY");
		Frame.RenderedPose.Location.Z = Context.EventData.GetValue<double>("LocationZ");
		Frame.RenderedPose.Rotation.Pitch = Context.EventData.GetValue<double>("RotationPitch");
		Frame.RenderedPose.Rotation.Yaw = Context.EventData.GetValue<double>("RotationYaw");
		Frame.RenderedPose.Rotation.Roll = Context.EventData.GetValue<double>("RotationRoll");
		Frame.RenderedPose.FieldOfView = Context.EventData.GetValue<float>("FieldOfView");
		Frame.RenderedPose.ProjectionMode = static_cast<ECameraProjectionMode::Type>(Context.EventData.GetValue<uint8>("ProjectionMode"));
		Frame.RenderedPose.OrthoWidth = Context.EventData.GetValue<float>("OrthoWidth");
		Frame.RenderedPose.OrthoNearClipPlane = Context.EventData.GetValue<float>("OrthoNearClipPlane");
		Frame.RenderedPose.OrthoFarClipPlane = Context.EventData.GetValue<float>("OrthoFarClipPlane");
		Frame.RenderedPose.bConstrainAspectRatio = Context.EventData.GetValue<bool>("bConstrainAspectRatio");
		Provider.AppendActiveFrame(EventTime, MoveTemp(Frame));
		return true;
	}

	if (RouteId == RouteCCSEvaluation)
	{
		FComposableCameraEvaluationTraceFrame Frame;
		Frame.FrameCycle = Cycle;
		Frame.RecordingTime = RecordingTime;
		Frame.WorldId = Context.EventData.GetValue<uint64>("WorldId");
		Frame.SourceObjectId = Context.EventData.GetValue<uint64>("SourceObjectId");
		Frame.OwnerPawnId = Context.EventData.GetValue<uint64>("OwnerPawnId");
		Frame.PlayerControllerId = Context.EventData.GetValue<uint64>("PlayerControllerId");
		Frame.ViewTargetActorId = Context.EventData.GetValue<uint64>("ViewTargetActorId");
		Frame.SourceKind = static_cast<EComposableCameraTraceSourceKind>(Context.EventData.GetValue<uint8>("SourceKind"));
		Frame.ProjectionStatus = static_cast<EComposableCameraTraceProjectionStatus>(Context.EventData.GetValue<uint8>("ProjectionStatus"));
		Frame.CameraTypeAssetName = ReadTraceName(Context.EventData, "CameraTypeAssetName");
		Frame.ContextName = ReadTraceName(Context.EventData, "ContextName");
		Frame.CCSPose.Location.X = Context.EventData.GetValue<double>("LocationX");
		Frame.CCSPose.Location.Y = Context.EventData.GetValue<double>("LocationY");
		Frame.CCSPose.Location.Z = Context.EventData.GetValue<double>("LocationZ");
		Frame.CCSPose.Rotation.Pitch = Context.EventData.GetValue<double>("RotationPitch");
		Frame.CCSPose.Rotation.Yaw = Context.EventData.GetValue<double>("RotationYaw");
		Frame.CCSPose.Rotation.Roll = Context.EventData.GetValue<double>("RotationRoll");
		Frame.CCSPose.FieldOfView = Context.EventData.GetValue<float>("FieldOfView");

		const TArrayView<const uint8> SerializedPrimitives = Context.EventData.GetArrayView<uint8>("SerializedPrimitives");
		TArray<uint8> PrimitiveBytes;
		if (SerializedPrimitives.Num() > 0)
		{
			PrimitiveBytes.Append(SerializedPrimitives.GetData(), SerializedPrimitives.Num());
		}
		DeserializeComposableCameraDebugPrimitives(PrimitiveBytes, Frame.Primitives);

		Provider.AppendEvaluationFrame(EventTime, MoveTemp(Frame));
		return true;
	}

	return false;
}
