// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "UObject/Class.h"

/**
 * Namespace for debug formatting utilities used by both the ShowDebug HUD
 * (runtime) and the editor debug overlay (WITH_EDITOR).
 *
 * All functions allocate FStrings — they are intended for debug display,
 * not hot-path evaluation.
 */
namespace ComposableCameraDebug
{
	/** Format a float to a compact display string. */
	inline FString FormatFloat(double Value)
	{
		return FString::Printf(TEXT("%.2f"), Value);
	}

	/** Format an FVector to a compact display string. */
	inline FString FormatVector(const FVector& V)
	{
		return FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), V.X, V.Y, V.Z);
	}

	/** Format an FRotator to a compact display string. */
	inline FString FormatRotator(const FRotator& R)
	{
		return FString::Printf(TEXT("(P=%.1f, Y=%.1f, R=%.1f)"), R.Pitch, R.Yaw, R.Roll);
	}

	/** Format an FTransform to a compact display string. */
	inline FString FormatTransform(const FTransform& T)
	{
		return FString::Printf(TEXT("Loc %s Rot %s Scale %s"),
			*FormatVector(T.GetLocation()),
			*FormatRotator(T.GetRotation().Rotator()),
			*FormatVector(T.GetScale3D()));
	}

	/** Read a typed value at a known byte offset from the data block and format as string.
	 *  EnumType is consulted only when PinType == Enum; when supplied, the int64 slot is
	 *  formatted as the corresponding entry name (e.g. "EMyEnum::Alpha"). When omitted
	 *  for an Enum slot the raw int64 value is printed instead — debug-only fallback. */
	inline FString FormatTypedValue(
		const FComposableCameraRuntimeDataBlock& DataBlock,
		int32 Offset,
		EComposableCameraPinType PinType,
		const UEnum* EnumType = nullptr)
	{
		switch (PinType)
		{
		case EComposableCameraPinType::Bool:
			return DataBlock.ReadValue<bool>(Offset) ? TEXT("true") : TEXT("false");
		case EComposableCameraPinType::Int32:
			return FString::FromInt(DataBlock.ReadValue<int32>(Offset));
		case EComposableCameraPinType::Float:
			return FormatFloat(DataBlock.ReadValue<float>(Offset));
		case EComposableCameraPinType::Double:
			return FormatFloat(DataBlock.ReadValue<double>(Offset));
		case EComposableCameraPinType::Vector2D:
		{
			FVector2D V = DataBlock.ReadValue<FVector2D>(Offset);
			return FString::Printf(TEXT("(%.1f, %.1f)"), V.X, V.Y);
		}
		case EComposableCameraPinType::Vector3D:
			return FormatVector(DataBlock.ReadValue<FVector>(Offset));
		case EComposableCameraPinType::Vector4:
		{
			FVector4 V = DataBlock.ReadValue<FVector4>(Offset);
			return FString::Printf(TEXT("(%.1f, %.1f, %.1f, %.1f)"), V.X, V.Y, V.Z, V.W);
		}
		case EComposableCameraPinType::Rotator:
			return FormatRotator(DataBlock.ReadValue<FRotator>(Offset));
		case EComposableCameraPinType::Transform:
			return FormatTransform(DataBlock.ReadValue<FTransform>(Offset));
		case EComposableCameraPinType::Actor:
		{
			AActor* Actor = DataBlock.ReadValue<AActor*>(Offset);
			return IsValid(Actor) ? Actor->GetName() : TEXT("null");
		}
		case EComposableCameraPinType::Object:
		{
			UObject* Obj = DataBlock.ReadValue<UObject*>(Offset);
			return IsValid(Obj) ? Obj->GetName() : TEXT("null");
		}
		case EComposableCameraPinType::Name:
		{
			const FName N = DataBlock.ReadValue<FName>(Offset);
			return N.IsNone() ? TEXT("None") : N.ToString();
		}
		case EComposableCameraPinType::Enum:
		{
			// Enum slots are normalized to int64 in the data block — see the
			// EComposableCameraPinType::Enum branch of GetPinTypeSize and the
			// thunk in ComposableCameraBlueprintLibrary.h. Resolve to the
			// authored entry name when we know the UEnum, otherwise print the
			// raw integer so debug output never silently lies about the slot.
			const int64 IntVal = DataBlock.ReadValue<int64>(Offset);
			if (EnumType)
			{
				return EnumType->GetNameStringByValue(IntVal);
			}
			return FString::Printf(TEXT("%lld"), IntVal);
		}
		case EComposableCameraPinType::Delegate:
			// Delegates have no data block representation — they are written
			// directly into the node's UPROPERTY at activation time.
			return TEXT("(delegate)");
		default:
			return TEXT("(struct)");
		}
	}

	/** Read a typed output pin value from the data block and format as string. */
	inline FString FormatOutputPinValue(
		const FComposableCameraRuntimeDataBlock& DataBlock,
		int32 NodeIndex,
		FName PinName,
		EComposableCameraPinType PinType,
		const UEnum* EnumType = nullptr)
	{
		const FComposableCameraPinKey Key{ NodeIndex, PinName };
		const int32* Offset = DataBlock.OutputPinOffsets.Find(Key);
		if (!Offset)
		{
			return TEXT("(no data)");
		}
		return FormatTypedValue(DataBlock, *Offset, PinType, EnumType);
	}
}
