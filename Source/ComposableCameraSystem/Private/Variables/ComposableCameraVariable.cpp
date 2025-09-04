// Copyright Sulley. All rights reserved.


#include "Variables/ComposableCameraVariable.h"

UComposableCameraVariable::UComposableCameraVariable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FComposableCameraVariableID UComposableCameraVariable::GetVariableID() const
{
	ensure(Guid.IsValid());
	return FComposableCameraVariableID::FromHashValue(GetTypeHash(Guid));
}

FComposableCameraVariableDefinition UComposableCameraVariable::GetVariableDefinition() const
{
	FComposableCameraVariableDefinition Definition;
	Definition.VariableID = GetVariableID();
	Definition.VariableType = GetVariableType();
#if WITH_EDITORONLY_DATA
	Definition.VariableName = GetDisplayName();
#endif
	return Definition;
}

#if WITH_EDITORONLY_DATA
FString UComposableCameraVariable::GetDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}
	return GetName();
}
#endif

#if WITH_EDITOR
FText UComposableCameraVariable::GetDisplayText() const
{
	if (!DisplayName.IsEmpty())
	{
		return FText::FromString(DisplayName);
	}
	return FText::FromName(GetFName());
}

void UComposableCameraVariable::PostLoad()
{
	if (!Guid.IsValid())
	{
		Guid = FGuid::NewGuid();
	}
	
	Super::PostLoad();
}

void UComposableCameraVariable::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_WasLoaded) && !Guid.IsValid())
	{
		Guid = FGuid::NewGuid();
	}
}

void UComposableCameraVariable::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	if (DuplicateMode == EDuplicateMode::Normal)
	{
		Guid = FGuid::NewGuid();
	}
}
#endif
