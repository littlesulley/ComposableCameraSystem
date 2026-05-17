// Copyright 2026 Sulley. All Rights Reserved.

#include "MovieScene/MovieSceneComposableCameraPatchSection.h"

#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "LevelSequence/ComposableCameraExposedBagUtils.h"
#include "MovieScene/MovieSceneComposableCameraPatchTrackInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneComposableCameraPatchSection)

UMovieSceneComposableCameraPatchSection::UMovieSceneComposableCameraPatchSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Patches are an additive overlay. Finishing the section means the patch
	// stops contributing. RestoreState would re-set whatever the patch was
	// modifying; ProjectDefault is the right baseline since the patch's exit
	// envelope already smoothly transitions out.
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::ProjectDefault);

	// Section is the single source of truth for "is the patch live", so
	// pre/post-roll evaluation would either spam AddPatch on context wakes
	// (preroll) or hold a phantom patch open past the section (postroll).

	// Patch lifetime IS the section's authored range (TrackInstance fires
	// OnInputRemoved when the playhead leaves the bounds), and the stateless
	// envelope alpha needs finite SectionStart / SectionEnd. Opt out of the
	// engine's "Infinite Key Areas" path so any pipeline that respects the
	// flag (the menu-driven CreateNewSection overload, splitter UX, etc)
	// refuses to set an infinite range. The simpler FSequencerUtilities::
	// CreateNewSection overload doesn't check this flag in 5.6. That's
	// routed around via FComposableCameraPatchTrackEditor's own helper.
	bSupportsInfiniteRange = false;
}

void UMovieSceneComposableCameraPatchSection::PostLoad()
{
	Super::PostLoad();
	// Catch up the bag layouts to the asset's current exposed surface. Handles
	// loading a section that was saved before the asset's exposed parameters
	// changed. MigrateToNewBagStruct preserves survivor values.
	RebuildBagsFromPatchAsset();
}

#if WITH_EDITOR
void UMovieSceneComposableCameraPatchSection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName   = PropertyChangedEvent.MemberProperty
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	const bool bIsAssetEdit =
		PropertyName == GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraPatchSection, PatchAsset)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraPatchSection, PatchAsset);

	if (bIsAssetEdit)
	{
		RebuildBagsFromPatchAsset();
		// Note: existing keyed channels for parameters that survive the asset
		// swap are deliberately left in place -UMovieSceneParameterSection's
		// Scalar/Bool/Vector/etc. arrays are keyed by FName, so a renamed-or-removed
		// parameter's channel becomes orphaned. Designer can clean those up via
		// the channel row's right-click "Delete" (handled by FParameterSection
		// base via RequestDeleteCategory / RequestDeleteKeyArea). Auto-pruning
		// orphans on asset edit would silently destroy keyed animation data
		// when a designer is mid-edit; leave it manual.
	}
}
#endif

void UMovieSceneComposableCameraPatchSection::ImportEntityImpl(
	UMovieSceneEntitySystemLinker* EntityLinker,
	const UE::MovieScene::FEntityImportParams& ImportParams,
	UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	UE_LOG(LogComposableCameraSystem, Verbose,
		TEXT("PatchSection::ImportEntityImpl fired for section '%s' (PatchAsset=%s)."),
		*GetName(),
		PatchAsset ? *PatchAsset->GetName() : TEXT("<null>"));

	// Skip empty sections. Without a PatchAsset there's nothing to add at
	// section enter, and producing an entity would cost an unnecessary
	// per-frame TrackInstance::OnAnimate visit.
	if (!PatchAsset)
	{
		return;
	}

	// Per-section TrackInstance dispatch. This intentionally REPLACES the
	// parent UMovieSceneParameterSection's ImportEntityImpl (which would emit
	// a parameter-extender entity). Patches don't go through the parameter-
	// extender pipeline; the TrackInstance owns the per-frame evaluation by
	// sampling channels directly via BuildParameterBlock.
	FMovieSceneTrackInstanceComponent TrackInstance{
		decltype(FMovieSceneTrackInstanceComponent::Owner)(this),
		UMovieSceneComposableCameraPatchTrackInstance::StaticClass()
	};

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.AddTag(FBuiltInComponentTypes::Get()->Tags.Root)
		.Add(FBuiltInComponentTypes::Get()->TrackInstance, TrackInstance)
	);
}

void UMovieSceneComposableCameraPatchSection::RebuildBagsFromPatchAsset()
{
	if (!PatchAsset)
	{
		Parameters.Reset();
		Variables.Reset();
		return;
	}

	// Same shape as FComposableCameraTypeAssetReference::RebuildBagsFromTypeAsset
	// -Parameters bag mirrors ExposedParameters, Variables bag mirrors
	// ExposedVariables (NOT InternalVariables, which are node-private).
	TArray<FPropertyBagPropertyDesc> ParameterDescs;
	ParameterDescs.Reserve(PatchAsset->ExposedParameters.Num());
	for (const FComposableCameraExposedParameter& Param : PatchAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}
		UE::ComposableCameras::ExposedBag::AddDescIfSupported(
			Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType, ParameterDescs);
	}

	TArray<FPropertyBagPropertyDesc> VariableDescs;
	VariableDescs.Reserve(PatchAsset->ExposedVariables.Num());
	for (const FComposableCameraInternalVariable& Var : PatchAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		UE::ComposableCameras::ExposedBag::AddDescIfSupported(
			Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType, VariableDescs);
	}

	if (const UPropertyBag* NewParamStruct = UPropertyBag::GetOrCreateFromDescs(ParameterDescs))
	{
		Parameters.MigrateToNewBagStruct(NewParamStruct);
	}
	if (const UPropertyBag* NewVarStruct = UPropertyBag::GetOrCreateFromDescs(VariableDescs))
	{
		Variables.MigrateToNewBagStruct(NewVarStruct);
	}
}

namespace
{
	/**
	 * Sample a scalar channel at the given frame and write the result into the
	 * parameter block under the matching pin type (Float vs Double). Returns
	 * true if the channel produced a value (had keys OR a default), false if
	 * the channel is "empty" (no keys, no default. Caller falls back to bag).
	 *
	 * `Evaluate` returns false only when the channel has zero keys AND no
	 * default value set; in that case we don't want to overwrite the bag value
	 * with an arbitrary 0.f.
	 */
	bool SampleScalarChannel(
		const FMovieSceneFloatChannel& Curve,
		FFrameTime Time,
		EComposableCameraPinType DestType,
		FName Name,
		FComposableCameraParameterBlock& OutBlock)
	{
		float Value = 0.f;
		if (!Curve.Evaluate(Time, Value))
		{
			return false;
		}
		switch (DestType)
		{
			case EComposableCameraPinType::Float:  OutBlock.SetFloat(Name, Value); return true;
			case EComposableCameraPinType::Double: OutBlock.SetDouble(Name, static_cast<double>(Value)); return true;
			default: return false;
		}
	}

	bool SampleBoolChannel(
		const FMovieSceneBoolChannel& Curve,
		FFrameTime Time,
		FName Name,
		FComposableCameraParameterBlock& OutBlock)
	{
		bool Value = false;
		if (!Curve.Evaluate(Time, Value))
		{
			return false;
		}
		OutBlock.SetBool(Name, Value);
		return true;
	}

	/** Sample a 2/3/4-channel set into a typed vector value. Returns true iff
	 *  every component channel produced a value. */
	bool SampleVector2DCurves(
		const FVector2DParameterNameAndCurves& C,
		FFrameTime Time,
		FComposableCameraParameterBlock& OutBlock)
	{
		float X = 0.f, Y = 0.f;
		if (!C.XCurve.Evaluate(Time, X) || !C.YCurve.Evaluate(Time, Y))
		{
			return false;
		}
		FComposableCameraParameterValue Entry;
		Entry.Set<FVector2D>(EComposableCameraPinType::Vector2D, FVector2D(X, Y));
		OutBlock.StoreValue(C.ParameterName, MoveTemp(Entry));
		return true;
	}

	bool SampleVectorCurves(
		const FVectorParameterNameAndCurves& C,
		FFrameTime Time,
		FName Name,
		EComposableCameraPinType DestType,
		FComposableCameraParameterBlock& OutBlock)
	{
		float X = 0.f, Y = 0.f, Z = 0.f;
		if (!C.XCurve.Evaluate(Time, X) || !C.YCurve.Evaluate(Time, Y) || !C.ZCurve.Evaluate(Time, Z))
		{
			return false;
		}
		switch (DestType)
		{
			case EComposableCameraPinType::Vector3D:
				OutBlock.SetVector(Name, FVector(X, Y, Z));
				return true;
			case EComposableCameraPinType::Rotator:
				// Convention: Xitch, Yaw, Zoll matches UE's standard
				// FRotator member order in the Details panel (Roll/Pitch/Yaw
				// is the exposed order but storage is Pitch/Yaw/Roll). We
				// store curve channel X/Y/Z directly as Pitch/Yaw/Roll so
				// the channel rows in Sequencer line up with the rotator
				// component the designer thinks they're keying.
				OutBlock.SetRotator(Name, FRotator(X, Y, Z));
				return true;
			default:
				return false;
		}
	}

	bool SampleColorCurvesAsVector4(
		const FColorParameterNameAndCurves& C,
		FFrameTime Time,
		FName Name,
		FComposableCameraParameterBlock& OutBlock)
	{
		float R = 0.f, G = 0.f, B = 0.f, A = 0.f;
		if (!C.RedCurve.Evaluate(Time, R) || !C.GreenCurve.Evaluate(Time, G)
			|| !C.BlueCurve.Evaluate(Time, B) || !C.AlphaCurve.Evaluate(Time, A))
		{
			return false;
		}
		FComposableCameraParameterValue Entry;
		Entry.Set<FVector4>(EComposableCameraPinType::Vector4, FVector4(R, G, B, A));
		OutBlock.StoreValue(Name, MoveTemp(Entry));
		return true;
	}

	/**
	 * Try to sample a channel for `Name` from the section's parameter curves.
	 * Returns true and writes into OutBlock if a matching curve exists AND the
	 * channel produced a value. Returns false to signal "fall back to bag".
	 *
	 * Walks each curve list (Scalar / Bool / Vector2D / Vector / Color) in
	 * lookup order; first match wins. Param-curve arrays are typically very
	 * short (<=N exposed parameters, each in at most one list), so the linear
	 * scan is acceptable.
	 */
	bool TrySampleChannelForName(
		const UMovieSceneComposableCameraPatchSection& Section,
		FName Name,
		EComposableCameraPinType DestType,
		FFrameTime Time,
		FComposableCameraParameterBlock& OutBlock)
	{
		// Scalar (Float / Double)
		for (const FScalarParameterNameAndCurve& C : Section.GetScalarParameterNamesAndCurves())
		{
			if (C.ParameterName == Name)
			{
				return SampleScalarChannel(C.ParameterCurve, Time, DestType, Name, OutBlock);
			}
		}
		// Bool
		for (const FBoolParameterNameAndCurve& C : Section.GetBoolParameterNamesAndCurves())
		{
			if (C.ParameterName == Name)
			{
				return SampleBoolChannel(C.ParameterCurve, Time, Name, OutBlock);
			}
		}
		// Vector2D
		for (const FVector2DParameterNameAndCurves& C : Section.GetVector2DParameterNamesAndCurves())
		{
			if (C.ParameterName == Name && DestType == EComposableCameraPinType::Vector2D)
			{
				return SampleVector2DCurves(C, Time, OutBlock);
			}
		}
		// Vector3D / Rotator
		for (const FVectorParameterNameAndCurves& C : Section.GetVectorParameterNamesAndCurves())
		{
			if (C.ParameterName == Name)
			{
				return SampleVectorCurves(C, Time, Name, DestType, OutBlock);
			}
		}
		// Vector4 via Color curves (re-use RGBA channel quad).
		for (const FColorParameterNameAndCurves& C : Section.GetColorParameterNamesAndCurves())
		{
			if (C.ParameterName == Name && DestType == EComposableCameraPinType::Vector4)
			{
				return SampleColorCurvesAsVector4(C, Time, Name, OutBlock);
			}
		}
		return false;
	}
}

void UMovieSceneComposableCameraPatchSection::BuildParameterBlock(
	FFrameNumber CurrentFrame, FComposableCameraParameterBlock& OutBlock) const
{
	if (!PatchAsset)
	{
		return;
	}
	OutBlock.Reserve(PatchAsset->ExposedParameters.Num() + PatchAsset->ExposedVariables.Num());

	const FFrameTime Time(CurrentFrame);

	// For each ExposedParameter on the asset:
	//   1. Try to sample a matching channel (if user promoted this param to keyable).
	//   2. Else fall back to the bag's static value.
	for (const FComposableCameraExposedParameter& Param : PatchAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}
		if (TrySampleChannelForName(*this, Param.ParameterName, Param.PinType, Time, OutBlock))
		{
			continue;
		}
		// Bag fallback. Identical to FComposableCameraTypeAssetReference::BuildParameterBlock.
		UE::ComposableCameras::ExposedBag::CopyBagValueIntoBlock(
			Parameters, Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType, OutBlock);
	}

	for (const FComposableCameraInternalVariable& Var : PatchAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		if (TrySampleChannelForName(*this, Var.VariableName, Var.VariableType, Time, OutBlock))
		{
			continue;
		}
		UE::ComposableCameras::ExposedBag::CopyBagValueIntoBlock(
			Variables, Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType, OutBlock);
	}
}
