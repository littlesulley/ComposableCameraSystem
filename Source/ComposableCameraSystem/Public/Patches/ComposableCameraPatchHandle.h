// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "ComposableCameraPatchHandle.generated.h"

class UComposableCameraPatchInstance;

/**
 * Caller-facing opaque handle to an active Patch.
 *
 * Held weakly: when the underlying instance is removed (expired, context popped,
 * Director destroyed), the handle's getters return defaulted values and IsActive
 * returns false. Callers do not need to null-check the handle on every getter.
 *
 * Construction is internal to UComposableCameraPatchManager. Callers receive
 * the handle from AddPatch and pass it to ExpirePatch / the BP getters.
 *
 * GC lifetime caveat: the handle UObject itself has only a weak back-reference
 * from the instance, so callers MUST keep the handle alive through their own
 * strong reference. Blueprint usage is automatic: BP variables hold strong
 * refs. C++ usage requires storing the handle in a UPROPERTY (or other
 * GC-tracked location) on the owning class; a raw local pointer that goes out
 * of scope will be collected, after which Manual-channel ExpirePatch becomes
 * impossible (Duration / Condition channels still expire normally).
 */
UCLASS(BlueprintType)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPatchHandle : public UObject
{
	GENERATED_BODY()

public:
	/** Internal binding. Set by PatchManager::AddPatch immediately after handle construction. */
	void BindInstance(UComposableCameraPatchInstance* InInstance);

	UComposableCameraPatchInstance* GetInstance() const;

	bool IsActive() const;

	EComposableCameraPatchPhase GetPhase() const;
	float GetAlpha() const;
	float GetElapsedTime() const;

private:
	UPROPERTY()
	TWeakObjectPtr<UComposableCameraPatchInstance> Instance;
};
