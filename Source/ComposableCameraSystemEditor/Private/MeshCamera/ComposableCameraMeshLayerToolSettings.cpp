// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

void UComposableCameraMeshLayerToolSettings::NormalizeLayers()
{
	TSet<FGuid> UsedIds;
	UsedIds.Reserve(Layers.Num());
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		FComposableCameraMeshLayerDefinition& Layer = Layers[LayerIndex];
		if (!Layer.LayerId.IsValid() || UsedIds.Contains(Layer.LayerId))
		{
			Layer.LayerId = FGuid::NewGuid();
			Layer.DebugColor = FLinearColor::MakeFromHSV8(
				static_cast<uint8>((LayerIndex * 47) % 255),
				180,
				255);
			Layer.DebugColor.A = 0.5f;
		}
		UsedIds.Add(Layer.LayerId);

		if (Layer.Name.IsNone())
		{
			Layer.Name = *FString::Printf(TEXT("Mesh Layer %d"), LayerIndex + 1);
		}
	}

	ActiveLayerIndex = Layers.IsEmpty()
		? 0
		: FMath::Clamp(ActiveLayerIndex, 0, Layers.Num() - 1);
}

int32 UComposableCameraMeshLayerToolSettings::AddLayer()
{
	const FScopedTransaction Transaction(NSLOCTEXT("MeshCamera", "AddLayer", "Add Mesh Camera Layer"));
	Modify();
	ActiveLayerIndex = Layers.AddDefaulted();
	Layers[ActiveLayerIndex].Name = NAME_None;
	NormalizeLayers();
	NotifyLayerDataChanged();
	return ActiveLayerIndex;
}

bool UComposableCameraMeshLayerToolSettings::RemoveActiveLayer()
{
	if (Layers.Num() <= 1 || !Layers.IsValidIndex(ActiveLayerIndex))
	{
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MeshCamera", "DeleteLayer", "Delete Mesh Camera Layer"));
	Modify();
	Layers.RemoveAt(ActiveLayerIndex);
	ActiveLayerIndex = FMath::Min(ActiveLayerIndex, Layers.Num() - 1);
	NormalizeLayers();
	NotifyLayerDataChanged();
	return true;
}

bool UComposableCameraMeshLayerToolSettings::MoveActiveLayer(int32 Direction)
{
	if (!Layers.IsValidIndex(ActiveLayerIndex) || Direction == 0)
	{
		return false;
	}

	const int32 TargetIndex = ActiveLayerIndex + FMath::Sign(Direction);
	if (!Layers.IsValidIndex(TargetIndex))
	{
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MeshCamera", "MoveLayer", "Reorder Mesh Camera Layers"));
	Modify();
	Layers.Swap(ActiveLayerIndex, TargetIndex);
	ActiveLayerIndex = TargetIndex;
	NotifyLayerDataChanged();
	return true;
}

bool UComposableCameraMeshLayerToolSettings::SelectLayer(const FGuid& LayerId)
{
	const int32 LayerIndex = Layers.IndexOfByPredicate(
		[LayerId](const FComposableCameraMeshLayerDefinition& Layer)
		{
			return Layer.LayerId == LayerId;
		});
	if (LayerIndex == INDEX_NONE)
	{
		return false;
	}

	if (ActiveLayerIndex != LayerIndex)
	{
		ActiveLayerIndex = LayerIndex;
		OnToolSettingsChanged.ExecuteIfBound();
	}
	return true;
}

FGuid UComposableCameraMeshLayerToolSettings::GetActiveLayerId() const
{
	return Layers.IsValidIndex(ActiveLayerIndex)
		? Layers[ActiveLayerIndex].LayerId
		: FGuid();
}

void UComposableCameraMeshLayerToolSettings::NotifyLayerDataChanged()
{
	NormalizeLayers();
	TouchDocument();
	OnLayerDataChanged.ExecuteIfBound();
}

void UComposableCameraMeshLayerToolSettings::TouchDocument()
{
	DocumentRevision = FGuid::NewGuid();
}

EComposableCameraMeshToolMode UComposableCameraMeshLayerToolSettings::GetToolMode() const
{
	if (Tool == EComposableCameraMeshDrawTool::Select) { return EComposableCameraMeshToolMode::Select; }
	if (Tool == EComposableCameraMeshDrawTool::Erase) { return EComposableCameraMeshToolMode::Erase; }
	return EComposableCameraMeshToolMode::Draw;
}

EComposableCameraMeshDrawTool UComposableCameraMeshLayerToolSettings::GetDrawTool() const
{
	return GetToolMode() == EComposableCameraMeshToolMode::Draw ? Tool : LastDrawTool;
}

void UComposableCameraMeshLayerToolSettings::SetToolMode(EComposableCameraMeshToolMode NewMode)
{
	if (GetToolMode() == NewMode) { return; }
	LastDrawTool = GetDrawTool();
	switch (NewMode)
	{
	case EComposableCameraMeshToolMode::Draw: Tool = LastDrawTool; break;
	case EComposableCameraMeshToolMode::Select: Tool = EComposableCameraMeshDrawTool::Select; break;
	case EComposableCameraMeshToolMode::Erase: Tool = EComposableCameraMeshDrawTool::Erase; break;
	}
	OnToolSettingsChanged.ExecuteIfBound();
}

void UComposableCameraMeshLayerToolSettings::SetDrawTool(EComposableCameraMeshDrawTool NewTool)
{
	if (NewTool == EComposableCameraMeshDrawTool::Select || NewTool == EComposableCameraMeshDrawTool::Erase || Tool == NewTool) { return; }
	LastDrawTool = NewTool;
	Tool = NewTool;
	OnToolSettingsChanged.ExecuteIfBound();
}

bool UComposableCameraMeshLayerToolSettings::IsToolPropertyVisible(FName PropertyName) const
{
	const EComposableCameraMeshToolMode Mode = GetToolMode();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ShapeGridSize))
	{
		return Mode == EComposableCameraMeshToolMode::Select
			|| (Mode == EComposableCameraMeshToolMode::Draw && Tool != EComposableCameraMeshDrawTool::Brush);
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, BrushRadius))
	{
		return Mode == EComposableCameraMeshToolMode::Erase || Tool == EComposableCameraMeshDrawTool::Brush;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, BrushSegments))
	{
		return Tool == EComposableCameraMeshDrawTool::Brush;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ShapeSampleSpacing))
	{
		return Mode == EComposableCameraMeshToolMode::Draw && Tool != EComposableCameraMeshDrawTool::Brush;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, CircleSegments))
	{
		return Tool == EComposableCameraMeshDrawTool::Circle;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ProjectionDistance))
	{
		return Mode != EComposableCameraMeshToolMode::Select;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, MinimumFloorNormalZ))
	{
		return Mode == EComposableCameraMeshToolMode::Draw;
	}
	// Tool is chosen by the mode buttons and Draw menu, never by a mixed enum row.
	return false;
}

#if WITH_EDITOR
void UComposableCameraMeshLayerToolSettings::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// PostEditUndo can emit ordinary property notifications during restoration.
	if (GIsTransacting) { return; }
	// Keep the numeric widget and its slider transaction alive until the final commit.
	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) { return; }
	NormalizeLayers();

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const bool bToolOnlyProperty =
		PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ActiveLayerIndex)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, Tool)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ShapeGridSize)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ShapeSampleSpacing)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, CircleSegments)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, BrushRadius)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, BrushSegments)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ProjectionDistance)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, MinimumFloorNormalZ);
	if (!bToolOnlyProperty)
	{
		NotifyLayerDataChanged();
	}
	else
	{
		OnToolSettingsChanged.ExecuteIfBound();
	}
}
#endif

void UComposableCameraMeshLayerSelection::PreEditChange(FProperty* PropertyAboutToChange)
{
	OnBeforeEdit.ExecuteIfBound();
	Super::PreEditChange(PropertyAboutToChange);
}

void UComposableCameraMeshLayerSelection::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	if (GIsTransacting) { return; }
	if (Event.ChangeType & EPropertyChangeType::Interactive) { return; }
	const FName Name = Event.MemberProperty ? Event.MemberProperty->GetFName() : Event.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerSelection, Layer))
	{
		OnLayerEdited.ExecuteIfBound();
	}
	else
	{
		OnShapeEdited.ExecuteIfBound(Event);
	}
}
