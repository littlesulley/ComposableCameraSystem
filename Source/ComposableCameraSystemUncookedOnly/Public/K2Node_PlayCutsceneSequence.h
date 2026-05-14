// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_PlayCutsceneSequence.generated.h"

/**
 * Custom K2 Node for playing a level sequence as a CCS-managed cutscene.
 *
 * This replaces the default UK2Node_AsyncAction auto-generated node, which
 * cannot expose the proxy object as an output data pin. This node provides:
 *
 * - All input parameters (LevelSequence, ContextName, EnterTransition, PlaybackSettings)
 * - A "Cutscene Action" output pin (UAsyncPlayCutsceneSequence*) so the user
 * can cache the reference and call StopCutsceneSequence() later
 * - An "On Finished" exec output that fires when the LS ends naturally
 *
 * ExpandNode wiring:
 * ExecIn->CallFactory ->AssignProxy->BindOnFinished ->CallActivate->ThenOut
 * OnFinished fires: CustomEvent.Then -> OnFinished exec output
 * CutsceneAction: ProxyTempVar -> output data pin
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_PlayCutsceneSequence: public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode Interface 
	virtual void AllocateDefaultPins() override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	// UK2Node Interface 
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool IsNodePure() const override { return false; }

	// Well-Known Pin Names 
	static const FName PN_LevelSequence;
	static const FName PN_ContextName;
	static const FName PN_EnterTransition;
	static const FName PN_PlaybackSettings;
	static const FName PN_CutsceneAction;
	static const FName PN_OnFinished;
};
