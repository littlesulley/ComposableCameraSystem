// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_RetVal(bool, FGetIsSimulatingInEditor);
struct COMPOSABLECAMERASYSTEM_API FIsSimulatingInEditor
{
public:
	static inline FGetIsSimulatingInEditor GetIsSimulatingInEditorDelegate;

	static bool GetIsSimulatingInEditor()
	{
#if WITH_EDITOR
		if (GetIsSimulatingInEditorDelegate.IsBound())
		{
			return GetIsSimulatingInEditorDelegate.Execute();
		}
#endif
		return false;
	}
};