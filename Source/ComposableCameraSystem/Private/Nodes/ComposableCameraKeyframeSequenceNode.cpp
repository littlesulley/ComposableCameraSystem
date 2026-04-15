// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraKeyframeSequenceNode.h"

#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlayer.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

void UComposableCameraKeyframeSequenceNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// OnInitialize runs before TickNode's auto-resolve prologue, so we read the
	// pin-backed UPROPERTYs through the fallback-aware GetInputPinValue path
	// (wire / exposed / override / UPROPERTY).
	AActor* InRelativeActor = GetInputPinValue<AActor*>("RelativeActor");
	ULevelSequence* InCameraSequence = GetInputPinValue<ULevelSequence*>("CameraSequence");

	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		if (!IsValid(InRelativeActor))
		{
			return;
		}

		if (USkeletalMeshComponent* Comp = InRelativeActor->GetComponentByClass<USkeletalMeshComponent>())
		{
			SkeletalMeshComponentForRelativeActor = Comp;
		}
	}

	if (InCameraSequence)
	{
		ALevelSequenceActor* LevelSequenceActor = nullptr;
		CameraPlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
			GetWorld(),
			InCameraSequence,
			FMovieSceneSequencePlaybackSettings{},
			LevelSequenceActor
		);

		if (UMovieScene* MovieScene = InCameraSequence->GetMovieScene())
		{
			for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
			{
				TArray<UMovieSceneTrack*> Tracks = Binding.GetTracks();

				for (UMovieSceneTrack* Track : Tracks)
				{
					if (Binding.GetName() == "CameraComponent"
						&& Track->IsA<UMovieSceneFloatTrack>()
						&& Track->GetTrackName() == FName("FieldOfView"))
					{
						TArray<UMovieSceneSection*> Sections = Track->GetAllSections();
						FOVSection = Cast<UMovieSceneFloatSection>(Sections[0]);
						break;
					}

					if (Binding.GetName() == "CineCameraActor"
						&& Track->IsA<UMovieScene3DTransformTrack>()
						&& Track->GetTrackName() == FName("Transform"))
					{
						TArray<UMovieSceneSection*> Sections = Track->GetAllSections();
						TransformSection = Cast<UMovieScene3DTransformSection>(Sections[0]);
						break;
					}
				}
			}
		}
	}

	bValidCameraSequence = InCameraSequence && CameraPlayer && FOVSection && TransformSection;
}

void UComposableCameraKeyframeSequenceNode::OnTickNode_Implementation(float DeltaTime,
	const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	FTransform CurrentRelativeTransform = FTransform::Identity;
	
	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToTransform)
	{
		CurrentRelativeTransform = RelativeTransform;	
	}
	else if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		if (SkeletalMeshComponentForRelativeActor && SkeletalMeshComponentForRelativeActor->DoesSocketExist(RelativeSocket))
		{
			CurrentRelativeTransform = SkeletalMeshComponentForRelativeActor->GetSocketTransform(RelativeSocket);
		}
		else if (RelativeActor)
		{
			CurrentRelativeTransform = RelativeActor->GetActorTransform();	
		}
	}

	if (!bValidCameraSequence)
	{
		OutCameraPose.Position = CurrentRelativeTransform.GetLocation();
		OutCameraPose.Rotation = CurrentRelativeTransform.GetRotation().Rotator();
		return;
	}

	// Convert from display rate to tick resolution.
	FFrameTime TickTime = FFrameRate::TransformTime(
		ElapsedFrames,
		CameraSequence->MovieScene->GetDisplayRate(),
		CameraSequence->MovieScene->GetTickResolution()
	);

	// Get target transform.
	auto [TargetFOV, TargetTransform] = GetTargetTransform(TickTime);

	// Apply fov and transform.
	// Use SetFieldOfViewDegrees so the FocalLength sentinel is cleared and this pose is unambiguously
	// in degrees mode — required so GetEffectiveFieldOfView() returns our value, not a derived focal-length FOV.
	if (TargetFOV != -1.f)
	{
		OutCameraPose.SetFieldOfViewDegrees(TargetFOV);
	}
	OutCameraPose.Position = UKismetMathLibrary::TransformLocation(CurrentRelativeTransform, TargetTransform.GetLocation());
	OutCameraPose.Rotation = UKismetMathLibrary::TransformRotation(CurrentRelativeTransform, TargetTransform.GetRotation().Rotator());

	// Update elasped time.
	ElapsedTime += DeltaTime;
	ElapsedFrames = FFrameTime::FromDecimal(
	      ElapsedTime
		* CameraPlayer->GetFrameRate().AsDecimal()
		* CameraPlayer->GetPlayRate()
	);

	// ElapsedFrames and CameraPlayer->GetFrameDuration() are both in display rate.
	if (ElapsedFrames.FrameNumber >= CameraPlayer->GetFrameDuration())
	{
		ElapsedStayAtLastFrameTime = ElapsedTime - CameraPlayer->GetDuration().AsSeconds();
		if (StayAtLastFrameTime >= 0.f && ElapsedStayAtLastFrameTime >= StayAtLastFrameTime)
		{
			// Keyframe sequence finished. Camera resume must be handled explicitly by the user.
			// TerminateCurrentCamera is deprecated since ParentPendingCamera has been removed.
		}
	}
}

void UComposableCameraKeyframeSequenceNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// Input: CameraSequence
	PinDecl = {};
	PinDecl.PinName = TEXT("CameraSequence");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "CameraSequence", "Camera Sequence");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Object;
	PinDecl.bRequired = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "CameraSequenceTip", "Level sequence asset containing camera keyframes (transform + optional FOV).");
	OutPins.Add(PinDecl);

	// Input: Method — selects whether the reference frame comes from RelativeTransform or RelativeActor.
	PinDecl = {};
	PinDecl.PinName = TEXT("Method");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "Method", "Method");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraRelativeFixedPoseMethod>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(Method)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "MethodTip",
		"Selects whether the sequence plays relative to RelativeTransform or to RelativeActor.");
	OutPins.Add(PinDecl);

	// Input: RelativeTransform
	PinDecl = {};
	PinDecl.PinName = TEXT("RelativeTransform");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "RelativeTransform", "Relative Transform");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Transform;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "RelativeTransformTip", "Base transform when Method is RelativeToTransform.");
	OutPins.Add(PinDecl);

	// Input: RelativeActor
	PinDecl.PinName = TEXT("RelativeActor");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "RelativeActor", "Relative Actor");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "RelativeActorTip", "Reference actor when Method is RelativeToActor.");
	OutPins.Add(PinDecl);

	// Input: RelativeSocket — skeletal-mesh socket on RelativeActor (optional).
	PinDecl = {};
	PinDecl.PinName = TEXT("RelativeSocket");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "RelativeSocket", "Relative Socket");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Name;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = RelativeSocket.ToString();
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "RelativeSocketTip",
		"Skeletal-mesh socket on RelativeActor used as the reference frame. If unresolved, the actor's transform is used instead.");
	OutPins.Add(PinDecl);

	// Input: StayAtLastFrameTime
	PinDecl = {};
	PinDecl.PinName = TEXT("StayAtLastFrameTime");
	PinDecl.DisplayName = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "StayAtLastFrameTime", "Stay At Last Frame Time");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Float;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.Tooltip = NSLOCTEXT("UComposableCameraKeyframeSequenceNode", "StayAtLastFrameTimeTip", "Time to stay at last frame.");
	OutPins.Add(PinDecl);
}


std::pair<float, FTransform> UComposableCameraKeyframeSequenceNode::GetTargetTransform(FFrameTime FrameTime)
{
	float TargetFOV = -1.f;
	FTransform TargetTransform = FTransform::Identity;
	
	if (FOVSection)
	{
		FMovieSceneFloatChannel* Channel = FOVSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>()[0];
		Channel->Evaluate(FrameTime.FrameNumber, TargetFOV);
	}

	if (TransformSection)
	{
		// Channels: Location.X, Location.Y, Location.Z, Rotation.X, Rotation.Y, Rotation.Z, Scale.X, Scale.Y, Scale.Z
		TArrayView<FMovieSceneDoubleChannel*> Channels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		
		FVector Location;
		Channels[0]->Evaluate(FrameTime.FrameNumber, Location.X);
		Channels[1]->Evaluate(FrameTime.FrameNumber, Location.Y);
		Channels[2]->Evaluate(FrameTime.FrameNumber, Location.Z);
        
		FRotator Rotation;
		Channels[3]->Evaluate(FrameTime.FrameNumber, Rotation.Roll);
		Channels[4]->Evaluate(FrameTime.FrameNumber, Rotation.Pitch);
		Channels[5]->Evaluate(FrameTime.FrameNumber, Rotation.Yaw);
        
		FVector Scale;
		Channels[6]->Evaluate(FrameTime.FrameNumber, Scale.X);
		Channels[7]->Evaluate(FrameTime.FrameNumber, Scale.Y);
		Channels[8]->Evaluate(FrameTime.FrameNumber, Scale.Z);
        
		TargetTransform = { Rotation, Location, Scale };
	}

	return { TargetFOV, TargetTransform };
}