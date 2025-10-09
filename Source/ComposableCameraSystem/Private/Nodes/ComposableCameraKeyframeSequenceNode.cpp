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

void UComposableCameraKeyframeSequenceNode::OnBeginPlayNode_Implementation(
	const FComposableCameraPose& CurrentCameraPose)
{
	if (Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor)
	{
		if (!RelativeActor)
		{
			return;
		}
		
		if (USkeletalMeshComponent* Comp = RelativeActor->GetComponentByClass<USkeletalMeshComponent>())
		{
			SkeletalMeshComponentForRelativeActor = Comp;
		}
	}

	if (CameraSequence)
	{
		ALevelSequenceActor* LevelSequenceActor = nullptr;
		CameraPlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
			GetWorld(),
			CameraSequence,
			FMovieSceneSequencePlaybackSettings{},
			LevelSequenceActor
		);

		if (UMovieScene* MovieScene = CameraSequence->GetMovieScene())
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

	bValidCameraSequence = CameraSequence && CameraPlayer && FOVSection && TransformSection;
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
	if (TargetFOV != -1.f)
	{
		OutCameraPose.FieldOfView = TargetFOV;
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
			UComposableCameraBlueprintLibrary::TerminateCurrentCamera(this, OwningPlayerCameraManager, nullptr);
		}
	}
}

void UComposableCameraKeyframeSequenceNode::ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer)
{
	if (UComposableCameraKeyframeSequenceNode* CastedInitializer = Cast<UComposableCameraKeyframeSequenceNode>(Initializer))
	{
		CameraSequence = CastedInitializer->CameraSequence;
		Method = CastedInitializer->Method;
		RelativeTransform = CastedInitializer->RelativeTransform;
		RelativeActor = CastedInitializer->RelativeActor;
		RelativeSocket = CastedInitializer->RelativeSocket;
		StayAtLastFrameTime = CastedInitializer->StayAtLastFrameTime;
	}
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
