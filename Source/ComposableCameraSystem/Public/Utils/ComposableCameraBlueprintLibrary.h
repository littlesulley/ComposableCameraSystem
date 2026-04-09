// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Variables/ComposableCameraVariable.h"
#include "ComposableCameraBlueprintLibrary.generated.h"

class AComposableCameraPlayerCameraManager;
class UComposableCameraTransitionDataAsset;
class UComposableCameraModifierBase;
class UComposableCameraActionBase;
class AComposableCameraCameraBase;
class UComposableCameraVariable;

#define LOCTEXT_NAMESPACE "ComposableCameraSystemBlueprintLibrary"

/**
 * Blueprint library.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Create a composable camera by camera class and activation parameters. This function does not update other states. \n
	 * The newly created camera has no parent camera, and it does not do transition. \n
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param CameraClass The camera class to instantiate. \n
	 * @param ActivationParams Parameters to define some of the properties when activating a new camera, e.g., if it's transient and the node initializers. \n
	 * @return The instanced camera.
	 */
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "CameraClass"))
	static AComposableCameraCameraBase* CreateComposableCameraByClass(
		const UObject* WorldContextObject,
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		FComposableCameraActivateParams ActivationParams);
	
	/** Activate a composable camera by camera class, all derived from ComposableCameraCameraBase. \n
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param CameraClass The camera class to instantiate. \n
	 * @param ContextName Optional context name. If valid, the camera activates in the specified context (auto-pushing it if needed). If NAME_None, activates in the current active context. \n
	 * @param TransitionDataAsset The transition data asset. If no transition data asset is provided, camera cut will be used. \n
	 * @param ActivationParams Parameters to define some of the properties when activating a new camera, e.g., if it's transient and the node initializers. \n
	 * @param bNewInstance When the current running camera has the same camera class as CameraClass specified here, whether to instantiate a new camera. \n
	 * @param OnPreBeginplayEvent Do something after the camera is constructed and initialized, before BeginPlay() is called. You should initialize all camera and node parameters here. \n
	 * @return The instanced camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "CameraClass"))
	static AComposableCameraCameraBase* ActivateComposableCameraByClass(
		const UObject* WorldContextObject,
		AComposableCameraPlayerCameraManager* PlayerCameraManager,
		TSubclassOf<AComposableCameraCameraBase> CameraClass,
		UPARAM(meta = (GetOptions = "ComposableCameraSystem.ComposableCameraProjectSettings.GetContextNames")) FName ContextName,
		UComposableCameraTransitionDataAsset* TransitionDataAsset,
		FComposableCameraActivateParams ActivationParams,
		bool bNewInstance,
		FOnCameraFinishConstructed OnPreBeginplayEvent);

	/** Terminate the current camera — pops the active (top) context off the stack.
	 * The previous context resumes with an optional transition. Cannot pop the base context.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param TransitionOverride Optional transition. If nullptr, falls back to the resume camera's DefaultTransition. \n
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
	 * @param TransitionOverride Optional transition. If nullptr, falls back to the resume camera's DefaultTransition. \n
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

	/** Custom thunk function for setting runtime values of a composable camera variable.
	 * @param Variable The variable to set.
	 * @param NewRuntimeValue The new runtime value.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (BlueprintInternalUseOnly = "true", CustomStructureParam = "NewRuntimeValue"))
	static void SetComposableCameraVariableRuntimeValue(UComposableCameraVariable* Variable, UPARAM(Ref) const int32& NewRuntimeValue);
	DECLARE_FUNCTION(execSetComposableCameraVariableRuntimeValue)
	{
		P_GET_OBJECT(UComposableCameraVariable, Variable);

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
				LOCTEXT("InvalidSetComposableCameraVariableRuntimeValue", "Failed to resolve NewRuntimeValue for SetComposableCameraVariableRuntimeValue")
			);
			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		}
		else if (Variable)
		{
			P_NATIVE_BEGIN
			
			UClass* SourceClass = Variable->GetClass();
			FProperty* SourceProperty = FindFProperty<FProperty>(SourceClass, TEXT("RuntimeValue"));
			void* SourcePtr = SourceProperty->ContainerPtrToValuePtr<void>(Variable);

			SourceProperty->CopyCompleteValue(SourcePtr, ValuePtr);
			
			P_NATIVE_END
		}
		
	}

	/** Custom thunk function for getting runtime values of a composable camera variable.
	 * @param Variable The variable to get.
	 * @param ReturnValue The returned runtime value for this variable.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (BlueprintInternalUseOnly = "true", CustomStructureParam = "ReturnValue"))
	static void GetComposableCameraVariableRuntimeValue(UComposableCameraVariable* Variable, int32& ReturnValue);
	DECLARE_FUNCTION(execGetComposableCameraVariableRuntimeValue)
	{
		P_GET_OBJECT(UComposableCameraVariable, Variable);

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
				LOCTEXT("InvalidSetComposableCameraVariableRuntimeValue", "Failed to resolve ReturnValue for GetComposableCameraVariableRuntimeValue")
			);
			FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
		}
		else if (Variable)
		{
			P_NATIVE_BEGIN

			UClass* SourceClass = Variable->GetClass();
			FProperty* SourceProperty = FindFProperty<FProperty>(SourceClass, TEXT("RuntimeValue"));
			void* SourcePtr = SourceProperty->ContainerPtrToValuePtr<void>(Variable);

			SourceProperty->CopyCompleteValue(ValuePtr, SourcePtr);
			
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