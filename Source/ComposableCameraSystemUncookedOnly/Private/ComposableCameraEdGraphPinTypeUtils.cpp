// Copyright Sulley. All Rights Reserved.

#include "ComposableCameraEdGraphPinTypeUtils.h"

#include "Nodes/ComposableCameraNodePinTypes.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

namespace ComposableCameraEdGraphPinTypeUtils
{
	FEdGraphPinType MakeEdGraphPinTypeFromCameraPinType(
		EComposableCameraPinType PinType,
		UScriptStruct* StructType,
		UEnum* EnumType,
		UFunction* SignatureFunction)
	{
		FEdGraphPinType Result;

		switch (PinType)
		{
		case EComposableCameraPinType::Bool:
			Result.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			break;

		case EComposableCameraPinType::Int32:
			Result.PinCategory = UEdGraphSchema_K2::PC_Int;
			break;

		case EComposableCameraPinType::Float:
			Result.PinCategory = UEdGraphSchema_K2::PC_Real;
			Result.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			break;

		case EComposableCameraPinType::Double:
			Result.PinCategory = UEdGraphSchema_K2::PC_Real;
			Result.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			break;

		case EComposableCameraPinType::Vector2D:
			Result.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Result.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
			break;

		case EComposableCameraPinType::Vector3D:
			Result.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Result.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			break;

		case EComposableCameraPinType::Vector4:
			Result.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Result.PinSubCategoryObject = TBaseStructure<FVector4>::Get();
			break;

		case EComposableCameraPinType::Rotator:
			Result.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Result.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			break;

		case EComposableCameraPinType::Transform:
			Result.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Result.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
			break;

		case EComposableCameraPinType::Actor:
			Result.PinCategory = UEdGraphSchema_K2::PC_Object;
			Result.PinSubCategoryObject = AActor::StaticClass();
			break;

		case EComposableCameraPinType::Object:
			Result.PinCategory = UEdGraphSchema_K2::PC_Object;
			Result.PinSubCategoryObject = UObject::StaticClass();
			break;

		case EComposableCameraPinType::Struct:
			Result.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Result.PinSubCategoryObject = StructType;
			break;

		case EComposableCameraPinType::Name:
			Result.PinCategory = UEdGraphSchema_K2::PC_Name;
			break;

		case EComposableCameraPinType::Enum:
			// Blueprint represents enum-typed pins as PC_Byte with a UEnum
			// sub-category object. The K2 schema lights up the enum dropdown
			// based purely on PinSubCategoryObject being a UEnum, so we don't
			// need to set PinSubCategory or anything else. nullptr produces an
			// unbound byte pin — valid but visually wrong; the binding-table
			// build path already rejects unbound enums upstream.
			Result.PinCategory = UEdGraphSchema_K2::PC_Byte;
			Result.PinSubCategoryObject = EnumType;
			break;

		case EComposableCameraPinType::Delegate:
			// Blueprint represents single-cast delegates as PC_Delegate with a
			// MemberReference pointing at the signature UFunction. The K2 schema
			// uses this to validate wiring and generate the correct thunk. The
			// MemberParent is the UClass owning the signature function (typically
			// the node class that declared the DECLARE_DYNAMIC_DELEGATE).
			Result.PinCategory = UEdGraphSchema_K2::PC_Delegate;
			if (SignatureFunction)
			{
				Result.PinSubCategoryMemberReference.MemberParent = SignatureFunction->GetOwnerClass();
				Result.PinSubCategoryMemberReference.MemberName = SignatureFunction->GetFName();
			}
			break;

		default:
			ensureMsgf(false,
				TEXT("MakeEdGraphPinTypeFromCameraPinType: unhandled EComposableCameraPinType case %d. ")
				TEXT("Add the new enum value to the switch in ")
				TEXT("ComposableCameraEdGraphPinTypeUtils.cpp."),
				static_cast<int32>(PinType));
			break;
		}

		return Result;
	}

	FName ResolveTypedSetterFunctionName(const FEdGraphPinType& PinType)
	{
		// Boolean / Int / Float / Double / Name -- direct typed setters.
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockBool);
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockInt32);
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
		{
			if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockFloat);
			}
			if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockDouble);
			}
		}
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockName);
		}

		// Object / Actor -- dispatch on whether the property class derives from AActor.
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
		{
			const UClass* PropClass = Cast<UClass>(PinType.PinSubCategoryObject);
			if (PropClass && PropClass->IsChildOf(AActor::StaticClass()))
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockActor);
			}
			return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockObject);
		}

		// Math structs and FFloatInterval -- typed setters bypass the BP
		// wildcard bug for pin-default literals on struct pins.
		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			const UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject);
			if (Struct == TBaseStructure<FVector2D>::Get())
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockVector2D);
			}
			if (Struct == TBaseStructure<FVector>::Get())
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockVector);
			}
			if (Struct == TBaseStructure<FVector4>::Get())
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockVector4);
			}
			if (Struct == TBaseStructure<FRotator>::Get())
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockRotator);
			}
			if (Struct == TBaseStructure<FTransform>::Get())
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockTransform);
			}
			if (Struct == TBaseStructure<FFloatInterval>::Get())
			{
				return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockFloatInterval);
			}
		}

		// Fallback to the wildcard CustomStructureParam setter for:
		//   - Enum (width normalization needs runtime FProperty inspection)
		//   - generic Struct (arbitrary user USTRUCT including non-POD)
		//   - Delegate
		// These don't trigger the MakeLiteralStruct + CustomStructureParam
		// pin-default bug in practice (Enum uses MakeLiteralByte, generic
		// Struct falls through to DefaultValue string, Delegate has no
		// pin-default).
		return GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, SetParameterBlockValue);
	}
}
