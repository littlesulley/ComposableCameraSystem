// Copyright Sulley. All rights reserved.

#pragma once

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
}