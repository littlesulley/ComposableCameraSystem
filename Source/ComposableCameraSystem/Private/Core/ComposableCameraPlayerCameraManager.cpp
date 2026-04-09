// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Actions/ComposableCameraActionBase.h"
#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraContextStack.h"
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

AComposableCameraPlayerCameraManager::AComposableCameraPlayerCameraManager(const  FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ContextStack = CreateDefaultSubobject<UComposableCameraContextStack>(TEXT("ContextStack"));
	ModifierManager = CreateDefaultSubobject<UComposableCameraModifierManager>(TEXT("ModifierManager"));
}

void AComposableCameraPlayerCameraManager::PopCameraContext(
	FName ContextName,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	const FComposableCameraActivateParams& ActivationParams)
{
	ContextStack->PopContext(ContextName, this, TransitionOverride, ActivationParams);
	RunningCamera = ContextStack->GetRunningCamera();
}

void AComposableCameraPlayerCameraManager::TerminateCurrentCamera(
	UComposableCameraTransitionDataAsset* TransitionOverride,
	const FComposableCameraActivateParams& ActivationParams)
{
	ContextStack->PopActiveContext(this, TransitionOverride, ActivationParams);
	RunningCamera = ContextStack->GetRunningCamera();
}

int32 AComposableCameraPlayerCameraManager::GetContextStackDepth() const
{
	return ContextStack ? ContextStack->GetStackDepth() : 0;
}

FName AComposableCameraPlayerCameraManager::GetActiveContextName() const
{
	return ContextStack ? ContextStack->GetActiveContextName() : NAME_None;
}

void AComposableCameraPlayerCameraManager::BeginPlay()
{
	Super::BeginPlay();
}

void AComposableCameraPlayerCameraManager::InitializeFor(APlayerController* PlayerController)
{
	Super::InitializeFor(PlayerController);

	// Push the base context — the first entry in project settings.
	// Done here (not BeginPlay) because InitializeFor runs during PostInitializeComponents,
	// which is before any actor's BeginPlay. This ensures the base context exists when
	// gameplay code calls ActivateCamera in their BeginPlay.
	const UComposableCameraProjectSettings* Settings = GetDefault<UComposableCameraProjectSettings>();
	if (Settings->ContextNames.Num() > 0)
	{
		ContextStack->EnsureContext(this, Settings->ContextNames[0]);
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("No context names in project settings. Define at least one context (the base gameplay context)."));
	}
}

void AComposableCameraPlayerCameraManager::SetViewTarget(AActor* NewViewTarget,
	FViewTargetTransitionParams TransitionParams)
{
	Super::SetViewTarget(NewViewTarget, TransitionParams);
}

void AComposableCameraPlayerCameraManager::ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation,
	FRotator& OutDeltaRot)
{
	Super::ProcessViewRotation(DeltaTime, OutViewRotation, OutDeltaRot);
}

void AComposableCameraPlayerCameraManager::DisplayDebug(class UCanvas* Canvas,
	const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	const FMinimalViewInfo& CurrentPOV = GetCameraCacheView();

	// Color schema (matching modifier section style):
	//   Section headers:  purple  FColor(100, 20, 100)  + LargeFont
	//   Content:          orange  FColor(222, 100, 5)

	// ========== Camera Pose ==========
	DisplayDebugManager.SetFont(GEngine->GetLargeFont());
	DisplayDebugManager.SetDrawColor(FColor(100, 20, 100));
	DisplayDebugManager.DrawString(TEXT("\nCamera Pose"));
	DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    Position:  %s"), *CurrentCameraPose.Position.ToCompactString()));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    Rotation:  %s"), *CurrentCameraPose.Rotation.ToCompactString()));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    FOV:       %.1f"), CurrentCameraPose.FieldOfView));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    Aspect:    %.3f"), CurrentPOV.AspectRatio));

	// ========== Running Camera & Nodes ==========
	DisplayDebugManager.SetDrawColor(FColor(100, 20, 100));
	DisplayDebugManager.DrawString(TEXT("\nRunning Camera"));

	if (RunningCamera)
	{
		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		DisplayDebugManager.DrawString(FString::Printf(TEXT("    Class:     %s"), *RunningCamera->GetClass()->GetName()));
		DisplayDebugManager.DrawString(FString::Printf(TEXT("    Tag:       %s"),
			RunningCamera->CameraTag.IsValid() ? *RunningCamera->CameraTag.ToString() : TEXT("(none)")));

		if (RunningCamera->IsTransient())
		{
			DisplayDebugManager.DrawString(FString::Printf(TEXT("    Transient: %.2f / %.2fs remaining"),
				RunningCamera->GetRemainingLifeTime(), RunningCamera->GetLifeTime()));
		}

		// Nodes
		DisplayDebugManager.SetDrawColor(FColor(100, 20, 100));
		DisplayDebugManager.DrawString(FString::Printf(TEXT("\nCamera Nodes (%d)"), RunningCamera->CameraNodes.Num()));
		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		for (int32 i = 0; i < RunningCamera->CameraNodes.Num(); ++i)
		{
			if (UComposableCameraCameraNodeBase* Node = RunningCamera->CameraNodes[i])
			{
				DisplayDebugManager.DrawString(FString::Printf(TEXT("    [%d] %s"), i, *Node->GetClass()->GetName()));
			}
		}
	}
	else
	{
		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		DisplayDebugManager.DrawString(TEXT("    (none)"));
	}

	// ========== Context Stack & Evaluation Tree ==========
	DisplayDebugManager.SetDrawColor(FColor(100, 20, 100));
	DisplayDebugManager.DrawString(TEXT("\nContext Stack & Evaluation Tree"));

	if (ContextStack)
	{
		TStringBuilder<1024> StackString;
		ContextStack->BuildDebugString(StackString);

		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));

		FString FullString = StackString.ToString();
		TArray<FString> Lines;
		FullString.ParseIntoArrayLines(Lines);

		for (const FString& Line : Lines)
		{
			DisplayDebugManager.DrawString(FString::Printf(TEXT("    %s"), *Line));
		}
	}

	// ========== Camera Actions ==========
	DisplayDebugManager.SetDrawColor(FColor(100, 20, 100));
	DisplayDebugManager.DrawString(TEXT("\nCamera Actions"));

	if (CameraActions.Num() > 0)
	{
		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		for (const UComposableCameraActionBase* Action : CameraActions)
		{
			if (Action)
			{
				DisplayDebugManager.DrawString(FString::Printf(TEXT("    %s %s"),
					*Action->GetClass()->GetName(),
					Action->bOnlyForCurrentCamera ? TEXT("(camera-scoped)") : TEXT("(persistent)")));
			}
		}
	}
	else
	{
		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		DisplayDebugManager.DrawString(TEXT("    (none)"));
	}

	// ========== Modifiers ==========
	BuildModifierDebugString(DisplayDebugManager);
}

AComposableCameraCameraBase* AComposableCameraPlayerCameraManager::CreateNewCamera(
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
	
	AComposableCameraCameraBase* NewCamera = ContextStack->GetActiveDirector()->CreateNewCamera(
		this, CameraClass, ActivationParams);
	
	return NewCamera;
}

AComposableCameraCameraBase* AComposableCameraPlayerCameraManager::ActivateNewCamera(
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionDataAsset* Transition,
	const FComposableCameraActivateParams& ActivationParams,
	FOnCameraFinishConstructed OnPreBeginplayEvent,
	FName ContextName)
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

	// Determine the target Director. If a context name is specified, ensure that context exists.
	// If NAME_None and the stack is empty, fall back to the base context from project settings.
	UComposableCameraDirector* PreviousActiveDirector = ContextStack->GetActiveDirector();
	UComposableCameraDirector* TargetDirector = PreviousActiveDirector;
	bool bContextSwitched = false;

	if (ContextName != NAME_None)
	{
		const FName ActiveName = ContextStack->GetActiveContextName();

		// EnsureContext will auto-push if the context doesn't exist yet, or return the existing Director.
		TargetDirector = ContextStack->EnsureContext(this, ContextName);
		if (!TargetDirector)
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT(
				"Failed to ensure context '%s'. Falling back to active context."),
				*ContextName.ToString());
			TargetDirector = ContextStack->GetActiveDirector();
		}
		else
		{
			// Check if we actually switched contexts (the target became the new active, or we're activating
			// on a non-active context).
			bContextSwitched = (TargetDirector != PreviousActiveDirector)
				&& (TargetDirector == ContextStack->GetActiveDirector());
		}
	}

	// If TargetDirector is still null (e.g. NAME_None on an empty stack), fall back to the
	// base context from project settings so the first activation doesn't crash.
	if (!TargetDirector)
	{
		const UComposableCameraProjectSettings* Settings = GetDefault<UComposableCameraProjectSettings>();
		if (Settings->ContextNames.Num() > 0)
		{
			TargetDirector = ContextStack->EnsureContext(this, Settings->ContextNames[0]);
		}

		if (!TargetDirector)
		{
			UE_LOG(LogComposableCameraSystem, Error, TEXT(
				"No valid context available. Cannot activate camera. "
				"Ensure at least one context name is configured in Project Settings > Composable Camera System."));
			return RunningCamera;
		}
	}

	// Activate the camera on the target Director.
	AComposableCameraCameraBase* NewCamera = nullptr;

	if (bContextSwitched && PreviousActiveDirector)
	{
		// Inter-context activation: route through ActivateNewCameraWithReferenceSource.
		// Whether there's a transition or a camera cut is handled internally —
		// the key distinction is that we're switching contexts and need the reference leaf path.
		NewCamera = TargetDirector->ActivateNewCameraWithReferenceSource(
			this, CameraClass, Transition, ActivationParams, OnPreBeginplayEvent, PreviousActiveDirector);
	}
	else
	{
		// Same-context activation — normal path.
		NewCamera = TargetDirector->ActivateNewCamera(
			this, CameraClass, Transition, ActivationParams, OnPreBeginplayEvent);
	}

	if (NewCamera)
	{
		CurrentNodeInitializerDataAsset = ActivationParams.NodeInitializerDataAsset;
		CurrentOnPreBeginplayEvent = OnPreBeginplayEvent;
		RunningCamera = NewCamera;
	}
	else
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Activating new camera of class %s failed, returning the currently running camera."),
			*CameraClass->StaticClass()->GetName());
	}

	return RunningCamera;
}

AComposableCameraCameraBase* AComposableCameraPlayerCameraManager::ReactivateCurrentCamera(UComposableCameraTransitionBase* Transition)
{
	if (!RunningCamera)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("ReactivateCurrentCamera: no running camera."));
		return nullptr;
	}

	TSubclassOf<AComposableCameraCameraBase> CameraClass = RunningCamera->GetClass();
	return ContextStack->GetActiveDirector()->ReactivateCurrentCamera(this, CameraClass, Transition, CurrentNodeInitializerDataAsset, CurrentOnPreBeginplayEvent);
}

void AComposableCameraPlayerCameraManager::AddModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	ModifierManager->AddModifier(ModifierAsset);
	OnModifierChanged();
}

void AComposableCameraPlayerCameraManager::RemoveModifier(UComposableCameraNodeModifierDataAsset* ModifierAsset)
{
	ModifierManager->RemoveModifier(ModifierAsset);
	OnModifierChanged();
}

void AComposableCameraPlayerCameraManager::ApplyModifiers(AComposableCameraCameraBase* Camera, bool bRefreshModifierData)
{
	if (bRefreshModifierData)
	{
		ModifierManager->GetModifierData().UpdateEffectiveModifiers(Camera);
	}

	const auto& Modifiers = ModifierManager->GetModifierData().EffectiveModifiers;
	Camera->ApplyModifiers(Modifiers);
}

void AComposableCameraPlayerCameraManager::OnModifierChanged()
{
	if (!RunningCamera)
	{
		return;
	}

	auto [bChanged, Transition] = ModifierManager->GetModifierData().UpdateEffectiveModifiers(RunningCamera);

	if (bChanged && !RunningCamera->bIsTransient /* Modifiers are only applicable for non-transient cameras. */)
	{
		if (!Transition && RunningCamera->DefaultTransition)
		{
			Transition = DuplicateObject(RunningCamera->DefaultTransition, this);
		}

		RunningCamera = ReactivateCurrentCamera(Transition);
	}
}

UComposableCameraActionBase* AComposableCameraPlayerCameraManager::AddCameraAction(
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

	// Bind delegates to the running camera.
	if (Action->ExecutionType == EComposableCameraActionExecutionType::PreCameraTick)
	{
		RunningCamera->OnActionPreTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
	}
	else if (Action->ExecutionType == EComposableCameraActionExecutionType::PostCameraTick)
	{
		RunningCamera->OnActionPostTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
	}

	return Action;
}

UComposableCameraActionBase* AComposableCameraPlayerCameraManager::FindCameraAction(
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

void AComposableCameraPlayerCameraManager::RemoveCameraAction(UComposableCameraActionBase* Action)
{
	if (RunningCamera)
	{
		RunningCamera->OnActionPreTick.RemoveDynamic(Action, &UComposableCameraActionBase::OnExecute);
		RunningCamera->OnActionPostTick.RemoveDynamic(Action, &UComposableCameraActionBase::OnExecute);
	}
}

void AComposableCameraPlayerCameraManager::ExpireCameraAction(TSubclassOf<UComposableCameraActionBase> ActionClass)
{
	if (auto* Action = FindCameraAction(ActionClass))
	{
		Action->ExpireAction();
	}
}

void AComposableCameraPlayerCameraManager::BindCameraActionsForNewCamera(AComposableCameraCameraBase* Camera)
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

void AComposableCameraPlayerCameraManager::ResumeCamera(AComposableCameraCameraBase* ResumeCamera, UComposableCameraTransitionBase* Transition,
	EComposableCameraResumeCameraTransformSchema TransformSchema, FTransform SpecifiedTransform, bool bUseSpecifiedRotation)
{
	FTransform InitialTransform {};

	switch (TransformSchema)
	{
	case EComposableCameraResumeCameraTransformSchema::PreserveCurrent:
		InitialTransform.SetLocation(GetCameraLocation());
		InitialTransform.SetRotation(GetCameraRotation().Quaternion());
		break;
	case EComposableCameraResumeCameraTransformSchema::PreserveResumed:
		InitialTransform.SetLocation(ResumeCamera->CameraPose.Position);
		InitialTransform.SetRotation(ResumeCamera->CameraPose.Rotation.Quaternion());
		break;
	case EComposableCameraResumeCameraTransformSchema::Specified:
		InitialTransform = SpecifiedTransform;
		break;
	}

	if (bUseSpecifiedRotation)
	{
		InitialTransform.SetRotation(SpecifiedTransform.GetRotation());
	}
	
	RunningCamera = ContextStack->GetActiveDirector()->ResumeCamera(ResumeCamera, Transition, InitialTransform);
}

const TSet<UComposableCameraActionBase*>& AComposableCameraPlayerCameraManager::GetCameraActions()
{
	return CameraActions;
}

FMinimalViewInfo AComposableCameraPlayerCameraManager::GetCameraViewFromCameraPose(const FComposableCameraPose& OutPose) const
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

void AComposableCameraPlayerCameraManager::DoUpdateCamera(float DeltaTime)
{
	Super::DoUpdateCamera(DeltaTime);

	// Must call FillCameraCache, since the call to Super::DoUpdateCamera will override the true camera view.
	FillCameraCache(LastDesiredView);

	// Update camera actions.
	UpdateActions(DeltaTime);
	
	FComposableCameraPose OutPose = ContextStack->Evaluate(DeltaTime);
	// Update RunningCamera and debug state — may change due to context auto-pop.
	RunningCamera = ContextStack->GetRunningCamera();
	CurrentContext = ContextStack->GetActiveContextName();
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

void AComposableCameraPlayerCameraManager::UpdateActions(float DeltaTime)
{
	TSet<UComposableCameraActionBase*> ActionsToRemove;
	for (auto* Action : CameraActions)
	{
		if (!Action)
		{
			ActionsToRemove.Add(Action);
			continue;
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

void AComposableCameraPlayerCameraManager::BuildModifierDebugString(FDisplayDebugManager& DisplayDebugManager)
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

