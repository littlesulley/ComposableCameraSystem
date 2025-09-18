// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphPinNameList.h"
#include "GameFramework/Actor.h"

class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API SComposableCameraGraphPinCollectionVariable
	: public SGraphPinNameList
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraGraphPinCollectionVariable) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj, const TArray<FString>& InNameList);

	SComposableCameraGraphPinCollectionVariable(){}
	virtual ~SComposableCameraGraphPinCollectionVariable(){}
	
protected:
	void RefreshNameList(const TArray<FString>& InNameList);
};
