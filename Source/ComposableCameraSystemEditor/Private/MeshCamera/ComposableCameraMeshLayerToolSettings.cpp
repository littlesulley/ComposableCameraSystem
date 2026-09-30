// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"

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
	ActiveLayerIndex = Layers.AddDefaulted();
	Layers[ActiveLayerIndex].Name = NAME_None;
	NormalizeLayers();
	OnLayerDataChanged.ExecuteIfBound();
	return ActiveLayerIndex;
}

bool UComposableCameraMeshLayerToolSettings::RemoveActiveLayer()
{
	if (Layers.Num() <= 1 || !Layers.IsValidIndex(ActiveLayerIndex))
	{
		return false;
	}

	Layers.RemoveAt(ActiveLayerIndex);
	ActiveLayerIndex = FMath::Min(ActiveLayerIndex, Layers.Num() - 1);
	NormalizeLayers();
	OnLayerDataChanged.ExecuteIfBound();
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

	Layers.Swap(ActiveLayerIndex, TargetIndex);
	ActiveLayerIndex = TargetIndex;
	OnLayerDataChanged.ExecuteIfBound();
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

	ActiveLayerIndex = LayerIndex;
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
	OnLayerDataChanged.ExecuteIfBound();
}

#if WITH_EDITOR
void UComposableCameraMeshLayerToolSettings::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NormalizeLayers();

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const bool bToolOnlyProperty =
		PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ActiveLayerIndex)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, BrushRadius)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, bErase)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, BrushSegments)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, ProjectionDistance)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UComposableCameraMeshLayerToolSettings, MinimumFloorNormalZ);
	if (!bToolOnlyProperty)
	{
		OnLayerDataChanged.ExecuteIfBound();
	}
}
#endif
