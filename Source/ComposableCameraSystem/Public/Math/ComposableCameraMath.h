// Copyright Sulley. All rights reserved.

#pragma once

#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

namespace ComposableCameraSystem
{
	inline float SmoothStep(float T)
	{
		return T * T * (3.f - 2.f * T);
	}

	inline float SmootherStep(float T)
	{
		return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
	}

	inline double SimpleExpDamp(float DeltaTime, float DampTime, float Input)
	{
		if (DeltaTime <= 0.f)
		{
			return 0.f;
		}

		if (DampTime <= 0.f)
		{
			return Input;
		}

		float lnResidual = FMath::Loge(0.01);
		return Input * (1.0f - FMath::Exp(lnResidual * DeltaTime / DampTime));
	}

	inline double NormalizeYaw(double InYaw)
	{
		while (InYaw > 180.)
		{
			InYaw -= 360.0;
		}
		while (InYaw < -180.)
		{
			InYaw += 360.0;
		}
		return InYaw;
	}

	inline FVector SlerpNormalized(const FVector& Start, const FVector& End, float Alpha)
	{
		float Dot = Start.Dot(End);
		float Theta = UKismetMathLibrary::DegAcos(Dot);

		if (FMath::Abs(Theta) < 0.001f)
		{
			return UKismetMathLibrary::VLerp(Start, End, Alpha);
		}
    
		float SinTheta = UKismetMathLibrary::DegSin(Theta);
		float StartRatio = UKismetMathLibrary::DegSin((1 - Alpha) * Theta) / SinTheta;
		float EndRatio = UKismetMathLibrary::DegSin(Alpha * Theta) / SinTheta;
    
		return StartRatio * Start + EndRatio * End;
	}

	inline FVector Slerp(const FVector& Start, const FVector& End, float Alpha)
	{
		float StartMag = Start.Length();
		float EndMag = End.Length();

		FVector StartDirection = Start.GetSafeNormal();
		FVector EndDirection = End.GetSafeNormal();
		FVector Direction = SlerpNormalized(StartDirection, EndDirection, Alpha);

		float Mag = FMath::Lerp(StartMag, EndMag, Alpha);

		return Direction * Mag;
	}
}
