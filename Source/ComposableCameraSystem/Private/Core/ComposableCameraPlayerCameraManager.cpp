// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Actions/ComposableCameraActionBase.h"
#include "Camera/CameraComponent.h"
#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraModifierManager.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "DataAssets/ComposableCameraTransitionTableDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Utils/ComposableCameraProjectSettings.h"
#include "Engine/Canvas.h"
#include "Engine/PostProcessUtils.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Nodes/ComposableCameraViewTargetProxyNode.h"
#include "Transitions/ComposableCameraViewTargetTransition.h"
#include "Utils/ComposableCameraDebugFormatUtils.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"

AComposableCameraPlayerCameraManager::AComposableCameraPlayerCameraManager(const  FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ContextStack = CreateDefaultSubobject<UComposableCameraContextStack>(TEXT("ContextStack"));
	ModifierManager = CreateDefaultSubobject<UComposableCameraModifierManager>(TEXT("ModifierManager"));
}

void AComposableCameraPlayerCameraManager::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	AComposableCameraPlayerCameraManager* This = CastChecked<AComposableCameraPlayerCameraManager>(InThis);
	This->PendingParameterBlock.AddReferencedObjects(Collector);
	Super::AddReferencedObjects(InThis, Collector);
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
	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("SetViewTarget called: NewViewTarget='%s'"),
		NewViewTarget ? *NewViewTarget->GetName() : TEXT("(null)"));

	// Guard against re-entrant calls. When we call ActivateNewCamera below,
	// the Director's internal bookkeeping may trigger Super::SetViewTarget,
	// which must not recurse back into implicit activation.
	if (bIsImplicitlyActivating)
	{
		Super::SetViewTarget(NewViewTarget, TransitionParams);
		return;
	}

	// Always pass through to Super with EMPTY transition params — we never
	// want the PCM's built-in PendingViewTarget blend. CCS handles all
	// blending through its own evaluation tree and transition system.
	// This keeps the PCM's ViewTarget in sync for external queries.
	Super::SetViewTarget(NewViewTarget, FViewTargetTransitionParams());

	// Only create a proxy for actors that have a top-level UCameraComponent —
	// one that IS the root component or is a direct child of root. This filters
	// out internal camera components buried inside other components (e.g.,
	// GameplayCameraComponent's OutputCameraComponent, which is a grandchild).
	// Actors without a qualifying camera (e.g. the player pawn) fall through
	// — CCS continues evaluating whatever cameras are already active.
	if (!NewViewTarget)
	{
		return;
	}

	UCameraComponent* ImplicitCameraComp = Cast<UCameraComponent>(NewViewTarget->GetRootComponent());
	if (!ImplicitCameraComp)
	{
		// Check direct children of root.
		USceneComponent* Root = NewViewTarget->GetRootComponent();
		if (Root)
		{
			TArray<USceneComponent*> RootChildren;
			Root->GetChildrenComponents(false, RootChildren);
			for (USceneComponent* Child : RootChildren)
			{
				if (UCameraComponent* Cam = Cast<UCameraComponent>(Child))
				{
					ImplicitCameraComp = Cam;
					break;
				}
			}
		}
	}

	if (!ImplicitCameraComp)
	{
		return;
	}

	// Don't activate if there's no context stack / director yet.
	if (!ContextStack || !ContextStack->GetActiveDirector())
	{
		return;
	}

	TGuardValue<bool> ReentrancyGuard(bIsImplicitlyActivating, true);

	// Create a proxy camera with a ViewTargetProxyNode that reads from the
	// new view target's UCameraComponent each tick. NOT transient — the
	// context lifecycle is managed by whoever pushed it (PlayCutsceneSequence
	// pops explicitly; for arbitrary SetViewTarget, the user returns manually).
	FComposableCameraActivateParams ActivationParams;

	// Build the OnPreBeginplay callback that creates and wires the proxy node.
	// We capture NewViewTarget by weak pointer to guard against the actor being
	// destroyed between now and the callback (which fires synchronously, but
	// defensive coding is cheap here).
	TWeakObjectPtr<AActor> WeakTarget = NewViewTarget;
	FOnCameraFinishConstructed OnPreBeginplay;

	// FOnCameraFinishConstructed is a dynamic delegate — cannot use BindLambda.
	// Instead, store the target and use a UFUNCTION-style callback. But we
	// don't want to add a UFUNCTION for this on the PCM (it's a hot path and
	// the target varies per call). Instead, we create the proxy node inline
	// after ActivateNewCamera returns, since the camera is already spawned
	// at that point and we have access to it.

	// Convert FViewTargetTransitionParams to a CCS transition.
	UComposableCameraTransitionBase* Transition = nullptr;
	if (TransitionParams.BlendTime > 0.f)
	{
		UComposableCameraViewTargetTransition* VTTransition =
			NewObject<UComposableCameraViewTargetTransition>(this);
		VTTransition->InitFromViewTargetParams(TransitionParams);
		Transition = VTTransition;
	}

	// Activate through the current active context's director (same-context activation).
	UComposableCameraDirector* ActiveDirector = ContextStack->GetActiveDirector();
	AComposableCameraCameraBase* ProxyCamera = ActiveDirector->ActivateNewCamera(
		this,
		AComposableCameraCameraBase::StaticClass(),
		Transition,
		ActivationParams,
		FOnCameraFinishConstructed());

	if (!ProxyCamera)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("SetViewTarget: Failed to create proxy camera for '%s'."),
			*NewViewTarget->GetName());
		return;
	}

	// Wire the proxy node to the camera. The camera was spawned with no nodes
	// (default AComposableCameraCameraBase has no nodes). We add a single
	// ViewTargetProxyNode pointing at the target actor.
	ProxyCamera->CameraNodes.Empty();
	ProxyCamera->ComputeNodes.Empty();

	UComposableCameraViewTargetProxyNode* ProxyNode =
		NewObject<UComposableCameraViewTargetProxyNode>(ProxyCamera);
	ProxyNode->SetViewTargetActor(NewViewTarget);
	ProxyCamera->CameraNodes.Add(ProxyNode);
	ProxyCamera->InitializeNodes();

	// Name the camera for debug identification.
	{
		const FName DesiredName(*FString::Printf(TEXT("ViewTargetProxy_%s"),
			*NewViewTarget->GetName()));
		const FName UniqueName = MakeUniqueObjectName(
			ProxyCamera->GetOuter(), ProxyCamera->GetClass(), DesiredName);
		ProxyCamera->Rename(*UniqueName.ToString());
#if WITH_EDITOR
		ProxyCamera->SetActorLabel(FString::Printf(TEXT("ViewTargetProxy_%s"),
			*NewViewTarget->GetName()));
#endif
	}

	RunningCamera = ProxyCamera;

	UE_LOG(LogComposableCameraSystem, Log,
		TEXT("SetViewTarget: Implicitly activated proxy camera for '%s' (BlendTime=%.2fs)."),
		*NewViewTarget->GetName(), TransitionParams.BlendTime);
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

	// Color schema:
	//   Section headers:  purple  FColor(100, 20, 100)  + LargeFont
	//   Content:          orange  FColor(222, 100, 5)
	//   Sub-headers:      teal    FColor(50, 180, 180)
	//   Values:           yellow  FColor(240, 210, 80)

	const auto DrawHeader = [&](const TCHAR* Text)
	{
		DisplayDebugManager.SetFont(GEngine->GetLargeFont());
		DisplayDebugManager.SetDrawColor(FColor(100, 20, 100));
		DisplayDebugManager.DrawString(FString::Printf(TEXT("\n%s"), Text));
	};

	const auto DrawSubHeader = [&](const TCHAR* Text)
	{
		DisplayDebugManager.SetFont(GEngine->GetLargeFont());
		DisplayDebugManager.SetDrawColor(FColor(50, 180, 180));
		DisplayDebugManager.DrawString(Text);
	};

	// ========== Camera Pose ==========
	DrawHeader(TEXT("Camera Pose"));
	DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    Position:  %s"), *CurrentCameraPose.Position.ToCompactString()));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    Rotation:  %s"), *CurrentCameraPose.Rotation.ToCompactString()));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    FOV:       %.1f"), CurrentCameraPose.GetEffectiveFieldOfView()));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    Aspect:    %.3f"), CurrentPOV.AspectRatio));

	// ========== Running Camera ==========
	{
		const UComposableCameraTypeAsset* TypeAsset = RunningCamera ? RunningCamera->SourceTypeAsset.Get() : nullptr;
		const FString CameraDisplayName = TypeAsset
			? TypeAsset->GetName()
			: (RunningCamera && RunningCamera->CameraTag.IsValid()
				? RunningCamera->CameraTag.ToString()
				: TEXT("(unknown)"));
		DrawHeader(*FString::Printf(TEXT("Running Camera: %s"), *CameraDisplayName));
	}

	if (RunningCamera)
	{
		const UComposableCameraTypeAsset* TypeAsset = RunningCamera->SourceTypeAsset.Get();

		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		DisplayDebugManager.DrawString(FString::Printf(TEXT("    Tag:  %s"),
			RunningCamera->CameraTag.IsValid() ? *RunningCamera->CameraTag.ToString() : TEXT("(none)")));

		if (RunningCamera->IsTransient())
		{
			DisplayDebugManager.DrawString(FString::Printf(TEXT("    Life: %.2f / %.2fs remaining"),
				RunningCamera->GetRemainingLifeTime(), RunningCamera->GetLifeTime()));
		}

		// ---- Camera Nodes ----
		const int32 ActiveNodeCount = [&]()
		{
			int32 Count = 0;
			for (const UComposableCameraCameraNodeBase* Node : RunningCamera->CameraNodes)
			{
				if (Node) ++Count;
			}
			return Count;
		}();

		DrawSubHeader(*FString::Printf(TEXT("  Camera Nodes (%d)"), ActiveNodeCount));
		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		for (int32 i = 0; i < RunningCamera->CameraNodes.Num(); ++i)
		{
			if (const UComposableCameraCameraNodeBase* Node = RunningCamera->CameraNodes[i])
			{
				DisplayDebugManager.DrawString(FString::Printf(TEXT("    [%2d] %s"), i,
					*Node->GetClass()->GetDisplayNameText().ToString()));
			}
		}

		// ---- Compute Nodes ----
		const int32 ActiveComputeCount = [&]()
		{
			int32 Count = 0;
			for (const UComposableCameraComputeNodeBase* Node : RunningCamera->ComputeNodes)
			{
				if (Node) ++Count;
			}
			return Count;
		}();

		if (ActiveComputeCount > 0)
		{
			DrawSubHeader(*FString::Printf(TEXT("  Compute Nodes (%d)"), ActiveComputeCount));
			DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
			for (int32 i = 0; i < RunningCamera->ComputeNodes.Num(); ++i)
			{
				if (const UComposableCameraComputeNodeBase* Node = RunningCamera->ComputeNodes[i])
				{
					DisplayDebugManager.DrawString(FString::Printf(TEXT("    [%2d] %s"), i,
						*Node->GetClass()->GetDisplayNameText().ToString()));
				}
			}
		}

		// ---- Exposed Parameters ----
		if (TypeAsset && RunningCamera->OwnedRuntimeDataBlock && RunningCamera->OwnedRuntimeDataBlock->IsValid())
		{
			const FComposableCameraRuntimeDataBlock& DataBlock = *RunningCamera->OwnedRuntimeDataBlock;
			const TArray<FComposableCameraExposedParameter>& Params = TypeAsset->GetExposedParameters();

			if (Params.Num() > 0)
			{
				DrawSubHeader(*FString::Printf(TEXT("  Exposed Parameters (%d)"), Params.Num()));
				DisplayDebugManager.SetDrawColor(FColor(240, 210, 80));
				TStringBuilder<192> Line;
				for (const FComposableCameraExposedParameter& Param : Params)
				{
					Line.Reset();
					Line.Appendf(TEXT("    %-24s = "), *Param.ParameterName.ToString());
					if (const int32* Offset = DataBlock.ExposedParameterOffsets.Find(Param.ParameterName))
					{
						ComposableCameraDebug::AppendTypedValue(Line, DataBlock, *Offset, Param.PinType, Param.EnumType);
					}
					else
					{
						Line.Append(TEXT("(unresolved)"));
					}
					DisplayDebugManager.DrawString(FString(Line));
				}
			}
		}

		// ---- Internal & Exposed Variables ----
		if (TypeAsset && RunningCamera->OwnedRuntimeDataBlock && RunningCamera->OwnedRuntimeDataBlock->IsValid())
		{
			const FComposableCameraRuntimeDataBlock& DataBlock = *RunningCamera->OwnedRuntimeDataBlock;

			// Build name→(type, enum) map. EnumType is meaningful only for the
			// Enum pin type but keeping it in the same lookup avoids a second
			// pass over the variable arrays per debug frame.
			struct FVarTypeInfo { EComposableCameraPinType PinType; const UEnum* EnumType; };
			TMap<FName, FVarTypeInfo> VarTypes;
			for (const FComposableCameraInternalVariable& Var : TypeAsset->InternalVariables)
			{
				VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
			}
			for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
			{
				VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
			}

			if (DataBlock.InternalVariableOffsets.Num() > 0)
			{
				DrawSubHeader(*FString::Printf(TEXT("  Variables (%d)"), DataBlock.InternalVariableOffsets.Num()));
				DisplayDebugManager.SetDrawColor(FColor(240, 210, 80));
				TStringBuilder<192> Line;
				for (const auto& Pair : DataBlock.InternalVariableOffsets)
				{
					EComposableCameraPinType VarType = EComposableCameraPinType::Float;
					const UEnum* VarEnum = nullptr;
					if (const FVarTypeInfo* Found = VarTypes.Find(Pair.Key))
					{
						VarType = Found->PinType;
						VarEnum = Found->EnumType;
					}
					Line.Reset();
					Line.Appendf(TEXT("    %-24s = "), *Pair.Key.ToString());
					ComposableCameraDebug::AppendTypedValue(Line, DataBlock, Pair.Value, VarType, VarEnum);
					DisplayDebugManager.DrawString(FString(Line));
				}
			}
		}
	}
	else
	{
		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));
		DisplayDebugManager.DrawString(TEXT("    (none)"));
	}

	// ========== Context Stack & Evaluation Tree ==========
	DrawHeader(TEXT("Context Stack & Evaluation Tree"));

	if (ContextStack)
	{
		FComposableCameraContextStackSnapshot Snapshot;
		ContextStack->BuildDebugSnapshot(Snapshot);

		DisplayDebugManager.SetDrawColor(FColor(222, 100, 5));

		TStringBuilder<192> Line;

		Line.Reset();
		Line.Appendf(TEXT("    Context Stack (depth: %d, pending destroy: %d)"),
			Snapshot.LiveStackDepth, Snapshot.PendingDestroyCount);
		DisplayDebugManager.DrawString(FString(Line));

		// Walk live contexts first (emitted top → base by BuildDebugSnapshot),
		// then pending-destroy ones. The `bIsPendingDestroy` flag distinguishes
		// them so we can render the "Pending Destroy:" sub-header once.
		bool bPendingHeaderEmitted = false;
		for (int32 LiveIdx = Snapshot.LiveStackDepth - 1, Ci = 0;
		     Ci < Snapshot.Contexts.Num(); ++Ci)
		{
			const FComposableCameraContextSnapshot& Context = Snapshot.Contexts[Ci];

			if (Context.bIsPendingDestroy)
			{
				if (!bPendingHeaderEmitted)
				{
					DisplayDebugManager.DrawString(TEXT("    Pending Destroy:"));
					bPendingHeaderEmitted = true;
				}
				Line.Reset();
				Line.Append(TEXT("       [pending] "));
				Context.ContextName.AppendString(Line);
				DisplayDebugManager.DrawString(FString(Line));
				continue;
			}

			// Live context header line. LiveIdx decrements as we walk top → base,
			// so it matches the old `[N]` index labels.
			Line.Reset();
			Line.Append(Context.bIsActive ? TEXT("    -> [") : TEXT("       ["));
			Line.Appendf(TEXT("%d] "), LiveIdx--);
			Context.ContextName.AppendString(Line);
			if (Context.bIsBase) { Line.Append(TEXT(" [base]")); }
			DisplayDebugManager.DrawString(FString(Line));

			// Director body — running camera + last pose + tree.
			Line.Reset();
			Line.Append(TEXT("        Running Camera: "));
			Line.Append(Context.RunningCameraDisplay);
			DisplayDebugManager.DrawString(FString(Line));

			Line.Reset();
			Line.Append(TEXT("        Last Pose: "));
			Line.Append(Context.LastPose.Position.ToCompactString());
			Line.Append(TEXT("  Rot: "));
			Line.Append(Context.LastPose.Rotation.ToCompactString());
			Line.Appendf(TEXT("  FOV: %.1f"), Context.LastPose.GetEffectiveFieldOfView());
			DisplayDebugManager.DrawString(FString(Line));

			DisplayDebugManager.DrawString(TEXT("        Evaluation Tree:"));

			if (Context.TreeNodes.Num() == 0)
			{
				DisplayDebugManager.DrawString(TEXT("            (empty tree)"));
			}
			else
			{
				for (const FComposableCameraTreeNodeSnapshot& TreeNode : Context.TreeNodes)
				{
					Line.Reset();
					ComposableCameraDebug::AppendTreeNodeLine(Line, TreeNode, /*BaseIndentCols=*/12);
					DisplayDebugManager.DrawString(FString(Line));
				}
			}
		}
	}

	// ========== Camera Actions ==========
	DrawHeader(TEXT("Camera Actions"));

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
		UComposableCameraTransitionBase* ActivatingTransition = nullptr;
		NewCamera = TargetDirector->ActivateNewCameraWithReferenceSource(
			this, CameraClass, Transition, ActivationParams, OnPreBeginplayEvent, PreviousActiveDirector,
			&ActivatingTransition);

		// Any transient context that got demoted from the top by EnsureContext
		// (e.g. we're switching back to an existing context below a transient
		// one) needs an implicit pop — its auto-pop loop is top-only, so
		// without this step it would be stranded on the stack.
		ContextStack->DemoteNonTopTransientContextsToPending(ActivatingTransition);
	}
	else
	{
		// Same-context activation — normal path.
		NewCamera = TargetDirector->ActivateNewCamera(
			this, CameraClass, Transition, ActivationParams, OnPreBeginplayEvent);
	}

	if (NewCamera)
	{
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

	// For type-asset cameras, restore the pending state so OnTypeAssetCameraConstructed
	// (fired via CurrentOnPreBeginplayEvent inside Director::ReactivateCurrentCamera)
	// can fully reconstruct the new camera from the same source asset and parameters.
	// Without this, PendingTypeAsset is null (cleared after the original activation),
	// and OnTypeAssetCameraConstructed early-returns — producing an empty camera with
	// no nodes, no data block, and no exec chains.
	if (UComposableCameraTypeAsset* TypeAsset = RunningCamera->SourceTypeAsset.Get())
	{
		PendingTypeAsset = TypeAsset;
		PendingParameterBlock = RunningCamera->SourceParameterBlock;
	}

	TSubclassOf<AComposableCameraCameraBase> CameraClass = RunningCamera->GetClass();
	return ContextStack->GetActiveDirector()->ReactivateCurrentCamera(this, CameraClass, Transition, CurrentOnPreBeginplayEvent);
}

AComposableCameraCameraBase* AComposableCameraPlayerCameraManager::ActivateNewCamera(
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	UComposableCameraTransitionBase* TransitionInstance,
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

	if (!TransitionInstance)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT(
			"Transition instance is not valid. Will use camera cut."));
	}

	// Determine the target Director (same logic as the DataAsset overload).
	UComposableCameraDirector* PreviousActiveDirector = ContextStack->GetActiveDirector();
	UComposableCameraDirector* TargetDirector = PreviousActiveDirector;
	bool bContextSwitched = false;

	if (ContextName != NAME_None)
	{
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
			bContextSwitched = (TargetDirector != PreviousActiveDirector)
				&& (TargetDirector == ContextStack->GetActiveDirector());
		}
	}

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

	// Activate the camera on the target Director using the raw transition instance.
	AComposableCameraCameraBase* NewCamera = nullptr;

	if (bContextSwitched && PreviousActiveDirector)
	{
		// Inter-context activation with raw transition instance.
		UComposableCameraTransitionBase* ActivatingTransition = nullptr;
		NewCamera = TargetDirector->ActivateNewCameraWithReferenceSource(
			this, CameraClass, TransitionInstance, ActivationParams, OnPreBeginplayEvent, PreviousActiveDirector,
			&ActivatingTransition);

		// See DesignDoc §17 invariant #27 — transient contexts demoted from
		// the top by this activation are implicitly popped, cleanup bound to
		// the activating transition. No-op if no transient context was
		// demoted or if no blend (cut path destroys immediately).
		ContextStack->DemoteNonTopTransientContextsToPending(ActivatingTransition);
	}
	else
	{
		// Same-context activation.
		NewCamera = TargetDirector->ActivateNewCamera(
			this, CameraClass, TransitionInstance, ActivationParams, OnPreBeginplayEvent);
	}

	if (NewCamera)
	{
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

AComposableCameraCameraBase* AComposableCameraPlayerCameraManager::ActivateNewCameraFromTypeAsset(
	UComposableCameraTypeAsset* CameraTypeAsset,
	UComposableCameraTransitionDataAsset* TransitionOverride,
	const FComposableCameraActivateParams& ActivationParams,
	const FComposableCameraParameterBlock& Parameters,
	FName ContextName)
{
	if (!CameraTypeAsset)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("ActivateNewCameraFromTypeAsset: CameraTypeAsset is null."));
		return RunningCamera;
	}

	if (CameraTypeAsset->NodeTemplates.Num() == 0)
	{
		UE_LOG(LogComposableCameraSystem, Warning, TEXT("ActivateNewCameraFromTypeAsset: CameraTypeAsset '%s' has no nodes."),
			*CameraTypeAsset->GetName());
		return RunningCamera;
	}

	// Store the type asset and parameters for the dynamic delegate callback.
	// FOnCameraFinishConstructed is a dynamic delegate (DECLARE_DYNAMIC_DELEGATE_OneParam)
	// which doesn't support BindLambda — we use BindDynamic to a UFUNCTION instead.
	PendingTypeAsset = CameraTypeAsset;
	PendingParameterBlock = Parameters;

	FOnCameraFinishConstructed OnPreBeginplay;
	OnPreBeginplay.BindDynamic(this, &AComposableCameraPlayerCameraManager::OnTypeAssetCameraConstructed);

	// Resolve the transition through the five-tier chain:
	//   1. Caller override → 2. Table lookup → 3. Source ExitTransition
	//   → 4. Target EnterTransition → 5. Hard cut
	const UComposableCameraTypeAsset* SourceTypeAsset =
		RunningCamera ? RunningCamera->SourceTypeAsset.Get() : nullptr;
	UComposableCameraTransitionBase* ResolvedTransition =
		ResolveTransition(SourceTypeAsset, CameraTypeAsset, TransitionOverride);

	AComposableCameraCameraBase* NewCamera = ActivateNewCamera(
		AComposableCameraCameraBase::StaticClass(),
		ResolvedTransition,
		ActivationParams,
		OnPreBeginplay,
		ContextName);

	return NewCamera;
}

void AComposableCameraPlayerCameraManager::OnTypeAssetCameraConstructed(AComposableCameraCameraBase* Camera)
{
	// Delegate to the PCM-independent free function that does all the real work
	// (node duplication, data block layout, parameter block application, per-node
	// Initialize dispatch, enter-transition copy). This path is shared with the
	// Level Sequence component path, which calls ConstructCameraFromTypeAsset
	// directly with no PCM involvement at all.
	UComposableCameraTypeAsset* TypeAsset = PendingTypeAsset.Get();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Camera, TypeAsset, PendingParameterBlock);

	// Clear pending state regardless of outcome; if the construct call
	// early-returned due to null inputs we still don't want stale pending state
	// leaking into the next activation.
	PendingTypeAsset = nullptr;
	PendingParameterBlock = FComposableCameraParameterBlock();
}

#if 0
// Previous in-place implementation extracted into UE::ComposableCameras::ConstructCameraFromTypeAsset
// at Source/ComposableCameraSystem/Private/Core/ComposableCameraTypeAssetInstantiator.cpp.
// Body elided below (kept for grep archaeology during the Phase A change; safe to delete after
// the next tag).
void AComposableCameraPlayerCameraManager::OnTypeAssetCameraConstructed_OLD(AComposableCameraCameraBase* Camera)
{
	Camera->CameraNodes.Empty();
	Camera->ComputeNodes.Empty();
	// ... (old body elided — see ConstructCameraFromTypeAsset)
}
#endif

UComposableCameraTransitionBase* AComposableCameraPlayerCameraManager::ResolveTransition(
	const UComposableCameraTypeAsset* SourceTypeAsset,
	const UComposableCameraTypeAsset* TargetTypeAsset,
	UComposableCameraTransitionDataAsset* CallerOverride) const
{
	// Tier 1: caller-supplied override.
	if (CallerOverride && CallerOverride->Transition)
	{
		return CallerOverride->Transition;
	}

	// Tier 2: project-level transition table lookup.
	const UComposableCameraProjectSettings* Settings = GetDefault<UComposableCameraProjectSettings>();
	if (const UComposableCameraTransitionTableDataAsset* Table = Settings->TransitionTable.Get())
	{
		if (UComposableCameraTransitionBase* TableMatch = Table->FindTransition(SourceTypeAsset, TargetTypeAsset))
		{
			return TableMatch;
		}
	}

	// Tier 3: source camera's exit transition.
	if (SourceTypeAsset && SourceTypeAsset->ExitTransition)
	{
		return SourceTypeAsset->ExitTransition;
	}

	// Tier 4: target camera's enter transition (EnterTransition).
	if (TargetTypeAsset && TargetTypeAsset->EnterTransition)
	{
		return TargetTypeAsset->EnterTransition;
	}

	// Tier 5: hard cut.
	return nullptr;
}

FOnCameraFinishConstructed AComposableCameraPlayerCameraManager::PrepareResumeCallback(AComposableCameraCameraBase* Camera)
{
	if (Camera)
	{
		if (UComposableCameraTypeAsset* TypeAsset = Camera->SourceTypeAsset.Get())
		{
			PendingTypeAsset = TypeAsset;
			PendingParameterBlock = Camera->SourceParameterBlock;

			FOnCameraFinishConstructed Callback;
			Callback.BindDynamic(this, &AComposableCameraPlayerCameraManager::OnTypeAssetCameraConstructed);
			return Callback;
		}
	}
	return FOnCameraFinishConstructed{};
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
		if (!Transition && RunningCamera->EnterTransition)
		{
			Transition = DuplicateObject(RunningCamera->EnterTransition, this);
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
	if (RunningCamera)
	{
		switch (Action->ExecutionType)
		{
		case EComposableCameraActionExecutionType::PreCameraTick:
			RunningCamera->OnActionPreTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
			break;
		case EComposableCameraActionExecutionType::PostCameraTick:
			RunningCamera->OnActionPostTick.AddDynamic(Action, &UComposableCameraActionBase::OnExecute);
			break;
		case EComposableCameraActionExecutionType::PreNodeTick:
		case EComposableCameraActionExecutionType::PostNodeTick:
			RunningCamera->RegisterNodeAction(Action);
			break;
		}
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
		RunningCamera->UnregisterNodeAction(Action);
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
		case EComposableCameraActionExecutionType::PreNodeTick:
		case EComposableCameraActionExecutionType::PostNodeTick:
			{
				Camera->RegisterNodeAction(Action);
				break;
			}
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

	// Transform & FOV: pose is authoritative. Resolve FOV to degrees via GetEffectiveFieldOfView()
	// so physical-mode poses (FocalLength-based) and degrees-mode poses (FieldOfView-based) both work,
	// and so the blended-output invariant (FocalLength = -1, FieldOfView in degrees) is respected.
	DesiredView.Location   = OutPose.Position;
	DesiredView.Rotation   = OutPose.Rotation;
	const double EffectiveFOV = OutPose.GetEffectiveFieldOfView();
	DesiredView.FOV        = static_cast<float>(EffectiveFOV);
	DesiredView.DesiredFOV = static_cast<float>(EffectiveFOV);

	// Projection & aspect: pose-authoritative. Nodes can override per-camera, otherwise defaults apply.
	DesiredView.ProjectionMode        = OutPose.ProjectionMode;
	DesiredView.bConstrainAspectRatio = OutPose.ConstrainAspectRatio;
	DesiredView.OrthoWidth            = OutPose.OrthographicWidth;
	DesiredView.OrthoNearClipPlane    = OutPose.OrthoNearClipPlane;
	DesiredView.OrthoFarClipPlane     = OutPose.OrthoFarClipPlane;
	if (OutPose.OverrideAspectRatioAxisConstraint)
	{
		DesiredView.AspectRatioAxisConstraint = OutPose.AspectRatioAxisConstraint;
	}

	if (RunningCamera)
	{
		UCameraComponent* CameraComponent = RunningCamera->GetCameraComponent();
		// AspectRatio value itself is still owned by the component (it's a viewport-shape property,
		// not a per-frame blendable one). Axis-constraint override above takes precedence if set.
		DesiredView.AspectRatio = CameraComponent->AspectRatio;
		if (!OutPose.OverrideAspectRatioAxisConstraint)
		{
			DesiredView.AspectRatioAxisConstraint = CameraComponent->AspectRatioAxisConstraint;
		}
		// Start from the component's designer-authored post-process as the base layer.
		DesiredView.PostProcessSettings    = CameraComponent->PostProcessSettings;
		DesiredView.PostProcessBlendWeight = CameraComponent->PostProcessBlendWeight;
	}

	// Layer 1: Apply pose's post-process settings on top of the component's base.
	// Only properties whose bOverride_* flag is true in the pose are overridden;
	// cameras without a PostProcess node have all overrides off and this is a no-op.
	FPostProcessUtils::OverridePostProcessSettings(DesiredView.PostProcessSettings, OutPose.PostProcessSettings);

	// Layer 2: Apply physical camera settings (DoF, exposure) on top.
	// No-op when PhysicalCameraBlendWeight <= 0, so non-physical cameras pay no cost.
	OutPose.ApplyPhysicalCameraSettings(DesiredView.PostProcessSettings, /*bOverwriteSettings=*/false);

	return DesiredView;
}

DECLARE_CYCLE_STAT(TEXT("PCM DoUpdateCamera"), STAT_CCS_PCM_DoUpdateCamera, STATGROUP_CCS);
DECLARE_CYCLE_STAT(TEXT("PCM UpdateActions"),  STAT_CCS_PCM_UpdateActions,  STATGROUP_CCS);

void AComposableCameraPlayerCameraManager::DoUpdateCamera(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_PCM_DoUpdateCamera);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_PCM_DoUpdateCamera);

	// We still call Super so engine-level PCM bookkeeping (its own modifier stack, ViewTarget
	// plumbing, post-process blendable accumulation) runs, but Super also writes its own view
	// into the camera cache, which we do NOT want to be authoritative. Instead, our
	// ContextStack->Evaluate output is authoritative.
	//
	// The cache is double-filled per frame on purpose:
	//   (1) FillCameraCache(LastDesiredView) here — restores last frame's pose, because
	//       UpdateActions / ContextStack->Evaluate may call GetCameraCacheView() and must
	//       see the prior-frame camera view, not whatever Super just wrote.
	//   (2) FillCameraCache(DesiredView) at the bottom — publishes this frame's evaluated
	//       pose so the engine renders from it.
	// Do not collapse one of these fills away; the intermediate stale-restore is load-bearing.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_PCM_Super_DoUpdateCamera);
		Super::DoUpdateCamera(DeltaTime);
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_PCM_RestorePriorFrameCache);
		FillCameraCache(LastDesiredView);
	}

	// Update camera actions.
	UpdateActions(DeltaTime);

	FComposableCameraPose OutPose = ContextStack->Evaluate(DeltaTime);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CCS_PCM_PostEvaluate);

		// Update RunningCamera and debug state — may change due to context auto-pop.
		RunningCamera = ContextStack->GetRunningCamera();
		CurrentContext = ContextStack->GetActiveContextName();
		FMinimalViewInfo DesiredView = GetCameraViewFromCameraPose(OutPose);
		CurrentCameraPose = OutPose;

		// Actor writeback is intentionally limited to Location / Rotation / FOV — i.e. things an
		// external observer would reasonably query on the camera actor. ProjectionMode, ortho
		// params, and PostProcessSettings are NOT pushed back onto the component: the component
		// holds the designer-authored post-process settings that GetCameraViewFromCameraPose reads
		// each frame, and nodes layer physical-camera/PP modifications transiently per-frame.
		// Writing PP back onto the component would create a feedback loop.
		if (RunningCamera)
		{
			RunningCamera->SetActorLocation(DesiredView.Location);
			RunningCamera->SetActorRotation(DesiredView.Rotation);
			RunningCamera->GetCameraComponent()->FieldOfView = DesiredView.FOV;
		}

		if (bSyncToControlRotation)
		{
			if (APlayerController* PC = GetOwningPlayerController())
			{
				PC->SetControlRotation(DesiredView.Rotation);
			}
		}

		LastDesiredView = DesiredView;
		FillCameraCache(DesiredView);
	}

#if !UE_BUILD_SHIPPING
	// Pose history capture happens AFTER the frame's pose is finalized so
	// the ring buffer holds the exact value the engine renders from, not
	// an intermediate. Gated at compile time — shipping builds strip the
	// ring entirely (see header guard on the members).
	CaptureCurrentFrameToPoseHistory();
#endif
}

void AComposableCameraPlayerCameraManager::UpdateActions(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_PCM_UpdateActions);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_PCM_UpdateActions);

	if (CameraActions.IsEmpty())
	{
		return;
	}

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

void AComposableCameraPlayerCameraManager::GetPoseHistory(TArray<FComposableCameraPoseHistoryEntry>& OutHistory) const
{
	OutHistory.Reset();
#if !UE_BUILD_SHIPPING
	if (PoseHistoryCountUsed == 0)
	{
		return;
	}

	OutHistory.Reserve(PoseHistoryCountUsed);

	// When the ring is partially full (early frames) the oldest entry
	// sits at index 0 and the newest at PoseHistoryCountUsed - 1. When
	// full, the oldest is at PoseHistoryHead (the next-write slot, which
	// overwrote the oldest last tick) and we walk forward wrapping.
	const int32 Start = (PoseHistoryCountUsed == PoseHistoryCapacity)
		? PoseHistoryHead
		: 0;

	for (int32 i = 0; i < PoseHistoryCountUsed; ++i)
	{
		const int32 SrcIdx = (Start + i) % PoseHistoryCapacity;
		OutHistory.Add(PoseHistoryRing[SrcIdx]);
	}
#endif
}

#if !UE_BUILD_SHIPPING

/**
 * Freeze switch for the pose-history ring buffer.
 *
 * When set to 1, `CaptureCurrentFrameToPoseHistory` becomes a no-op —
 * the ring buffer stops receiving new entries, so the Pose History
 * debug panel's sparklines freeze in place. Intended for inspecting
 * the exact shape of a transition after the fact without the curves
 * scrolling away.
 *
 * The ring buffer is NOT cleared when freeze toggles back off. Capture
 * just resumes writing at the current head, which means the first frames
 * after unfreezing are mixed with up-to-2s-old pre-freeze content until
 * the ring rolls over. Acceptable trade-off for a debug tool — clearing
 * would lose any "what led up to this" context the user might want.
 *
 * Lives here (not in the debug-panel cpp) because the gate is applied
 * at the capture site; the panel doesn't need to know about it.
 */
static TAutoConsoleVariable<int32> CVarPoseHistoryFreeze(
	TEXT("CCS.Debug.Panel.PoseHistory.Freeze"),
	0,
	TEXT("Freeze the pose-history ring buffer. 0=capture live (default), 1=stop capturing new entries so the sparkline panel holds its current shape."),
	ECVF_Default);

/** Read-only accessor for the debug panel so it can render a [FROZEN]
 *  indicator without duplicating the CVar declaration. Debug-only,
 *  compiled out in shipping along with the rest of the panel. */
bool AComposableCameraPlayerCameraManager::IsPoseHistoryFrozen()
{
	return CVarPoseHistoryFreeze.GetValueOnGameThread() != 0;
}

void AComposableCameraPlayerCameraManager::CaptureCurrentFrameToPoseHistory()
{
	// Freeze gate: when on, do nothing — the sparkline panel keeps
	// rendering whatever was in the ring at the moment the freeze
	// engaged. See the CVar comment for the resume-from-head rationale.
	if (CVarPoseHistoryFreeze.GetValueOnGameThread() != 0)
	{
		return;
	}

	// Lazy allocation: reserve the full capacity on first call so the
	// steady-state ring never reallocates. We don't pre-reserve in the
	// constructor because that'd pay 6 KB of memory in non-shipping even
	// when the Pose History panel is never shown — pushing it to the
	// first real tick delays it until the camera is actually running.
	if (PoseHistoryRing.Num() == 0)
	{
		PoseHistoryRing.SetNum(PoseHistoryCapacity);
	}

	FComposableCameraPoseHistoryEntry& Slot = PoseHistoryRing[PoseHistoryHead];
	Slot.Position     = CurrentCameraPose.Position;
	Slot.Rotation     = CurrentCameraPose.Rotation;
	Slot.FOVDegrees   = static_cast<float>(CurrentCameraPose.GetEffectiveFieldOfView());
	Slot.GameTime     = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
	Slot.ContextName  = CurrentContext;

	// Advance the head; wrap via modulo.
	PoseHistoryHead = (PoseHistoryHead + 1) % PoseHistoryCapacity;
	// PoseHistoryCountUsed grows until it hits capacity, then stays.
	if (PoseHistoryCountUsed < PoseHistoryCapacity)
	{
		++PoseHistoryCountUsed;
	}
}
#endif // !UE_BUILD_SHIPPING

