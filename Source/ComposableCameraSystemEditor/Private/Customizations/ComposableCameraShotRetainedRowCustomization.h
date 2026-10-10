// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"

class IPropertyRowGenerator;

/** Shot Editor only: native value editors with stable row topology across mode changes. */
class FShotEditorRetainedRowCustomization final : public IPropertyTypeCustomization
{
public:
	static void Register(IPropertyRowGenerator& Generator);
	static TAttribute<EVisibility> MakeVisibility(const TSharedPtr<IPropertyHandle>& Handle);
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header,
		IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Builder,
		IPropertyTypeCustomizationUtils& Utils) override;
private:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
};
