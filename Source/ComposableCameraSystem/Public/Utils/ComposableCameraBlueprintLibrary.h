// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "UObject/UnrealType.h"
#include "ComposableCameraBlueprintLibrary.generated.h"

class AComposableCameraPlayerCameraManager;
class UComposableCameraTransitionDataAsset;
class UComposableCameraTypeAsset;
class UComposableCameraModifierBase;
class UComposableCameraActionBase;
class AComposableCameraCameraBase;
class UDataTable;

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
		Stack.MostRecentPropertyAddress = nullptr;
		Stack.MostRecentPropertyContainer = nullptr;
		Stack.StepCompiledIn<FProperty>(nullptr);

		const FProperty* ValueProperty = Stack.MostRecentProperty;
		void* ValuePtr = Stack.MostRecentPropertyAddress;

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
			if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(ValueProperty))
			{
				if (const FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty())
				{
					const int64 Value = Underlying->GetSignedIntPropertyValue(ValuePtr);
					Entry.PinType = EComposableCameraPinType::Enum;
					Entry.Data.SetNumUninitialized(sizeof(int64));
					FMemory::Memcpy(Entry.Data.GetData(), &Value, sizeof(int64));
					bHandled = true;
				}
			}
			else if (const FByteProperty* ByteProp = CastField<FByteProperty>(ValueProperty))
			{
				if (ByteProp->GetIntPropertyEnum() != nullptr)
				{
					const int64 Value = ByteProp->GetSignedIntPropertyValue(ValuePtr);
					Entry.PinType = EComposableCameraPinType::Enum;
					Entry.Data.SetNumUninitialized(sizeof(int64));
					FMemory::Memcpy(Entry.Data.GetData(), &Value, sizeof(int64));
					bHandled = true;
				}
			}

			if (!bHandled)
			{
				// Default path: any non-enum POD-shaped value (including FName,
				// which is 8 bytes and memcpy-safe). Copy the property's bytes
				// verbatim — the data block slot's GetPinTypeSize matches.
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
};


#undef LOCTEXT_NAMESPACE