// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshCamera/ComposableCameraMeshSurfaceTypes.h"
#include "UObject/Object.h"
#include "ComposableCameraMeshLayerToolSettings.generated.h"

UENUM()
enum class EComposableCameraMeshDrawTool : uint8
{
	Brush,
	Rectangle,
	Circle,
	Polygon,
	Select,
	Erase
};

/** Toolkit modes group the existing viewport tools without changing document data. */
enum class EComposableCameraMeshToolMode : uint8
{
	Draw,
	Select,
	Erase
};

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

	/** Transactional document. Derived visualization stays in the mode. */
	UPROPERTY(Transient)
	FComposableCameraMeshSurfaceAuthoringData WorkingData;

	UPROPERTY(Transient)
	FGuid DocumentRevision;

	UPROPERTY(EditAnywhere, NonTransactional, Category = "Drawing")
	EComposableCameraMeshDrawTool Tool = EComposableCameraMeshDrawTool::Brush;

	UPROPERTY(EditAnywhere, NonTransactional, Category = "Drawing", meta = (EditCondition = "Tool == EComposableCameraMeshDrawTool::Brush || Tool == EComposableCameraMeshDrawTool::Erase", EditConditionHides, ClampMin = "10.0", ClampMax = "5000.0", UIMin = "25.0", UIMax = "1000.0"))
	double BrushRadius = 150.0;

	UPROPERTY(EditAnywhere, NonTransactional, AdvancedDisplay, Category = "Drawing", meta = (EditCondition = "Tool == EComposableCameraMeshDrawTool::Brush", EditConditionHides, ClampMin = "6", ClampMax = "32"))
	int32 BrushSegments = 12;

	/** Snap shape points to the document's XY grid. Zero disables snapping. */
	UPROPERTY(EditAnywhere, NonTransactional, Category = "Drawing", meta = (EditCondition = "Tool != EComposableCameraMeshDrawTool::Brush && Tool != EComposableCameraMeshDrawTool::Erase", EditConditionHides, ClampMin = "0.0", UIMax = "100.0", Units = "cm"))
	double ShapeGridSize = 0.0;

	/** Maximum projected triangle edge length in document space. Does not coarsen the outline. */
	UPROPERTY(EditAnywhere, NonTransactional, AdvancedDisplay, Category = "Drawing", meta = (EditCondition = "Tool == EComposableCameraMeshDrawTool::Rectangle || Tool == EComposableCameraMeshDrawTool::Circle || Tool == EComposableCameraMeshDrawTool::Polygon", EditConditionHides, ClampMin = "10.0", UIMax = "500.0", Units = "cm"))
	double ShapeSampleSpacing = 100.0;

	UPROPERTY(EditAnywhere, NonTransactional, AdvancedDisplay, Category = "Drawing", meta = (EditCondition = "Tool == EComposableCameraMeshDrawTool::Circle", EditConditionHides, ClampMin = "12", ClampMax = "128"))
	int32 CircleSegments = 64;

	UPROPERTY(EditAnywhere, NonTransactional, AdvancedDisplay, Category = "Drawing", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	double ProjectionDistance = 100.0;

	UPROPERTY(EditAnywhere, NonTransactional, AdvancedDisplay, Category = "Drawing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MinimumFloorNormalZ = 0.25;

	FSimpleDelegate OnLayerDataChanged;
	FSimpleDelegate OnToolSettingsChanged;

	void NormalizeLayers();
	int32 AddLayer();
	bool RemoveActiveLayer();
	bool MoveActiveLayer(int32 Direction);
	bool SelectLayer(const FGuid& LayerId);
	FGuid GetActiveLayerId() const;
	void NotifyLayerDataChanged();
	void TouchDocument();
	EComposableCameraMeshToolMode GetToolMode() const;
	EComposableCameraMeshDrawTool GetDrawTool() const;
	void SetToolMode(EComposableCameraMeshToolMode NewMode);
	void SetDrawTool(EComposableCameraMeshDrawTool NewTool);
	bool IsToolPropertyVisible(FName PropertyName) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	EComposableCameraMeshDrawTool LastDrawTool = EComposableCameraMeshDrawTool::Brush;
};

DECLARE_DELEGATE_OneParam(FComposableCameraMeshSelectionChanged, const FPropertyChangedEvent&);

/** Stable Details object: never points into a relocatable document array. */
UCLASS(Transient)
class UComposableCameraMeshLayerSelection : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, NonTransactional, Category = "Layer")
	FComposableCameraMeshLayerDefinition Layer;

	UPROPERTY(Transient) bool bHasShape = false;
	UPROPERTY(VisibleAnywhere, NonTransactional, Category = "Selected Shape", meta = (EditCondition = "bHasShape", EditConditionHides, HideEditConditionToggle))
	EComposableCameraMeshShapeType ShapeType = EComposableCameraMeshShapeType::Rectangle;

	UPROPERTY(EditAnywhere, NonTransactional, Category = "Selected Shape", meta = (EditCondition = "bHasShape", EditConditionHides, HideEditConditionToggle, Units = "cm"))
	FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, NonTransactional, Category = "Selected Shape", meta = (EditCondition = "bHasShape && ShapeType == EComposableCameraMeshShapeType::Rectangle", EditConditionHides, HideEditConditionToggle, ClampMin = "0.01", Units = "cm"))
	FVector2D Size = FVector2D(100.0, 100.0);
	UPROPERTY(EditAnywhere, NonTransactional, Category = "Selected Shape", meta = (EditCondition = "bHasShape && ShapeType == EComposableCameraMeshShapeType::Circle", EditConditionHides, HideEditConditionToggle, ClampMin = "0.01", Units = "cm"))
	double Radius = 100.0;
	UPROPERTY(EditAnywhere, NonTransactional, Category = "Selected Shape", meta = (EditCondition = "bHasShape && ShapeType == EComposableCameraMeshShapeType::Polygon", EditConditionHides, HideEditConditionToggle))
	TArray<FVector2D> Vertices;

	FSimpleDelegate OnBeforeEdit;
	FSimpleDelegate OnLayerEdited;
	FComposableCameraMeshSelectionChanged OnShapeEdited;

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
};
