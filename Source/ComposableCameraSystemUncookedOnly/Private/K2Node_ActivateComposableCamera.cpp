// Copyright Sulley. All rights reserved.

#include "K2Node_ActivateComposableCamera.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_TemporaryVariable.h"
#include "KismetCompiler.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Containers/Ticker.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

#include "ComposableCameraEdGraphPinTypeUtils.h"
#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"
#include "Cameras/ComposableCameraCameraBase.h"

#define LOCTEXT_NAMESPACE "K2Node_ActivateComposableCamera"

// Well-Known Pin Names 

const FName UK2Node_ActivateComposableCamera::PN_PlayerIndex(TEXT("PlayerIndex"));
const FName UK2Node_ActivateComposableCamera::PN_CameraTypeAsset(TEXT("CameraTypeAsset"));
const FName UK2Node_ActivateComposableCamera::PN_ContextName(TEXT("ContextName"));
const FName UK2Node_ActivateComposableCamera::PN_TransitionOverride(TEXT("TransitionOverride"));
const FName UK2Node_ActivateComposableCamera::PN_ActivationParams(TEXT("ActivationParams"));
const FName UK2Node_ActivateComposableCamera::PN_ReturnValue(TEXT("ReturnValue"));

// Pin Allocation 

void UK2Node_ActivateComposableCamera::AllocateDefaultPins()
{
	// Exec pins.
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// Player Index.
	UEdGraphPin* PlayerIndexPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, PN_PlayerIndex);
	PlayerIndexPin->DefaultValue = TEXT("0");

	// Camera Type Asset (object reference).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UComposableCameraTypeAsset::StaticClass();
		CreatePin(EGPD_Input, PinType, PN_CameraTypeAsset);
	}

	// Context Name.
	UEdGraphPin* ContextPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, PN_ContextName);
	ContextPin->DefaultValue = TEXT("None");

	// Transition Override (optional object reference).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UComposableCameraTransitionDataAsset::StaticClass();
		CreatePin(EGPD_Input, PinType, PN_TransitionOverride);
	}

	// Activation Params (struct).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = FComposableCameraActivateParams::StaticStruct();
		CreatePin(EGPD_Input, PinType, PN_ActivationParams);
	}

	// Return value (the activated camera).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = AComposableCameraCameraBase::StaticClass();
		CreatePin(EGPD_Output, PinType, PN_ReturnValue);
	}

	// Create dynamic parameter pins from the cached type asset.
	CreateDynamicParameterPins();

	Super::AllocateDefaultPins();
}

// Node Display 

FText UK2Node_ActivateComposableCamera::GetTooltipText() const
{
	if (CachedTypeAsset)
	{
		return FText::Format(LOCTEXT("TooltipWithAsset", "Activate camera from type asset '{0}' with exposed parameters."),
			FText::FromString(CachedTypeAsset->GetName()));
	}
	return LOCTEXT("TooltipGeneric", "Activate a composable camera from a Camera Type Asset with typed parameter pins.");
}

FText UK2Node_ActivateComposableCamera::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedTypeAsset && (TitleType == ENodeTitleType::FullTitle || TitleType == ENodeTitleType::EditableTitle))
	{
		return FText::Format(LOCTEXT("NodeTitleWithAsset", "Activate Camera\n{0}"),
			FText::FromString(CachedTypeAsset->GetName()));
	}
	return LOCTEXT("NodeTitleGeneric", "Activate Camera (Type Asset)");
}

FLinearColor UK2Node_ActivateComposableCamera::GetNodeTitleColor() const
{
	// Teal to match the Camera Type Asset color.
	return FLinearColor::FromSRGBColor(FColor(20, 150, 140));
}

FSlateIcon UK2Node_ActivateComposableCamera::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent");
}

// Menu Actions 

void UK2Node_ActivateComposableCamera::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();

	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		NodeSpawner->DefaultMenuSignature.Category = GetMenuCategory();
		NodeSpawner->DefaultMenuSignature.MenuName =
			LOCTEXT("GenericMenuName", "Activate Camera (Type Asset)");
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_ActivateComposableCamera::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "ComposableCameraSystem|Camera");
}

// Pin Change Handlers 

void UK2Node_ActivateComposableCamera::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	// Skip during active reconstruction: the engine's pin-rewire phase can fire
	// this notification on the freshly-created CameraTypeAsset pin BEFORE the
	// saved DefaultObject has been transferred from OldPins, producing a
	// transient "asset is null" state. Acting on it would wipe UserOverrideNames
	// via OnCameraTypeAssetChanged's else-branch and corrupt the next pin pass.
	if (bIsReconstructing)
	{
		return;
	}

	if (Pin && Pin->PinName == PN_CameraTypeAsset)
	{
		OnCameraTypeAssetChanged();
	}
}

void UK2Node_ActivateComposableCamera::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	// Same rationale as PinDefaultValueChanged: the base reconstruction's link
	// transfer can fire this mid-reconstruction. A nested ReconstructNode in that
	// window would double-allocate pins and drop UserOverrideNames.
	if (bIsReconstructing)
	{
		return;
	}

	if (Pin && Pin->PinName == PN_CameraTypeAsset)
	{
		// If the asset pin has been connected to a variable, we lose the static asset reference.
		// Clear dynamic pins since we can't know the parameters at compile time.
		if (Pin->LinkedTo.Num() > 0)
		{
			CachedTypeAsset = nullptr;
			ReconstructNode();
		}
	}
}

UComposableCameraTypeAsset* UK2Node_ActivateComposableCamera::GetCameraTypeAsset() const
{
	return CachedTypeAsset;
}

void UK2Node_ActivateComposableCamera::OnCameraTypeAssetChanged()
{
	// Read the new asset from the pin's DefaultObject.
	UEdGraphPin* AssetPin = FindPin(PN_CameraTypeAsset);
	UComposableCameraTypeAsset* NewAsset = nullptr;

	if (AssetPin && AssetPin->DefaultObject)
	{
		NewAsset = Cast<UComposableCameraTypeAsset>(AssetPin->DefaultObject);
	}

	if (NewAsset != CachedTypeAsset)
	{
		CachedTypeAsset = NewAsset;

		// Auto-clean orphaned overrides: names that no longer exist on the new
		// asset's exposed parameters/variables are removed immediately. This
		// keeps the node surface in sync with the asset - the old "preserve
		// orphans and clean up manually" model was error-prone.
		if (CachedTypeAsset && UserOverrideNames.Num() > 0)
		{
			TSet<FName> AssetNames;
			AssetNames.Reserve(CachedTypeAsset->ExposedParameters.Num() + CachedTypeAsset->ExposedVariables.Num());
			for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
			{
				AssetNames.Add(Param.ParameterName);
			}
			for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
			{
				AssetNames.Add(Var.VariableName);
			}
			UserOverrideNames.RemoveAll([&AssetNames](FName Name)
			{
				return !AssetNames.Contains(Name);
			});
		}
		else if (!CachedTypeAsset)
		{
			// No asset selected - all overrides are orphans.
			UserOverrideNames.Reset();
		}

		ReconstructNode();

		if (UBlueprint* Blueprint = GetBlueprint())
		{
			if (!Blueprint->bBeingCompiled)
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			}
		}
	}
}

// Lifetime 

void UK2Node_ActivateComposableCamera::PostLoad()
{
	Super::PostLoad();

	// M1 migration: legacy K2 nodes saved before the opt-in override model
	// previously displayed a pin for EVERY exposed parameter and exposed
	// variable on their cached type asset. To preserve that surface on load,
	// populate UserOverrideNames with the full set of non-required exposed
	// names the first time we see a legacy node. Required parameters are
	// omitted because they're force-created regardless.
	if (!bUserOverridesInitialized)
	{
		if (CachedTypeAsset != nullptr)
		{
			InitializeUserOverridesFromCachedAsset();
		}
		bUserOverridesInitialized = true;
		// PostLoad migration intentionally does not dirty the package; the
		// next real edit in the blueprint will save the migrated state.
	}

	SubscribeToAssetChangeDelegate();

	// Auto-recover from baked-in orphan pin state. Background:
	// an earlier reconstruction running with CachedTypeAsset == nullptr (EDL
	// race, or pre-guard corruption) failed to recreate a dynamic pin the
	// user had wired; the engine substituted an orphan placeholder in the
	// new Pins[] array so the wire wouldn't be silently dropped. If the
	// blueprint was saved at that point, the orphan got committed to disk.
	// On every subsequent plain load, the Pins[] array (orphan included) is
	// deserialized as-is and no reconstruction runs - so the per-Reallocate
	// self-heal below never gets a chance to run. The user sees a red
	// "In use pin X no longer exists" warning every cold restart and only
	// a manual Refresh clears it (because Refresh forces reconstruction
	// AFTER the asset has fully PostLoaded, at which point self-heal works).
	//
	// Detect that disk-baked orphan state here and trigger one reconstruction
	// pass. ReallocatePinsDuringReconstruction's self-heal handles the rest:
	// recovers CachedTypeAsset (DefaultObject -> DefaultValue fallback),
	// rebuilds dynamic pins from the now-fully-PostLoaded asset, drops the
	// orphan placeholder. Reconstruction does not dirty the package by
	// itself; the user's next save persists the cleaned layout naturally.
	bool bHasOrphanPin = false;
	for (const UEdGraphPin* Pin: Pins)
	{
		if (Pin && Pin->bOrphanedPin)
		{
			bHasOrphanPin = true;
			break;
		}
	}
	if (bHasOrphanPin)
	{
		// Defer reconstruction to next tick. Calling ReconstructNode directly
		// from PostLoad trips ZenLoader's RF_NeedLoad ensure (Obj.cpp:1314)
		// because reconstruction touches related objects (the blueprint, the
		// CachedTypeAsset, sibling nodes) that may still be in the load batch
		// with RF_NeedLoad set. By the time the ticker fires, the load batch
		// has completed for every object in this PostLoad cascade.
		//
		// Capturing via TWeakObjectPtr keeps GC honest: if the blueprint /
		// node is destroyed before the ticker fires, the lambda no-ops.
		TWeakObjectPtr<UK2Node_ActivateComposableCamera> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float) -> bool
			{
				if (UK2Node_ActivateComposableCamera* StrongThis = WeakThis.Get())
				{
					if (IsValid(StrongThis))
					{
						// ConditionalPostLoad forces the asset's own PostLoad
						// migration (GUID backfill, name de-duplication) to
						// complete before reconstruction reads its arrays.
						if (StrongThis->CachedTypeAsset)
						{
							StrongThis->CachedTypeAsset->ConditionalPostLoad();
						}
						UE_LOG(LogComposableCameraSystem, Warning,
							TEXT("K2 ActivateComposableCamera deferred PostLoad recovery: orphan pin detected; reconstructing now."));
						StrongThis->ReconstructNode();
					}
				}
				return false; // one-shot; do not repeat
			}),
			0.0f);
	}
}

void UK2Node_ActivateComposableCamera::BeginDestroy()
{
	UnsubscribeFromAssetChangeDelegate();
	Super::BeginDestroy();
}

void UK2Node_ActivateComposableCamera::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	// Freshly placed nodes start with an empty UserOverrideNames array and
	// must NOT later be treated as legacy data by PostLoad, so we flip the
	// migration flag immediately. Note that nodes placed via the per-asset
	// spawner in GetMenuActions do get CachedTypeAsset set during
	// CustomizeNodeDelegate, but PostPlacedNewNode still runs afterward with
	// an intentionally empty override set - the author will opt-in from the
	// right-click menu as needed.
	bUserOverridesInitialized = true;

	SubscribeToAssetChangeDelegate();
}

void UK2Node_ActivateComposableCamera::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	// Re-entrancy guard. The engine's Super::ReallocatePinsDuringReconstruction
	// below invokes RewireOldPinsToNewPins, which transfers OldPin default
	// values / link state onto the freshly-created new pins. On the
	// CameraTypeAsset pin this transfer can surface as a PinDefaultValueChanged
	// notification against the new pin while its DefaultObject is still null,
	// and external subscribers (our own OnObjectPropertyChanged handler being
	// one) can also fire during load. Both would call through to a nested
	// ReconstructNode that observes a torn UserOverrideNames state and silently
	// drops user-authored override pins (the symptom: "pin X no longer exists"
	// on the next editor restart). The guard ensures those notifications
	// short-circuit while this pass is in flight.
	TGuardValue<bool> ReconstructionGuard(bIsReconstructing, true);

	// Stale-state self-heal for CachedTypeAsset. Two distinct failure modes
	// land here:
	//
	// 1. Legacy on-disk corruption. Blueprints saved by a build that
	// predates the re-entrancy guard above may have committed
	// CachedTypeAsset == nullptr while the saved CameraTypeAsset pin
	// still carries the right reference - the original bug nulled
	// CachedTypeAsset (via OnCameraTypeAssetChanged's else-branch)
	// without disturbing the pin. Once that desync is on disk no path
	// naturally recovers it, because the only writer of CachedTypeAsset
	// is OnCameraTypeAssetChanged, which only fires from
	// PinDefaultValueChanged, which is not re-issued on plain load.
	//
	// 2. EDL load-order race. Even on a freshly-saved blueprint, the type
	// asset's PostLoad may not have completed by the time blueprint
	// compilation triggers our reconstruction. In that window the K2
	// node's CachedTypeAsset (TObjectPtr) reads as nullptr AND the
	// OldPin's DefaultObject (also a hard reference) reads as nullptr,
	// while OldPin->DefaultValue still carries the serialized asset
	// path string - the pin renders the asset name to the user via
	// that string, so the picker looks fine, but downstream code that
	// consults the pointer sees null. The race is intermittent: across
	// cold restarts the same blueprint+asset pair can land on either
	// side of EDL ordering.
	//
	// Without recovery, both modes drop into CreateDynamicParameterPins with
	// a null asset, skip every dynamic pin, and orphan every author-wired
	// override pin ("In use pin X no longer exists"). Recover here, before
	// any downstream code (the LinkedTo self-heal below, the Super call that
	// runs CreateDynamicParameterPins) consults CachedTypeAsset.
	//
	// Two-tier recovery: prefer OldPin->DefaultObject when the linker has
	// already resolved the pointer, fall back to FSoftObjectPath::TryLoad on
	// OldPin->DefaultValue when EDL hasn't gotten there yet. Synchronous
	// load is acceptable in this position because the K2 node has a hard
	// UPROPERTY dependency on this asset; pulling the remaining preload
	// window forward by one synchronous load is correct, not a workaround.
	if (CachedTypeAsset == nullptr)
	{
		for (const UEdGraphPin* OldPin: OldPins)
		{
			if (!OldPin || OldPin->PinName != PN_CameraTypeAsset)
			{
				continue;
			}

			if (OldPin->DefaultObject)
			{
				CachedTypeAsset = Cast<UComposableCameraTypeAsset>(OldPin->DefaultObject);
				if (CachedTypeAsset)
				{
					break;
				}
			}

			if (!OldPin->DefaultValue.IsEmpty())
			{
				FSoftObjectPath Path(OldPin->DefaultValue);
				UObject* Resolved = Path.ResolveObject();
				if (!Resolved)
				{
					Resolved = Path.TryLoad();
				}
				if (UComposableCameraTypeAsset* Recovered = Cast<UComposableCameraTypeAsset>(Resolved))
				{
					UE_LOG(LogComposableCameraSystem, Verbose,
						TEXT("K2 ActivateComposableCamera: recovered CachedTypeAsset '%s' from OldPin->DefaultValue (DefaultObject was null at reconstruction time -- EDL load-order race)."),
						*Recovered->GetName());
					CachedTypeAsset = Recovered;
					break;
				}
			}
		}

		// Make sure the recovered asset has finished its own PostLoad before
		// CreateDynamicParameterPins reads its ExposedParameters /
		// ExposedVariables arrays. Both DefaultObject and TryLoad paths can
		// hand us an asset whose data has been serialized but whose PostLoad
		// migration (EnsureExposedVariableGuids, DeduplicateExposedNames)
		// hasn't run; reading those arrays mid-migration risks tripping the
		// downstream "name not in cached asset" filter and dropping a pin
		// the author actually wired.
		if (CachedTypeAsset)
		{
			CachedTypeAsset->ConditionalPostLoad();
		}
	}

	// Self-healing: if an old dynamic pin had live connections that the user
	// clearly cared about, make sure its name is in UserOverrideNames before
	// we call through to the base reconstruction (which will trigger
	// AllocateDefaultPins->CreateDynamicParameterPins). This guards against
	// situations where the author's intent-to-override would otherwise be
	// silently lost - for example, a required parameter being made optional
	// on the type asset after the blueprint was saved.
	//
	// Only LinkedTo is considered here; a pin with only a modified default
	// value is not automatically preserved, because the more likely intent
	// of a "default change" on a pin that was about to disappear is already
	// handled by the regular AssetNames diff inside CreateDynamicParameterPins.
	if (CachedTypeAsset != nullptr)
	{
		TSet<FName> AssetNames;
		AssetNames.Reserve(CachedTypeAsset->ExposedParameters.Num() + CachedTypeAsset->ExposedVariables.Num());
		for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
		{
			AssetNames.Add(Param.ParameterName);
		}
		for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
		{
			AssetNames.Add(Var.VariableName);
		}

		for (UEdGraphPin* OldPin: OldPins)
		{
			if (!OldPin || OldPin->Direction != EGPD_Input)
			{
				continue;
			}
			if (OldPin->LinkedTo.Num() == 0)
			{
				continue;
			}
			if (!AssetNames.Contains(OldPin->PinName))
			{
				continue;
			}
			// Skip required parameters - they're force-created and must not
			// pollute UserOverrideNames.
			if (IsNameRequiredParameter(OldPin->PinName))
			{
				continue;
			}
			UserOverrideNames.AddUnique(OldPin->PinName);
		}
	}

	Super::ReallocatePinsDuringReconstruction(OldPins);
}

// Override Set Management 

void UK2Node_ActivateComposableCamera::InitializeUserOverridesFromCachedAsset()
{
	if (!CachedTypeAsset)
	{
		return;
	}

	UserOverrideNames.Reset();

	for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone() || Param.bRequired)
		{
			continue;
		}
		UserOverrideNames.AddUnique(Param.ParameterName);
	}
	for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		UserOverrideNames.AddUnique(Var.VariableName);
	}
}

bool UK2Node_ActivateComposableCamera::IsNameRequiredParameter(FName Name) const
{
	if (!CachedTypeAsset || Name.IsNone())
	{
		return false;
	}
	for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
	{
		if (Param.ParameterName == Name)
		{
			return Param.bRequired;
		}
	}
	return false;
}

bool UK2Node_ActivateComposableCamera::IsNameInCachedAsset(FName Name) const
{
	if (!CachedTypeAsset || Name.IsNone())
	{
		return false;
	}
	for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
	{
		if (Param.ParameterName == Name)
		{
			return true;
		}
	}
	for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
	{
		if (Var.VariableName == Name)
		{
			return true;
		}
	}
	return false;
}

void UK2Node_ActivateComposableCamera::AddOverridePin(FName Name)
{
	if (Name.IsNone() || UserOverrideNames.Contains(Name))
	{
		return;
	}
	// Defensive: don't add a required parameter to the override set. Required
	// parameters are force-created, so doing so would be redundant and would
	// pollute the set with entries that shouldn't be removable by the user.
	if (IsNameRequiredParameter(Name))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("AddOverridePinTransaction", "Add Override Pin"));
	Modify();

	UserOverrideNames.Add(Name);
	ReconstructNode();

	if (UBlueprint* Blueprint = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

void UK2Node_ActivateComposableCamera::RemoveOverridePin(FName Name)
{
	if (Name.IsNone())
	{
		return;
	}
	// Required parameters aren't in UserOverrideNames to begin with; refuse
	// to act on them so a stale menu click doesn't accidentally mutate state.
	if (IsNameRequiredParameter(Name))
	{
		return;
	}
	if (!UserOverrideNames.Contains(Name))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveOverridePinTransaction", "Remove Override Pin"));
	Modify();

	UserOverrideNames.Remove(Name);
	ReconstructNode();

	if (UBlueprint* Blueprint = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

void UK2Node_ActivateComposableCamera::CleanUpOrphanOverrides()
{
	if (!CachedTypeAsset || UserOverrideNames.Num() == 0)
	{
		return;
	}

	TSet<FName> AssetNames;
	AssetNames.Reserve(CachedTypeAsset->ExposedParameters.Num() + CachedTypeAsset->ExposedVariables.Num());
	for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
	{
		AssetNames.Add(Param.ParameterName);
	}
	for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
	{
		AssetNames.Add(Var.VariableName);
	}

	// Dry-run pass so we only open a transaction if there's actual work to do.
	bool bHasOrphans = false;
	for (const FName& Name: UserOverrideNames)
	{
		if (!AssetNames.Contains(Name))
		{
			bHasOrphans = true;
			break;
		}
	}
	if (!bHasOrphans)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("CleanUpOrphanOverridesTransaction", "Clean Up Orphan Overrides"));
	Modify();

	UserOverrideNames.RemoveAll([&AssetNames](FName Name)
	{
		return !AssetNames.Contains(Name);
	});
	ReconstructNode();

	if (UBlueprint* Blueprint = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

// Asset Change Notification 

void UK2Node_ActivateComposableCamera::SubscribeToAssetChangeDelegate()
{
	if (ObjectPropertyChangedHandle.IsValid())
	{
		return;
	}
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UK2Node_ActivateComposableCamera::HandleObjectPropertyChanged);
}

void UK2Node_ActivateComposableCamera::UnsubscribeFromAssetChangeDelegate()
{
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		ObjectPropertyChangedHandle.Reset();
	}
}

void UK2Node_ActivateComposableCamera::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& /*Event*/)
{
	// Fast-reject the vast majority of change events: the multicast delegate
	// fires for every UObject property change in the editor, so filtering
	// early on identity is the only way to keep this cheap.
	if (Object == nullptr || Object != CachedTypeAsset)
	{
		return;
	}

	// Skip while our own ReallocatePinsDuringReconstruction is in flight.
	// OnObjectPropertyChanged can fire during asset load/save side-effects and
	// a nested ReconstructNode from that path would tear the pin state mid-pass.
	if (bIsReconstructing)
	{
		return;
	}

	// Only act if we're actually in a valid graph - during shutdown or
	// teardown, ReconstructNode would either no-op or crash.
	if (!GetGraph() || !GetOuter())
	{
		return;
	}

	// Auto-clean orphaned overrides when the asset's content changes (e.g. an
	// exposed parameter was removed in the graph editor). Same logic as
	// OnCameraTypeAssetChanged but without the asset-swap preamble.
	if (CachedTypeAsset && UserOverrideNames.Num() > 0)
	{
		TSet<FName> AssetNames;
		AssetNames.Reserve(CachedTypeAsset->ExposedParameters.Num() + CachedTypeAsset->ExposedVariables.Num());
		for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
		{
			AssetNames.Add(Param.ParameterName);
		}
		for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
		{
			AssetNames.Add(Var.VariableName);
		}
		UserOverrideNames.RemoveAll([&AssetNames](FName Name)
		{
			return !AssetNames.Contains(Name);
		});
	}

	ReconstructNode();
}

// Context Menu 

void UK2Node_ActivateComposableCamera::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (!Menu || !Context || !CachedTypeAsset)
	{
		return;
	}

	// Actions mutate node state; the menu delegate is const so we have to
	// cast away const to bind UObject-member callbacks.
	UK2Node_ActivateComposableCamera* MutableThis =
		const_cast<UK2Node_ActivateComposableCamera*>(this);

	// Pin context: Remove Override Pin 
	if (Context->Pin != nullptr)
	{
		const FName PinName = Context->Pin->PinName;
		if (!DynamicParameterPinNames.Contains(PinName))
		{
			return;
		}
		if (IsNameRequiredParameter(PinName))
		{
			// Required parameters are always on - nothing to offer here.
			return;
		}

		FToolMenuSection& PinSection = Menu->FindOrAddSection(TEXT("ComposableCameraOverridePin"),
			LOCTEXT("OverridePinSectionLabel", "Composable Camera"));

		PinSection.AddMenuEntry(TEXT("RemoveOverridePin"),
			LOCTEXT("RemoveOverridePinLabel", "Remove Override Pin"),
			LOCTEXT("RemoveOverridePinTooltip",
				"Remove this override pin. The camera will use the value authored on the Camera Type Asset at activation time."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateUObject(MutableThis,
				&UK2Node_ActivateComposableCamera::RemoveOverridePin,
				PinName)));
		return;
	}

	// Node context: Add Override Pin + Clean Up Orphan Overrides 
	if (Context->Node != this)
	{
		return;
	}

	FToolMenuSection& NodeSection = Menu->FindOrAddSection(TEXT("ComposableCameraOverrides"),
		LOCTEXT("OverridesSectionLabel", "Composable Camera"));

	// Partition the asset's exposed names into "addable" buckets. Required
	// parameters are skipped - they're force-created, so there's nothing to
	// add. Names already in UserOverrideNames are also skipped because a pin
	// for them already exists on the node.
	const TSet<FName> OverrideSet(UserOverrideNames);

	TArray<FName> AddableOptionalParams;
	TArray<FName> AddableExposedVariables;

	// Defensive dedup, mirroring CreateDynamicParameterPins. The asset-level
	// uniqueness invariant should already guarantee this, but UToolMenus
	// silently collapses duplicate menu-entry FNames into a single visible
	// row - when a click then routes through AddOverridePin, the symptom is
	// "I clicked one menu item but two pins appeared". Tracking SeenMenuNames
	// across both buckets prevents that.
	TSet<FName> SeenMenuNames;
	SeenMenuNames.Reserve(CachedTypeAsset->ExposedParameters.Num() + CachedTypeAsset->ExposedVariables.Num());

	for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone() || Param.bRequired)
		{
			continue;
		}
		if (OverrideSet.Contains(Param.ParameterName))
		{
			continue;
		}
		if (SeenMenuNames.Contains(Param.ParameterName))
		{
			continue;
		}
		AddableOptionalParams.Add(Param.ParameterName);
		SeenMenuNames.Add(Param.ParameterName);
	}
	for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		if (OverrideSet.Contains(Var.VariableName))
		{
			continue;
		}
		if (SeenMenuNames.Contains(Var.VariableName))
		{
			continue;
		}
		AddableExposedVariables.Add(Var.VariableName);
		SeenMenuNames.Add(Var.VariableName);
	}

	const bool bHasAddables =
		AddableOptionalParams.Num() > 0 || AddableExposedVariables.Num() > 0;

	if (bHasAddables)
	{
		// Capture by value: the lambda can outlive the enclosing stack frame
		// when the submenu is opened asynchronously by the tool-menus system.
		NodeSection.AddSubMenu(TEXT("AddOverridePin"),
			LOCTEXT("AddOverridePinLabel", "Add Override Pin"),
			LOCTEXT("AddOverridePinTooltip",
				"Add a pin that overrides an exposed parameter or exposed variable on the selected Camera Type Asset. Names that are not overridden use the default value authored on the asset."),
			FNewToolMenuDelegate::CreateLambda(
				[MutableThis, AddableOptionalParams, AddableExposedVariables]
				(UToolMenu* SubMenu)
				{
					if (!SubMenu)
					{
						return;
					}

					if (AddableOptionalParams.Num() > 0)
					{
						FToolMenuSection& ParamSection = SubMenu->AddSection(TEXT("AddableExposedParameters"),
							LOCTEXT("AddableExposedParametersLabel", "Exposed Parameters"));
						for (const FName& Name: AddableOptionalParams)
						{
							ParamSection.AddMenuEntry(Name,
								FText::FromName(Name),
								LOCTEXT("AddOverrideParamEntryTooltip",
									"Add an override pin for this exposed parameter."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateUObject(MutableThis,
									&UK2Node_ActivateComposableCamera::AddOverridePin,
									Name)));
						}
					}

					if (AddableExposedVariables.Num() > 0)
					{
						FToolMenuSection& VarSection = SubMenu->AddSection(TEXT("AddableExposedVariables"),
							LOCTEXT("AddableExposedVariablesLabel", "Exposed Variables"));
						for (const FName& Name: AddableExposedVariables)
						{
							VarSection.AddMenuEntry(Name,
								FText::FromName(Name),
								LOCTEXT("AddOverrideVarEntryTooltip",
									"Add an override pin for this exposed variable."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateUObject(MutableThis,
									&UK2Node_ActivateComposableCamera::AddOverridePin,
									Name)));
						}
					}
				}));
	}

	// "Clean Up Orphan Overrides" is only shown when there's actually
	// something to clean - it would be noise otherwise.
	bool bHasOrphans = false;
	for (const FName& Name: UserOverrideNames)
	{
		if (!IsNameInCachedAsset(Name))
		{
			bHasOrphans = true;
			break;
		}
	}
	if (bHasOrphans)
	{
		NodeSection.AddMenuEntry(TEXT("CleanUpOrphanOverrides"),
			LOCTEXT("CleanUpOrphanOverridesLabel", "Clean Up Orphan Overrides"),
			LOCTEXT("CleanUpOrphanOverridesTooltip",
				"Remove override entries that reference parameters or variables no longer present on the selected Camera Type Asset."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateUObject(MutableThis,
				&UK2Node_ActivateComposableCamera::CleanUpOrphanOverrides)));
	}
}

// Compile-Time Validation 

void UK2Node_ActivateComposableCamera::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	if (!CachedTypeAsset)
	{
		// A null asset is already caught by ExpandNode's error path - no need
		// to duplicate the message here.
		return;
	}

	// Orphan detection: surface a warning for every UserOverrideNames entry
	// whose name no longer exists on the cached asset. In normal use, orphans
	// are auto-cleaned by OnCameraTypeAssetChanged / HandleObjectPropertyChanged,
	// so this is a compile-time backstop for edge cases where neither handler
	// ran (e.g. the node was never opened in the editor after an asset edit).
	TSet<FName> AssetNames;
	AssetNames.Reserve(CachedTypeAsset->ExposedParameters.Num() + CachedTypeAsset->ExposedVariables.Num());
	for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
	{
		AssetNames.Add(Param.ParameterName);
	}
	for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
	{
		AssetNames.Add(Var.VariableName);
	}

	for (const FName& Name: UserOverrideNames)
	{
		if (!AssetNames.Contains(Name))
		{
			MessageLog.Warning(
				*FText::Format(LOCTEXT("OrphanOverrideWarning",
						"@@ has an override for '{0}', which no longer exists on Camera Type Asset '{1}'. The override will be ignored and removed on the next node refresh."),
					FText::FromName(Name),
					FText::FromString(CachedTypeAsset->GetName())
				).ToString(),
				this);
		}
	}
}

// Dynamic Pin Management 

void UK2Node_ActivateComposableCamera::RemoveDynamicParameterPins()
{
	for (const FName& PinName: DynamicParameterPinNames)
	{
		if (UEdGraphPin* Pin = FindPin(PinName))
		{
			Pin->BreakAllPinLinks();
			Pins.Remove(Pin);
		}
	}
	DynamicParameterPinNames.Empty();
}

void UK2Node_ActivateComposableCamera::CreateDynamicParameterPins()
{
	RemoveDynamicParameterPins();

	if (!CachedTypeAsset)
	{
		// No type asset means no exposed parameter pins - hide the advanced
		// section entirely so the node doesn't show a dangling expander.
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
		return;
	}

	// Opt-in filtering: a non-required parameter or exposed variable is only
	// materialized as a pin if the author has explicitly added it to
	// UserOverrideNames via the right-click "Add Override Pin" menu. Required
	// exposed parameters are the one exception - they're force-created on
	// every reconstruction because the runtime's ApplyParameterBlock treats a
	// missing required value as a fatal activation error.
	const TSet<FName> OverrideSet(UserOverrideNames);

	// Defensive dedup. The type asset's expose-time guard
	// (UComposableCameraTypeAsset::MakeUniqueExposedName) and PostLoad
	// migration (DeduplicateExposedNames) should already guarantee that no
	// two entries across ExposedParameters ExposedVariables share an FName,
	// but we still track names we've materialized as pins this pass: an
	// in-memory edit that bypasses the guard (e.g. a Python script, a future
	// duplicate-paste path, or a stale asset loaded before the migration
	// landed) would otherwise produce a CreatePin assertion when the second
	// duplicate tries to claim the same pin name. Skipping with a warning
	// keeps the graph compilable in pathological cases.
	TSet<FName> CreatedPinNames;
	CreatedPinNames.Reserve(CachedTypeAsset->ExposedParameters.Num() + CachedTypeAsset->ExposedVariables.Num());

	// Exposed parameters 
	for (const FComposableCameraExposedParameter& Param: CachedTypeAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}

		const bool bShouldCreate = Param.bRequired || OverrideSet.Contains(Param.ParameterName);
		if (!bShouldCreate)
		{
			continue;
		}

		if (CreatedPinNames.Contains(Param.ParameterName))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("[%s] K2 ActivateComposableCamera: skipping duplicate exposed parameter '%s' - another entry already claimed this pin name. Re-save the type asset to let PostLoad rename the duplicate."),
				*CachedTypeAsset->GetName(), *Param.ParameterName.ToString());
			continue;
		}

		FEdGraphPinType PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(Param.PinType, Param.StructType, Param.EnumType, Param.SignatureFunction);
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, PinType, Param.ParameterName);

		// PinFriendlyName precedence:
		// 1. If ParameterName == TargetPinName (the unique-name guard didn't
		// have to suffix), use Param.DisplayName as authored on the camera
		// node - that's the prettiest label and matches the in-graph pin.
		// 2. If ParameterName != TargetPinName, the asset's expose-time guard
		// (UComposableCameraTypeAsset::MakeUniqueExposedName) appended
		// "_2" / "_3" / ... to disambiguate two pins that share an
		// underlying name. Mirror that suffix on the friendly label - 
		// otherwise both K2 pins would render as the same DisplayName
		// ("Strength" / "Strength") and the user couldn't tell which
		// camera-node pin each one routes to.
		// 3. Empty DisplayName falls through to the engine default, which
		// renders the FName (= ParameterName, already unique).
		if (Param.ParameterName != Param.TargetPinName)
		{
			// MakeUniqueExposedName only ever appends to the base name, so
			// the suffix is the trailing portion after TargetPinName. Slice
			// it off rather than parsing the digits - that way any future
			// change to the suffix scheme stays correctly mirrored here
			// without a parallel format string to maintain.
			const FString ParamNameStr = Param.ParameterName.ToString();
			const FString TargetNameStr = Param.TargetPinName.ToString();
			FString SuffixStr;
			if (ParamNameStr.StartsWith(TargetNameStr))
			{
				SuffixStr = ParamNameStr.RightChop(TargetNameStr.Len());
			}
			else
			{
				// Defensive: an asset that was renamed by some other path
				// (manual edit, script) may not match the StartsWith
				// invariant. Fall back to the full unique name so the pin
				// is at least readable.
				SuffixStr.Reset();
			}

			if (!Param.DisplayName.IsEmpty() && !SuffixStr.IsEmpty())
			{
				NewPin->PinFriendlyName = FText::Format(NSLOCTEXT("K2Node_ActivateComposableCamera",
						"ExposedParamFriendlyNameWithSuffix", "{0}{1}"),
					Param.DisplayName,
					FText::FromString(SuffixStr));
			}
			else
			{
				// Either no DisplayName at all, or we couldn't recover the
				// suffix - render the unique ParameterName directly so the
				// user still sees the disambiguator.
				NewPin->PinFriendlyName = FText::FromName(Param.ParameterName);
			}
		}
		else if (!Param.DisplayName.IsEmpty())
		{
			NewPin->PinFriendlyName = Param.DisplayName;
		}

		if (!Param.Tooltip.IsEmpty())
		{
			NewPin->PinToolTip = Param.Tooltip.ToString();
		}

		// Resolve the pin default from the node's actual pin state (class
		// default or per-instance override in NodePinOverrides). For required
		// parameters this default is still set on the pin so the author can
		// see the "suggested" value at a glance, but the runtime
		// ApplyParameterBlock will still reject the activation if the caller
		// doesn't supply one - required means required.
		// Delegate pins have no textual default - they are bound at activation
		// time, not authored as a string. Skip the default-value resolution to
		// avoid showing a stale text box on the K2 node.
		if (Param.PinType != EComposableCameraPinType::Delegate)
		{
			const FString PinDefault = CachedTypeAsset->GetExposedParameterDefaultValue(Param);
			if (!PinDefault.IsEmpty())
			{
				NewPin->DefaultValue = PinDefault;
			}
		}

		// Required pins are always visible (non-advanced) because they're
		// mandatory; optional opt-in pins live under the advanced-pins caret
		// so the node stays compact when the author has added several of them.
		NewPin->bAdvancedView = !Param.bRequired;

		DynamicParameterPinNames.Add(Param.ParameterName);
		CreatedPinNames.Add(Param.ParameterName);
	}

	// Exposed variables 
	//
	// Exposed variables have no "required" concept - their InitialValueString
	// is a fully-valid runtime fallback, so a non-overridden exposed variable
	// simply doesn't appear on the node. When the author does add one, the
	// pin behaves identically to an opted-in optional exposed parameter:
	// ExpandNode routes its value through SetParameterBlockValue by name, and
	// the runtime's ApplyParameterBlock copies it into the variable slot at
	// activation.
	for (const FComposableCameraInternalVariable& Var: CachedTypeAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}

		if (!OverrideSet.Contains(Var.VariableName))
		{
			continue;
		}

		// Same defensive dedup as the exposed-parameter loop above. The
		// uniqueness invariant is enforced across BOTH collections, so a
		// variable that collides with an already-materialized parameter
		// pin name (or with a previous duplicate variable) is also skipped.
		if (CreatedPinNames.Contains(Var.VariableName))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("[%s] K2 ActivateComposableCamera: skipping duplicate exposed variable '%s' - another entry already claimed this pin name. Re-save the type asset to let PostLoad rename the duplicate."),
				*CachedTypeAsset->GetName(), *Var.VariableName.ToString());
			continue;
		}

		FEdGraphPinType PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(Var.VariableType, Var.StructType, Var.EnumType);
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, PinType, Var.VariableName);

		// FComposableCameraInternalVariable has no DisplayName field - use
		// the variable name directly. If a variable needs a prettier label
		// in the future, add a DisplayName field to the struct and the K2
		// node will pick it up the same way ExposedParameter does.

		if (!Var.Tooltip.IsEmpty())
		{
			NewPin->PinToolTip = Var.Tooltip.ToString();
		}

		// Source the pin default from InitialValueString - the exposed
		// variable's author-time initial value. This mirrors the exposed
		// parameter's node-pin-default behavior: an unwired pin still
		// carries the authored default into the ParameterBlock, so the
		// runtime's activation-path fallback to InitialValueString is only
		// taken when the caller explicitly clears the pin.
		if (!Var.InitialValueString.IsEmpty())
		{
			NewPin->DefaultValue = Var.InitialValueString;
		}

		// Same advanced-view rationale as opt-in exposed parameters - these
		// pins are opt-in overrides, not first-class inputs.
		NewPin->bAdvancedView = true;

		DynamicParameterPinNames.Add(Var.VariableName);
		CreatedPinNames.Add(Var.VariableName);
	}

	// Decide how the advanced section presents on this node. The caret is
	// only meaningful if at least one pin is actually advanced - a node that
	// has only required-parameter pins (all non-advanced) should not show an
	// empty caret, and a node with no dynamic pins at all should show none
	// either.
	//
	// AdvancedPinDisplay is a serialized UPROPERTY on UEdGraphNode, so any
	// user-chosen Shown/Hidden state persists across reconstructions. We
	// only flip the default once - when transitioning from NoPins (no
	// advanced pins) to having at least one - so switching type assets
	// preserves the author's expand/collapse preference.
	int32 NumAdvancedPins = 0;
	for (const FName& PinName: DynamicParameterPinNames)
	{
		if (const UEdGraphPin* Pin = FindPin(PinName))
		{
			if (Pin->bAdvancedView)
			{
				++NumAdvancedPins;
			}
		}
	}

	if (NumAdvancedPins == 0)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
	}
	else if (AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
	}
}

// ExpandNode 

void UK2Node_ActivateComposableCamera::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	// Validate.
	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = FindPinChecked(UEdGraphSchema_K2::PN_Then);

	if (!CachedTypeAsset && DynamicParameterPinNames.Num() > 0)
	{
		CompilerContext.MessageLog.Error(
			*LOCTEXT("ErrorNoAsset",
				"ActivateComposableCamera node @@ has dynamic parameter pins but no Camera Type Asset assigned.")
			.ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// Step 1: Create a temporary variable for the FComposableCameraParameterBlock 

	UK2Node_TemporaryVariable* TempBlockNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	TempBlockNode->VariableType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	TempBlockNode->VariableType.PinSubCategoryObject = FComposableCameraParameterBlock::StaticStruct();
	TempBlockNode->AllocateDefaultPins();

	UEdGraphPin* TempBlockVarPin = TempBlockNode->GetVariablePin();

	// Step 2: For each dynamic parameter pin, chain a SetParameterBlockValue call 

	UEdGraphPin* CurrentExecChainThen = nullptr; // Will be linked forward.
	UEdGraphPin* FirstSetterExec = nullptr;

	for (const FName& ParamPinName: DynamicParameterPinNames)
	{
		UEdGraphPin* DynamicPin = FindPin(ParamPinName);
		if (!DynamicPin)
		{
			continue;
		}

		// Delegate pins that are not connected have no meaningful default value - 
		// an unbound FScriptDelegate is a no-op. Skip the SetParameterBlockValue
		// call entirely so we don't emit a thunk for an empty delegate.
		if (DynamicPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
			&& DynamicPin->LinkedTo.Num() == 0)
		{
			continue;
		}

		// Spawn the intermediate CallFunction node. Dispatch per pin type to
		// a typed BP setter for POD-shaped pin types (Bool / numeric / Name /
		// math structs / Object / Actor); fall back to the wildcard
		// SetParameterBlockValue for Enum / arbitrary Struct / Delegate.
		// The typed-setter dispatch sidesteps the UE 5.6 BP CustomStructureParam
		// wildcard bug that mis-types pin-default struct literals routed
		// through MakeLiteralStruct intermediates -- see TechDoc.md Section 7.2.
		const FName SetterFunctionName =
			ComposableCameraEdGraphPinTypeUtils::ResolveTypedSetterFunctionName(DynamicPin->PinType);
		UK2Node_CallFunction* SetterNode =
			CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		SetterNode->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(SetterFunctionName));
		SetterNode->AllocateDefaultPins();

		UEdGraphPin* SetterExecPin = SetterNode->GetExecPin();
		UEdGraphPin* SetterThenPin = SetterNode->GetThenPin();
		UEdGraphPin* SetterBlockPin = SetterNode->FindPinChecked(TEXT("ParameterBlock"));
		UEdGraphPin* SetterNamePin = SetterNode->FindPinChecked(TEXT("ParameterName"));
		UEdGraphPin* SetterValuePin = SetterNode->FindPinChecked(TEXT("Value"));

		// Wire the temp variable to the ParameterBlock ref pin.
		TempBlockVarPin->MakeLinkTo(SetterBlockPin);

		// Set the parameter name.
		SetterNamePin->DefaultValue = ParamPinName.ToString();

		// Match the Value pin type to the dynamic pin's type.
		SetterValuePin->PinType = DynamicPin->PinType;

		// Wire the value: either from connected source or from a MakeLiteral for the default.
		if (DynamicPin->LinkedTo.Num() > 0)
		{
			CompilerContext.MovePinLinksToIntermediate(*DynamicPin, *SetterValuePin);
		}
		else if (UK2Node_CallFunction* MakeLiteral =
			MakeLiteralValueForPin(CompilerContext, SourceGraph, DynamicPin))
		{
			MakeLiteral->GetReturnValuePin()->MakeLinkTo(SetterValuePin);
		}
		else
		{
			// For types without a MakeLiteral (e.g., custom structs, objects),
			// move the default value directly.
			SetterValuePin->DefaultValue = DynamicPin->DefaultValue;
			SetterValuePin->DefaultObject = DynamicPin->DefaultObject;
		}

		// Chain execution.
		if (!FirstSetterExec)
		{
			FirstSetterExec = SetterExecPin;
		}
		if (CurrentExecChainThen)
		{
			CurrentExecChainThen->MakeLinkTo(SetterExecPin);
		}
		CurrentExecChainThen = SetterThenPin;
	}

	// Step 3: Spawn the final ActivateComposableCameraFromTypeAsset call 

	UK2Node_CallFunction* ActivateNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	ActivateNode->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, ActivateComposableCameraFromTypeAsset)));
	ActivateNode->AllocateDefaultPins();

	UEdGraphPin* ActivateExecPin = ActivateNode->GetExecPin();
	UEdGraphPin* ActivateThenPin = ActivateNode->GetThenPin();

	// Wire static input pins.
	UEdGraphPin* ActivateWorldCtx = ActivateNode->FindPinChecked(TEXT("WorldContextObject"));
	UEdGraphPin* ActivatePlayerIdx = ActivateNode->FindPinChecked(TEXT("PlayerIndex"));
	UEdGraphPin* ActivateAssetPin = ActivateNode->FindPinChecked(TEXT("CameraTypeAsset"));
	UEdGraphPin* ActivateContextPin = ActivateNode->FindPinChecked(TEXT("ContextName"));
	UEdGraphPin* ActivateTransPin = ActivateNode->FindPinChecked(TEXT("TransitionOverride"));
	UEdGraphPin* ActivateParamsPin = ActivateNode->FindPinChecked(TEXT("Parameters"));
	UEdGraphPin* ActivateActParamsPin = ActivateNode->FindPinChecked(TEXT("ActivationParams"));
	UEdGraphPin* ActivateReturnPin = ActivateNode->GetReturnValuePin();

	// WorldContextObject is auto-resolved by the compiler via meta=(WorldContext).
	// Move our static pins.
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_PlayerIndex), *ActivatePlayerIdx);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_CameraTypeAsset), *ActivateAssetPin);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ContextName), *ActivateContextPin);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_TransitionOverride), *ActivateTransPin);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ActivationParams), *ActivateActParamsPin);

	// Wire the temp ParameterBlock variable to the Parameters input.
	TempBlockVarPin->MakeLinkTo(ActivateParamsPin);

	// Wire the return value.
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ReturnValue), *ActivateReturnPin);

	// Step 4: Wire execution chain 

	if (FirstSetterExec)
	{
		// ExecIn -> first setter -> ... -> last setter->ActivateNode -> ThenOut
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *FirstSetterExec);
		CurrentExecChainThen->MakeLinkTo(ActivateExecPin);
		CompilerContext.MovePinLinksToIntermediate(*ThenPin, *ActivateThenPin);
	}
	else
	{
		// No dynamic pins - ExecIn->ActivateNode -> ThenOut
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *ActivateExecPin);
		CompilerContext.MovePinLinksToIntermediate(*ThenPin, *ActivateThenPin);
	}

	// Clean up this node.
	BreakAllNodeLinks();
}

// Literal Helpers 

UK2Node_CallFunction* UK2Node_ActivateComposableCamera::MakeLiteralValueForPin(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
	UEdGraphPin* SourceValuePin)
{
#define CCS_MAKE_LITERAL(FuncLib, FuncName) \
	CreateMakeLiteralNode(CompilerContext, SourceGraph, this, FuncLib::StaticClass(), TEXT(#FuncName), SourceValuePin)

#define CCS_LITERAL_BY_CATEGORY(CategoryName, FuncName) \
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::CategoryName) \
	{ return CCS_MAKE_LITERAL(UKismetSystemLibrary, FuncName); }

#define CCS_LITERAL_BY_STRUCT(StructObj, FuncName) \
	if (SourceValuePin->PinType.PinSubCategoryObject == StructObj) \
	{ return CCS_MAKE_LITERAL(UComposableCameraBlueprintLibrary, FuncName); }

	CCS_LITERAL_BY_CATEGORY(PC_Boolean, MakeLiteralBool)
	CCS_LITERAL_BY_CATEGORY(PC_Int, MakeLiteralInt)

	// Float/Double via PC_Real with subcategory.
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real &&
		SourceValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
	{
		return CCS_MAKE_LITERAL(UKismetSystemLibrary, MakeLiteralFloat);
	}
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real &&
		SourceValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
	{
		return CCS_MAKE_LITERAL(UKismetSystemLibrary, MakeLiteralDouble);
	}

	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		CCS_LITERAL_BY_STRUCT(TBaseStructure<FVector>::Get(), MakeLiteralVector)
		CCS_LITERAL_BY_STRUCT(TBaseStructure<FVector4>::Get(), MakeLiteralVector4)
		CCS_LITERAL_BY_STRUCT(TBaseStructure<FVector2D>::Get(), MakeLiteralVector2D)
		CCS_LITERAL_BY_STRUCT(TBaseStructure<FRotator>::Get(), MakeLiteralRotator)
		CCS_LITERAL_BY_STRUCT(TBaseStructure<FTransform>::Get(), MakeLiteralTransform)
	}

	// Object references (Actor, UObject). Without a MakeLiteral node, a null-default
	// Object pin on a CustomStructureParam function doesn't generate valid bytecode - 
	// the K2 compiler may skip the value operand entirely, desynchronising the
	// bytecode stream and causing StepCompiledIn in subsequent SetParameterBlockValue
	// calls to resolve the wrong FProperty.
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		return CCS_MAKE_LITERAL(UComposableCameraBlueprintLibrary, MakeLiteralObject);
	}

	// FName.
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		return CCS_MAKE_LITERAL(UComposableCameraBlueprintLibrary, MakeLiteralName);
	}

	// Enum (PC_Byte with a UEnum subtype). Without a MakeLiteral node the
	// compiler skips the value operand for CustomStructureParam functions,
	// desynchronising the bytecode stream - same issue as Object literals.
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte
		&& SourceValuePin->PinType.PinSubCategoryObject.IsValid())
	{
		UK2Node_CallFunction* MakeLiteralNode = CreateMakeLiteralNode(CompilerContext, SourceGraph, this,
			UComposableCameraBlueprintLibrary::StaticClass(),
			TEXT("MakeLiteralByte"), SourceValuePin);
		// Override the Value and Return pin types to carry the enum subtype so
		// the DefaultValue string (e.g. "HoldLastFrame") parses correctly
		// instead of being rejected as an invalid uint8.
		UEdGraphPin* ValPin = MakeLiteralNode->FindPinChecked(TEXT("Value"));
		ValPin->PinType = SourceValuePin->PinType;
		MakeLiteralNode->GetReturnValuePin()->PinType = SourceValuePin->PinType;
		return MakeLiteralNode;
	}

	return nullptr;

#undef CCS_MAKE_LITERAL
#undef CCS_LITERAL_BY_CATEGORY
#undef CCS_LITERAL_BY_STRUCT
}

UK2Node_CallFunction* UK2Node_ActivateComposableCamera::CreateMakeLiteralNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
	UK2Node* SourceNode, UClass* FunctionLibraryClass,
	const TCHAR* FunctionName, UEdGraphPin* SourceValuePin)
{
	UK2Node_CallFunction* MakeLiteralNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(SourceNode, SourceGraph);
	MakeLiteralNode->FunctionReference.SetExternalMember(FunctionName, FunctionLibraryClass);
	MakeLiteralNode->AllocateDefaultPins();
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeLiteralNode, SourceGraph);

	UEdGraphPin* LiteralValuePin = MakeLiteralNode->FindPinChecked(TEXT("Value"));
	LiteralValuePin->DefaultValue = SourceValuePin->DefaultValue;
	LiteralValuePin->DefaultTextValue = SourceValuePin->DefaultTextValue;
	LiteralValuePin->AutogeneratedDefaultValue = SourceValuePin->AutogeneratedDefaultValue;
	LiteralValuePin->DefaultObject = SourceValuePin->DefaultObject;

	return MakeLiteralNode;
}

#undef LOCTEXT_NAMESPACE
