// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Shared destination for global CCS tools; individual tools own their entries. */
class FComposableCameraEditorToolsMenu
{
public:
	static const FName MenuName;
	static void Register();
	static void Unregister();

private:
	static void RegisterMenus();
};
