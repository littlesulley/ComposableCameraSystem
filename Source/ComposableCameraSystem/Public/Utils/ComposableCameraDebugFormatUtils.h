// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Debug/ComposableCameraDebugPanelData.h"
#include "Misc/StringBuilder.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "UObject/Class.h"

/**
 * Debug formatters used by the ShowDebug HUD (runtime) and the editor debug
 * overlay (WITH_EDITOR). Two faces per formatter:
 *
 *   AppendX(Builder, ...) . Writes into a caller-provided FStringBuilderBase.
 *                            Zero-alloc as long as the builder's inline buffer
 *                            is big enough (TStringBuilder<256> comfortably
 *                            fits any single pin value). Use this on any hot
 *                            path that produces one text line per tick.
 *
 *   FormatX(...)          . Returns a freshly-allocated FString. Thin
 *                            wrapper around AppendX. Kept for cold call sites
 *                            (property customizations, tests, showdebug
 *                            sub-headers). Do NOT introduce new hot-path uses.
 */
namespace ComposableCameraDebug
{
	/** Append a float to the builder with 2-decimal precision. */
	inline void AppendFloat(FStringBuilderBase& Builder, double Value)
	{
		Builder.Appendf(TEXT("%.2f"), Value);
	}

	/** Append an FVector in `(X, Y, Z)` form, 1-decimal precision. */
	inline void AppendVector(FStringBuilderBase& Builder, const FVector& V)
	{
		Builder.Appendf(TEXT("(%.1f, %.1f, %.1f)"), V.X, V.Y, V.Z);
	}

	/** Append an FRotator in `(P=..., Y=..., R=...)` form, 1-decimal precision. */
	inline void AppendRotator(FStringBuilderBase& Builder, const FRotator& R)
	{
		Builder.Appendf(TEXT("(P=%.1f, Y=%.1f, R=%.1f)"), R.Pitch, R.Yaw, R.Roll);
	}

	/** Append an FTransform with Loc/Rot/Scale components. */
	inline void AppendTransform(FStringBuilderBase& Builder, const FTransform& T)
	{
		Builder.Append(TEXT("Loc "));
		AppendVector(Builder, T.GetLocation());
		Builder.Append(TEXT(" Rot "));
		AppendRotator(Builder, T.GetRotation().Rotator());
		Builder.Append(TEXT(" Scale "));
		AppendVector(Builder, T.GetScale3D());
	}

	/** Read a typed value at a byte offset from the data block and append as text.
	 *  EnumType is consulted only when PinType == Enum; when supplied, the int64 slot
	 *  is rendered as the authored entry name. When omitted for an Enum slot, the
	 *  raw int64 is printed so debug output never silently lies about the slot. */
	inline void AppendTypedValue(
		FStringBuilderBase& Builder,
		const FComposableCameraRuntimeDataBlock& DataBlock,
		int32 Offset,
		EComposableCameraPinType PinType,
		const UEnum* EnumType = nullptr)
	{
		switch (PinType)
		{
		case EComposableCameraPinType::Bool:
			Builder.Append(DataBlock.ReadValue<bool>(Offset) ? TEXT("true") : TEXT("false"));
			break;
		case EComposableCameraPinType::Int32:
			Builder.Appendf(TEXT("%d"), DataBlock.ReadValue<int32>(Offset));
			break;
		case EComposableCameraPinType::Float:
			AppendFloat(Builder, DataBlock.ReadValue<float>(Offset));
			break;
		case EComposableCameraPinType::Double:
			AppendFloat(Builder, DataBlock.ReadValue<double>(Offset));
			break;
		case EComposableCameraPinType::Vector2D:
		{
			const FVector2D V = DataBlock.ReadValue<FVector2D>(Offset);
			Builder.Appendf(TEXT("(%.1f, %.1f)"), V.X, V.Y);
			break;
		}
		case EComposableCameraPinType::Vector3D:
			AppendVector(Builder, DataBlock.ReadValue<FVector>(Offset));
			break;
		case EComposableCameraPinType::Vector4:
		{
			const FVector4 V = DataBlock.ReadValue<FVector4>(Offset);
			Builder.Appendf(TEXT("(%.1f, %.1f, %.1f, %.1f)"), V.X, V.Y, V.Z, V.W);
			break;
		}
		case EComposableCameraPinType::Rotator:
			AppendRotator(Builder, DataBlock.ReadValue<FRotator>(Offset));
			break;
		case EComposableCameraPinType::Transform:
			AppendTransform(Builder, DataBlock.ReadValue<FTransform>(Offset));
			break;
		case EComposableCameraPinType::Actor:
		{
			AActor* Actor = DataBlock.ReadValue<AActor*>(Offset);
			if (IsValid(Actor))
			{
				Actor->GetFName().AppendString(Builder);
			}
			else
			{
				Builder.Append(TEXT("null"));
			}
			break;
		}
		case EComposableCameraPinType::Object:
		{
			UObject* Obj = DataBlock.ReadValue<UObject*>(Offset);
			if (IsValid(Obj))
			{
				Obj->GetFName().AppendString(Builder);
			}
			else
			{
				Builder.Append(TEXT("null"));
			}
			break;
		}
		case EComposableCameraPinType::Name:
		{
			const FName N = DataBlock.ReadValue<FName>(Offset);
			if (N.IsNone())
			{
				Builder.Append(TEXT("None"));
			}
			else
			{
				N.AppendString(Builder);
			}
			break;
		}
		case EComposableCameraPinType::Enum:
		{
			const int64 IntVal = DataBlock.ReadValue<int64>(Offset);
			if (EnumType)
			{
				Builder.Append(EnumType->GetNameStringByValue(IntVal));
			}
			else
			{
				Builder.Appendf(TEXT("%lld"), IntVal);
			}
			break;
		}
		case EComposableCameraPinType::Delegate:
			Builder.Append(TEXT("(delegate)"));
			break;
		default:
			Builder.Append(TEXT("(struct)"));
			break;
		}
	}

	/** Read a typed output pin value from the data block and append as text. */
	inline void AppendOutputPinValue(
		FStringBuilderBase& Builder,
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
			Builder.Append(TEXT("(no data)"));
			return;
		}
		AppendTypedValue(Builder, DataBlock, *Offset, PinType, EnumType);
	}

	/** Append `N` spaces to the builder. */
	inline void AppendIndent(FStringBuilderBase& Builder, int32 N)
	{
		for (int32 i = 0; i < N; ++i)
		{
			Builder.AppendChar(TEXT(' '));
		}
	}

	/** Append one tree-node snapshot as a single text line (no trailing newline).
	 *  `BaseIndentCols` is the number of spaces prefixed before the per-depth
	 *  indent (2 spaces per Depth level). Used by both `showdebug camera` and
	 *  any future dump command that emits the tree as text. */
	inline void AppendTreeNodeLine(
		FStringBuilderBase& Builder,
		const FComposableCameraTreeNodeSnapshot& Node,
		int32 BaseIndentCols)
	{
		AppendIndent(Builder, BaseIndentCols + Node.Depth * 2);

		switch (Node.Kind)
		{
		case EComposableCameraTreeNodeKind::Leaf:
			if (Node.bDestroyed)
			{
				Builder.Append(TEXT("[Leaf] (destroyed)"));
			}
			else
			{
				Builder.Append(TEXT("[Leaf] "));
				Builder.Append(Node.DisplayLabel);
				if (Node.bIsTransient)
				{
					Builder.Appendf(TEXT(" (transient, %.1f/%.1fs)"), Node.LifeElapsed, Node.LifeTotal);
				}
			}
			break;

		case EComposableCameraTreeNodeKind::ReferenceLeaf:
			Builder.Append(TEXT("[RefLeaf] snapshot of "));
			Builder.Append(Node.DisplayLabel);
			break;

		case EComposableCameraTreeNodeKind::InnerTransition:
			if (Node.TransitionProgress >= 0.f)
			{
				Builder.Appendf(TEXT("[Transition] %s  %.0f%%  (%.2f/%.2fs)"),
					*Node.DisplayLabel,
					Node.TransitionProgress * 100.f,
					Node.TransitionElapsed,
					Node.TransitionTotal);
			}
			else
			{
				Builder.Append(TEXT("[Transition] (null)"));
			}
			break;
		}
	}

	// ---- FString-returning wrappers. Cold call sites only. ------------------
	// These allocate. Prefer AppendX on any code that runs once per frame.

	inline FString FormatFloat(double Value)
	{
		TStringBuilder<32> Builder;
		AppendFloat(Builder, Value);
		return FString(Builder);
	}

	inline FString FormatVector(const FVector& V)
	{
		TStringBuilder<64> Builder;
		AppendVector(Builder, V);
		return FString(Builder);
	}

	inline FString FormatRotator(const FRotator& R)
	{
		TStringBuilder<64> Builder;
		AppendRotator(Builder, R);
		return FString(Builder);
	}

	inline FString FormatTransform(const FTransform& T)
	{
		TStringBuilder<192> Builder;
		AppendTransform(Builder, T);
		return FString(Builder);
	}

	inline FString FormatTypedValue(
		const FComposableCameraRuntimeDataBlock& DataBlock,
		int32 Offset,
		EComposableCameraPinType PinType,
		const UEnum* EnumType = nullptr)
	{
		TStringBuilder<192> Builder;
		AppendTypedValue(Builder, DataBlock, Offset, PinType, EnumType);
		return FString(Builder);
	}

	inline FString FormatOutputPinValue(
		const FComposableCameraRuntimeDataBlock& DataBlock,
		int32 NodeIndex,
		FName PinName,
		EComposableCameraPinType PinType,
		const UEnum* EnumType = nullptr)
	{
		TStringBuilder<192> Builder;
		AppendOutputPinValue(Builder, DataBlock, NodeIndex, PinName, PinType, EnumType);
		return FString(Builder);
	}
}
