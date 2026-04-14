// Copyright Sulley. All Rights Reserved.

#include "ComposableCameraEdGraphPinTypeUtils.h"

#include "Nodes/ComposableCameraNodePinTypes.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"

namespace ComposableCameraEdGraphPinTypeUtils
{
	FEdGraphPinType MakeEdGraphPinTypeFromCameraPinType(
		EComposableCameraPinType PinType,
		UScriptStruct* StructType)
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
}
