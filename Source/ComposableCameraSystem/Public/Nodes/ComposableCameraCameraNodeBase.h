// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraCameraNodeBase.generated.h"

class AComposableCameraCameraBase;
class AComposableCameraPlayerCameraManager;
struct FComposableCameraPose;
struct FComposableCameraRuntimeDataBlock;

/**
 * Base node for all camera nodes.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType, NotBlueprintable, CollapseCategories, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraCameraNodeBase
	: public UObject
{
	GENERATED_BODY()

public:
	void Initialize(AComposableCameraCameraBase* InOwningCamera, AComposableCameraPlayerCameraManager* InPlayerCameraManager);
	void TickNode(float DeltaTime, const FComposableCameraPose CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	FGameplayTag GetOwningCameraTag() const;

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	AComposableCameraCameraBase* GetOwningCamera() const { return OwningCamera; }

	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	AComposableCameraPlayerCameraManager* GetOwningPlayerCameraManager() const { return OwningPlayerCameraManager; }

	// ─── Pin System ──────────────────────────────────────────────────────

	/**
	 * Declare this node's input and output data pins.
	 * Override in subclasses to define pins. The editor reads these to generate
	 * visual pins, and the runtime uses them to allocate the RuntimeDataBlock.
	 *
	 * Default implementation returns empty (no pins).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "ComposableCameraSystem|Node|Pins")
	void GetPinDeclarations(TArray<FComposableCameraNodePinDeclaration>& OutPins) const;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const {}

	/** Set the runtime data block for this node. Called during camera instantiation from type assets. */
	void SetRuntimeDataBlock(FComposableCameraRuntimeDataBlock* InDataBlock, int32 InNodeIndex)
	{
		RuntimeDataBlock = InDataBlock;
		RuntimeNodeIndex = InNodeIndex;
	}

	/** Check if this node has a RuntimeDataBlock attached. */
	bool HasRuntimeDataBlock() const { return RuntimeDataBlock != nullptr; }

	// ─── Pin Gathering (preferred entry point) ──────────────────────────

	/**
	 * Gather ALL pin declarations: calls GetPinDeclarations() (the virtual chain),
	 * then auto-appends pins for every Instanced subobject UPROPERTY on this node.
	 *
	 * All external callers (editor, type-asset builder, runtime data-block allocator)
	 * should call this instead of GetPinDeclarations() directly, so that subobject
	 * pins are included without per-node boilerplate.
	 */
	void GatherAllPinDeclarations(TArray<FComposableCameraNodePinDeclaration>& OutPins) const;

	// ─── Subobject Pin Helpers ───────────────────────────────────────────

	/**
	 * Generate pin declarations for an Instanced subobject's EditAnywhere properties.
	 *
	 * Iterates the subobject's UClass properties, maps each to an
	 * EComposableCameraPinType via TryMapPropertyToPinType, and emits one input
	 * pin declaration per mappable property with the compound name
	 * "SubobjectPropertyName.FieldName".
	 *
	 * Properties tagged meta=(NoPinExposure) are skipped. Null subobjects are
	 * handled gracefully (no pins emitted).
	 *
	 * Prefer GatherAllPinDeclarations() which calls this automatically for every
	 * Instanced property. Direct calls are only needed for unusual subobject
	 * relationships that reflection cannot discover (e.g. subobjects stored in
	 * containers).
	 */
	void DeclareSubobjectPins(
		FName SubobjectPropertyName,
		const UObject* Subobject,
		TArray<FComposableCameraNodePinDeclaration>& OutPins) const;

	/**
	 * Apply resolved pin values to an Instanced subobject's properties.
	 *
	 * For each mappable EditAnywhere property on the subobject, checks if the
	 * compound pin name has a resolved value in the RuntimeDataBlock (via
	 * TryResolveInputPin). If so, writes the value into the subobject's UPROPERTY.
	 * If not resolved, the subobject retains its authored (Instanced editor) value.
	 *
	 * Safe to call when RuntimeDataBlock is null (no-op).
	 *
	 * Prefer letting Initialize() handle this automatically (it calls
	 * AutoApplySubobjectPinValues before OnInitialize). Direct calls are only
	 * needed for unusual subobject relationships.
	 */
	void ApplySubobjectPinValues(
		FName SubobjectPropertyName,
		UObject* Subobject);

private:
	/** Auto-iterate all Instanced UPROPERTY fields and declare their child pins. */
	void AutoDeclareSubobjectPins(TArray<FComposableCameraNodePinDeclaration>& OutPins) const;

	/** Auto-iterate all Instanced UPROPERTY fields and apply resolved pin values. */
	void AutoApplySubobjectPinValues();

public:

	// ─── Pin Value Accessors (C++ template) ──────────────────────────────

	/** Read an input pin's resolved value. Checks wired → exposed → default. */
	template<typename T>
	T GetInputPinValue(FName PinName) const;

	/** Write an output pin's value to the RuntimeDataBlock. */
	template<typename T>
	void SetOutputPinValue(FName PinName, const T& Value);

	/** Read a camera-level internal variable. */
	template<typename T>
	T GetInternalVariable(FName VariableName) const;

	/** Write a camera-level internal variable. */
	template<typename T>
	void SetInternalVariable(FName VariableName, const T& Value);

	// ─── Pin Value Accessors (Blueprint-callable, type-specific) ─────────

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	bool GetInputPinValueBool(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	int32 GetInputPinValueInt32(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	float GetInputPinValueFloat(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	double GetInputPinValueDouble(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	FVector GetInputPinValueVector(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	FRotator GetInputPinValueRotator(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	FTransform GetInputPinValueTransform(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	AActor* GetInputPinValueActor(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueBool(FName PinName, bool Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueInt32(FName PinName, int32 Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueFloat(FName PinName, float Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueDouble(FName PinName, double Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueVector(FName PinName, FVector Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueRotator(FName PinName, FRotator Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueTransform(FName PinName, FTransform Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueActor(FName PinName, AActor* Value);

	// Internal variable Blueprint accessors
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	float GetInternalVariableFloat(FName VariableName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	FVector GetInternalVariableVector(FName VariableName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	void SetInternalVariableFloat(FName VariableName, float Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	void SetInternalVariableVector(FName VariableName, FVector Value);

public:
	UFUNCTION()
	virtual void OnPreTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);

	UFUNCTION()
	virtual void OnPostTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	
protected:
	/**
	 * Per-activation one-shot initialization. Called exactly once per camera
	 * activation, after OwningCamera / OwningPlayerCameraManager / RuntimeDataBlock
	 * have all been wired. This is the hook for caching refs, instantiating
	 * internal objects, reading exposed parameters, and seeding any per-activation
	 * state the node needs before the first Tick.
	 *
	 * Nodes that need the outgoing camera's pose (what BeginPlayNode used to
	 * receive as CurrentCameraPose) should read it via
	 * OwningPlayerCameraManager->GetCurrentCameraPose() — this is the same value
	 * AActor::BeginPlay was passing in when it called BeginPlayCamera.
	 *
	 * BlueprintNativeEvent: Blueprint subclasses can override "InitializeNode"
	 * to replace the C++ implementation. C++ subclasses override
	 * OnInitialize_Implementation and should call Super when chaining.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "InitializeNode", Category = "ComposableCameraSystem|Node")
	void OnInitialize();
	virtual void OnInitialize_Implementation() {}

	/**
	 * Main node logic implemented here. This node can read/write pin values and/or CameraPose.
	 * @param DeltaTime Delta time for this frame.
	 * @param CurrentCameraPose Current camera pose.
	 * @param OutCameraPose Output camera pose for this node.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "TickNode", Category = "ComposableCameraSystem|Node")
	void OnTickNode(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose);
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) {}

protected:

protected:
	UPROPERTY(BlueprintReadOnly, Transient, Category = "ComposableCameraSystem|Node")
	AComposableCameraCameraBase* OwningCamera;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "ComposableCameraSystem|Node")
	AComposableCameraPlayerCameraManager* OwningPlayerCameraManager;

	/** Runtime data block for the pin system. Set when running from a camera type asset. */
	FComposableCameraRuntimeDataBlock* RuntimeDataBlock = nullptr;

	/** This node's index in the camera type asset's NodeTemplates array. */
	int32 RuntimeNodeIndex = INDEX_NONE;

#if WITH_EDITOR
public:
	/** Snapshot of the camera pose captured immediately after this node's
	 *  TickNode completed. Written by TickNode's debug wrapper; read by the
	 *  editor's debug ticker to populate the graph overlay. Zero-cost in
	 *  shipping builds (compiled out). */
	FComposableCameraPose DebugPoseAfterTick;

	/** Set to true each frame this node is ticked, cleared at the start of
	 *  TickCamera so the editor can distinguish active vs. skipped nodes. */
	bool bDebugWasTickedThisFrame = false;
#endif
};

// ─── Template Implementations ──────────────────────────────────────────

template<typename T>
T UComposableCameraCameraNodeBase::GetInputPinValue(FName PinName) const
{
	if (RuntimeDataBlock)
	{
		T Result{};
		if (RuntimeDataBlock->TryResolveInputPin<T>(RuntimeNodeIndex, PinName, Result))
		{
			return Result;
		}
	}
	return T{};
}

template<typename T>
void UComposableCameraCameraNodeBase::SetOutputPinValue(FName PinName, const T& Value)
{
	if (RuntimeDataBlock)
	{
		RuntimeDataBlock->WriteOutputPin<T>(RuntimeNodeIndex, PinName, Value);
	}
}

template<typename T>
T UComposableCameraCameraNodeBase::GetInternalVariable(FName VariableName) const
{
	if (RuntimeDataBlock && RuntimeDataBlock->HasInternalVariable(VariableName))
	{
		return RuntimeDataBlock->ReadInternalVariable<T>(VariableName);
	}
	return T{};
}

template<typename T>
void UComposableCameraCameraNodeBase::SetInternalVariable(FName VariableName, const T& Value)
{
	if (RuntimeDataBlock)
	{
		RuntimeDataBlock->WriteInternalVariable<T>(VariableName, Value);
	}
}
