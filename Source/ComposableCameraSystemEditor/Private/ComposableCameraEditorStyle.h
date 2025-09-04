// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

/**
 * Editor style.
 */
struct FComposableCameraEditorStyle final : public FSlateStyleSet
{
public:
	FComposableCameraEditorStyle();
	virtual ~FComposableCameraEditorStyle();

	static TSharedRef<FComposableCameraEditorStyle> Get();

private:
	static TSharedPtr<FComposableCameraEditorStyle> Singleton;
};
