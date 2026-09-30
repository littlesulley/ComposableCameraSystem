// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class FPropertyEditorModule;

/** Four-section Details layout for UComposableCameraMeshProfile. */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraMeshProfileCustomization final
	: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	static void Register(FPropertyEditorModule& PropertyEditorModule);
	static void Unregister(FPropertyEditorModule& PropertyEditorModule);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
