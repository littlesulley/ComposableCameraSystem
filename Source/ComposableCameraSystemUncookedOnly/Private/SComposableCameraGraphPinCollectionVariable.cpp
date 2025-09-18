// Copyright Sulley. All rights reserved.

#include "SComposableCameraGraphPinCollectionVariable.h"

void SComposableCameraGraphPinCollectionVariable::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj,
	const TArray<FString>& InNameList)
{
	RefreshNameList(InNameList);
	SGraphPinNameList::Construct(SGraphPinNameList::FArguments(), InGraphPinObj, NameList);
}

void SComposableCameraGraphPinCollectionVariable::RefreshNameList(const TArray<FString>& InNameList)
{
	NameList.Empty();

	for (const FString& Name : InNameList)
	{
		TSharedPtr<FName> NamePropertyItem = MakeShareable(new FName(Name));
		NameList.Add(NamePropertyItem);
	}
}
