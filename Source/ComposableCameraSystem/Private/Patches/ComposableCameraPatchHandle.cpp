// Copyright 2026 Sulley. All Rights Reserved.

#include "Patches/ComposableCameraPatchHandle.h"

#include "Patches/ComposableCameraPatchInstance.h"

void UComposableCameraPatchHandle::BindInstance(UComposableCameraPatchInstance* InInstance)
{
	Instance = InInstance;
}

UComposableCameraPatchInstance* UComposableCameraPatchHandle::GetInstance() const
{
	return Instance.Get();
}

bool UComposableCameraPatchHandle::IsActive() const
{
	if (const UComposableCameraPatchInstance* Resolved = Instance.Get())
	{
		return Resolved->Phase != EComposableCameraPatchPhase::Expired;
	}
	return false;
}

EComposableCameraPatchPhase UComposableCameraPatchHandle::GetPhase() const
{
	if (const UComposableCameraPatchInstance* Resolved = Instance.Get())
	{
		return Resolved->Phase;
	}
	return EComposableCameraPatchPhase::Expired;
}

float UComposableCameraPatchHandle::GetAlpha() const
{
	if (const UComposableCameraPatchInstance* Resolved = Instance.Get())
	{
		return Resolved->CurrentAlpha;
	}
	return 0.f;
}

float UComposableCameraPatchHandle::GetElapsedTime() const
{
	if (const UComposableCameraPatchInstance* Resolved = Instance.Get())
	{
		return Resolved->ElapsedTimeActive;
	}
	return 0.f;
}
