// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Variables/ComposableCameraVariable.h"
#include "ComposableCameraBlueprintLibrary.generated.h"

class UComposableCameraModifierBase;
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
	/** Activate a composable camera by camera class, all derived from ComposableCameraCameraBase. \n
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param CameraClass The camera class to instantiate. \n
	 * @param TransitionParams The transition parameters to use. If no transition param is provided, camera cut will be used. \n
	 * @param ActivationParams Parameters to define some of the properties when activating a new camera, e.g., if it's transient and the node initializers. \n
	 * @param bNewInstance When the current running camera has the same camera class as CameraClass specified here, whether to instantiate a new camera. \n
	 * @param OnPreBeginplayEvent Do something after the camera is constructed and initialized, before BeginPlay() is called. You should initialize all camera and node parameters here. \n 
	 * @return The instanced camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "CameraClass"))
	static AComposableCameraCameraBase* ActivateComposableCameraByClass(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	FComposableCameraTransitionParams TransitionParams,
	FComposableCameraActivateParams ActivationParams,
	bool bNewInstance,
	FOnCameraFinishConstructed OnPreBeginplayEvent);

	/** Terminate current camera.
	 * @param WorldContextObject World context object.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static void TerminateCurrentCamera(const UObject* WorldContextObject);

	/** Add a modifier data asset.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param ModifierAsset Data asset for modifiers to add.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static void AddModifier(const UObject* WorldContextObject, AComposableCameraPlayerCamaraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset);

	/** Remove a modifier data asset.
	 * @param WorldContextObject World context object. \n
	 * @param PlayerCameraManager The player camera manager, must be a ComposableCameraPlayerCameraManager. \n
	 * @param ModifierAsset Data asset for modifiers to remove.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject"))
	static void RemoveModifier(const UObject* WorldContextObject, AComposableCameraPlayerCamaraManager* PlayerCameraManager, UComposableCameraNodeModifierDataAsset* ModifierAsset);

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