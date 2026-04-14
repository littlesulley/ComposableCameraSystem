// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCameraNodeBase.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "ComposableCameraSystemModule.h"

void UComposableCameraCameraNodeBase::Initialize(AComposableCameraCameraBase* InOwningCamera, AComposableCameraPlayerCameraManager* InPlayerCameraManager)
{
	OwningCamera = InOwningCamera;
	OwningPlayerCameraManager = InPlayerCameraManager;

	// Auto-apply subobject pin values before the subclass's OnInitialize,
	// so that interpolator/subobject properties reflect any wired or exposed
	// overrides before the subclass builds typed instances from them.
	AutoApplySubobjectPinValues();

	OnInitialize();
}

void UComposableCameraCameraNodeBase::TickNode(float DeltaTime, const FComposableCameraPose CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OnTickNode(DeltaTime, CurrentCameraPose, OutCameraPose);

#if WITH_EDITOR
	DebugPoseAfterTick = OutCameraPose;
	bDebugWasTickedThisFrame = true;
#endif
}

FGameplayTag UComposableCameraCameraNodeBase::GetOwningCameraTag() const
{
	return OwningCamera ? OwningCamera->CameraTag : FGameplayTag::EmptyTag;
}

void UComposableCameraCameraNodeBase::OnPreTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Intentionally empty. Override in C++ subclasses if pre-tick behavior is needed.
}

void UComposableCameraCameraNodeBase::OnPostTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Intentionally empty. Override in C++ subclasses if post-tick behavior is needed.
}

// ─── Pin Gathering ─────────────────────────────────────────────────────

void UComposableCameraCameraNodeBase::GatherAllPinDeclarations(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Step 1: subclass-declared pins (virtual chain).
	GetPinDeclarations(OutPins);

	// Step 2: auto-discover Instanced subobject pins.
	AutoDeclareSubobjectPins(OutPins);
}

void UComposableCameraCameraNodeBase::AutoDeclareSubobjectPins(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (!Property->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			continue;
		}

		const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property);
		if (!ObjProp)
		{
			continue;
		}

		const UObject* Subobject = ObjProp->GetObjectPropertyValue(
			ObjProp->ContainerPtrToValuePtr<void>(this));

		DeclareSubobjectPins(Property->GetFName(), Subobject, OutPins);
	}
}

void UComposableCameraCameraNodeBase::AutoApplySubobjectPinValues()
{
	if (!RuntimeDataBlock)
	{
		return;
	}

	for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (!Property->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			continue;
		}

		FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property);
		if (!ObjProp)
		{
			continue;
		}

		UObject* Subobject = ObjProp->GetObjectPropertyValue(
			ObjProp->ContainerPtrToValuePtr<void>(this));

		ApplySubobjectPinValues(Property->GetFName(), Subobject);
	}
}

// ─── Subobject Pin Helpers ──────────────────────────────────────────────

void UComposableCameraCameraNodeBase::DeclareSubobjectPins(
	FName SubobjectPropertyName,
	const UObject* Subobject,
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	if (!Subobject)
	{
		return;
	}

	const FString SubobjectPrefix = SubobjectPropertyName.ToString() + TEXT(".");
	const FText SubobjectDisplayName = FText::FromString(
		FName::NameToDisplayString(SubobjectPropertyName.ToString(), /*bIsBool=*/ false));

	for (TFieldIterator<FProperty> PropIt(Subobject->GetClass()); PropIt; ++PropIt)
	{
		const FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Opt-out: subobject authors can tag properties with NoPinExposure.
		if (Property->HasMetaData(TEXT("NoPinExposure")))
		{
			continue;
		}

		EComposableCameraPinType PinType;
		UScriptStruct* StructType = nullptr;
		if (!TryMapPropertyToPinType(Property, PinType, StructType))
		{
			continue;
		}

		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = FName(*(SubobjectPrefix + Property->GetName()));
		PinDecl.DisplayName = FText::Format(
			NSLOCTEXT("ComposableCameraSystem", "SubobjectPinDisplayFmt", "{0} > {1}"),
			SubobjectDisplayName,
			Property->GetDisplayNameText());
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = PinType;
		PinDecl.StructType = StructType;
		PinDecl.Tooltip = Property->GetToolTipText();

		// Serialize the current property value as the default string.
		FString ValueString;
		Property->ExportTextItem_Direct(ValueString, Property->ContainerPtrToValuePtr<void>(Subobject), nullptr, nullptr, PPF_None);
		PinDecl.DefaultValueString = MoveTemp(ValueString);

		OutPins.Add(MoveTemp(PinDecl));
	}
}

void UComposableCameraCameraNodeBase::ApplySubobjectPinValues(
	FName SubobjectPropertyName,
	UObject* Subobject)
{
	if (!Subobject || !RuntimeDataBlock)
	{
		return;
	}

	const FString SubobjectPrefix = SubobjectPropertyName.ToString() + TEXT(".");

	for (TFieldIterator<FProperty> PropIt(Subobject->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		if (Property->HasMetaData(TEXT("NoPinExposure")))
		{
			continue;
		}

		EComposableCameraPinType PinType;
		UScriptStruct* StructType = nullptr;
		if (!TryMapPropertyToPinType(Property, PinType, StructType))
		{
			continue;
		}

		const FName CompoundPinName(*(SubobjectPrefix + Property->GetName()));
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Subobject);

		// Type-dispatch: read from the data block and write into the subobject property.
		switch (PinType)
		{
		case EComposableCameraPinType::Bool:
			{ bool V; if (RuntimeDataBlock->TryResolveInputPin<bool>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<bool*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Int32:
			{ int32 V; if (RuntimeDataBlock->TryResolveInputPin<int32>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<int32*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Float:
			{ float V; if (RuntimeDataBlock->TryResolveInputPin<float>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<float*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Double:
			{ double V; if (RuntimeDataBlock->TryResolveInputPin<double>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<double*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector2D:
			{ FVector2D V; if (RuntimeDataBlock->TryResolveInputPin<FVector2D>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FVector2D*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector3D:
			{ FVector V; if (RuntimeDataBlock->TryResolveInputPin<FVector>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FVector*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector4:
			{ FVector4 V; if (RuntimeDataBlock->TryResolveInputPin<FVector4>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FVector4*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Rotator:
			{ FRotator V; if (RuntimeDataBlock->TryResolveInputPin<FRotator>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FRotator*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Transform:
			{ FTransform V; if (RuntimeDataBlock->TryResolveInputPin<FTransform>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FTransform*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Actor:
			{
				AActor* V = nullptr;
				if (RuntimeDataBlock->TryResolveInputPin<AActor*>(RuntimeNodeIndex, CompoundPinName, V))
				{
					// Validate before writing into the UPROPERTY — the data block
					// stores pointers as type-erased bytes invisible to GC, so a
					// destroyed actor leaves a dangling pointer rather than null.
					*static_cast<AActor**>(ValuePtr) = IsValid(V) ? V : nullptr;
				}
			}
			break;
		case EComposableCameraPinType::Object:
			{
				UObject* V = nullptr;
				if (RuntimeDataBlock->TryResolveInputPin<UObject*>(RuntimeNodeIndex, CompoundPinName, V))
				{
					*static_cast<UObject**>(ValuePtr) = IsValid(V) ? V : nullptr;
				}
			}
			break;
		case EComposableCameraPinType::Struct:
			// Struct pin resolution uses raw memcpy at the data block level.
			// For subobject properties this would require size/alignment validation.
			// Deferred until there is a concrete use case.
			break;
		}
	}
}

// ─── Blueprint-callable Pin Value Accessors ─────────────────────────────

bool UComposableCameraCameraNodeBase::GetInputPinValueBool(FName PinName) const
{
	return GetInputPinValue<bool>(PinName);
}

int32 UComposableCameraCameraNodeBase::GetInputPinValueInt32(FName PinName) const
{
	return GetInputPinValue<int32>(PinName);
}

float UComposableCameraCameraNodeBase::GetInputPinValueFloat(FName PinName) const
{
	return GetInputPinValue<float>(PinName);
}

double UComposableCameraCameraNodeBase::GetInputPinValueDouble(FName PinName) const
{
	return GetInputPinValue<double>(PinName);
}

FVector UComposableCameraCameraNodeBase::GetInputPinValueVector(FName PinName) const
{
	return GetInputPinValue<FVector>(PinName);
}

FRotator UComposableCameraCameraNodeBase::GetInputPinValueRotator(FName PinName) const
{
	return GetInputPinValue<FRotator>(PinName);
}

FTransform UComposableCameraCameraNodeBase::GetInputPinValueTransform(FName PinName) const
{
	return GetInputPinValue<FTransform>(PinName);
}

AActor* UComposableCameraCameraNodeBase::GetInputPinValueActor(FName PinName) const
{
	return GetInputPinValue<AActor*>(PinName);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueBool(FName PinName, bool Value)
{
	SetOutputPinValue<bool>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueInt32(FName PinName, int32 Value)
{
	SetOutputPinValue<int32>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueFloat(FName PinName, float Value)
{
	SetOutputPinValue<float>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueDouble(FName PinName, double Value)
{
	SetOutputPinValue<double>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueVector(FName PinName, FVector Value)
{
	SetOutputPinValue<FVector>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueRotator(FName PinName, FRotator Value)
{
	SetOutputPinValue<FRotator>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueTransform(FName PinName, FTransform Value)
{
	SetOutputPinValue<FTransform>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueActor(FName PinName, AActor* Value)
{
	SetOutputPinValue<AActor*>(PinName, Value);
}

// ─── Blueprint-callable Internal Variable Accessors ─────────────────────

float UComposableCameraCameraNodeBase::GetInternalVariableFloat(FName VariableName) const
{
	return GetInternalVariable<float>(VariableName);
}

FVector UComposableCameraCameraNodeBase::GetInternalVariableVector(FName VariableName) const
{
	return GetInternalVariable<FVector>(VariableName);
}

void UComposableCameraCameraNodeBase::SetInternalVariableFloat(FName VariableName, float Value)
{
	SetInternalVariable<float>(VariableName, Value);
}

void UComposableCameraCameraNodeBase::SetInternalVariableVector(FName VariableName, FVector Value)
{
	SetInternalVariable<FVector>(VariableName, Value);
}
