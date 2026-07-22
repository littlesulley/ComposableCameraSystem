// Copyright 2026 Sulley. All Rights Reserved.

#include "DataAssets/ComposableCameraParameterTableRow.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraTypeAsset.h"

void FComposableCameraParameterTableRow::BuildParameterBlock(
	const UComposableCameraTypeAsset& TypeAsset,
	FComposableCameraParameterBlock& OutParameters,
	const FString& SourceDescription) const
{
	OutParameters = FComposableCameraParameterBlock();

	const TArray<FComposableCameraExposedParameter>& Exposed =
		TypeAsset.GetExposedParameters();
	const TArray<FComposableCameraInternalVariable>& ExposedVariables =
		TypeAsset.ExposedVariables;

	TSet<FName> KnownNames;
	KnownNames.Reserve(Exposed.Num() + ExposedVariables.Num());

	for (const FComposableCameraExposedParameter& Parameter : Exposed)
	{
		KnownNames.Add(Parameter.ParameterName);

		const FString* AuthoredValue = Parameters.Values.Find(Parameter.ParameterName);
		const FString DefaultValue = TypeAsset.GetExposedParameterDefaultValue(Parameter);
		const FString& ValueString = AuthoredValue ? *AuthoredValue : DefaultValue;
		if (ValueString.IsEmpty())
		{
			continue;
		}

		FString ParseError;
		const bool bParsed = FComposableCameraParameterBlock::ApplyStringValue(
			OutParameters,
			Parameter.ParameterName,
			Parameter.PinType,
			Parameter.StructType,
			Parameter.EnumType,
			ValueString,
			&ParseError);
		if (!bParsed)
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT(
				"Camera activation source '%s': parameter '%s' parse failed (%s). "
				"Falling back to CameraType default."),
				*SourceDescription,
				*Parameter.ParameterName.ToString(),
				*ParseError);

			if (AuthoredValue
				&& !DefaultValue.IsEmpty()
				&& !DefaultValue.Equals(ValueString))
			{
				FString FallbackError;
				FComposableCameraParameterBlock::ApplyStringValue(
					OutParameters,
					Parameter.ParameterName,
					Parameter.PinType,
					Parameter.StructType,
					Parameter.EnumType,
					DefaultValue,
					&FallbackError);
			}
		}
	}

	for (const FComposableCameraInternalVariable& Variable : ExposedVariables)
	{
		if (Variable.VariableName.IsNone())
		{
			continue;
		}

		KnownNames.Add(Variable.VariableName);

		const FString* AuthoredValue = Parameters.Values.Find(Variable.VariableName);
		const FString& ValueString = AuthoredValue
			? *AuthoredValue
			: Variable.InitialValueString;
		if (ValueString.IsEmpty())
		{
			continue;
		}

		FString ParseError;
		const bool bParsed = FComposableCameraParameterBlock::ApplyStringValue(
			OutParameters,
			Variable.VariableName,
			Variable.VariableType,
			Variable.StructType,
			Variable.EnumType,
			ValueString,
			&ParseError);
		if (!bParsed)
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT(
				"Camera activation source '%s': exposed variable '%s' parse failed (%s). "
				"Falling back to InitialValueString."),
				*SourceDescription,
				*Variable.VariableName.ToString(),
				*ParseError);

			if (AuthoredValue
				&& !Variable.InitialValueString.IsEmpty()
				&& !Variable.InitialValueString.Equals(ValueString))
			{
				FString FallbackError;
				FComposableCameraParameterBlock::ApplyStringValue(
					OutParameters,
					Variable.VariableName,
					Variable.VariableType,
					Variable.StructType,
					Variable.EnumType,
					Variable.InitialValueString,
					&FallbackError);
			}
		}
	}

	for (const TPair<FName, FString>& Entry : Parameters.Values)
	{
		if (!KnownNames.Contains(Entry.Key))
		{
			UE_LOG(LogComposableCameraSystem, Verbose, TEXT(
				"Camera activation source '%s': orphaned entry '%s' is not present on CameraType '%s'."),
				*SourceDescription,
				*Entry.Key.ToString(),
				*TypeAsset.GetName());
		}
	}
}
