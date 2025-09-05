// Copyright Sulley. All rights reserved.

#include "Variables/ComposableCameraParameter.h"
#include "Variables/ComposableCameraVariableCollection.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"

bool FBooleanComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.Type == NAME_BoolProperty)
	{
		Value = (Tag.BoolVal != 0);
		return true;
	}

	return false;
}

bool FInteger32ComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.Type == NAME_IntProperty || Tag.Type == NAME_Int32Property)
	{
		Slot << Value;
		return true;
	}

	return false;
}

bool FFloatComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.Type == NAME_FloatProperty)
	{
		Slot << Value;
		return true;
	}

	return false;
}

bool FDoubleComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_DoubleProperty))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FVector2fComposableCameraContextParameter::FVector2fComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FVector2fComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Vector2f) || Tag.GetType().IsStruct(NAME_Vector2D))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FVector2dComposableCameraContextParameter::FVector2dComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FVector2dComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Vector2d))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FVector3fComposableCameraContextParameter::FVector3fComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FVector3fComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Vector3f))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FVector3dComposableCameraContextParameter::FVector3dComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FVector3dComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Vector3d))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FVector4fComposableCameraContextParameter::FVector4fComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FVector4fComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Vector4f))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FVector4dComposableCameraContextParameter::FVector4dComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FVector4dComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Vector4d))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FRotator3fComposableCameraContextParameter::FRotator3fComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FRotator3fComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Rotator3f))
	{
		Slot << Value;
		return true;
	}

	return false;
}

FRotator3dComposableCameraContextParameter::FRotator3dComposableCameraContextParameter()
	: Value(EForceInit::ForceInit)
{
}

bool FRotator3dComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Rotator3d))
	{
		Slot << Value;
		return true;
	}

	return false;
}

bool FTransform3fComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{
	if (Tag.GetType().IsStruct(NAME_Transform3f))
	{
		Slot << Value;
		return true;
	}

	return false;
}

bool FTransform3dComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{

	if (Tag.GetType().IsStruct(NAME_Transform3d))
	{
		Slot << Value;
		return true;
	}

	return false;
}

bool FActorComposableCameraContextParameter::SerializeFromMismatchedTag(const FPropertyTag& Tag,
	FStructuredArchive::FSlot Slot)
{

	if (Tag.GetType().IsStruct(NAME_Actor))
	{
		Slot << Value;
		return true;
	}

	return false;
}

#define COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(ValueType, ValueName) \
void F##ValueName##ComposableCameraContextParameter::PostSerialize(const FArchive& Ar) \
{ \
	if (Ar.IsLoading()) \
	{ \
		if (Variable && Variable->GetOuter()->IsA<UComposableCameraCameraNodeBase>()) \
		{ \
			Variable = nullptr;\
		} \
	} \
} \
ValueType F##ValueName##ComposableCameraContextParameter::GetValue(const UComposableCameraVariableCollection& Collection) const \
{ \
	if (!VariableID.IsValid()) \
	{ \
		return Value; \
	} \
	else \
	{ \
		if (ValueType* ActualValue = const_cast<ValueType*>(Collection.FindValue<ValueType>(VariableID))) \
		{ \
			return *ActualValue; \
		} \
		return Value; \
	} \
}
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_ALL_TYPES()
#undef COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE