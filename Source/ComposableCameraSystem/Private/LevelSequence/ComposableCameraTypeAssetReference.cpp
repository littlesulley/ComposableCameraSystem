// Copyright Sulley. All rights reserved.

#include "LevelSequence/ComposableCameraTypeAssetReference.h"

#include "ComposableCameraSystemModule.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "LevelSequence/ComposableCameraExposedBagUtils.h"

void FComposableCameraTypeAssetReference::RebuildBagsFromTypeAsset()
{
	if (!TypeAsset)
	{
		Parameters.Reset();
		Variables.Reset();
		return;
	}

	// Parameters bag: one descriptor per entry in TypeAsset->ExposedParameters.
	TArray<FPropertyBagPropertyDesc> ParameterDescs;
	ParameterDescs.Reserve(TypeAsset->ExposedParameters.Num());
	for (const FComposableCameraExposedParameter& Param : TypeAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}
		UE::ComposableCameras::ExposedBag::AddDescIfSupported(Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType, ParameterDescs);
	}

	// Variables bag: one descriptor per entry in TypeAsset->ExposedVariables.
	// InternalVariables are node-private (not caller-overridable). They do not
	// appear in the bag and are driven purely by the TypeAsset's InitialValueString.
	TArray<FPropertyBagPropertyDesc> VariableDescs;
	VariableDescs.Reserve(TypeAsset->ExposedVariables.Num());
	for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		UE::ComposableCameras::ExposedBag::AddDescIfSupported(Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType, VariableDescs);
	}

	// MigrateToNewBagStruct preserves values for properties whose name + type
	// survive the new layout and resets the rest to their type defaults. This
	// is what we want: renaming an exposed parameter ->entry re-created; pure
	// addition of new parameters ->existing values preserved; type change ->	// value reset (can't carry a float through into a vector slot safely).
	if (const UPropertyBag* NewParamStruct = UPropertyBag::GetOrCreateFromDescs(ParameterDescs))
	{
		Parameters.MigrateToNewBagStruct(NewParamStruct);
	}
	if (const UPropertyBag* NewVarStruct = UPropertyBag::GetOrCreateFromDescs(VariableDescs))
	{
		Variables.MigrateToNewBagStruct(NewVarStruct);
	}
}

void FComposableCameraTypeAssetReference::BuildParameterBlock(FComposableCameraParameterBlock& OutBlock) const
{
	if (!TypeAsset)
	{
		return;
	}

	for (const FComposableCameraExposedParameter& Param : TypeAsset->ExposedParameters)
	{
		if (Param.ParameterName.IsNone())
		{
			continue;
		}
		UE::ComposableCameras::ExposedBag::CopyBagValueIntoBlock(Parameters, Param.ParameterName, Param.PinType, Param.StructType, Param.EnumType, OutBlock);
	}

	for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
	{
		if (Var.VariableName.IsNone())
		{
			continue;
		}
		UE::ComposableCameras::ExposedBag::CopyBagValueIntoBlock(Variables, Var.VariableName, Var.VariableType, Var.StructType, Var.EnumType, OutBlock);
	}
}
