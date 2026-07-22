// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "UObject/Object.h"
#include "ComposableCameraMeshLayerToolSettings.generated.h"

UCLASS(Transient)
class UComposableCameraMeshLayerToolSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Edited only through the toolkit's selectable Layer list. */
	UPROPERTY(Transient)
	TArray<FComposableCameraMeshLayerDefinition> Layers;

	/** Internal selection cache. Users select a Layer row instead of editing this index. */
	UPROPERTY(Transient)
	int32 ActiveLayerIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Paint", meta = (ClampMin = "10.0", ClampMax = "5000.0", UIMin = "25.0", UIMax = "1000.0"))
	double BrushRadius = 150.0;

	UPROPERTY(EditAnywhere, Category = "Paint")
	bool bErase = false;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Paint", meta = (ClampMin = "6", ClampMax = "32"))
	int32 BrushSegments = 12;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Paint", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	double ProjectionDistance = 100.0;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Paint", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinimumFloorNormalZ = 0.25;

	FSimpleDelegate OnLayerDataChanged;

	void NormalizeLayers();
	int32 AddLayer();
	bool RemoveActiveLayer();
	bool MoveActiveLayer(int32 Direction);
	bool SelectLayer(const FGuid& LayerId);
	FGuid GetActiveLayerId() const;
	void NotifyLayerDataChanged();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
