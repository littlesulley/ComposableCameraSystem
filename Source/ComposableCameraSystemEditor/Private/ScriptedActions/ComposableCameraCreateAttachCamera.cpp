// Copyright Sulley. All rights reserved.


#include "ScriptedActions/ComposableCameraCreateAttachCamera.h"

#include "EditorUtilityLibrary.h"
#include "LevelEditorSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Subsystems/EditorActorSubsystem.h"

void UComposableCameraCreateAttachCamera::CreateAndAttachCamera()
{
	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	ULevelEditorSubsystem* LevelSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	
	const TArray<AActor*> SelectedActors = UEditorUtilityLibrary::GetSelectionSet();
		
	if (SelectedActors.Num() != 1)
	{
		return;
	}
	
	AActor* SelectedActor = SelectedActors[0];

	FWorldContext* WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
	UWorld* World = WorldContext->World();

	if (!World)
	{
		return;
	}
	
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(),
		FVector{100.f, 0.f, 50.f},
		FRotator{0.f, 180.f, 0.f});
	
	if (Camera)
	{
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		Camera->AttachToActor(SelectedActor, FAttachmentTransformRules::KeepRelativeTransform);

		TArray<AActor*> SetActors { Camera };
		ActorSubsystem->SetSelectedLevelActors(SetActors);
		LevelSubsystem->PilotLevelActor(Camera);
	}
}
