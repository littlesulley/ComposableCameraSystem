// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Variables/ComposableCameraVariable.h"
#include "ComposableCameraBlueprintLibrary.generated.h"

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
	 * @return The instanced camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Camera", meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "CameraClass"))
	static AComposableCameraCameraBase* ActivateComposableCameraByClass(
	const UObject* WorldContextObject,
	AComposableCameraPlayerCamaraManager* PlayerCameraManager,
	TSubclassOf<AComposableCameraCameraBase> CameraClass,
	FComposableCameraTransitionParams TransitionParams,
	FComposableCameraActivateParams ActivationParams,
	bool bNewInstance);

	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, CustomThunk, meta = (CustomStructureParam = "NewRuntimeValue"))
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
		else
		{
			P_NATIVE_BEGIN

			UClass* SourceClass = Variable->GetClass();
			FProperty* SourceProperty = FindFProperty<FProperty>(SourceClass, TEXT("RuntimeValue"));
			void* SourcePtr = SourceProperty->ContainerPtrToValuePtr<void>(Variable);

			SourceProperty->CopyCompleteValue(ValuePtr, SourcePtr);

			P_NATIVE_END
		}
	}
};


#undef LOCTEXT_NAMESPACE