// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "UObject/UnrealType.h"
#include "ComposableCameraBlueprintLibrary.generated.h"

class AComposableCameraPlayerCameraManager;
class UComposableCameraTransitionDataAsset;
class UAsyncPlayCutsceneSequence;
class ULevelSequence;
class UComposableCameraTypeAsset;
class UComposableCameraModifierBase;
class UComposableCameraActionBase;
class AComposableCameraCameraBase;
class UDataTable;
class UComposableCameraPatchTypeAsset;
class UComposableCameraPatchHandle;
struct FComposableCameraPatchActivateParams;

#define LOCTEXT_NAMESPACE "ComposableCameraSystemBlueprintLibrary"

/**
 * Blueprint library.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Activate a composable camera from a Camera Type Asset (data-driven workflow). \n
	 * The type asset defines the node composition, exposed parameters, internal variables, and default transition. \n
	 *
	 * This function is hidden from the Blueprint palette because designers should author
	 * activation calls through UK2Node_ActivateComposableCamera instead — that K2 node
	 * generates a typed pin per exposed parameter and expands into this call at compile
	 * time. Exposing the raw FComposableCameraParameterBlock form in the BP menu would
	 * create a second, untyped, strictly worse workflow alongside the K2 node.
	 *
	 * @param WorldContextObject World context object. \n
	 * @param PlayerIndex Player index (0 for single player). \n
	 * @param CameraTypeAsset The camera type data asset to instantiate. \n
	 * @param ContextName Optional context name. If valid, the camera activates in the specified context (auto-pushing if needed). If NAME_None, activates in the current active context. \n
	 * @param TransitionOverride Optional transition. If nullptr, the type asset's EnterTransition is used. \n
	 * @param Parameters Parameter block with exposed parameter values for this camera type. \n
	 * @param ActivationParams Parameters to define transient, lifetime, and pose preservation behavior. \n
	 * @return The activated camera instance.
	 */
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static AComposableCameraCameraBase* ActivateComposableCameraFromTypeAsset(
		const UObject* WorldContextObject,
		int32 PlayerIndex,
		UComposableCameraTypeAsset* CameraTypeAsset,
		UPARAM(meta = (GetOptions = "ComposableCameraSystem.ComposableCameraProjectSettings.GetContextNames")) FName ContextName,
		UComposableCameraTransitionDataAsset* TransitionOverride,
		FComposableCameraParameterBlock Parameters,
		FComposableCameraActivateParams ActivationParams);

	/** Activate a composable camera from a DataTable row.
	 *
	 * The row is expected to be of type FComposableCameraParameterTableRow. The
	 * row's CameraType is sync-loaded and its Parameters.Values map is parsed via
	 * FComposableCameraParameterBlock::ApplyStringValue using the type's exposed
	 * parameters. Parse failures are logged to LogComposableCameraSystem and
	 * fall back to the node pin's authored default so activation never
	 * refuses to proceed on a single bad cell; parameters with no valid source
	 * at all end up at the runtime data block's zero-initialized default.
	 *
	 * If OverrideParameters is non-empty, its entries take precedence over the
	 * row-parsed values — an override entry for a given name replaces the row
	 * value entirely. This is how the K2 node's "Add Override Pin" feature works:
	 * the row provides the base configuration, and the override block carries
	 * per-call-site adjustments authored on the K2 node's dynamic pins.
	 *
	 * This function is hidden from the Blueprint palette because designers should
	 * author DataTable-driven activation calls through UK2Node_ActivateComposableCameraFromDataTable
	 * instead — that K2 node provides a row-struct-filtered DataTable asset picker
	 * and a live row-name dropdown, and expands into this call at compile time.
	 *
	 * @param WorldContextObject   World context object.
	 * @param PlayerIndex          Player index (0 for single player).
	 * @param DataTable            DataTable asset containing the row.
	 * @param RowName              Name of the row to activate.  The row's
	 *                             ActivationParams struct is used directly.
	 * @param OverrideParameters   Optional per-call-site overrides that take
	 *                             precedence over the row's string-map values.
	 * @return The activated camera instance, or nullptr on failure.
	 */
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, Category = "ComposableCameraSystem|Camera",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Activate Camera From Data Table"))
	static AComposableCameraCameraBase* ActivateComposableCameraFromDataTable(
		const UObject* WorldContextObject,
		int32 PlayerIndex,
		UDataTable* DataTable,
		FName RowName,
		FComposableCameraParameterBlock OverrideParameters);

	/**
	 * Create and register a PlayCutsceneSequence action. Does NOT call Activate().
	 *
	 * This is BlueprintInternalUseOnly because the Blueprint entry point is
	 * UK2Node_PlayCutsceneSequence, which calls this function in its ExpandNode
	 * and then binds delegates + calls Activate() as separate expansion steps.
	 *
	 * The factory lives here (not on UAsyncPlayCutsceneSequence) to prevent
	 * UK2Node_AsyncAction from auto-registering a second, broken async node.
	 */
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly,
		Category = "ComposableCameraSystem|LevelSequence",
		meta = (WorldContext = "WorldContextObject"))
	static UAsyncPlayCutsceneSequence* PlayCutsceneSequence(
		UObject* WorldContextObject,
		ULevelSequence* InLevelSequence,
		UPARAM(meta = (GetOptions = "ComposableCameraSystem.ComposableCameraProjectSettings.GetContextNames")) FName ContextName,
		UComposableCameraTransitionDataAsset* EnterTransition = nullptr,
		FMovieSceneSequencePlaybackSettings PlaybackSettings = FMovieSceneSequencePlaybackSettings());

	/** Terminate the current camera — pops the active (top) context off the stack.
	 * The previous context resumes with an optional transition. Cannot pop the base context.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param TransitionOverride Optional transition. If nullptr, falls back to the resume camera's EnterTransition. \n
	 * @param ActivationParams Optional activation params for the resume camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Context", meta = (WorldContext = "WorldContextObject"))
	static void TerminateCurrentCamera(
		const UObject* WorldContextObject,
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		UComposableCameraTransitionDataAsset* TransitionOverride = nullptr,
		FComposableCameraActivateParams ActivationParams = FComposableCameraActivateParams());

	/** Pop a specific camera context by name.
	 * If this is the active context, the previous context resumes with an optional transition.
	 * Cannot pop the base context if it is the last one remaining.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param ContextName The name identifying which context to pop. \n
	 * @param TransitionOverride Optional transition. If nullptr, falls back to the resume camera's EnterTransition. \n
	 * @param ActivationParams Optional activation params for the resume camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Context", meta = (WorldContext = "WorldContextObject"))
	static void PopCameraContext(
		const UObject* WorldContextObject,
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		UPARAM(meta = (GetOptions = "ComposableCameraSystem.ComposableCameraProjectSettings.GetContextNames")) FName ContextName,
		UComposableCameraTransitionDataAsset* TransitionOverride = nullptr,
		FComposableCameraActivateParams ActivationParams = FComposableCameraActivateParams());

	/** Get the current depth of the camera context stack.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @return The number of contexts on the stack (1 = base context only).
	 */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Context", meta = (WorldContext = "WorldContextObject"))
	static int32 GetCameraContextStackDepth(
		const UObject* WorldContextObject,
		AComposableCameraPlayerCameraManager* PlayerCameraManager);

	/** Get the name of the currently active (top) context.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @return The active context's name.
	 */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Context", meta = (WorldContext = "WorldContextObject"))
	static FName GetActiveContextName(
		const UObject* WorldContextObject,
		AComposableCameraPlayerCameraManager* PlayerCameraManager);

	/** Add a modifier data asset.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param ModifierAsset Data asset for modifiers to add.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static void AddModifier(const UObject* WorldContextObject, AComposableCameraPlayerCameraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset);

	/** Remove a modifier data asset.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param ModifierAsset Data asset for modifiers to remove.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static void RemoveModifier(const UObject* WorldContextObject, AComposableCameraPlayerCameraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset);
	
	/** Add a camera action. Multiple actions of the same class are not allowed.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param ActionClass The class of action you want to add. \n
	 * @param bOnlyForCurrentCamera If this action is only valid for current running camera. If true, the action will expire when the current camera is blended out.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "ActionClass"))
	static UComposableCameraActionBase* AddAction(const UObject* WorldContextObject, AComposableCameraPlayerCameraManager* PlayerCameraManager, TSubclassOf<UComposableCameraActionBase> ActionClass, bool bOnlyForCurrentCamera = false);

	/** Expire a camera action.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param ActionClass The class of action you want to expire.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static void ExpireAction(const UObject* WorldContextObject, AComposableCameraPlayerCameraManager* PlayerCameraManager, TSubclassOf<UComposableCameraActionBase> ActionClass);

	/** Get player camera manager and cast it to ComposableCameraPlayerCameraManager. Can be null if it's not the type.
	 * @param Index Player index.
	 */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static AComposableCameraPlayerCameraManager* GetComposableCameraPlayerCameraManager(const UObject* WorldContextObject, int Index);

	// ─── Camera Patch ─────────────────────────────────────────────────────
	//
	// BP authors interact with AddCameraPatch through UK2Node_AddCameraPatch,
	// which generates a typed pin per exposed parameter / exposed variable on
	// the selected PatchAsset and expands into the call below at compile time.
	// The raw library entry is BlueprintInternalUseOnly so the untyped form
	// (with a hand-built FComposableCameraParameterBlock) doesn't compete with
	// the K2 node in the palette.

	/**
	 * Add a Camera Patch on the active context's Director, or on a named
	 * context if ContextName is non-None.
	 *
	 * Hidden from the Blueprint palette — designers should author this through
	 * UK2Node_AddCameraPatch, which generates a typed pin per exposed parameter
	 * / exposed variable on the chosen Patch asset and expands into this call
	 * at compile time.
	 *
	 * @param PlayerIndex   Player index (0 for single player). Resolved to a
	 *                      UComposableCameraPlayerCameraManager via
	 *                      GetComposableCameraPlayerCameraManager — matches
	 *                      ActivateComposableCameraFromTypeAsset's PlayerIndex
	 *                      surface so the two K2 nodes feel like siblings.
	 * @param PatchAsset    The Patch type asset (a subclass of CameraTypeAsset).
	 * @param ContextName   NAME_None → target the current active context (the
	 *                      common case). Otherwise the context with that name
	 *                      must already be on the stack — the patch attaches to
	 *                      THAT context's Director, even if it is currently
	 *                      buried below the active context. Patches on a buried
	 *                      context tick (their Director's Evaluate still runs)
	 *                      but are not user-visible until the context returns
	 *                      to the top — useful for staging gameplay overlays
	 *                      while a cutscene is playing.
	 * @param Params        Envelope / lifetime / composition activation parameters; see FComposableCameraPatchActivateParams docs for sentinels.
	 * @param Parameters    Exposed-parameter / exposed-variable values for the Patch evaluator. Same keyspace as the block accepted by ActivateComposableCameraFromTypeAsset.
	 * @return              A handle to the added Patch (nullptr on rejection — see log warning for the reason).
	 */
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, Category = "ComposableCameraSystem|Patch", meta = (WorldContext = "WorldContextObject"))
	static UComposableCameraPatchHandle* AddCameraPatch(
		const UObject* WorldContextObject,
		int32 PlayerIndex,
		UComposableCameraPatchTypeAsset* PatchAsset,
		UPARAM(meta = (GetOptions = "ComposableCameraSystem.ComposableCameraProjectSettings.GetContextNames")) FName ContextName,
		FComposableCameraPatchActivateParams Params,
		FComposableCameraParameterBlock Parameters);

	/**
	 * Manually retire a Patch by its handle. Flips the Patch to Exiting via
	 * the normal envelope ramp; the actual removal happens at the end of the
	 * next Apply pass.
	 *
	 * @param Handle               Handle returned from AddCameraPatch.
	 * @param ExitDurationOverride < 0 → use the Patch's authored ExitDuration. >= 0 replaces the per-Patch ExitDuration (pass 0 for an instant cut-off).
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Patch")
	static void ExpireCameraPatch(UComposableCameraPatchHandle* Handle, float ExitDurationOverride = -1.f);

	/**
	 * Soft-expire every active Patch on the named context's Director. Each
	 * Patch flips to Exiting via its normal envelope ramp — mid-Entering
	 * patches fade out from their current alpha rather than popping to 1
	 * first. Already-Exiting / Expired patches are left alone (idempotent).
	 *
	 * @param ContextName          The context whose Director's PatchManager to sweep. NAME_None → active context.
	 * @param ExitDurationOverride < 0 → each patch keeps its own ExitDuration. >= 0 replaces every patch's ExitDuration uniformly.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Patch", meta = (WorldContext = "WorldContextObject"))
	static void ExpireAllPatchesOnContext(
		const UObject* WorldContextObject,
		int32 PlayerIndex,
		UPARAM(meta = (GetOptions = "ComposableCameraSystem.ComposableCameraProjectSettings.GetContextNames")) FName ContextName,
		float ExitDurationOverride = -1.f);

	// ─── Camera Patch — handle introspection (BP-pure) ────────────────────
	//
	// All four are weak-handle-safe: a stale handle (instance destroyed)
	// returns false / Expired / 0 / 0 respectively. Callers do NOT need to
	// null-check the handle on every call — a null handle is treated the same
	// as a stale one. Match the §12.1 surface of PatchSystemProposal.

	/** True iff the handle's instance is still alive AND in Entering / Active phase. */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Patch")
	static bool IsPatchActive(const UComposableCameraPatchHandle* Handle);

	/** Current lifecycle phase. Returns Expired when the handle is stale. */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Patch")
	static EComposableCameraPatchPhase GetPatchPhase(const UComposableCameraPatchHandle* Handle);

	/** Current envelope alpha [0..1]. Returns 0 when the handle is stale. */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Patch")
	static float GetPatchAlpha(const UComposableCameraPatchHandle* Handle);

	/** Cumulative time spent in Active phase (seconds). The Duration channel
	 *  fires when this reaches the Patch's resolved Duration. Returns 0 for a
	 *  stale handle or a Patch that hasn't reached Active yet. */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Patch")
	static float GetPatchElapsedTime(const UComposableCameraPatchHandle* Handle);

	/** Custom thunk function for setting a single value in a ParameterBlock.
	 * Used internally by UK2Node_ActivateComposableCamera to fill the parameter block at compile time.
	 * @param ParameterBlock The parameter block to modify.
	 * @param ParameterName The parameter name key.
	 * @param Value The value to set (type-erased via CustomStructureParam).
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (BlueprintInternalUseOnly = "true", CustomStructureParam = "Value"))
	static void SetParameterBlockValue(UPARAM(ref) FComposableCameraParameterBlock& ParameterBlock, FName ParameterName, const int32& Value);
	DECLARE_FUNCTION(execSetParameterBlockValue)
	{
		// Read the ParameterBlock reference.
		P_GET_STRUCT_REF(FComposableCameraParameterBlock, ParameterBlock);
		P_GET_PROPERTY(FNameProperty, ParameterName);

		// Read the type-erased value.
		// StepCompiledIn needs a write buffer for literal values (const enum,
		// const object, etc.) that have no backing property address. 64 bytes
		// covers all pin-supported types (largest is FTransform at ~48 bytes).
		uint8 ValueBuffer[64];
		FMemory::Memzero(ValueBuffer, sizeof(ValueBuffer));

		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentPropertyContainer = nullptr;
		Stack.StepCompiledIn<FProperty>(ValueBuffer);

		const FProperty* ValueProperty = Stack.MostRecentProperty;
		void* ValuePtr = Stack.MostRecentPropertyAddress;
		if (!ValuePtr)
		{
			// Literal value was written into our buffer.
			ValuePtr = ValueBuffer;
		}

		P_FINISH;

		if (ValueProperty == nullptr || ValuePtr == nullptr)
		{
			FBlueprintExceptionInfo ExceptionInfo(
				EBlueprintExceptionType::AbortExecution,
				LOCTEXT("InvalidSetParameterBlockValue", "Failed to resolve Value for SetParameterBlockValue")
			);
			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		}
		else
		{
			// Delegate properties are routed to the parallel DelegateValues
			// map — they are not POD and cannot be memcpy'd into the byte
			// array. ApplyDelegateBindings writes them to the node's
			// UPROPERTY at activation time via reflection.
			// Handled before P_NATIVE_BEGIN so the early return doesn't
			// break the paired P_NATIVE_BEGIN / P_NATIVE_END macros (which
			// expand to try/catch in editor builds).
			if (const FDelegateProperty* DelegateProp = CastField<FDelegateProperty>(ValueProperty))
			{
				const FScriptDelegate& Delegate = *static_cast<const FScriptDelegate*>(ValuePtr);
				ParameterBlock.SetDelegate(ParameterName, Delegate);
				return; // early out — delegate stored, nothing else to do
			}

			P_NATIVE_BEGIN

			// Enum pin special case: the runtime data block stores enum slots as
			// normalized int64 (see the EComposableCameraPinType::Enum branch of
			// GetPinTypeSize and the WriteEnumInt64ToProperty helper in
			// ComposableCameraCameraNodeBase.cpp). Blueprint enum pins, however,
			// arrive here as either FEnumProperty (underlying numeric width
			// uint8/int32/int64) or FByteProperty-with-Enum (always 1 byte). If
			// we copied the native bytes verbatim the slot would be the wrong
			// width and the eventual ApplyParameterBlock memcpy would either
			// truncate or short-write. Instead, read the value as int64 via the
			// numeric property API and store the canonical 8-byte form so the
			// downstream copy and the ResolveAllInputPins narrow-cast both find
			// what they expect.

			FComposableCameraParameterValue Entry;
			bool bHandled = false;

			// ── Enum ────────────────────────────────────────────────
			// Normalize to int64 regardless of the backing property's
			// native width (uint8 for FByteProperty, variable for
			// FEnumProperty). The data block always stores enums as
			// 8-byte int64 slots.
			if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(ValueProperty))
			{
				if (const FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty())
				{
					const int64 Value = Underlying->GetSignedIntPropertyValue(ValuePtr);
					Entry.Set<int64>(EComposableCameraPinType::Enum, Value);
					bHandled = true;
				}
			}
			else if (const FByteProperty* ByteProp = CastField<FByteProperty>(ValueProperty))
			{
				if (ByteProp->GetIntPropertyEnum() != nullptr)
				{
					const int64 Value = ByteProp->GetSignedIntPropertyValue(ValuePtr);
					Entry.Set<int64>(EComposableCameraPinType::Enum, Value);
					bHandled = true;
				}
			}

			// ── Object / Actor ──────────────────────────────────────
			// FObjectProperty operates on TObjectPtr<T> whose memory
			// layout may differ from a raw UObject* in some engine
			// configurations. Extract the resolved pointer via the
			// property API to guarantee we store a plain UObject*.
			if (!bHandled)
			{
				if (const FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(ValueProperty))
				{
					UObject* ObjValue = ObjectProp->GetObjectPropertyValue(ValuePtr);
					const EComposableCameraPinType ObjPinType =
						(ObjectProp->PropertyClass && ObjectProp->PropertyClass->IsChildOf(AActor::StaticClass()))
						? EComposableCameraPinType::Actor
						: EComposableCameraPinType::Object;
					Entry.Set<UObject*>(ObjPinType, ObjValue);
					bHandled = true;
				}
			}

			// ── Bool ────────────────────────────────────────────────
			// FBoolProperty may be a bitfield; GetPropertyValue
			// handles the mask and returns a clean bool.
			if (!bHandled)
			{
				if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(ValueProperty))
				{
					const bool Value = BoolProp->GetPropertyValue(ValuePtr);
					Entry.Set<bool>(EComposableCameraPinType::Bool, Value);
					bHandled = true;
				}
			}

			// ── Default (POD) ───────────────────────────────────────
			// Covers Int32, Float, Double, FName, FVector, FRotator,
			// FTransform, and any other memcpy-safe type. The data
			// block slot's GetPinTypeSize matches GetSize() for all
			// of these. PinType is left at the default (Float) —
			// it's unused in the CopyRawTo → ReadValue path.
			if (!bHandled)
			{
				const int32 Size = ValueProperty->GetSize();
				Entry.Data.SetNumUninitialized(Size);
				ValueProperty->CopyCompleteValue(Entry.Data.GetData(), ValuePtr);
			}
			ParameterBlock.Values.Add(ParameterName, MoveTemp(Entry));

			P_NATIVE_END
		}
	}

	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe, BlueprintInternalUseOnly="true"))
	static FVector MakeLiteralVector(FVector Value);

	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe, BlueprintInternalUseOnly="true"))
	static FVector4 MakeLiteralVector4(FVector4 Value);

	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe, BlueprintInternalUseOnly="true"))
	static FVector2D MakeLiteralVector2D(FVector2D Value);
	
	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe, BlueprintInternalUseOnly="true"))
	static FRotator MakeLiteralRotator(FRotator Value);

	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe, BlueprintInternalUseOnly="true"))
	static FTransform MakeLiteralTransform(FTransform Value);

	UFUNCTION(BlueprintPure, meta=(BlueprintInternalUseOnly="true"))
	static UObject* MakeLiteralObject(UObject* Value);

	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe, BlueprintInternalUseOnly="true"))
	static FName MakeLiteralName(FName Value);

	UFUNCTION(BlueprintPure, meta=(BlueprintThreadSafe, BlueprintInternalUseOnly="true"))
	static uint8 MakeLiteralByte(uint8 Value);
};


#undef LOCTEXT_NAMESPACE