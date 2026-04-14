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
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Utils/ComposableCameraDebugFormatUtils.h"

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
	DisplayDebugManager.DrawString(FString::Printf(TEXT("    FOV:       %.1f"), CurrentCameraPose.FieldOfView));
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
				for (const FComposableCameraExposedParameter& Param : Params)
				{
					FString ValueStr;
					const int32* Offset = DataBlock.ExposedParameterOffsets.Find(Param.ParameterName);
					if (Offset)
					{
						ValueStr = ComposableCameraDebug::FormatTypedValue(DataBlock, *Offset, Param.PinType);
					}
					else
					{
						ValueStr = TEXT("(unresolved)");
					}
					DisplayDebugManager.DrawString(FString::Printf(TEXT("    %-24s = %s"),
						*Param.ParameterName.ToString(), *ValueStr));
				}
			}
		}

		// ---- Internal & Exposed Variables ----
		if (TypeAsset && RunningCamera->OwnedRuntimeDataBlock && RunningCamera->OwnedRuntimeDataBlock->IsValid())
		{
			const FComposableCameraRuntimeDataBlock& DataBlock = *RunningCamera->OwnedRuntimeDataBlock;

			// Build name→type map
			TMap<FName, EComposableCameraPinType> VarTypes;
			for (const FComposableCameraInternalVariable& Var : TypeAsset->InternalVariables)
			{
				VarTypes.Add(Var.VariableName, Var.VariableType);
			}
			for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
			{
				VarTypes.Add(Var.VariableName, Var.VariableType);
			}

			if (DataBlock.InternalVariableOffsets.Num() > 0)
			{
				DrawSubHeader(*FString::Printf(TEXT("  Variables (%d)"), DataBlock.InternalVariableOffsets.Num()));
				DisplayDebugManager.SetDrawColor(FColor(240, 210, 80));
				for (const auto& Pair : DataBlock.InternalVariableOffsets)
				{
					EComposableCameraPinType VarType = EComposableCameraPinType::Float;
					if (const EComposableCameraPinType* Found = VarTypes.Find(Pair.Key))
					{
						VarType = *Found;
					}
					FString ValueStr = ComposableCameraDebug::FormatTypedValue(DataBlock, Pair.Value, VarType);
					DisplayDebugManager.DrawString(FString::Printf(TEXT("    %-24s = %s"),
						*Pair.Key.ToString(), *ValueStr));
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
		NewCamera = TargetDirector->ActivateNewCameraWithReferenceSource(
			this, CameraClass, TransitionInstance, ActivationParams, OnPreBeginplayEvent, PreviousActiveDirector);
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
	UComposableCameraTypeAsset* TypeAsset = PendingTypeAsset.Get();
	if (!Camera || !TypeAsset)
	{
		PendingTypeAsset = nullptr;
		return;
	}

	// Propagate camera-identity fields from the type asset onto the spawned
	// instance so modifiers and context-stack resume logic see the right values.
	Camera->CameraTag = TypeAsset->CameraTag;
	Camera->bDefaultPreserveCameraPose = TypeAsset->bDefaultPreserveCameraPose;

	// Stamp the source type asset and parameter block onto the camera so that
	// ReactivateCurrentCamera (triggered by modifier changes) can fully
	// reconstruct the camera from the same source instead of producing an
	// empty shell. See PCM::ReactivateCurrentCamera for the consumer.
	Camera->SourceTypeAsset = TypeAsset;
	Camera->SourceParameterBlock = PendingParameterBlock;

	// Name the spawned camera actor after the source type asset so it is
	// identifiable in the World Outliner and debug logs. Use
	// MakeUniqueObjectName so a resume-from-pop path (which spawns a new
	// camera while the old one with the same name is still alive during
	// the transition) does not assert on a name collision.
	{
		const FString AssetName = TypeAsset->GetName();
		const FName DesiredName(*FString::Printf(TEXT("Camera_%s"), *AssetName));
		const FName UniqueName = MakeUniqueObjectName(Camera->GetOuter(), Camera->GetClass(), DesiredName);
		Camera->Rename(*UniqueName.ToString());
#if WITH_EDITOR
		Camera->SetActorLabel(FString::Printf(TEXT("Camera_%s"), *AssetName));
#endif
	}

	// Clear any default nodes the camera class may have.
	Camera->CameraNodes.Empty();
	Camera->ComputeNodes.Empty();

	// Build a set of camera-node indices that are actually referenced by the
	// execution chain. Nodes not in this set are orphaned in the graph (no
	// exec-pin connection) and are skipped during duplication to save memory
	// and initialization cost. We still push nullptr at their index so that
	// CameraNodes[i] keeps its 1:1 correspondence with NodeTemplates[i] —
	// the RuntimeDataBlock pin-key offsets and FullExecChain indices depend
	// on that mapping.
	TSet<int32> ConnectedNodeIndices;
	if (TypeAsset->FullExecChain.Num() > 0)
	{
		for (const FComposableCameraExecEntry& Entry : TypeAsset->FullExecChain)
		{
			if (Entry.CameraNodeIndex != INDEX_NONE)
			{
				ConnectedNodeIndices.Add(Entry.CameraNodeIndex);
			}
		}
	}
	else if (TypeAsset->ExecutionOrder.Num() > 0)
	{
		// Legacy fallback: FullExecChain is empty, use the flat ExecutionOrder.
		for (const int32 Idx : TypeAsset->ExecutionOrder)
		{
			ConnectedNodeIndices.Add(Idx);
		}
	}
	// If both are empty (should not happen for type-asset cameras), we
	// duplicate everything — ConnectedNodeIndices stays empty and the
	// bHasExecChain flag below gates the skip logic off.
	const bool bHasExecChain = ConnectedNodeIndices.Num() > 0;

	// Duplicate node templates from the type asset.
	// Reserve the full size up front to maintain index correspondence.
	Camera->CameraNodes.SetNum(TypeAsset->NodeTemplates.Num());
	for (int32 i = 0; i < TypeAsset->NodeTemplates.Num(); ++i)
	{
		UComposableCameraCameraNodeBase* Template = TypeAsset->NodeTemplates[i];
		if (!Template)
		{
			Camera->CameraNodes[i] = nullptr;
			continue;
		}

		// Skip orphaned nodes: present in NodeTemplates but not referenced by
		// the execution chain. The nullptr preserves index correspondence.
		if (bHasExecChain && !ConnectedNodeIndices.Contains(i))
		{
			Camera->CameraNodes[i] = nullptr;
			continue;
		}

		UComposableCameraCameraNodeBase* NodeInstance = DuplicateObject<UComposableCameraCameraNodeBase>(Template, Camera);
		if (!NodeInstance)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ActivateNewCameraFromTypeAsset: Failed to duplicate node template at index %d (%s)."),
				i, *Template->GetClass()->GetName());
			Camera->CameraNodes[i] = nullptr;
			continue;
		}
		Camera->CameraNodes[i] = NodeInstance;
	}

	// Duplicate compute node templates from the type asset.
	//
	// Compute nodes live in their own index space from the data block's point
	// of view — the runtime NodeIndex used for SetRuntimeDataBlock is
	// (NodeTemplates.Num() + ComputeIdx), matching the layout allocated by
	// BuildRuntimeDataLayout. That keeps output pin keys (and per-instance
	// default override keys) unique across chains without the pin key struct
	// needing to discriminate.
	//
	// Same skip-unconnected logic as camera nodes: build a set of referenced
	// compute-node indices from ComputeFullExecChain (or ComputeExecutionOrder
	// as legacy fallback), and push nullptr for orphaned indices.
	TSet<int32> ConnectedComputeNodeIndices;
	if (TypeAsset->ComputeFullExecChain.Num() > 0)
	{
		for (const FComposableCameraExecEntry& Entry : TypeAsset->ComputeFullExecChain)
		{
			if (Entry.CameraNodeIndex != INDEX_NONE)
			{
				ConnectedComputeNodeIndices.Add(Entry.CameraNodeIndex);
			}
		}
	}
	else if (TypeAsset->ComputeExecutionOrder.Num() > 0)
	{
		for (const int32 Idx : TypeAsset->ComputeExecutionOrder)
		{
			ConnectedComputeNodeIndices.Add(Idx);
		}
	}
	const bool bHasComputeExecChain = ConnectedComputeNodeIndices.Num() > 0;

	Camera->ComputeNodes.SetNum(TypeAsset->ComputeNodeTemplates.Num());
	for (int32 i = 0; i < TypeAsset->ComputeNodeTemplates.Num(); ++i)
	{
		UComposableCameraComputeNodeBase* Template = TypeAsset->ComputeNodeTemplates[i];
		if (!Template)
		{
			Camera->ComputeNodes[i] = nullptr;
			continue;
		}

		if (bHasComputeExecChain && !ConnectedComputeNodeIndices.Contains(i))
		{
			Camera->ComputeNodes[i] = nullptr;
			continue;
		}

		UComposableCameraComputeNodeBase* NodeInstance =
			DuplicateObject<UComposableCameraComputeNodeBase>(Template, Camera);
		if (!NodeInstance)
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("ActivateNewCameraFromTypeAsset: Failed to duplicate compute node template at index %d (%s)."),
				i, *Template->GetClass()->GetName());
			Camera->ComputeNodes[i] = nullptr;
			continue;
		}
		Camera->ComputeNodes[i] = NodeInstance;
	}

	// Build the RuntimeDataBlock layout, owned by the camera.
	Camera->OwnedRuntimeDataBlock = MakeUnique<FComposableCameraRuntimeDataBlock>();
	*Camera->OwnedRuntimeDataBlock = TypeAsset->BuildRuntimeDataLayout();

	// Apply caller-provided parameter values.
	TypeAsset->ApplyParameterBlock(*Camera->OwnedRuntimeDataBlock, PendingParameterBlock);

	// Wire the data block to each node instance.
	// Nodes hold raw pointers — they never outlive the camera that owns the block.
	FComposableCameraRuntimeDataBlock* DataBlock = Camera->OwnedRuntimeDataBlock.Get();
	for (int32 i = 0; i < Camera->CameraNodes.Num(); ++i)
	{
		if (Camera->CameraNodes[i])
		{
			Camera->CameraNodes[i]->SetRuntimeDataBlock(DataBlock, i);
		}
	}

	// Wire compute nodes to the same data block, using the offset index space
	// (NodeTemplates.Num() + ComputeIdx). The base index must come from the
	// TypeAsset's author-order array count rather than Camera->CameraNodes.Num()
	// in case the camera-node duplication loop skipped a null template — the
	// data block layout uses TypeAsset->NodeTemplates.Num() as its base, so we
	// match that exactly here.
	const int32 ComputeNodeIndexBase = TypeAsset->NodeTemplates.Num();
	Camera->TypeAssetNodeTemplateCount = ComputeNodeIndexBase;
	for (int32 i = 0; i < Camera->ComputeNodes.Num(); ++i)
	{
		if (Camera->ComputeNodes[i])
		{
			Camera->ComputeNodes[i]->SetRuntimeDataBlock(DataBlock, ComputeNodeIndexBase + i);
		}
	}

	// Copy exec chains from the type asset to the camera for runtime dispatch.
	// FullExecChain and ComputeFullExecChain contain the interleaved
	// CameraNode + SetVariable sequences; TickCamera and BeginPlayCamera walk
	// these at runtime instead of the flat node arrays when they are non-empty.
	Camera->FullExecChain = TypeAsset->FullExecChain;
	Camera->ComputeFullExecChain = TypeAsset->ComputeFullExecChain;

	// Reorder ComputeNodes by ComputeExecutionOrder ONLY when the new
	// ComputeFullExecChain is empty (legacy assets saved before the field
	// existed). When ComputeFullExecChain is present, the exec chain itself
	// drives execution order and its CameraNodeIndex entries reference
	// ComputeNodeTemplates author-order indices — reordering the array
	// would break that correspondence.
	if (Camera->ComputeFullExecChain.Num() == 0
		&& TypeAsset->ComputeExecutionOrder.Num() > 0
		&& Camera->ComputeNodes.Num() > 0)
	{
		TArray<TObjectPtr<UComposableCameraComputeNodeBase>> Reordered;
		Reordered.Reserve(Camera->ComputeNodes.Num());
		for (const int32 AuthorIdx : TypeAsset->ComputeExecutionOrder)
		{
			if (Camera->ComputeNodes.IsValidIndex(AuthorIdx))
			{
				Reordered.Add(Camera->ComputeNodes[AuthorIdx]);
			}
		}
		// Preserve any compute nodes that weren't referenced by the execution
		// order (e.g. orphaned / disconnected in the graph) by appending them
		// at the end.
		for (UComposableCameraComputeNodeBase* Node : Camera->ComputeNodes)
		{
			if (Node && !Reordered.Contains(Node))
			{
				Reordered.Add(Node);
			}
		}
		Camera->ComputeNodes = MoveTemp(Reordered);
	}

	// Run per-node Initialize now that CameraNodes is populated and every node is
	// wired to the RuntimeDataBlock. Director::ActivateNewCamera already called
	// Camera->Initialize earlier in the flow, but at that point CameraNodes was
	// empty (the base class spawned via SpawnActorDeferred has no default nodes),
	// so the per-node init loop was a no-op. Without this second call, every
	// type-asset node silently skipped Node::Initialize — meaning OwningCamera /
	// OwningPlayerCameraManager, the OnInitialize BP event, OnPreTick/OnPostTick
	// delegate wiring all never ran for type-asset cameras. Exposed parameters
	// still flowed through the data block and hid the bug in most cases.
	Camera->InitializeNodes();

	// Copy the type asset's enter transition onto the camera (for resume/pop transitions).
	if (TypeAsset->EnterTransition && !Camera->EnterTransition)
	{
		Camera->EnterTransition = DuplicateObject<UComposableCameraTransitionBase>(
			TypeAsset->EnterTransition, Camera);
	}

	// Clear pending state.
	PendingTypeAsset = nullptr;
	PendingParameterBlock = FComposableCameraParameterBlock();
}

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

