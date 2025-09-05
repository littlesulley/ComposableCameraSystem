// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraVariable.h"
#include "UObject/Object.h"
#include "ComposableCameraMacros.h"
#include "ComposableCameraParameter.generated.h"

class UComposableCameraVariableCollection;

#define COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(ParameterClass) \
	ParameterClass(typename TCallTraits<ParameterClass::ValueType>::ParamType InValue) \
		: Value(InValue) \
	{} \
	bool HasOverride() const { return VariableID.IsValid(); } \
	bool HasUserOverride() const { return Variable != nullptr; } \
	bool HasNonUserOverride() const { return VariableID.IsValid() && (!Variable || Variable->GetVariableID() != VariableID); } \
	ParameterClass::ValueType GetValue(const UComposableCameraVariableCollection& Collection) const; \
	void PostSerialize(const FArchive& Ar);

#define COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(ParameterClass) \
	ParameterClass() {} \
	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(ParameterClass)

// All camera parameters have:
//
// - Value: a value for the user to tweak. This is the "default" value.
// - Variable: a variable chosen by the user to drive this parameter. 
// - VariableID: the ID of the variable driving this parameter.
//
// When Variable is set, VariableID is the ID of that variable.
// When Variable is not set, VariableID is the ID of something else.

/** Boolean camera parameter. */
USTRUCT(BlueprintType)
struct FBooleanComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = bool;
	using VariableAssetType = UBooleanComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category = Common, meta=(SequencerUseParentPropertyName=true))
	bool Value = false;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UBooleanComposableCameraVariable> Variable;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(FBooleanComposableCameraContextParameter)
};

/** Integer camera parameter. */
USTRUCT(BlueprintType)
struct FInteger32ComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = int32;
	using VariableAssetType = UInteger32ComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	int32 Value = 0;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UInteger32ComposableCameraVariable> Variable;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(FInteger32ComposableCameraContextParameter)
};

/** Float camera parameter. */
USTRUCT(BlueprintType)
struct FFloatComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = float;
	using VariableAssetType = UFloatComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	float Value = 0.f;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UFloatComposableCameraVariable> Variable;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(FFloatComposableCameraContextParameter)
};

/** Double camera parameter. */
USTRUCT(BlueprintType)
struct FDoubleComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = double;
	using VariableAssetType = UDoubleComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	double Value = 0.0;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UDoubleComposableCameraVariable> Variable;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(FDoubleComposableCameraContextParameter)
};

/** Vector2f camera parameter. */
USTRUCT(BlueprintType)
struct FVector2fComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FVector2f;
	using VariableAssetType = UVector2fComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FVector2f Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UVector2fComposableCameraVariable> Variable;

	FVector2fComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FVector2fComposableCameraContextParameter)
};

/** Vector2d camera parameter. */
USTRUCT(BlueprintType)
struct FVector2dComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FVector2D;
	using VariableAssetType = UVector2dComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FVector2D Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UVector2dComposableCameraVariable> Variable;

	FVector2dComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FVector2dComposableCameraContextParameter)
};

/** Vector3f camera parameter. */
USTRUCT(BlueprintType)
struct FVector3fComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FVector3f;
	using VariableAssetType = UVector3fComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FVector3f Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UVector3fComposableCameraVariable> Variable;

	FVector3fComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FVector3fComposableCameraContextParameter)
};

/** Vector3d camera parameter. */
USTRUCT(BlueprintType)
struct FVector3dComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FVector;
	using VariableAssetType = UVector3dComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FVector Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UVector3dComposableCameraVariable> Variable;

	FVector3dComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FVector3dComposableCameraContextParameter)
};

/** Vector4f camera parameter. */
USTRUCT(BlueprintType)
struct FVector4fComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FVector4f;
	using VariableAssetType = UVector4fComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FVector4f Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UVector4fComposableCameraVariable> Variable;

	FVector4fComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FVector4fComposableCameraContextParameter)
};

/** Vector4d camera parameter. */
USTRUCT(BlueprintType)
struct FVector4dComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FVector4;
	using VariableAssetType = UVector4dComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FVector4 Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UVector4dComposableCameraVariable> Variable;

	FVector4dComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FVector4dComposableCameraContextParameter)
};

/** Rotator3f camera parameter. */
USTRUCT(BlueprintType)
struct FRotator3fComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FRotator3f;
	using VariableAssetType = URotator3fComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FRotator3f Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<URotator3fComposableCameraVariable> Variable;

	FRotator3fComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FRotator3fComposableCameraContextParameter)
};

/** Rotator3d camera parameter. */
USTRUCT(BlueprintType)
struct FRotator3dComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FRotator;
	using VariableAssetType = URotator3dComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FRotator Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<URotator3dComposableCameraVariable> Variable;

	FRotator3dComposableCameraContextParameter();
	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS(FRotator3dComposableCameraContextParameter)
};

/** Transform3f camera parameter. */
USTRUCT(BlueprintType)
struct FTransform3fComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FTransform3f;
	using VariableAssetType = UTransform3fComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FTransform3f Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UTransform3fComposableCameraVariable> Variable;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(FTransform3fComposableCameraContextParameter)
};

/** Transform3d camera parameter. */
USTRUCT(BlueprintType)
struct FTransform3dComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = FTransform;
	using VariableAssetType = UTransform3dComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	FTransform Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UTransform3dComposableCameraVariable> Variable;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(FTransform3dComposableCameraContextParameter)
};

/** Actor camera parameter. */
USTRUCT(BlueprintType)
struct FActorComposableCameraContextParameter
{
	GENERATED_BODY()

	using ValueType = AActor*;
	using VariableAssetType = UActorComposableCameraVariable;

	UPROPERTY(EditAnywhere, Interp, Category=Common, meta=(SequencerUseParentPropertyName=true))
	AActor* Value;

	UPROPERTY()
	FComposableCameraVariableID VariableID;

	UPROPERTY(EditAnywhere, Category=Common)
	TObjectPtr<UActorComposableCameraVariable> Variable;

	bool SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot);

	COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS(FActorComposableCameraContextParameter)
};

#undef COMPOSABLECAMERASYSTEM_PARAMETER_VALUE_CONSTRUCTORS
#undef COMPOSABLECAMERASYSTEM_PARAMETER_ALL_VALUE_CONSTRUCTORS

template<typename ValueType>
bool CameraParameterValueEquals(typename TCallTraits<ValueType>::ParamType A, typename TCallTraits<ValueType>::ParamType B)
{
	return A == B;
}

template<>
inline bool CameraParameterValueEquals<FTransform3f>(const FTransform3f& A, const FTransform3f& B)
{
	return A.Equals(B);
}

template<>
inline bool CameraParameterValueEquals<FTransform3d>(const FTransform3d& A, const FTransform3d& B)
{
	return A.Equals(B);
}

// Any camera parameter might replace a previously non-parameterized property (i.e. a "fixed" property
// of the underlying type, like bool, int32, float, etc.)
// When someone upgrades the fixed property to a parameterized property, any previously saved data will
// run into a mismatched tag. So the parameters will handle that by loading the saved value inside of
// them.
#define COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE(ValueType, ValueName)\
template<> struct TStructOpsTypeTraits<F##ValueName##ComposableCameraContextParameter>\
: public TStructOpsTypeTraitsBase2<F##ValueName##ComposableCameraContextParameter>\
{\
enum { WithStructuredSerializeFromMismatchedTag = true, WithPostSerialize = true };\
};
COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_ALL_TYPES()
#undef COMPOSABLECAMERASYSTEMEDITOR_CAMERA_VARIABLE_FOR_TYPE