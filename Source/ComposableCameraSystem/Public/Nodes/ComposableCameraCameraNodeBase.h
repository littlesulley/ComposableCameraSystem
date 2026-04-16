// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraCameraNodeBase.generated.h"

class AComposableCameraCameraBase;
class AComposableCameraPlayerCameraManager;
struct FComposableCameraPose;
struct FComposableCameraRuntimeDataBlock;

/**
 * Cached description of a single pin ↔ UPROPERTY binding on a camera node class.
 *
 * Produced once per concrete UClass by UComposableCameraCameraNodeBase::GetOrBuildPinBindings()
 * and reused by every instance of that class to resolve declared input pins without
 * per-frame reflection. Bindings are indexed by raw byte offset into the node UObject,
 * not by FProperty*, so the hot path is a straight pointer + switch.
 *
 * Only top-level pins matched to a top-level UPROPERTY on the node are recorded here.
 * Subobject property pins (compound "Subobject.Field" names) are still handled by
 * ApplySubobjectPinValues and are NOT included.
 */
struct FComposableCameraNodePinBinding
{
	/** Pin name as declared in GetPinDeclarations and matched against a UPROPERTY FName. */
	FName PinName;

	/** Pin type for typed dispatch at resolve time. */
	EComposableCameraPinType PinType = EComposableCameraPinType::Float;

	/** For PinType == Struct: the specific USTRUCT type. Ignored otherwise. */
	UScriptStruct* StructType = nullptr;

	/** For PinType == Enum: the specific UEnum the pin represents. Ignored otherwise.
	 *  The data block stores the value as a normalized int64; we use the backing
	 *  FProperty (by offset) to narrow-cast into the actual storage width
	 *  (uint8 / int32 / int64) at write time. Held as a weak ref so we don't
	 *  keep the enum alive via a non-UPROPERTY cache. */
	TWeakObjectPtr<UEnum> EnumType;

	/** For PinType == Enum: the backing FProperty, captured when the binding
	 *  table is built. Used to narrow-cast the int64 value from the data block
	 *  into the property's actual underlying width. nullptr for non-Enum pins. */
	const FProperty* BackingProperty = nullptr;

	/** Byte offset of the backing UPROPERTY into the node UObject (via FProperty::GetOffset_ForInternal). */
	int32 FieldOffset = 0;
};

/**
 * Class-level binding table for a camera node UClass.
 *
 * Built once (lazily, on first call) per concrete UClass and cached module-locally.
 * The node's TickNode prologue uses this to re-resolve every input pin into its
 * matching UPROPERTY field each frame, so subclass code can just read members
 * directly instead of calling GetInputPinValue<T>(FName).
 */
struct FComposableCameraNodePinBindingTable
{
	/** All input pins that have a matched top-level UPROPERTY on the node. */
	TArray<FComposableCameraNodePinBinding> InputBindings;
};

/**
 * Base node for all camera nodes.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories, ClassGroup = ComposableCameraSystem)
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

	// ─── Top-level Pin Auto-Resolution ───────────────────────────────────

	/**
	 * Re-resolve every declared top-level input pin into its matching UPROPERTY
	 * field on this node. Called automatically by TickNode() before OnTickNode()
	 * when ShouldAutoResolveInputPins() returns true.
	 *
	 * The binding between a pin and a UPROPERTY is by exact FName match against
	 * the name declared in GetPinDeclarations(). Each matched UPROPERTY must map
	 * cleanly to an EComposableCameraPinType (via TryMapPropertyToPinType) — if
	 * a pin has no backing UPROPERTY or the types don't align, the pin is skipped
	 * here and subclass code must use GetInputPinValue<T>() for it.
	 *
	 * Subobject property pins ("Subobject.Field" compound names) are NOT touched
	 * by this method — they are handled once at Initialize() via AutoApplySubobjectPinValues().
	 *
	 * Performance: the per-class binding table is built once on first use and
	 * cached module-locally. Per-frame cost is a tight switch-dispatch loop with
	 * no reflection, one raw memory write per matched pin.
	 */
	void ResolveAllInputPins();

protected:
	/**
	 * Opt-out hook for the auto-resolve-before-tick behavior. Override and return
	 * false on nodes that manage their own pin reads (e.g. nodes whose UPROPERTYs
	 * must survive across frames or are written by external actors mid-tick).
	 *
	 * Default: true — any node with a pin + matching UPROPERTY gets the member
	 * refreshed before every OnTickNode call.
	 */
	virtual bool ShouldAutoResolveInputPins() const { return true; }

private:
	/** Auto-iterate all Instanced UPROPERTY fields and declare their child pins. */
	void AutoDeclareSubobjectPins(TArray<FComposableCameraNodePinDeclaration>& OutPins) const;

	/** Auto-iterate all Instanced UPROPERTY fields and apply resolved pin values. */
	void AutoApplySubobjectPinValues();

	/**
	 * Get or lazily build the pin binding table for this node's concrete UClass.
	 * Thread-safe (game-thread-only in practice, but guarded with a critical
	 * section so Blueprint recompile on the game thread cannot race with
	 * ResolveAllInputPins on the same frame).
	 */
	const FComposableCameraNodePinBindingTable& GetOrBuildPinBindings() const;

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
	FName GetInputPinValueName(FName PinName) const;

	/** Read an enum input pin's resolved value into a wildcard enum out parameter.
	 *  The runtime data block stores enum slots as a normalized int64 (see
	 *  GetPinTypeSize / WriteEnumInt64ToProperty). The Blueprint-facing property
	 *  bound to OutValue may be FEnumProperty (underlying numeric width may be
	 *  uint8/int32/int64) or FByteProperty (always uint8). We narrow-cast the
	 *  int64 into the caller's property at execution time to keep the BP thunk
	 *  symmetric with SetParameterBlockValue in ComposableCameraBlueprintLibrary. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "ComposableCameraSystem|Node|Pins", meta = (CustomStructureParam = "OutValue"))
	void GetInputPinValueEnum(FName PinName, int32& OutValue) const;
	DECLARE_FUNCTION(execGetInputPinValueEnum)
	{
		P_GET_PROPERTY(FNameProperty, PinName);

		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentPropertyContainer = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

		const FProperty* OutValueProperty = Stack.MostRecentProperty;
		void* OutValuePtr = Stack.MostRecentPropertyAddress;

		P_FINISH;

		if (OutValueProperty == nullptr || OutValuePtr == nullptr)
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::AbortExecution,
				NSLOCTEXT("ComposableCameraSystem", "InvalidGetInputPinValueEnum", "Failed to resolve OutValue for GetInputPinValueEnum")
			);
			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			return;
		}

		P_NATIVE_BEGIN
		const int64 Value = P_THIS->GetInputPinValue<int64>(PinName);
		P_THIS->WriteEnumInt64ToBPProperty(OutValueProperty, OutValuePtr, Value);
		P_NATIVE_END
	}

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

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueName(FName PinName, FName Value);

	/** Write a wildcard enum value to an output pin. Mirrors GetInputPinValueEnum —
	 *  reads the enum from the caller's property (FEnumProperty or FByteProperty)
	 *  via the numeric-property API, normalizes to int64, and stores it in the
	 *  data block slot. Callers on the consumer side read it back through the
	 *  same int64 channel (see ResolveAllInputPins). */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "ComposableCameraSystem|Node|Pins", meta = (CustomStructureParam = "Value"))
	void SetOutputPinValueEnum(FName PinName, const int32& Value);
	DECLARE_FUNCTION(execSetOutputPinValueEnum)
	{
		P_GET_PROPERTY(FNameProperty, PinName);

		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentPropertyContainer = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

		const FProperty* ValueProperty = Stack.MostRecentProperty;
		const void* ValuePtr = Stack.MostRecentPropertyAddress;

		P_FINISH;

		if (ValueProperty == nullptr || ValuePtr == nullptr)
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::AbortExecution,
				NSLOCTEXT("ComposableCameraSystem", "InvalidSetOutputPinValueEnum", "Failed to resolve Value for SetOutputPinValueEnum")
			);
			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			return;
		}

		P_NATIVE_BEGIN
		int64 Normalized = 0;
		if (P_THIS->ReadBPEnumPropertyAsInt64(ValueProperty, ValuePtr, Normalized))
		{
			P_THIS->SetOutputPinValue<int64>(PinName, Normalized);
		}
		P_NATIVE_END
	}

	// Internal variable Blueprint accessors
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	float GetInternalVariableFloat(FName VariableName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	FVector GetInternalVariableVector(FName VariableName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	FName GetInternalVariableName(FName VariableName) const;

	/** Read an enum internal variable into a wildcard enum out parameter. Storage
	 *  is a normalized int64; the thunk narrow-casts into the caller's property
	 *  width. See GetInputPinValueEnum for the same pattern on the pin surface. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "ComposableCameraSystem|Node|Variables", meta = (CustomStructureParam = "OutValue"))
	void GetInternalVariableEnum(FName VariableName, int32& OutValue) const;
	DECLARE_FUNCTION(execGetInternalVariableEnum)
	{
		P_GET_PROPERTY(FNameProperty, VariableName);

		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentPropertyContainer = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

		const FProperty* OutValueProperty = Stack.MostRecentProperty;
		void* OutValuePtr = Stack.MostRecentPropertyAddress;

		P_FINISH;

		if (OutValueProperty == nullptr || OutValuePtr == nullptr)
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::AbortExecution,
				NSLOCTEXT("ComposableCameraSystem", "InvalidGetInternalVariableEnum", "Failed to resolve OutValue for GetInternalVariableEnum")
			);
			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			return;
		}

		P_NATIVE_BEGIN
		const int64 Value = P_THIS->GetInternalVariable<int64>(VariableName);
		P_THIS->WriteEnumInt64ToBPProperty(OutValueProperty, OutValuePtr, Value);
		P_NATIVE_END
	}

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	void SetInternalVariableFloat(FName VariableName, float Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	void SetInternalVariableVector(FName VariableName, FVector Value);

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Variables")
	void SetInternalVariableName(FName VariableName, FName Value);

	/** Write a wildcard enum value to an internal variable. Normalizes to int64
	 *  via the caller's FEnumProperty/FByteProperty and stores in the data block
	 *  slot — the same representation every other enum consumer reads. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "ComposableCameraSystem|Node|Variables", meta = (CustomStructureParam = "Value"))
	void SetInternalVariableEnum(FName VariableName, const int32& Value);
	DECLARE_FUNCTION(execSetInternalVariableEnum)
	{
		P_GET_PROPERTY(FNameProperty, VariableName);

		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentPropertyContainer = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

		const FProperty* ValueProperty = Stack.MostRecentProperty;
		const void* ValuePtr = Stack.MostRecentPropertyAddress;

		P_FINISH;

		if (ValueProperty == nullptr || ValuePtr == nullptr)
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::AbortExecution,
				NSLOCTEXT("ComposableCameraSystem", "InvalidSetInternalVariableEnum", "Failed to resolve Value for SetInternalVariableEnum")
			);
			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
			return;
		}

		P_NATIVE_BEGIN
		int64 Normalized = 0;
		if (P_THIS->ReadBPEnumPropertyAsInt64(ValueProperty, ValuePtr, Normalized))
		{
			P_THIS->SetInternalVariable<int64>(VariableName, Normalized);
		}
		P_NATIVE_END
	}

private:
	/** Shared helper used by all enum CustomThunks to normalize a Blueprint-side
	 *  enum property (FEnumProperty or FByteProperty) into the data block's
	 *  canonical int64 representation. Returns false if the property is not an
	 *  enum-backed numeric type. */
	bool ReadBPEnumPropertyAsInt64(const FProperty* ValueProperty, const void* ValuePtr, int64& OutValue) const;

	/** Inverse of ReadBPEnumPropertyAsInt64: narrow-cast a normalized int64 into
	 *  the caller's enum property (FEnumProperty underlying numeric width, or
	 *  FByteProperty uint8). No-op on unknown kinds. Non-const because
	 *  DECLARE_FUNCTION inline thunks need a matching this-pointer — the write
	 *  itself is into the caller's memory, not the node. */
	void WriteEnumInt64ToBPProperty(const FProperty* OutValueProperty, void* OutValuePtr, int64 Value) const;

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
