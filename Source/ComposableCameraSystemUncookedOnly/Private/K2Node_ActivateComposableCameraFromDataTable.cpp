// Copyright 2026 Sulley. All Rights Reserved.

#include "K2Node_ActivateComposableCameraFromDataTable.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/DataTable.h"
#include "K2Node_CallFunction.h"
#include "K2Node_TemporaryVariable.h"
#include "KismetCompiler.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Containers/Ticker.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

#include "ComposableCameraEdGraphPinTypeUtils.h"
#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"
#include "Cameras/ComposableCameraCameraBase.h"

#define LOCTEXT_NAMESPACE "K2Node_ActivateComposableCameraFromDataTable"

// Well-Known Pin Names 

const FName UK2Node_ActivateComposableCameraFromDataTable::DataTablePinName(TEXT("DataTable"));
const FName UK2Node_ActivateComposableCameraFromDataTable::RowNamePinName(TEXT("RowName"));
const FName UK2Node_ActivateComposableCameraFromDataTable::PN_PlayerIndex(TEXT("PlayerIndex"));
const FName UK2Node_ActivateComposableCameraFromDataTable::PN_ReturnValue(TEXT("ReturnValue"));

// Pin Allocation 

void UK2Node_ActivateComposableCameraFromDataTable::AllocateDefaultPins()
{
	// Exec pins.
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// Player Index.
	UEdGraphPin* PlayerIndexPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Int, PN_PlayerIndex);
	PlayerIndexPin->DefaultValue = TEXT("0");

	// DataTable (object reference to UDataTable - filtered by pin widget).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UDataTable::StaticClass();
		CreatePin(EGPD_Input, PinType, DataTablePinName);
	}

	// RowName.
	UEdGraphPin* RowNamePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, RowNamePinName);
	RowNamePin->DefaultValue = TEXT("None");

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

FText UK2Node_ActivateComposableCameraFromDataTable::GetTooltipText() const
{
	if (CachedTypeAsset)
	{
		return FText::Format(LOCTEXT("TooltipWithAsset",
				"Activate camera from DataTable row (type '{0}') with optional parameter overrides."),
			FText::FromString(CachedTypeAsset->GetName()));
	}
	return LOCTEXT("TooltipGeneric",
		"Activate a composable camera from a DataTable row with optional parameter overrides.");
}

FText UK2Node_ActivateComposableCameraFromDataTable::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedTypeAsset && (TitleType == ENodeTitleType::FullTitle || TitleType == ENodeTitleType::EditableTitle))
	{
		return FText::Format(LOCTEXT("NodeTitleWithAsset", "Activate Camera (DataTable)\n{0}"),
			FText::FromString(CachedTypeAsset->GetName()));
	}
	return LOCTEXT("NodeTitleGeneric", "Activate Camera (DataTable)");
}

FLinearColor UK2Node_ActivateComposableCameraFromDataTable::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor(20, 150, 140));
}

FSlateIcon UK2Node_ActivateComposableCameraFromDataTable::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent");
}

// Menu Actions 

void UK2Node_ActivateComposableCameraFromDataTable::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		Spawner->DefaultMenuSignature.Category = GetMenuCategory();
		Spawner->DefaultMenuSignature.MenuName =
			LOCTEXT("GenericMenuName", "Activate Camera (DataTable)");
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_ActivateComposableCameraFromDataTable::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "ComposableCameraSystem|Camera");
}

// Pin Change Handlers 

void UK2Node_ActivateComposableCameraFromDataTable::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (!Pin)
	{
		return;
	}

	// Skip during active reconstruction: the engine's pin-rewire phase can fire
	// this notification on the freshly-created DataTable / RowName pins BEFORE
	// the saved DefaultObject / DefaultValue have been transferred from OldPins.
	// Acting on it would tear UserOverrideNames mid-pass via a nested
	// ReconstructNode. See TechDoc.md Section 7.2.
	if (bIsReconstructing)
	{
		return;
	}

	if (Pin->PinName == DataTablePinName)
	{
		ClearRowNameIfInvalidForCurrentDataTable();
		OnDataTablePinChangedDelegate.Broadcast();
		ResolveCameraTypeFromDataTable();
	}
	else if (Pin->PinName == RowNamePinName)
	{
		// RowName changed - the CameraType may differ per row.
		ResolveCameraTypeFromDataTable();
	}
}

void UK2Node_ActivateComposableCameraFromDataTable::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (!Pin)
	{
		return;
	}

	// Same rationale as PinDefaultValueChanged: the base reconstruction's link
	// transfer can fire this mid-reconstruction.
	if (bIsReconstructing)
	{
		return;
	}

	if (Pin->PinName == DataTablePinName)
	{
		ClearRowNameIfInvalidForCurrentDataTable();
		OnDataTablePinChangedDelegate.Broadcast();
		ResolveCameraTypeFromDataTable();
	}
	else if (Pin->PinName == RowNamePinName)
	{
		// If RowName got linked, we can't statically resolve the row anymore.
		ResolveCameraTypeFromDataTable();
	}
}

UComposableCameraTypeAsset* UK2Node_ActivateComposableCameraFromDataTable::GetCameraTypeAsset() const
{
	return CachedTypeAsset;
}

// DataTable / CameraType Resolution 

void UK2Node_ActivateComposableCameraFromDataTable::ResolveCameraTypeFromDataTable()
{
	UComposableCameraTypeAsset* NewAsset = nullptr;

	const UDataTable* DataTable = ResolveLiteralDataTable();
	if (DataTable)
	{
		UEdGraphPin* RowNamePin = FindPin(RowNamePinName, EGPD_Input);
		if (RowNamePin && RowNamePin->LinkedTo.Num() == 0)
		{
			const FString RowNameStr = RowNamePin->GetDefaultAsString();
			const FName RowName(*RowNameStr);
			if (!RowName.IsNone())
			{
				const FComposableCameraParameterTableRow* Row =
					DataTable->FindRow<FComposableCameraParameterTableRow>(RowName, TEXT("K2Node ResolveCameraType"));
				if (Row && !Row->CameraType.IsNull())
				{
					NewAsset = Row->CameraType.LoadSynchronous();
				}
			}
		}
	}

	if (NewAsset != CachedTypeAsset)
	{
		CachedTypeAsset = NewAsset;

		// Auto-clean orphaned overrides when the asset changes.
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

void UK2Node_ActivateComposableCameraFromDataTable::ClearRowNameIfInvalidForCurrentDataTable()
{
	UEdGraphPin* RowNamePin = FindPin(RowNamePinName, EGPD_Input);
	if (!RowNamePin)
	{
		return;
	}

	if (RowNamePin->LinkedTo.Num() > 0)
	{
		return;
	}

	const FString CurrentRowName = RowNamePin->GetDefaultAsString();
	if (CurrentRowName.IsEmpty() || CurrentRowName == FName(NAME_None).ToString())
	{
		return;
	}

	const UDataTable* DataTable = ResolveLiteralDataTable();
	if (!DataTable)
	{
		return;
	}

	const TArray<FName> RowNames = DataTable->GetRowNames();
	if (RowNames.Contains(FName(*CurrentRowName)))
	{
		return;
	}

	const FString NoneString = FName(NAME_None).ToString();
	if (const UEdGraphSchema* Schema = RowNamePin->GetSchema())
	{
		Schema->TrySetDefaultValue(*RowNamePin, NoneString);
	}
	else if (UEdGraphNode* OwningNode = RowNamePin->GetOwningNodeUnchecked())
	{
		OwningNode->Modify();
		RowNamePin->DefaultValue = NoneString;
	}
}

UScriptStruct* UK2Node_ActivateComposableCameraFromDataTable::GetRequiredRowStruct()
{
	return FComposableCameraParameterTableRow::StaticStruct();
}

UDataTable* UK2Node_ActivateComposableCameraFromDataTable::ResolveLiteralDataTable() const
{
	const UEdGraphPin* DataTablePin = FindPin(DataTablePinName, EGPD_Input);
	if (!DataTablePin)
	{
		return nullptr;
	}

	if (DataTablePin->LinkedTo.Num() > 0)
	{
		return nullptr;
	}

	UDataTable* DataTable = Cast<UDataTable>(DataTablePin->DefaultObject);
	if (!DataTable)
	{
		return nullptr;
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct || !RowStruct->IsChildOf(GetRequiredRowStruct()))
	{
		return nullptr;
	}

	return DataTable;
}

// Lifetime 

void UK2Node_ActivateComposableCameraFromDataTable::PostLoad()
{
	Super::PostLoad();

	// Resolve the CameraType on load so dynamic pins are correct.
	// We don't call ResolveCameraTypeFromDataTable here because that would
	// ReconstructNode during PostLoad - instead, just populate CachedTypeAsset
	// silently. AllocateDefaultPins will use it on the next reconstruction.
	const UDataTable* DataTable = ResolveLiteralDataTable();
	if (DataTable)
	{
		UEdGraphPin* RowNamePin = FindPin(RowNamePinName, EGPD_Input);
		if (RowNamePin && RowNamePin->LinkedTo.Num() == 0)
		{
			const FString RowNameStr = RowNamePin->GetDefaultAsString();
			const FName RowName(*RowNameStr);
			if (!RowName.IsNone())
			{
				const FComposableCameraParameterTableRow* Row =
					DataTable->FindRow<FComposableCameraParameterTableRow>(RowName, TEXT("K2Node PostLoad"));
				if (Row && !Row->CameraType.IsNull())
				{
					CachedTypeAsset = Row->CameraType.LoadSynchronous();
				}
			}
		}
	}

	SubscribeToAssetChangeDelegate();

	// Auto-recover from baked-in orphan pin state. See the matching block in
	// UK2Node_ActivateComposableCamera and TechDoc.md Section 7.2 for full background:
	// once a previous reconstruction with CachedTypeAsset == nullptr left an
	// orphan pin in Pins[] and the user saved the blueprint, plain load just
	// deserializes the orphan and no reconstruction runs, so the per-Reallocate
	// self-heal never fires. Detect it here and trigger a deferred reconstruct.
	// FTSTicker's one-tick deferral is required: calling ReconstructNode
	// directly from PostLoad trips ZenLoader's RF_NeedLoad ensure (Obj.cpp:1314)
	// because reconstruction touches related load-batch objects (the blueprint,
	// the DataTable, the CachedTypeAsset, sibling nodes) that may not yet have
	// cleared RF_NeedLoad. By the time the ticker fires, the load batch is
	// complete.
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
		TWeakObjectPtr<UK2Node_ActivateComposableCameraFromDataTable> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float) -> bool
			{
				if (UK2Node_ActivateComposableCameraFromDataTable* StrongThis = WeakThis.Get())
				{
					if (IsValid(StrongThis))
					{
						if (StrongThis->CachedTypeAsset)
						{
							StrongThis->CachedTypeAsset->ConditionalPostLoad();
						}
						UE_LOG(LogComposableCameraSystem, Warning,
							TEXT("K2 ActivateComposableCameraFromDataTable deferred PostLoad recovery: orphan pin detected; reconstructing now."));
						StrongThis->ReconstructNode();
					}
				}
				return false; // one-shot; do not repeat
			}),
			0.0f);
	}
}

void UK2Node_ActivateComposableCameraFromDataTable::BeginDestroy()
{
	UnsubscribeFromAssetChangeDelegate();
	Super::BeginDestroy();
}

void UK2Node_ActivateComposableCameraFromDataTable::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
	SubscribeToAssetChangeDelegate();
}

void UK2Node_ActivateComposableCameraFromDataTable::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	// Re-entrancy guard. RewireOldPinsToNewPins can fire pin notifications
	// (PinDefaultValueChanged on the freshly-created DataTable / RowName pins
	// while their state is still being transferred from OldPins) and external
	// subscribers (our OnObjectPropertyChanged handler) can fire during load.
	// Both would call through to a nested ReconstructNode that observes a torn
	// UserOverrideNames state and silently drops user-authored override pins.
	// See TechDoc.md Section 7.2 for the full failure-mode taxonomy.
	TGuardValue<bool> ReconstructionGuard(bIsReconstructing, true);

	// Stale-state self-heal for CachedTypeAsset. Two failure modes converge
	// here (mirrors the matching block in UK2Node_ActivateComposableCamera):
	//
	// 1. Legacy on-disk corruption -- a blueprint saved before the
	// re-entrancy guard landed can have CachedTypeAsset == nullptr while
	// the saved DataTable + RowName pins still resolve to the right row.
	// 2. EDL load-order race -- on cold load the DataTable / referenced
	// CameraType asset's PostLoad may not have completed by reconstruction
	// time. CachedTypeAsset (TObjectPtr) reads as nullptr AND
	// OldPin->DefaultObject for the DataTable pin can read as nullptr
	// while OldPin->DefaultValue still carries the path string.
	//
	// Recovery is two-step here (DataTable variant): rebuild DataTable from
	// the OldPin's DefaultObject->DefaultValue (FSoftObjectPath::TryLoad),
	// then look up the row by name and chase Row->CameraType.LoadSynchronous().
	// ConditionalPostLoad on both the DataTable and the recovered CameraType
	// so their PostLoad migrations finish before downstream code reads their
	// arrays.
	if (CachedTypeAsset == nullptr)
	{
		UDataTable* RecoveredDataTable = nullptr;
		FName RecoveredRowName = NAME_None;

		for (const UEdGraphPin* OldPin: OldPins)
		{
			if (!OldPin)
			{
				continue;
			}

			if (OldPin->PinName == DataTablePinName && !RecoveredDataTable)
			{
				if (OldPin->LinkedTo.Num() > 0)
				{
					// DataTable pin is wired through a variable -- can't
					// statically resolve. Leave CachedTypeAsset null; this is
					// the same behavior as ResolveLiteralDataTable() rejecting
					// linked pins.
					continue;
				}

				if (OldPin->DefaultObject)
				{
					RecoveredDataTable = Cast<UDataTable>(OldPin->DefaultObject);
				}
				if (!RecoveredDataTable && !OldPin->DefaultValue.IsEmpty())
				{
					FSoftObjectPath Path(OldPin->DefaultValue);
					UObject* Resolved = Path.ResolveObject();
					if (!Resolved)
					{
						Resolved = Path.TryLoad();
					}
					if (UDataTable* DT = Cast<UDataTable>(Resolved))
					{
						UE_LOG(LogComposableCameraSystem, Verbose,
							TEXT("K2 ActivateComposableCameraFromDataTable: recovered DataTable '%s' from OldPin->DefaultValue (DefaultObject was null at reconstruction time -- EDL load-order race)."),
							*DT->GetName());
						RecoveredDataTable = DT;
					}
				}
			}
			else if (OldPin->PinName == RowNamePinName && RecoveredRowName.IsNone())
			{
				if (OldPin->LinkedTo.Num() == 0)
				{
					const FString RowNameStr = OldPin->GetDefaultAsString();
					if (!RowNameStr.IsEmpty())
					{
						RecoveredRowName = FName(*RowNameStr);
					}
				}
			}
		}

		if (RecoveredDataTable && !RecoveredRowName.IsNone())
		{
			RecoveredDataTable->ConditionalPostLoad();

			const UScriptStruct* RowStruct = RecoveredDataTable->GetRowStruct();
			if (RowStruct && RowStruct->IsChildOf(GetRequiredRowStruct()))
			{
				const FComposableCameraParameterTableRow* Row =
					RecoveredDataTable->FindRow<FComposableCameraParameterTableRow>(RecoveredRowName, TEXT("K2Node Reallocate self-heal"));
				if (Row && !Row->CameraType.IsNull())
				{
					if (UComposableCameraTypeAsset* Recovered = Row->CameraType.LoadSynchronous())
					{
						Recovered->ConditionalPostLoad();
						CachedTypeAsset = Recovered;
					}
				}
			}
		}
	}

	// Self-healing: if an old dynamic pin had live connections that the user
	// clearly cared about, make sure its name is in UserOverrideNames before
	// we call through to the base reconstruction.
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

bool UK2Node_ActivateComposableCameraFromDataTable::IsNameRequiredParameter(FName Name) const
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

bool UK2Node_ActivateComposableCameraFromDataTable::IsNameInCachedAsset(FName Name) const
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

void UK2Node_ActivateComposableCameraFromDataTable::AddOverridePin(FName Name)
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

void UK2Node_ActivateComposableCameraFromDataTable::RemoveOverridePin(FName Name)
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

void UK2Node_ActivateComposableCameraFromDataTable::CleanUpOrphanOverrides()
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

void UK2Node_ActivateComposableCameraFromDataTable::SubscribeToAssetChangeDelegate()
{
	if (ObjectPropertyChangedHandle.IsValid())
	{
		return;
	}
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UK2Node_ActivateComposableCameraFromDataTable::HandleObjectPropertyChanged);
}

void UK2Node_ActivateComposableCameraFromDataTable::UnsubscribeFromAssetChangeDelegate()
{
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		ObjectPropertyChangedHandle.Reset();
	}
}

void UK2Node_ActivateComposableCameraFromDataTable::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& /*Event*/)
{
	if (Object == nullptr || Object != CachedTypeAsset)
	{
		return;
	}

	// Skip while our own ReallocatePinsDuringReconstruction is in flight.
	// OnObjectPropertyChanged can fire during asset load/save side-effects;
	// a nested ReconstructNode from that path would tear pin state mid-pass.
	if (bIsReconstructing)
	{
		return;
	}

	if (!GetGraph() || !GetOuter())
	{
		return;
	}

	// Auto-clean orphaned overrides.
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

void UK2Node_ActivateComposableCameraFromDataTable::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (!Menu || !Context || !CachedTypeAsset)
	{
		return;
	}

	UK2Node_ActivateComposableCameraFromDataTable* MutableThis =
		const_cast<UK2Node_ActivateComposableCameraFromDataTable*>(this);

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
			return;
		}

		FToolMenuSection& PinSection = Menu->FindOrAddSection(TEXT("ComposableCameraOverridePin"),
			LOCTEXT("OverridePinSectionLabel", "Composable Camera"));

		PinSection.AddMenuEntry(TEXT("RemoveOverridePin"),
			LOCTEXT("RemoveOverridePinLabel", "Remove Override Pin"),
			LOCTEXT("RemoveOverridePinTooltip",
				"Remove this override pin. The camera will use the DataTable row value (or the asset default) at activation time."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateUObject(MutableThis,
				&UK2Node_ActivateComposableCameraFromDataTable::RemoveOverridePin,
				PinName)));
		return;
	}

	// Node context: Add Override Pin 
	if (Context->Node != this)
	{
		return;
	}

	FToolMenuSection& NodeSection = Menu->FindOrAddSection(TEXT("ComposableCameraOverrides"),
		LOCTEXT("OverridesSectionLabel", "Composable Camera"));

	const TSet<FName> OverrideSet(UserOverrideNames);

	TArray<FName> AddableOptionalParams;
	TArray<FName> AddableExposedVariables;

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
		NodeSection.AddSubMenu(TEXT("AddOverridePin"),
			LOCTEXT("AddOverridePinLabel", "Add Override Pin"),
			LOCTEXT("AddOverridePinTooltip",
				"Add a pin that overrides a parameter from the DataTable row. The override value takes precedence over the row value at activation time."),
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
									&UK2Node_ActivateComposableCameraFromDataTable::AddOverridePin,
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
									&UK2Node_ActivateComposableCameraFromDataTable::AddOverridePin,
									Name)));
						}
					}
				}));
	}

	// "Clean Up Orphan Overrides" - only when there's something to clean.
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
				"Remove override entries that reference parameters no longer present on the row's Camera Type Asset."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateUObject(MutableThis,
				&UK2Node_ActivateComposableCameraFromDataTable::CleanUpOrphanOverrides)));
	}
}

// Compile-Time Validation 

void UK2Node_ActivateComposableCameraFromDataTable::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	if (!CachedTypeAsset)
	{
		// Orphan detection not possible without a resolved asset.
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

void UK2Node_ActivateComposableCameraFromDataTable::RemoveDynamicParameterPins()
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

void UK2Node_ActivateComposableCameraFromDataTable::CreateDynamicParameterPins()
{
	RemoveDynamicParameterPins();

	if (!CachedTypeAsset)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
		return;
	}

	const TSet<FName> OverrideSet(UserOverrideNames);

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
				TEXT("[%s] K2 ActivateCameraFromDataTable: skipping duplicate exposed parameter '%s'."),
				*CachedTypeAsset->GetName(), *Param.ParameterName.ToString());
			continue;
		}

		FEdGraphPinType PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(Param.PinType, Param.StructType, Param.EnumType, Param.SignatureFunction);
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, PinType, Param.ParameterName);

		// PinFriendlyName precedence - same logic as sibling node.
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
				NewPin->PinFriendlyName = FText::Format(NSLOCTEXT("K2Node_ActivateComposableCameraFromDataTable",
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

		// Seed the pin default from the type asset (same as sibling node).
		// The DataTable row value is applied at runtime, not at authoring time.
		if (Param.PinType != EComposableCameraPinType::Delegate)
		{
			const FString PinDefault = CachedTypeAsset->GetExposedParameterDefaultValue(Param);
			if (!PinDefault.IsEmpty())
			{
				NewPin->DefaultValue = PinDefault;
			}
		}

		NewPin->bAdvancedView = !Param.bRequired;

		DynamicParameterPinNames.Add(Param.ParameterName);
		CreatedPinNames.Add(Param.ParameterName);
	}

	// Exposed variables 
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

		if (CreatedPinNames.Contains(Var.VariableName))
		{
			UE_LOG(LogComposableCameraSystem, Warning,
				TEXT("[%s] K2 ActivateCameraFromDataTable: skipping duplicate exposed variable '%s'."),
				*CachedTypeAsset->GetName(), *Var.VariableName.ToString());
			continue;
		}

		FEdGraphPinType PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(Var.VariableType, Var.StructType, Var.EnumType);
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

	// Advanced pin display state - same logic as sibling node.
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

void UK2Node_ActivateComposableCameraFromDataTable::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = FindPinChecked(UEdGraphSchema_K2::PN_Then);

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// Step 1: Create a temporary variable for the override FComposableCameraParameterBlock 

	UK2Node_TemporaryVariable* TempBlockNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	TempBlockNode->VariableType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	TempBlockNode->VariableType.PinSubCategoryObject = FComposableCameraParameterBlock::StaticStruct();
	TempBlockNode->AllocateDefaultPins();

	UEdGraphPin* TempBlockVarPin = TempBlockNode->GetVariablePin();

	// Step 2: For each dynamic parameter pin, chain a SetParameterBlockValue call 

	UEdGraphPin* CurrentExecChainThen = nullptr;
	UEdGraphPin* FirstSetterExec = nullptr;

	for (const FName& ParamPinName: DynamicParameterPinNames)
	{
		UEdGraphPin* DynamicPin = FindPin(ParamPinName);
		if (!DynamicPin)
		{
			continue;
		}

		// Skip unconnected delegate pins (no meaningful default value).
		if (DynamicPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
			&& DynamicPin->LinkedTo.Num() == 0)
		{
			continue;
		}

		// Dispatch per pin type to a typed BP setter; wildcard fallback for
		// Enum / arbitrary Struct / Delegate. See TechDoc.md Section 7.2 for the
		// BP CustomStructureParam wildcard bug this avoids.
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

	// Step 3: Spawn the final ActivateComposableCameraFromDataTable call 

	UK2Node_CallFunction* ActivateNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	ActivateNode->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, ActivateComposableCameraFromDataTable)));
	ActivateNode->AllocateDefaultPins();

	UEdGraphPin* ActivateExecPin = ActivateNode->GetExecPin();
	UEdGraphPin* ActivateThenPin = ActivateNode->GetThenPin();

	// Wire static input pins.
	UEdGraphPin* ActivatePlayerIdx = ActivateNode->FindPinChecked(TEXT("PlayerIndex"));
	UEdGraphPin* ActivateDataTablePin = ActivateNode->FindPinChecked(TEXT("DataTable"));
	UEdGraphPin* ActivateRowNamePin = ActivateNode->FindPinChecked(TEXT("RowName"));
	UEdGraphPin* ActivateOverridePin = ActivateNode->FindPinChecked(TEXT("OverrideParameters"));
	UEdGraphPin* ActivateReturnPin = ActivateNode->GetReturnValuePin();

	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_PlayerIndex), *ActivatePlayerIdx);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(DataTablePinName), *ActivateDataTablePin);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(RowNamePinName), *ActivateRowNamePin);

	// Wire the override ParameterBlock.
	TempBlockVarPin->MakeLinkTo(ActivateOverridePin);

	// Wire the return value.
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ReturnValue), *ActivateReturnPin);

	// Step 4: Wire execution chain 

	if (FirstSetterExec)
	{
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *FirstSetterExec);
		CurrentExecChainThen->MakeLinkTo(ActivateExecPin);
		CompilerContext.MovePinLinksToIntermediate(*ThenPin, *ActivateThenPin);
	}
	else
	{
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *ActivateExecPin);
		CompilerContext.MovePinLinksToIntermediate(*ThenPin, *ActivateThenPin);
	}

	BreakAllNodeLinks();
}

// Literal Helpers 

UK2Node_CallFunction* UK2Node_ActivateComposableCameraFromDataTable::MakeLiteralValueForPin(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
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

	// Object references (Actor, UObject).
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		return CCS_MAKE_LITERAL(UComposableCameraBlueprintLibrary, MakeLiteralObject);
	}

	// FName.
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		return CCS_MAKE_LITERAL(UComposableCameraBlueprintLibrary, MakeLiteralName);
	}

	return nullptr;

#undef CCS_MAKE_LITERAL
#undef CCS_LITERAL_BY_CATEGORY
#undef CCS_LITERAL_BY_STRUCT
}

UK2Node_CallFunction* UK2Node_ActivateComposableCameraFromDataTable::CreateMakeLiteralNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph,
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
