// Copyright 2026 Sulley. All Rights Reserved.

#include "LevelSequence/ComposableCameraPinTypeUtils.h"

#include "GameFramework/Actor.h"

namespace UE::ComposableCameras
{
	bool PinTypeToPropertyBagType(
		EComposableCameraPinType InPinType,
		const UScriptStruct* InStructType,
		const UEnum* InEnumType,
		EPropertyBagPropertyType& OutBagPropertyType,
		const UObject*& OutValueTypeObject)
	{
		OutValueTypeObject = nullptr;

		switch (InPinType)
		{
		case EComposableCameraPinType::Bool:
			OutBagPropertyType = EPropertyBagPropertyType::Bool;
			return true;

		case EComposableCameraPinType::Int32:
			OutBagPropertyType = EPropertyBagPropertyType::Int32;
			return true;

		case EComposableCameraPinType::Float:
			OutBagPropertyType = EPropertyBagPropertyType::Float;
			return true;

		case EComposableCameraPinType::Double:
			OutBagPropertyType = EPropertyBagPropertyType::Double;
			return true;

		case EComposableCameraPinType::Name:
			OutBagPropertyType = EPropertyBagPropertyType::Name;
			return true;

		case EComposableCameraPinType::Vector2D:
			OutBagPropertyType = EPropertyBagPropertyType::Struct;
			OutValueTypeObject = TBaseStructure<FVector2D>::Get();
			return true;

		case EComposableCameraPinType::Vector3D:
			OutBagPropertyType = EPropertyBagPropertyType::Struct;
			OutValueTypeObject = TBaseStructure<FVector>::Get();
			return true;

		case EComposableCameraPinType::Vector4:
			OutBagPropertyType = EPropertyBagPropertyType::Struct;
			OutValueTypeObject = TBaseStructure<FVector4>::Get();
			return true;

		case EComposableCameraPinType::Rotator:
			OutBagPropertyType = EPropertyBagPropertyType::Struct;
			OutValueTypeObject = TBaseStructure<FRotator>::Get();
			return true;

		case EComposableCameraPinType::Transform:
			OutBagPropertyType = EPropertyBagPropertyType::Struct;
			OutValueTypeObject = TBaseStructure<FTransform>::Get();
			return true;

		case EComposableCameraPinType::Struct:
			if (!InStructType)
			{
				return false;
			}
			OutBagPropertyType = EPropertyBagPropertyType::Struct;
			OutValueTypeObject = InStructType;
			return true;

		case EComposableCameraPinType::Actor:
			OutBagPropertyType = EPropertyBagPropertyType::Object;
			OutValueTypeObject = AActor::StaticClass();
			return true;

		case EComposableCameraPinType::Object:
			OutBagPropertyType = EPropertyBagPropertyType::Object;
			OutValueTypeObject = UObject::StaticClass();
			return true;

		case EComposableCameraPinType::Enum:
			if (!InEnumType)
			{
				return false;
			}
			OutBagPropertyType = EPropertyBagPropertyType::Enum;
			OutValueTypeObject = InEnumType;
			return true;

		case EComposableCameraPinType::Delegate:
		default:
			// Delegates and anything unrecognized: not representable in the bag.
			return false;
		}
	}
}
