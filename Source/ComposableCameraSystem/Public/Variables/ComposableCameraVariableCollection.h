// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Variables/ComposableCameraVariable.h"
#include "ComposableCameraVariableCollection.generated.h"

/**
 * A collection of composable camera variables.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraVariableCollection : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraVariableCollection(const FObjectInitializer& ObjectInitializer);

	template <typename ValueType>
	const ValueType* FindValue(const FComposableCameraVariableID& VariableID) const
	{
		for (auto& Variable : Variables)
		{
			if (Variable->GetVariableID() == VariableID)
			{
				return reinterpret_cast<const ValueType*>(Variable->GetDefaultValuePtr());
			}
		}
		return nullptr;
	}
	
	virtual void PostLoad() override;

private:
#if WITH_EDITOR
	void CleanUpStrayObjects();
#endif

public:
	UPROPERTY(EditDefaultsOnly, Instanced)
	TArray<TObjectPtr<UComposableCameraVariable>> Variables;
};