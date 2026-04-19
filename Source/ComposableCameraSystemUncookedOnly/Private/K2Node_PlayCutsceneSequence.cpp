// Copyright Sulley. All rights reserved.

#include "K2Node_PlayCutsceneSequence.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_TemporaryVariable.h"
#include "KismetCompiler.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "AsyncActions/AsyncPlayCutsceneSequence.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "LevelSequence.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_PlayCutsceneSequence"

// ─── Well-Known Pin Names ──────────────────────────────────────────────────────

const FName UK2Node_PlayCutsceneSequence::PN_LevelSequence(TEXT("InLevelSequence"));
const FName UK2Node_PlayCutsceneSequence::PN_ContextName(TEXT("ContextName"));
const FName UK2Node_PlayCutsceneSequence::PN_EnterTransition(TEXT("EnterTransition"));
const FName UK2Node_PlayCutsceneSequence::PN_PlaybackSettings(TEXT("PlaybackSettings"));
const FName UK2Node_PlayCutsceneSequence::PN_CutsceneAction(TEXT("CutsceneAction"));
const FName UK2Node_PlayCutsceneSequence::PN_OnFinished(TEXT("OnFinished"));

// ─── Pin Allocation ────────────────────────────────────────────────────────────

void UK2Node_PlayCutsceneSequence::AllocateDefaultPins()
{
	// ── Exec pins ──
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// On Finished exec output — fires when the LS ends naturally.
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, PN_OnFinished);

	// ── Input data pins ──

	// Level Sequence (ULevelSequence*).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = ULevelSequence::StaticClass();
		CreatePin(EGPD_Input, PinType, PN_LevelSequence);
	}

	// Context Name (FName).
	{
		UEdGraphPin* ContextPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, PN_ContextName);
		ContextPin->DefaultValue = TEXT("None");
	}

	// Enter Transition (UComposableCameraTransitionDataAsset*, optional).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UComposableCameraTransitionDataAsset::StaticClass();
		CreatePin(EGPD_Input, PinType, PN_EnterTransition);
	}

	// Playback Settings (FMovieSceneSequencePlaybackSettings struct).
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = FMovieSceneSequencePlaybackSettings::StaticStruct();
		CreatePin(EGPD_Input, PinType, PN_PlaybackSettings);
	}

	// ── Output data pins ──

	// Cutscene Action (UAsyncPlayCutsceneSequence*) — the action reference.
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UAsyncPlayCutsceneSequence::StaticClass();
		CreatePin(EGPD_Output, PinType, PN_CutsceneAction);
	}

	Super::AllocateDefaultPins();
}

// ─── Node Display ──────────────────────────────────────────────────────────────

FText UK2Node_PlayCutsceneSequence::GetTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Play a level sequence as a CCS-managed cutscene.\n\n"
		"Pushes a cutscene context and starts LS playback. Camera cuts within the LS "
		"are handled via the PCM's SetViewTarget override, creating transient proxy "
		"cameras in the cutscene context.\n\n"
		"Use the Cutscene Action output to call Stop Cutscene Sequence when needed.");
}

FText UK2Node_PlayCutsceneSequence::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Play Cutscene Sequence");
}

FLinearColor UK2Node_PlayCutsceneSequence::GetNodeTitleColor() const
{
	// Match the async action node color (dark blue-ish).
	return FLinearColor(0.0f, 0.15f, 0.36f);
}

FSlateIcon UK2Node_PlayCutsceneSequence::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.LevelSequenceActor");
}

// ─── Menu Actions ──────────────────────────────────────────────────────────────

void UK2Node_PlayCutsceneSequence::GetMenuActions(
	FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		NodeSpawner->DefaultMenuSignature.Category = GetMenuCategory();
		NodeSpawner->DefaultMenuSignature.MenuName =
			LOCTEXT("MenuName", "Play Cutscene Sequence");
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_PlayCutsceneSequence::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "ComposableCameraSystem|Level Sequence");
}

// ─── ExpandNode ────────────────────────────────────────────────────────────────

void UK2Node_PlayCutsceneSequence::ExpandNode(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = FindPinChecked(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* OnFinishedPin = FindPinChecked(PN_OnFinished);
	UEdGraphPin* CutsceneActionPin = FindPinChecked(PN_CutsceneAction);

	// ── Step 1: Create a temp variable for the proxy (UAsyncPlayCutsceneSequence*) ──

	UK2Node_TemporaryVariable* ProxyTempVar =
		CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	ProxyTempVar->VariableType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ProxyTempVar->VariableType.PinSubCategoryObject = UAsyncPlayCutsceneSequence::StaticClass();
	ProxyTempVar->AllocateDefaultPins();
	UEdGraphPin* ProxyVarPin = ProxyTempVar->GetVariablePin();

	// ── Step 2: Call the factory function (BlueprintLibrary::PlayCutsceneSequence) ──

	UK2Node_CallFunction* CallFactory =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallFactory->SetFromFunction(
		UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, PlayCutsceneSequence)));
	CallFactory->AllocateDefaultPins();

	// Wire input pins to the factory call.
	// WorldContextObject is auto-resolved by the compiler via meta=(WorldContext).
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_LevelSequence),
		*CallFactory->FindPinChecked(TEXT("InLevelSequence")));
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ContextName),
		*CallFactory->FindPinChecked(TEXT("ContextName")));
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_EnterTransition),
		*CallFactory->FindPinChecked(TEXT("EnterTransition")));
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_PlaybackSettings),
		*CallFactory->FindPinChecked(TEXT("PlaybackSettings")));

	// ── Step 3: Assign factory return to temp variable ──

	UK2Node_AssignmentStatement* AssignProxy =
		CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
	AssignProxy->AllocateDefaultPins();

	// The assignment's variable AND value pin types must both be set explicitly —
	// the assignment node creates wildcard pins by default and the K2 compiler
	// raises "type of Variable is undetermined" if they remain unresolved.
	FEdGraphPinType ProxyPinType;
	ProxyPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ProxyPinType.PinSubCategoryObject = UAsyncPlayCutsceneSequence::StaticClass();

	AssignProxy->GetVariablePin()->PinType = ProxyPinType;
	AssignProxy->GetValuePin()->PinType = ProxyPinType;

	// Wire: factory return → assignment value.
	UEdGraphPin* FactoryReturnPin = CallFactory->GetReturnValuePin();
	FactoryReturnPin->MakeLinkTo(AssignProxy->GetValuePin());

	// Wire: temp var → assignment variable.
	ProxyVarPin->MakeLinkTo(AssignProxy->GetVariablePin());

	// ── Step 4: Bind OnFinished delegate on the proxy ──

	// Create a custom event that fires when OnFinished broadcasts.
	UK2Node_CustomEvent* OnFinishedEvent =
		CompilerContext.SpawnIntermediateNode<UK2Node_CustomEvent>(this, SourceGraph);
	OnFinishedEvent->CustomFunctionName =
		FName(*FString::Printf(TEXT("OnCutsceneFinished_%s"), *NodeGuid.ToString()));
	OnFinishedEvent->AllocateDefaultPins();

	// Move the OnFinished exec output pin links to the custom event's Then pin.
	CompilerContext.MovePinLinksToIntermediate(
		*OnFinishedPin,
		*OnFinishedEvent->FindPinChecked(UEdGraphSchema_K2::PN_Then));

	// Create AddDelegate node to bind the custom event to proxy->OnFinished.
	UK2Node_AddDelegate* AddDelegateNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_AddDelegate>(this, SourceGraph);
	AddDelegateNode->DelegateReference.SetExternalMember(
		FName(TEXT("OnFinished")), UAsyncPlayCutsceneSequence::StaticClass());
	AddDelegateNode->AllocateDefaultPins();

	// Wire: proxy temp var → AddDelegate self (target object).
	if (UEdGraphPin* AddDelegateSelf = Schema->FindSelfPin(*AddDelegateNode, EGPD_Input))
	{
		ProxyVarPin->MakeLinkTo(AddDelegateSelf);
	}

	// Wire: custom event's delegate output → AddDelegate's delegate input.
	if (UEdGraphPin* AddDelegateDelegatePin = AddDelegateNode->GetDelegatePin())
	{
		UEdGraphPin* EventDelegatePin = OnFinishedEvent->FindPinChecked(UK2Node_CustomEvent::DelegateOutputName);
		EventDelegatePin->MakeLinkTo(AddDelegateDelegatePin);
	}

	// ── Step 5: Call Activate() on the proxy ──

	UK2Node_CallFunction* CallActivate =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallActivate->SetFromFunction(
		UBlueprintAsyncActionBase::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UBlueprintAsyncActionBase, Activate)));
	CallActivate->AllocateDefaultPins();

	// Wire: proxy temp var → Activate self.
	if (UEdGraphPin* ActivateSelf = Schema->FindSelfPin(*CallActivate, EGPD_Input))
	{
		ProxyVarPin->MakeLinkTo(ActivateSelf);
	}

	// ── Step 6: Wire the Cutscene Action output pin ──

	CompilerContext.MovePinLinksToIntermediate(*CutsceneActionPin, *ProxyVarPin);

	// ── Step 7: Wire the execution chain ──
	//
	// ExecIn → CallFactory → AssignProxy → AddDelegate → CallActivate → ThenOut

	CompilerContext.MovePinLinksToIntermediate(*ExecPin, *CallFactory->GetExecPin());
	CallFactory->GetThenPin()->MakeLinkTo(AssignProxy->GetExecPin());
	AssignProxy->GetThenPin()->MakeLinkTo(AddDelegateNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
	AddDelegateNode->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(CallActivate->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*ThenPin, *CallActivate->GetThenPin());

	// Clean up this node from the graph.
	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
