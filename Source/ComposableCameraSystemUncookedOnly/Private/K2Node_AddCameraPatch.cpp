// Copyright Sulley. All rights reserved.

#include "K2Node_AddCameraPatch.h"

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
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Patches/ComposableCameraPatchHandle.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_AddCameraPatch"

// ─── Well-Known Pin Names ──────────────────────────────────────────────────────

const FName UK2Node_AddCameraPatch::PN_PlayerIndex(TEXT("PlayerIndex"));
const FName UK2Node_AddCameraPatch::PN_PatchAsset(TEXT("PatchAsset"));
const FName UK2Node_AddCameraPatch::PN_ContextName(TEXT("ContextName"));
const FName UK2Node_AddCameraPatch::PN_Params(TEXT("Params"));
const FName UK2Node_AddCameraPatch::PN_ReturnValue(TEXT("ReturnValue"));

// ─── Pin Allocation ────────────────────────────────────────────────────────────

void UK2Node_AddCameraPatch::AllocateDefaultPins()
{
	// Exec pins.
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// Player Index. Mirrors UK2Node_ActivateComposableCamera so the two K2 nodes
	// have identical static-pin shape; AddCameraPatch resolves the PCM internally
	// via GetComposableCameraPlayerCameraManager(WorldContext, PlayerIndex).
	UEdGraphPin* PlayerIndexPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, PN_PlayerIndex);
	PlayerIndexPin->DefaultValue = TEXT("0");

	// Patch Type Asset (object reference).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UComposableCameraPatchTypeAsset::StaticClass();
		CreatePin(EGPD_Input, PinType, PN_PatchAsset);
	}

	// Context Name. NAME_None routes to the active context (the typical
	// case); a non-None name targets a specific context that's already on
	// the stack. Mirrors the matching pin on UK2Node_ActivateComposableCamera.
	UEdGraphPin* ContextPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, PN_ContextName);
	ContextPin->DefaultValue = TEXT("None");

	// Activation Params (struct).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = FComposableCameraPatchActivateParams::StaticStruct();
		CreatePin(EGPD_Input, PinType, PN_Params);
	}

	// Return value (the patch handle).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UComposableCameraPatchHandle::StaticClass();
		CreatePin(EGPD_Output, PinType, PN_ReturnValue);
	}

	// Create dynamic parameter pins from the cached patch asset.
	CreateDynamicParameterPins();

	Super::AllocateDefaultPins();
}

// ─── Node Display ──────────────────────────────────────────────────────────────

FText UK2Node_AddCameraPatch::GetTooltipText() const
{
	if (CachedPatchAsset)
	{
		return FText::Format(
			LOCTEXT("TooltipWithAsset", "Add Camera Patch from asset '{0}' with exposed parameters."),
			FText::FromString(CachedPatchAsset->GetName()));
	}
	return LOCTEXT("TooltipGeneric", "Add a Camera Patch from a Patch Type Asset with typed parameter pins.");
}

FText UK2Node_AddCameraPatch::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedPatchAsset && (TitleType == ENodeTitleType::FullTitle || TitleType == ENodeTitleType::EditableTitle))
	{
		return FText::Format(
			LOCTEXT("NodeTitleWithAsset", "Add Camera Patch\n{0}"),
			FText::FromString(CachedPatchAsset->GetName()));
	}
	return LOCTEXT("NodeTitleGeneric", "Add Camera Patch");
}

FLinearColor UK2Node_AddCameraPatch::GetNodeTitleColor() const
{
	// Same teal as UK2Node_ActivateComposableCamera. The two K2 nodes share the
	// same opt-in override / dynamic pin model and should read as siblings in
	// the graph; the warm-orange Patch identity stays scoped to the Content
	// Browser asset (thumbnail + AssetDefinition color in EditorDesignDoc §22),
	// so authors see "this is a Patch *asset*" there but "this is a sibling of
	// Activate Camera" on the K2 node where the visual cue actually matters
	// for graph reading.
	return FLinearColor::FromSRGBColor(FColor(20, 150, 140));
}

FSlateIcon UK2Node_AddCameraPatch::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent");
}

// ─── Menu Actions ──────────────────────────────────────────────────────────────

void UK2Node_AddCameraPatch::GetMenuActions(
	FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();

	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		NodeSpawner->DefaultMenuSignature.Category = GetMenuCategory();
		NodeSpawner->DefaultMenuSignature.MenuName =
			LOCTEXT("GenericMenuName", "Add Camera Patch");
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_AddCameraPatch::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "ComposableCameraSystem|Patch");
}

// ─── Pin Change Handlers ───────────────────────────────────────────────────────

void UK2Node_AddCameraPatch::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	// Skip during active reconstruction: the engine's pin-rewire phase can fire
	// this notification on the freshly-created PatchAsset pin BEFORE the saved
	// DefaultObject has been transferred from OldPins, producing a transient
	// "asset is null" state. Acting on it would wipe UserOverrideNames via
	// OnPatchAssetChanged's else-branch and corrupt the next pin pass.
	if (bIsReconstructing)
	{
		return;
	}

	if (Pin && Pin->PinName == PN_PatchAsset)
	{
		OnPatchAssetChanged();
	}
}

void UK2Node_AddCameraPatch::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	// Same rationale as PinDefaultValueChanged: the base reconstruction's link
	// transfer can fire this mid-reconstruction. A nested ReconstructNode in that
	// window would double-allocate pins and drop UserOverrideNames.
	if (bIsReconstructing)
	{
		return;
	}

	if (Pin && Pin->PinName == PN_PatchAsset)
	{
		// If the asset pin has been connected to a variable, we lose the static asset reference.
		// Clear dynamic pins since we can't know the parameters at compile time.
		if (Pin->LinkedTo.Num() > 0)
		{
			CachedPatchAsset = nullptr;
			ReconstructNode();
		}
	}
}

UComposableCameraPatchTypeAsset* UK2Node_AddCameraPatch::GetPatchTypeAsset() const
{
	return CachedPatchAsset;
}

void UK2Node_AddCameraPatch::OnPatchAssetChanged()
{
	// Read the new asset from the pin's DefaultObject.
	UEdGraphPin* AssetPin = FindPin(PN_PatchAsset);
	UComposableCameraPatchTypeAsset* NewAsset = nullptr;

	if (AssetPin && AssetPin->DefaultObject)
	{
		NewAsset = Cast<UComposableCameraPatchTypeAsset>(AssetPin->DefaultObject);
	}

	if (NewAsset != CachedPatchAsset)
	{
		CachedPatchAsset = NewAsset;

		// Auto-clean orphaned overrides: names that no longer exist on the new
		// asset's exposed parameters/variables are removed immediately. Keeps
		// the node surface in sync with the asset.
		if (CachedPatchAsset && UserOverrideNames.Num() > 0)
		{
			TSet<FName> AssetNames;
			AssetNames.Reserve(CachedPatchAsset->ExposedParameters.Num() + CachedPatchAsset->ExposedVariables.Num());
			for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
			{
				AssetNames.Add(Param.ParameterName);
			}
			for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
			{
				AssetNames.Add(Var.VariableName);
			}
			UserOverrideNames.RemoveAll([&AssetNames](FName Name)
			{
				return !AssetNames.Contains(Name);
			});
		}
		else if (!CachedPatchAsset)
		{
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

// ─── Lifetime ──────────────────────────────────────────────────────────────────

void UK2Node_AddCameraPatch::PostLoad()
{
	Super::PostLoad();
	SubscribeToAssetChangeDelegate();

	// Auto-recover from baked-in orphan pin state. See the matching block in
	// UK2Node_ActivateComposableCamera and TechDoc.md §7.2 for full background:
	// once a previous reconstruction with CachedPatchAsset == nullptr left an
	// orphan pin in Pins[] and the user saved the blueprint, plain load just
	// deserializes the orphan -- no reconstruction runs, the per-Reallocate
	// self-heal never fires, and the user sees a red "In use pin X no longer
	// exists" warning every cold restart that only manual Refresh clears.
	//
	// Detect that disk-baked orphan state and trigger one deferred reconstruction.
	// FTSTicker's one-tick deferral is required: calling ReconstructNode directly
	// from PostLoad trips ZenLoader's RF_NeedLoad ensure (Obj.cpp:1314) because
	// reconstruction touches related load-batch objects (the blueprint, the
	// CachedPatchAsset, sibling nodes) that may not yet have cleared RF_NeedLoad.
	// By the time the ticker fires, the load batch is complete.
	bool bHasOrphanPin = false;
	for (const UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->bOrphanedPin)
		{
			bHasOrphanPin = true;
			break;
		}
	}
	if (bHasOrphanPin)
	{
		TWeakObjectPtr<UK2Node_AddCameraPatch> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis](float) -> bool
			{
				if (UK2Node_AddCameraPatch* StrongThis = WeakThis.Get())
				{
					if (IsValid(StrongThis))
					{
						if (StrongThis->CachedPatchAsset)
						{
							StrongThis->CachedPatchAsset->ConditionalPostLoad();
						}
						UE_LOG(LogComposableCameraSystem, Warning,
							TEXT("K2 AddCameraPatch deferred PostLoad recovery: orphan pin detected; reconstructing now."));
						StrongThis->ReconstructNode();
					}
				}
				return false; // one-shot; do not repeat
			}),
			0.0f);
	}
}

void UK2Node_AddCameraPatch::BeginDestroy()
{
	UnsubscribeFromAssetChangeDelegate();
	Super::BeginDestroy();
}

void UK2Node_AddCameraPatch::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
	SubscribeToAssetChangeDelegate();
}

void UK2Node_AddCameraPatch::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	// Re-entrancy guard. See UK2Node_ActivateComposableCamera for the full
	// rationale — same engine quirks apply (RewireOldPinsToNewPins firing pin
	// notifications mid-rewire, OnObjectPropertyChanged firing during asset
	// load/save). Without the guard, a nested ReconstructNode would observe a
	// torn UserOverrideNames state and silently drop user-authored override pins.
	TGuardValue<bool> ReconstructionGuard(bIsReconstructing, true);

	// Stale-state self-heal for CachedPatchAsset. Two failure modes converge
	// here (see the matching block in UK2Node_ActivateComposableCamera and
	// TechDoc.md §7.2 for full rationale):
	//
	//   1. Legacy on-disk corruption -- a blueprint saved before the re-entrancy
	//      guard landed can have CachedPatchAsset == nullptr while the saved
	//      PatchAsset pin still carries the right reference.
	//   2. EDL load-order race -- on cold load the patch asset's PostLoad may
	//      not have completed by the time blueprint compilation triggers our
	//      reconstruction. CachedPatchAsset (TObjectPtr) and OldPin->DefaultObject
	//      both read as nullptr; only OldPin->DefaultValue still carries the
	//      serialized asset path string. The picker shows the asset name to the
	//      user via that string, but pointer-side code sees null.
	//
	// Two-tier recovery: prefer OldPin->DefaultObject when the linker has
	// resolved the pointer, fall back to FSoftObjectPath::TryLoad on
	// OldPin->DefaultValue when EDL hasn't gotten there yet. Synchronous load
	// is correct here -- the K2 node has a hard UPROPERTY dependency on this
	// asset; pulling its remaining preload window forward by one synchronous
	// load is in spec, not a workaround. ConditionalPostLoad afterward ensures
	// the asset's own PostLoad migration (GUID backfill, name de-duplication)
	// completes before CreateDynamicParameterPins reads its arrays.
	if (CachedPatchAsset == nullptr)
	{
		for (const UEdGraphPin* OldPin : OldPins)
		{
			if (!OldPin || OldPin->PinName != PN_PatchAsset)
			{
				continue;
			}

			if (OldPin->DefaultObject)
			{
				CachedPatchAsset = Cast<UComposableCameraPatchTypeAsset>(OldPin->DefaultObject);
				if (CachedPatchAsset)
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
				if (UComposableCameraPatchTypeAsset* Recovered = Cast<UComposableCameraPatchTypeAsset>(Resolved))
				{
					UE_LOG(LogComposableCameraSystem, Verbose,
						TEXT("K2 AddCameraPatch: recovered CachedPatchAsset '%s' from OldPin->DefaultValue (DefaultObject was null at reconstruction time -- EDL load-order race)."),
						*Recovered->GetName());
					CachedPatchAsset = Recovered;
					break;
				}
			}
		}

		if (CachedPatchAsset)
		{
			CachedPatchAsset->ConditionalPostLoad();
		}
	}

	// Self-healing: if an old dynamic pin had live connections that the user
	// clearly cared about, make sure its name is in UserOverrideNames before
	// we call through to the base reconstruction. Guards against the case
	// where a required parameter is made optional on the asset after the
	// blueprint was saved.
	if (CachedPatchAsset != nullptr)
	{
		TSet<FName> AssetNames;
		AssetNames.Reserve(CachedPatchAsset->ExposedParameters.Num() + CachedPatchAsset->ExposedVariables.Num());
		for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
		{
			AssetNames.Add(Param.ParameterName);
		}
		for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
		{
			AssetNames.Add(Var.VariableName);
		}

		for (UEdGraphPin* OldPin : OldPins)
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
			if (IsNameRequiredParameter(OldPin->PinName))
			{
				continue;
			}
			UserOverrideNames.AddUnique(OldPin->PinName);
		}
	}

	Super::ReallocatePinsDuringReconstruction(OldPins);
}

// ─── Override Set Management ──────────────────────────────────────────────────

bool UK2Node_AddCameraPatch::IsNameRequiredParameter(FName Name) const
{
	if (!CachedPatchAsset || Name.IsNone())
	{
		return false;
	}
	for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
	{
		if (Param.ParameterName == Name)
		{
			return Param.bRequired;
		}
	}
	return false;
}

bool UK2Node_AddCameraPatch::IsNameInCachedAsset(FName Name) const
{
	if (!CachedPatchAsset || Name.IsNone())
	{
		return false;
	}
	for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
	{
		if (Param.ParameterName == Name)
		{
			return true;
		}
	}
	for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
	{
		if (Var.VariableName == Name)
		{
			return true;
		}
	}
	return false;
}

void UK2Node_AddCameraPatch::AddOverridePin(FName Name)
{
	if (Name.IsNone() || UserOverrideNames.Contains(Name))
	{
		return;
	}
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

void UK2Node_AddCameraPatch::RemoveOverridePin(FName Name)
{
	if (Name.IsNone())
	{
		return;
	}
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

void UK2Node_AddCameraPatch::CleanUpOrphanOverrides()
{
	if (!CachedPatchAsset || UserOverrideNames.Num() == 0)
	{
		return;
	}

	TSet<FName> AssetNames;
	AssetNames.Reserve(CachedPatchAsset->ExposedParameters.Num() + CachedPatchAsset->ExposedVariables.Num());
	for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
	{
		AssetNames.Add(Param.ParameterName);
	}
	for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
	{
		AssetNames.Add(Var.VariableName);
	}

	bool bHasOrphans = false;
	for (const FName& Name : UserOverrideNames)
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

// ─── Asset Change Notification ────────────────────────────────────────────────

void UK2Node_AddCameraPatch::SubscribeToAssetChangeDelegate()
{
	if (ObjectPropertyChangedHandle.IsValid())
	{
		return;
	}
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(
		this, &UK2Node_AddCameraPatch::HandleObjectPropertyChanged);
}

void UK2Node_AddCameraPatch::UnsubscribeFromAssetChangeDelegate()
{
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		ObjectPropertyChangedHandle.Reset();
	}
}

void UK2Node_AddCameraPatch::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& /*Event*/)
{
	if (Object == nullptr || Object != CachedPatchAsset)
	{
		return;
	}

	if (bIsReconstructing)
	{
		return;
	}

	if (!GetGraph() || !GetOuter())
	{
		return;
	}

	if (CachedPatchAsset && UserOverrideNames.Num() > 0)
	{
		TSet<FName> AssetNames;
		AssetNames.Reserve(CachedPatchAsset->ExposedParameters.Num() + CachedPatchAsset->ExposedVariables.Num());
		for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
		{
			AssetNames.Add(Param.ParameterName);
		}
		for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
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

// ─── Context Menu ─────────────────────────────────────────────────────────────

void UK2Node_AddCameraPatch::GetNodeContextMenuActions(
	UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (!Menu || !Context || !CachedPatchAsset)
	{
		return;
	}

	UK2Node_AddCameraPatch* MutableThis =
		const_cast<UK2Node_AddCameraPatch*>(this);

	// ── Pin context: Remove Override Pin ──────────────────────────────
	if (Context->Pin != nullptr)
	{
		const FName PinName = Context->Pin->PinName;
		if (!DynamicParameterPinNames.Contains(PinName))
		{
			return;
		}
		if (IsNameRequiredParameter(PinName))
		{
			return;
		}

		FToolMenuSection& PinSection = Menu->FindOrAddSection(
			TEXT("ComposableCameraPatchOverridePin"),
			LOCTEXT("OverridePinSectionLabel", "Composable Camera Patch"));

		PinSection.AddMenuEntry(
			TEXT("RemoveOverridePin"),
			LOCTEXT("RemoveOverridePinLabel", "Remove Override Pin"),
			LOCTEXT("RemoveOverridePinTooltip",
				"Remove this override pin. The patch will use the value authored on the Patch Type Asset at activation time."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateUObject(
				MutableThis,
				&UK2Node_AddCameraPatch::RemoveOverridePin,
				PinName)));
		return;
	}

	// ── Node context: Add Override Pin + Clean Up Orphan Overrides ────
	if (Context->Node != this)
	{
		return;
	}

	FToolMenuSection& NodeSection = Menu->FindOrAddSection(
		TEXT("ComposableCameraPatchOverrides"),
		LOCTEXT("OverridesSectionLabel", "Composable Camera Patch"));

	const TSet<FName> OverrideSet(UserOverrideNames);

	TArray<FName> AddableOptionalParams;
	TArray<FName> AddableExposedVariables;

	// Defensive cross-set dedup — see UK2Node_ActivateComposableCamera for
	// the full rationale (UToolMenus collapses duplicate-name entries into a
	// single visible row, but a click routes through AddOverridePin and
	// produces two pins simultaneously without this guard).
	TSet<FName> SeenMenuNames;
	SeenMenuNames.Reserve(CachedPatchAsset->ExposedParameters.Num() + CachedPatchAsset->ExposedVariables.Num());

	for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
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
	for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
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
		NodeSection.AddSubMenu(
			TEXT("AddOverridePin"),
			LOCTEXT("AddOverridePinLabel", "Add Override Pin"),
			LOCTEXT("AddOverridePinTooltip",
				"Add a pin that overrides an exposed parameter or exposed variable on the selected Patch Type Asset. Names that are not overridden use the default value authored on the asset."),
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
						FToolMenuSection& ParamSection = SubMenu->AddSection(
							TEXT("AddableExposedParameters"),
							LOCTEXT("AddableExposedParametersLabel", "Exposed Parameters"));
						for (const FName& Name : AddableOptionalParams)
						{
							ParamSection.AddMenuEntry(
								Name,
								FText::FromName(Name),
								LOCTEXT("AddOverrideParamEntryTooltip",
									"Add an override pin for this exposed parameter."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateUObject(
									MutableThis,
									&UK2Node_AddCameraPatch::AddOverridePin,
									Name)));
						}
					}

					if (AddableExposedVariables.Num() > 0)
					{
						FToolMenuSection& VarSection = SubMenu->AddSection(
							TEXT("AddableExposedVariables"),
							LOCTEXT("AddableExposedVariablesLabel", "Exposed Variables"));
						for (const FName& Name : AddableExposedVariables)
						{
							VarSection.AddMenuEntry(
								Name,
								FText::FromName(Name),
								LOCTEXT("AddOverrideVarEntryTooltip",
									"Add an override pin for this exposed variable."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateUObject(
									MutableThis,
									&UK2Node_AddCameraPatch::AddOverridePin,
									Name)));
						}
					}
				}));
	}

	bool bHasOrphans = false;
	for (const FName& Name : UserOverrideNames)
	{
		if (!IsNameInCachedAsset(Name))
		{
			bHasOrphans = true;
			break;
		}
	}
	if (bHasOrphans)
	{
		NodeSection.AddMenuEntry(
			TEXT("CleanUpOrphanOverrides"),
			LOCTEXT("CleanUpOrphanOverridesLabel", "Clean Up Orphan Overrides"),
			LOCTEXT("CleanUpOrphanOverridesTooltip",
				"Remove override entries that reference parameters or variables no longer present on the selected Patch Type Asset."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateUObject(
				MutableThis,
				&UK2Node_AddCameraPatch::CleanUpOrphanOverrides)));
	}
}

// ─── Compile-Time Validation ──────────────────────────────────────────────────

void UK2Node_AddCameraPatch::ValidateNodeDuringCompilation(
	FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	if (!CachedPatchAsset)
	{
		return;
	}

	TSet<FName> AssetNames;
	AssetNames.Reserve(CachedPatchAsset->ExposedParameters.Num() + CachedPatchAsset->ExposedVariables.Num());
	for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
	{
		AssetNames.Add(Param.ParameterName);
	}
	for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
	{
		AssetNames.Add(Var.VariableName);
	}

	for (const FName& Name : UserOverrideNames)
	{
		if (!AssetNames.Contains(Name))
		{
			MessageLog.Warning(
				*FText::Format(
					LOCTEXT("OrphanOverrideWarning",
						"@@ has an override for '{0}', which no longer exists on Patch Type Asset '{1}'. The override will be ignored and removed on the next node refresh."),
					FText::FromName(Name),
					FText::FromString(CachedPatchAsset->GetName())
				).ToString(),
				this);
		}
	}
}

// ─── Dynamic Pin Management ────────────────────────────────────────────────────

void UK2Node_AddCameraPatch::RemoveDynamicParameterPins()
{
	for (const FName& PinName : DynamicParameterPinNames)
	{
		if (UEdGraphPin* Pin = FindPin(PinName))
		{
			Pin->BreakAllPinLinks();
			Pins.Remove(Pin);
		}
	}
	DynamicParameterPinNames.Empty();
}

void UK2Node_AddCameraPatch::CreateDynamicParameterPins()
{
	RemoveDynamicParameterPins();

	if (!CachedPatchAsset)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
		return;
	}

	const TSet<FName> OverrideSet(UserOverrideNames);

	// Defensive cross-set dedup. See UK2Node_ActivateComposableCamera for the
	// full rationale — duplicate names across ExposedParameters ∪ ExposedVariables
	// would otherwise trigger a CreatePin assertion on the second collision.
	TSet<FName> CreatedPinNames;
	CreatedPinNames.Reserve(CachedPatchAsset->ExposedParameters.Num() + CachedPatchAsset->ExposedVariables.Num());

	// ── Exposed parameters ──────────────────────────────────────────────
	for (const FComposableCameraExposedParameter& Param : CachedPatchAsset->ExposedParameters)
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
				TEXT("[%s] K2 AddCameraPatch: skipping duplicate exposed parameter '%s' — another entry already claimed this pin name. Re-save the patch asset to let PostLoad rename the duplicate."),
				*CachedPatchAsset->GetName(), *Param.ParameterName.ToString());
			continue;
		}

		FEdGraphPinType PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(
			Param.PinType, Param.StructType, Param.EnumType, Param.SignatureFunction);
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, PinType, Param.ParameterName);

		// PinFriendlyName precedence — see UK2Node_ActivateComposableCamera for
		// the full rationale. Mirrors the asset's MakeUniqueExposedName suffix
		// when ParameterName != TargetPinName so two pins that share an
		// underlying camera-node pin name still render distinguishably.
		if (Param.ParameterName != Param.TargetPinName)
		{
			const FString ParamNameStr = Param.ParameterName.ToString();
			const FString TargetNameStr = Param.TargetPinName.ToString();
			FString SuffixStr;
			if (ParamNameStr.StartsWith(TargetNameStr))
			{
				SuffixStr = ParamNameStr.RightChop(TargetNameStr.Len());
			}

			if (!Param.DisplayName.IsEmpty() && !SuffixStr.IsEmpty())
			{
				NewPin->PinFriendlyName = FText::Format(
					NSLOCTEXT("K2Node_AddCameraPatch",
						"ExposedParamFriendlyNameWithSuffix", "{0}{1}"),
					Param.DisplayName,
					FText::FromString(SuffixStr));
			}
			else
			{
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

		if (Param.PinType != EComposableCameraPinType::Delegate)
		{
			const FString PinDefault = CachedPatchAsset->GetExposedParameterDefaultValue(Param);
			if (!PinDefault.IsEmpty())
			{
				NewPin->DefaultValue = PinDefault;
			}
		}

		NewPin->bAdvancedView = !Param.bRequired;

		DynamicParameterPinNames.Add(Param.ParameterName);
		CreatedPinNames.Add(Param.ParameterName);
	}

	// ── Exposed variables ────────────────────────────────────────────────
	for (const FComposableCameraInternalVariable& Var : CachedPatchAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}

		if (!OverrideSet.Contains(Var.VariableName))
		{
			continue;
		}

		if (CreatedPinNames.Contains(Var.VariableName))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("[%s] K2 AddCameraPatch: skipping duplicate exposed variable '%s' — another entry already claimed this pin name. Re-save the patch asset to let PostLoad rename the duplicate."),
				*CachedPatchAsset->GetName(), *Var.VariableName.ToString());
			continue;
		}

		FEdGraphPinType PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(
			Var.VariableType, Var.StructType, Var.EnumType);
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, PinType, Var.VariableName);

		if (!Var.Tooltip.IsEmpty())
		{
			NewPin->PinToolTip = Var.Tooltip.ToString();
		}

		if (!Var.InitialValueString.IsEmpty())
		{
			NewPin->DefaultValue = Var.InitialValueString;
		}

		NewPin->bAdvancedView = true;

		DynamicParameterPinNames.Add(Var.VariableName);
		CreatedPinNames.Add(Var.VariableName);
	}

	// Advanced-pin caret state — see sibling K2 node for the rationale on the
	// NoPins / Hidden / Shown transition rule.
	int32 NumAdvancedPins = 0;
	for (const FName& PinName : DynamicParameterPinNames)
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

// ─── ExpandNode ────────────────────────────────────────────────────────────────

void UK2Node_AddCameraPatch::ExpandNode(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = FindPinChecked(UEdGraphSchema_K2::PN_Then);

	if (!CachedPatchAsset && DynamicParameterPinNames.Num() > 0)
	{
		CompilerContext.MessageLog.Error(
			*LOCTEXT("ErrorNoAsset",
				"AddCameraPatch node @@ has dynamic parameter pins but no Patch Type Asset assigned.")
			.ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	// ── Step 1: Create a temporary variable for the FComposableCameraParameterBlock ──

	UK2Node_TemporaryVariable* TempBlockNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	TempBlockNode->VariableType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	TempBlockNode->VariableType.PinSubCategoryObject = FComposableCameraParameterBlock::StaticStruct();
	TempBlockNode->AllocateDefaultPins();

	UEdGraphPin* TempBlockVarPin = TempBlockNode->GetVariablePin();

	// ── Step 2: For each dynamic parameter pin, chain a SetParameterBlockValue call ──

	UEdGraphPin* CurrentExecChainThen = nullptr;
	UEdGraphPin* FirstSetterExec = nullptr;

	for (const FName& ParamPinName : DynamicParameterPinNames)
	{
		UEdGraphPin* DynamicPin = FindPin(ParamPinName);
		if (!DynamicPin)
		{
			continue;
		}

		// Unbound delegate pins are a no-op at activation; skip the thunk.
		if (DynamicPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
			&& DynamicPin->LinkedTo.Num() == 0)
		{
			continue;
		}

		// Dispatch per pin type to a typed BP setter; wildcard fallback for
		// Enum / arbitrary Struct / Delegate. See TechDoc.md §7.2 for the
		// BP CustomStructureParam wildcard bug this avoids.
		const FName SetterFunctionName =
			ComposableCameraEdGraphPinTypeUtils::ResolveTypedSetterFunctionName(DynamicPin->PinType);
		UK2Node_CallFunction* SetterNode =
			CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		SetterNode->SetFromFunction(
			UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(SetterFunctionName));
		SetterNode->AllocateDefaultPins();

		UEdGraphPin* SetterExecPin = SetterNode->GetExecPin();
		UEdGraphPin* SetterThenPin = SetterNode->GetThenPin();
		UEdGraphPin* SetterBlockPin = SetterNode->FindPinChecked(TEXT("ParameterBlock"));
		UEdGraphPin* SetterNamePin = SetterNode->FindPinChecked(TEXT("ParameterName"));
		UEdGraphPin* SetterValuePin = SetterNode->FindPinChecked(TEXT("Value"));

		TempBlockVarPin->MakeLinkTo(SetterBlockPin);
		SetterNamePin->DefaultValue = ParamPinName.ToString();
		SetterValuePin->PinType = DynamicPin->PinType;

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
			SetterValuePin->DefaultValue = DynamicPin->DefaultValue;
			SetterValuePin->DefaultObject = DynamicPin->DefaultObject;
		}

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

	// ── Step 3: Spawn the final AddCameraPatch call ──

	UK2Node_CallFunction* AddPatchNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	AddPatchNode->SetFromFunction(
		UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, AddCameraPatch)));
	AddPatchNode->AllocateDefaultPins();

	UEdGraphPin* AddPatchExecPin = AddPatchNode->GetExecPin();
	UEdGraphPin* AddPatchThenPin = AddPatchNode->GetThenPin();

	// Wire static input pins. WorldContextObject is auto-resolved by the
	// compiler via meta=(WorldContext) — we don't expose it as a static pin
	// on the K2 node.
	UEdGraphPin* AddPatchPlayerIdxPin = AddPatchNode->FindPinChecked(TEXT("PlayerIndex"));
	UEdGraphPin* AddPatchAssetPin = AddPatchNode->FindPinChecked(TEXT("PatchAsset"));
	UEdGraphPin* AddPatchContextPin = AddPatchNode->FindPinChecked(TEXT("ContextName"));
	UEdGraphPin* AddPatchParamsPin = AddPatchNode->FindPinChecked(TEXT("Params"));
	UEdGraphPin* AddPatchParamBlockPin = AddPatchNode->FindPinChecked(TEXT("Parameters"));
	UEdGraphPin* AddPatchReturnPin = AddPatchNode->GetReturnValuePin();

	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_PlayerIndex), *AddPatchPlayerIdxPin);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_PatchAsset), *AddPatchAssetPin);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ContextName), *AddPatchContextPin);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_Params), *AddPatchParamsPin);

	TempBlockVarPin->MakeLinkTo(AddPatchParamBlockPin);

	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ReturnValue), *AddPatchReturnPin);

	// ── Step 4: Wire execution chain ──

	if (FirstSetterExec)
	{
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *FirstSetterExec);
		CurrentExecChainThen->MakeLinkTo(AddPatchExecPin);
		CompilerContext.MovePinLinksToIntermediate(*ThenPin, *AddPatchThenPin);
	}
	else
	{
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *AddPatchExecPin);
		CompilerContext.MovePinLinksToIntermediate(*ThenPin, *AddPatchThenPin);
	}

	BreakAllNodeLinks();
}

// ─── Literal Helpers ───────────────────────────────────────────────────────────

UK2Node_CallFunction* UK2Node_AddCameraPatch::MakeLiteralValueForPin(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
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

	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		return CCS_MAKE_LITERAL(UComposableCameraBlueprintLibrary, MakeLiteralObject);
	}

	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		return CCS_MAKE_LITERAL(UComposableCameraBlueprintLibrary, MakeLiteralName);
	}

	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte
		&& SourceValuePin->PinType.PinSubCategoryObject.IsValid())
	{
		UK2Node_CallFunction* MakeLiteralNode = CreateMakeLiteralNode(
			CompilerContext, SourceGraph, this,
			UComposableCameraBlueprintLibrary::StaticClass(),
			TEXT("MakeLiteralByte"), SourceValuePin);
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

UK2Node_CallFunction* UK2Node_AddCameraPatch::CreateMakeLiteralNode(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
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
