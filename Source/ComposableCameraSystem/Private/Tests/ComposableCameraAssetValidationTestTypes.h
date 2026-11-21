// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ComposableCameraAssetValidationTestTypes.generated.h"

class UCurveFloat;
class UCurveVector;

/** Reflected output classes exercise author-time object constraints without loading assets. */
UCLASS(Abstract, NotBlueprintable, Hidden)
class UComposableCameraValidationObjectSource : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UCurveFloat> FloatCurve;
	UPROPERTY()
	TObjectPtr<UCurveVector> VectorCurve;
	UPROPERTY()
	TObjectPtr<UObject> GenericObject;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& Pins) const override
	{
		for (const FName Name : { FName(TEXT("FloatCurve")), FName(TEXT("VectorCurve")), FName(TEXT("GenericObject")) })
		{
			auto& Pin = Pins.AddDefaulted_GetRef();
			Pin.PinName = Name;
			Pin.Direction = EComposableCameraPinDirection::Output;
			Pin.PinType = EComposableCameraPinType::Object;
		}
	}
};
