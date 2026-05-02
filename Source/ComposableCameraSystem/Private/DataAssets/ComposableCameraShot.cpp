// Copyright Sulley. All rights reserved.

#include "DataAssets/ComposableCameraShot.h"

bool FComposableCameraAnchorSpec::ResolveWorldPosition(
	TConstArrayView<FComposableCameraShotTarget> Targets,
	FVector& OutPos) const
{
	switch (Mode)
	{
	case EShotAnchorMode::SingleTarget:
		{
			if (TargetIndex < 0 || TargetIndex >= Targets.Num())
			{
				return false;
			}
			return Targets[TargetIndex].Target.ResolveWorldPoint(OutPos);
		}

	case EShotAnchorMode::WeightedWorldCentroid:
		{
			FVector Accumulator = FVector::ZeroVector;
			float TotalWeight = 0.f;

			for (const FComposableCameraAnchorTargetWeight& Entry : WeightedTargets)
			{
				if (Entry.Weight <= 0.f)
				{
					continue;
				}
				if (Entry.TargetIndex < 0 || Entry.TargetIndex >= Targets.Num())
				{
					continue;
				}

				FVector Pivot;
				if (!Targets[Entry.TargetIndex].Target.ResolveWorldPoint(Pivot))
				{
					continue;
				}

				Accumulator += Pivot * Entry.Weight;
				TotalWeight += Entry.Weight;
			}

			if (TotalWeight <= UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}

			OutPos = Accumulator / TotalWeight;
			return true;
		}

	case EShotAnchorMode::FixedWorldPosition:
		{
			OutPos = WorldPosition;
			return true;
		}
	}

	// Defensive: unknown enum case.
	return false;
}
