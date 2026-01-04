// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraPlayerCamaraManager.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "IAutomationControllerManager.h"
#include "Actions/ComposableCameraActionBase.h"
#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraModifierManager.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Engine/Canvas.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraProjectSettings.h"

class UComposableCameraTransitionBase;

AComposableCameraPlayerCamaraManager::AComposableCameraPlayerCamaraManager(const  FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Director = CreateDefaultSubobject<UComposableCameraDirector>(TEXT("Director"));
	ModifierManager = CreateDefaultSubobject<UComposableCameraModifierManager>(TEXT("ModifierManager"));
}

void AComposableCameraPlayerCamaraManager::BeginPlay()
{
	Super::BeginPlay();
}

void AComposableCameraPlayerCamaraManager::InitializeFor(APlayerController* PlayerController)
{
	Super::InitializeFor(PlayerController);
}

void AComposableCameraPlayerCamaraManager::SetViewTarget(AActor* NewViewTarget,
	FViewTargetTransitionParams TransitionParams)
{
	Super::SetViewTarget(NewViewTarget, TransitionParams);
}

void AComposableCameraPlayerCamaraManager::ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation,
	FRotator& OutDeltaRot)
{
	Super::ProcessViewRotation(DeltaTime, OutViewRotation, OutDeltaRot);
}

void AComposableCameraPlayerCamaraManager::DisplayDebug(class UCanvas* Canvas,
	const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	const FMinimalViewInfo& CurrentPOV = GetCameraCacheView();
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	DisplayDebugManager.SetDrawColor(FColor(122, 122, 255));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   Camera Location: %s, Camera Rotation :%s, FOV: %f"), *CurrentPOV.Location.ToCompactString(), *CurrentPOV.Rotation.ToCompactString(), CurrentPOV.FOV));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("   AspectRatio: %1.3f"), CurrentPOV.AspectRatio));
	BuildModifierDebugString(DisplayDebugManager);
}

AComposableCameraCameraBase* AComposableCameraPlayerCamaraManager::CreateNewCamera(
	TSubclassOf<AComposableCameraCameraBase> CameraClass, const FComposableCameraActivateParams& ActivationParams)
{
	if (CameraClass == nullptr)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class is null."));
		return nullptr;
	}
	if (!IsValid(CameraClass))
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class %s is not valid."),
			*CameraClass->StaticClass()->GetName());
		return nullptr;
	}
	
	AComposableCameraCameraBase* NewCamera = Director->CreateNewCamera(
		this, CameraClass, ActivationParams);
	
	return NewCamera;
}

AComposableCameraCameraBase* AComposableCameraPlayerCamaraManager::ActivateNewCamera(
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* Transition,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent)
{
	if (CameraClass == nullptr)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class is null."));
		return RunningCamera;
	}
	if (!IsValid(CameraClass))
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Camera class %s is not valid."),
			*CameraClass->StaticClass()->GetName());
		return RunningCamera;
	}
	
	if (!Transition || !Transition->Transition)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Transition is not valid. Will use camera cut."));
	}
	
	AComposableCameraCameraBase* NewCamera = Director->ActivateNewCamera(
		this, CameraClass, Transition, ActivationParams, OnPreBeginplayEvent);
	if (NewCamera)
	{
		CurrentNodeInitializerDataAsset = ActivationParams.NodeInitializerDataAsset;
		CurrentOnPreBeginplayEvent = OnPreBeginplayEvent;
		RunningCamera = NewCamera;
		RefreshCameraChain();
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Activating new camera of class %s failed, returning the currently running camera."),
			*CameraClass->StaticClass()->GetName());
	}

	return RunningCamera;
}

AComposableCameraCameraBase* AComposableCameraPlayerCamaraManager::ReactivateCurrentCamera(UComposableCameraTransitionBase* Transition)
{
	TSubclassOf<AComposableCameraCameraBase> CameraClass = RunningCamera->GetClass();
	return Director->ReactivateCurrentCamera(this, CameraClass, Transition, CurrentNodeInitializerDataAsset, CurrentOnPreBeginplayEvent);
}

void AComposableCameraPlayerCamaraManager::AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	ModifierManager->AddModifier(ModifierAsset);
	OnModifierChanged();
}

void AComposableCameraPlayerCamaraManager::RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	ModifierManager->RemoveModifier(ModifierAsset);
	OnModifierChanged();
}

void AComposableCameraPlayerCamaraManager::ApplyModifiers(AComposableCameraCameraBase* Camera, bool bRefreshModifierData)
{
	if (bRefreshModifierData)
	{
		ModifierManager->GetModifierData().UpdateEffectiveModifiers(Camera);
	}

	const auto& Modifiers = ModifierManager->GetModifierData().EffectiveModifiers;
	Camera->ApplyModifiers(Modifiers);
}

void AComposableCameraPlayerCamaraManager::OnModifierChanged()
{
	auto [bChanged, Transition] = ModifierManager->GetModifierData().UpdateEffectiveModifiers(RunningCamera);

	if (bChanged && !RunningCamera->bIsTransient /* Modifiers are only applicable for non-transient cameras. */)
	{
		if (!Transition && RunningCamera->DefaultTransition)
		{
			Transition = DuplicateObject(RunningCamera->DefaultTransition, this);	
		}

		RunningCamera = ReactivateCurrentCamera(Transition);
		RefreshCameraChain();
	}
}

UComposableCameraActionBase* AComposableCameraPlayerCamaraManager::AddCameraAction(
	TSubclassOf<UComposableCameraActionBase> ActionClass, bool bOnlyForCurrentCamera)
{
	if (!ActionClass)
	{
		return nullptr;
	}
	
	UComposableCameraActionBase* Action = NewObject<UComposableCameraActionBase>(this, ActionClass);
	Action->bOnlyForCurrentCamera = bOnlyForCurrentCamera;
	Action->PlayerCameraManager = this;
	CameraActions.Add(Action);

	// Recursively bind delegates, if required.
	if (bOnlyForCurrentCamera)
	{
		if (Action->ExecutionType == EComposableCameraActionExecutionType::PreCameraTick)
		{
			RunningCamera->OnActionPreTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
		}
		else if (Action->ExecutionType == EComposableCameraActionExecutionType::PostCameraTick)
		{
			RunningCamera->OnActionPostTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
		}
	}
	else
	{
		AComposableCameraCameraBase* CurrentCamera = RunningCamera;
		while (CurrentCamera)
		{
			if (Action->ExecutionType == EComposableCameraActionExecutionType::PreCameraTick)
			{
				CurrentCamera->OnActionPreTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
			}
			else if (Action->ExecutionType == EComposableCameraActionExecutionType::PostCameraTick)
			{
				CurrentCamera->OnActionPostTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
			}
			CurrentCamera = CurrentCamera->ParentPendingCamera.Get();
		}
	}

	return Action;
}

UComposableCameraActionBase* AComposableCameraPlayerCamaraManager::FindCameraAction(
	TSubclassOf<UComposableCameraActionBase> ActionClass)
{
	for (UComposableCameraActionBase* Action : CameraActions)
	{
		if (Action && Action->GetClass() == ActionClass)
		{
			return Action;
		}
	}
	return nullptr;
}

void AComposableCameraPlayerCamaraManager::RemoveCameraAction(UComposableCameraActionBase* Action)
{
	// Recursively unbind delegates.
	AComposableCameraCameraBase* CurrentCamera = RunningCamera;
	while (CurrentCamera)
	{
		CurrentCamera->OnActionPreTick.RemoveDynamic(Action, &UComposableCameraActionBase::OnExecute);
		CurrentCamera->OnActionPostTick.RemoveDynamic(Action, &UComposableCameraActionBase::OnExecute);
		CurrentCamera = CurrentCamera->ParentPendingCamera.Get();
	}
}

void AComposableCameraPlayerCamaraManager::ExpireCameraAction(TSubclassOf<UComposableCameraActionBase> ActionClass)
{
	if (auto* Action = FindCameraAction(ActionClass))
	{
		Action->ExpireAction();
	}
}

void AComposableCameraPlayerCamaraManager::BindCameraActionsForNewCamera(AComposableCameraCameraBase* Camera)
{
	for (auto* Action: GetCameraActions())
	{
		if (!Action || Action->bOnlyForCurrentCamera)
		{
			continue;
		}

		switch (Action->ExecutionType)
		{
		case EComposableCameraActionExecutionType::PreCameraTick:
			{
				Camera->OnActionPreTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
				break;
			}
		case EComposableCameraActionExecutionType::PostCameraTick:
			{
				Camera->OnActionPostTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
				break;
			}
		// @TODO: TO BE IMPLEMENTED.
		case EComposableCameraActionExecutionType::PreNodeTick:
		case EComposableCameraActionExecutionType::PostNodeTick:
			{ break; }
		}
	}
}

void AComposableCameraPlayerCamaraManager::ResumeCamera(AComposableCameraCameraBase* ResumeCamera,
                                                        UComposableCameraTransitionBase* Transition, bool bPreserveCameraPose)
{
	FTransform InitialTransform {};
	if (bPreserveCameraPose)
	{
		InitialTransform.SetLocation(GetCameraLocation());
		InitialTransform.SetRotation(GetCameraRotation().Quaternion());
	}
	else
	{
		InitialTransform.SetLocation(ResumeCamera->CameraPose.Position);
		InitialTransform.SetRotation(ResumeCamera->CameraPose.Rotation.Quaternion());
	}
	
	RunningCamera = Director->ResumeCamera(ResumeCamera, Transition, InitialTransform);
}

const TSet<UComposableCameraActionBase*>& AComposableCameraPlayerCamaraManager::GetCameraActions()
{
	return CameraActions;
}

FMinimalViewInfo AComposableCameraPlayerCamaraManager::GetCameraViewFromCameraPose(const FComposableCameraPose& OutPose) const
{
	FMinimalViewInfo DesiredView = GetCameraCacheView();

	DesiredView.Location = OutPose.Position;
	DesiredView.Rotation = OutPose.Rotation;
	DesiredView.FOV = OutPose.FieldOfView;
	DesiredView.DesiredFOV = OutPose.FieldOfView;

	if (RunningCamera)
	{
		UCameraComponent* CameraComponent = RunningCamera->GetCameraComponent();
		DesiredView.AspectRatio = CameraComponent->AspectRatio;
		DesiredView.bConstrainAspectRatio = CameraComponent->bConstrainAspectRatio;
		DesiredView.AspectRatioAxisConstraint = CameraComponent->AspectRatioAxisConstraint;
		DesiredView.ProjectionMode = CameraComponent->ProjectionMode;
		DesiredView.OrthoWidth = CameraComponent->OrthoWidth;
		DesiredView.OrthoNearClipPlane = CameraComponent->OrthoNearClipPlane;
		DesiredView.OrthoFarClipPlane = CameraComponent->OrthoFarClipPlane;
		DesiredView.PostProcessSettings = CameraComponent->PostProcessSettings;
		DesiredView.PostProcessBlendWeight = CameraComponent->PostProcessBlendWeight;
	}
	
	return DesiredView;
}

void AComposableCameraPlayerCamaraManager::DoUpdateCamera(float DeltaTime)
{
	Super::DoUpdateCamera(DeltaTime);

	// Must call FillCameraCache, since the call to Super::DoUpdateCamera will override the true camera view.
	FillCameraCache(LastDesiredView);

	// Update camera actions.
	UpdateActions(DeltaTime);
	
	FComposableCameraPose OutPose = Director->Evaluate(DeltaTime);
	FMinimalViewInfo DesiredView = GetCameraViewFromCameraPose(OutPose);
	CurrentCameraPose = OutPose;

	if (RunningCamera)
	{
		RunningCamera->SetActorLocation(DesiredView.Location);
		RunningCamera->SetActorRotation(DesiredView.Rotation);
		RunningCamera->GetCameraComponent()->FieldOfView = DesiredView.FOV;
	}

	if (bSyncToControlRotation)
	{
		GetOwningPlayerController()->SetControlRotation(DesiredView.Rotation);
	}

	LastDesiredView = DesiredView;
	FillCameraCache(DesiredView);
}

void AComposableCameraPlayerCamaraManager::RefreshCameraChain() const
{
	const UComposableCameraProjectSettings* Settings = GetDefault<UComposableCameraProjectSettings>();
	const int32 MaxCameraChainLength = Settings->MaxCameraChainCleanupDepth;

	int CurrentCameraChainLength = 0;
	AComposableCameraCameraBase* CurrentCamera = RunningCamera;

	while (CurrentCamera->ParentPendingCamera && CurrentCameraChainLength + 1 < MaxCameraChainLength)
	{
		++CurrentCameraChainLength;
		CurrentCamera = CurrentCamera->ParentPendingCamera.Get();
	}

	// Recursively destroy camera above the position.
	CurrentCamera = CurrentCamera->ParentPendingCamera.Get();
	while (CurrentCamera)
	{
		AComposableCameraCameraBase* ParentCamera = CurrentCamera->ParentPendingCamera.Get();
		CurrentCamera->ParentPendingCamera = nullptr;
		CurrentCamera->Destroy();
		CurrentCamera = ParentCamera;
	}
}

void AComposableCameraPlayerCamaraManager::UpdateActions(float DeltaTime)
{
	TSet<UComposableCameraActionBase*> ActionsToRemove;
	for (auto* Action : CameraActions)
	{
		if (!Action)
		{
			ActionsToRemove.Add(Action);
		}

		if (!Action->OnCanExecute(DeltaTime, CurrentCameraPose))
		{
			RemoveCameraAction(Action);
			ActionsToRemove.Add(Action);
		}
	}

	for (auto* Action : ActionsToRemove)
	{
		CameraActions.Remove(Action);
	}
}

void AComposableCameraPlayerCamaraManager::BuildModifierDebugString(FDisplayDebugManager& DisplayDebugManager)
{
	const auto& ModifierDataStruct = ModifierManager->GetModifierData();
	const auto& ModifierData = ModifierDataStruct.ModifierData;
	const auto& EffectiveModifiers = ModifierDataStruct.EffectiveModifiers;
	
	TStringBuilder<512> ModifierDataString;
	TStringBuilder<512> EffectiveModifiersString;
	TStringBuilder<512> Formatter;
	int IndentLevel = 0;
	
	const auto AddText = [&](auto& Builder, const TCHAR* Fmt, ...)
	{
		va_list Args;
		va_start(Args, Fmt);
		{
			Formatter.Reset();
			Formatter.AppendV(Fmt, Args);
			const TCHAR* Message = Formatter.ToString();
			Builder.Append(Message);
		}
		va_end(Args);
	};
	
	const auto GetIndentString = [&]()
	{
		return FString::ChrN(IndentLevel * 8, ' ');
	};
	
	DisplayDebugManager.SetFont(GEngine->GetLargeFont());

	// Build All Modifiers.
	DisplayDebugManager.SetDrawColor(FColor( 100, 20, 100 ));
	DisplayDebugManager.DrawString(FString("\nAll Modifiers"));
	DisplayDebugManager.SetDrawColor(FColor( 222, 100, 5 ));
	
	for (const auto& CameraModifier : ModifierData)
	{
		const auto& CameraTag = CameraModifier.Key;
		const auto& NodeModifierArray = CameraModifier.Value;
		
		// Begin Level 0: CameraTag
		AddText(ModifierDataString, TEXT("%s[Camera Tag] %s:\n"), *GetIndentString(), *CameraTag.ToString());
		
		// Begin Level 1: Node Class
		++IndentLevel;
		for (const auto& NodeModifiers : NodeModifierArray)
		{
			const auto& NodeClass = NodeModifiers.Key;
			const auto& Modifiers = NodeModifiers.Value;
			
			AddText(ModifierDataString, TEXT("%s[Camera Node] %s:\n"), *GetIndentString(), *NodeClass->GetName());
			
			// Begin Level 2: Modifiers
			++IndentLevel;
			for (const auto& Modifier : Modifiers)
			{
				if (Modifier.Asset && Modifier.Modifier)
				{
					AddText(ModifierDataString, TEXT("%s[Modifier] %s from [Asset]%s with priority %d\n"), 
						*GetIndentString(), 
						*Modifier.Modifier->GetName(),
						*Modifier.Asset->GetName(),
						Modifier.Asset->Priority);
				}
			}
			--IndentLevel;
		}
		--IndentLevel;
	}
	
	DisplayDebugManager.DrawString(ModifierDataString.ToString());
	
	// Build Effective Modifiers.
	IndentLevel = 0;
	DisplayDebugManager.SetDrawColor(FColor( 100, 20, 100 ));
	DisplayDebugManager.DrawString(FString("Effective Modifiers"));
	DisplayDebugManager.SetDrawColor(FColor( 222, 100, 5 ));
	
	for (const auto& EffectiveModifier : EffectiveModifiers)
	{
		const auto& NodeClass = EffectiveModifier.Key;
		const auto& Modifier = EffectiveModifier.Value;
		
		AddText(EffectiveModifiersString, TEXT("%s[Camera Node] %s:\n"), *GetIndentString(), *NodeClass->GetName());
			
		++IndentLevel;
		
		if (Modifier.Asset && Modifier.Modifier)
		{
			AddText(EffectiveModifiersString, TEXT("%s[Modifier] %s from [Asset] %s with priority %d\n"), 
				*GetIndentString(), 
				*Modifier.Modifier->GetName(),
				*Modifier.Asset->GetName(),
				Modifier.Asset->Priority);
		}
		
		--IndentLevel;
	}
	
	DisplayDebugManager.DrawString(EffectiveModifiersString.ToString());
}

