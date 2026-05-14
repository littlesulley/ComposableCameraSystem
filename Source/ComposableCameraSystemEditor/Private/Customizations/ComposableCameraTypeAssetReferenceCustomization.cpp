// Copyright Sulley. All rights reserved.

#include "Customizations/ComposableCameraTypeAssetReferenceCustomization.h"

#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "LevelSequence/ComposableCameraTypeAssetReference.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "ComposableCameraTypeAssetReferenceCustomization"

namespace
{
	struct FCompatibilityIssues
	{
		int32 ComputeOnlyCount = 0;
		TArray<FString> RequiresPCMClassNames;

		bool HasAny() const
		{
			return ComputeOnlyCount > 0 || RequiresPCMClassNames.Num() > 0;
		}
	};

	/**
	 * Walk a TypeAsset's NodeTemplates and ComputeNodeTemplates, query each
	 * entry's GetLevelSequenceCompatibility(), and accumulate the findings.
	 * Null templates are skipped silently.
	 */
	void GatherCompatibilityIssues(const UComposableCameraTypeAsset* TypeAsset, FCompatibilityIssues& OutIssues)
	{
		if (!TypeAsset)
		{
			return;
		}

		TSet<FString> SeenPCMClasses; // dedup; one node class can appear many times

		auto Inspect = [&](const UComposableCameraCameraNodeBase* Node)
		{
			if (!Node)
			{
				return;
			}
			const EComposableCameraNodeLevelSequenceCompatibility Compat = Node->GetLevelSequenceCompatibility();
			switch (Compat)
			{
			case EComposableCameraNodeLevelSequenceCompatibility::ComputeOnly:
				++OutIssues.ComputeOnlyCount;
				break;
			case EComposableCameraNodeLevelSequenceCompatibility::RequiresPCM:
				{
					const FString ClassName = Node->GetClass()->GetName();
					bool bAlreadyIn = false;
					SeenPCMClasses.Add(ClassName, &bAlreadyIn);
					if (!bAlreadyIn)
					{
						OutIssues.RequiresPCMClassNames.Add(ClassName);
					}
				}
				break;
			case EComposableCameraNodeLevelSequenceCompatibility::Compatible:
			default:
				break;
			}
		};

		for (const UComposableCameraCameraNodeBase* Node: TypeAsset->NodeTemplates)
		{
			Inspect(Node);
		}
		for (const UComposableCameraComputeNodeBase* Node: TypeAsset->ComputeNodeTemplates)
		{
			Inspect(Node);
		}
	}

	/**
	 * Inspect every raw FComposableCameraTypeAssetReference behind the struct
	 * property handle and merge the issues into a single view. Handles
	 * multi-select (Details panel on N selected actors) cleanly by unioning
	 * compute-only counts and PCM class lists across all instances.
	 */
	void GatherCompatibilityIssuesFromHandle(TSharedRef<IPropertyHandle> PropertyHandle,
		FCompatibilityIssues& OutIssues)
	{
		TArray<void*> RawDataArray;
		PropertyHandle->AccessRawData(RawDataArray);

		TSet<FString> SeenPCMClasses;
		for (void* RawPtr: RawDataArray)
		{
			if (!RawPtr)
			{
				continue;
			}
			const FComposableCameraTypeAssetReference& Ref =
				*static_cast<FComposableCameraTypeAssetReference*>(RawPtr);

			FCompatibilityIssues PerInstance;
			GatherCompatibilityIssues(Ref.TypeAsset, PerInstance);

			OutIssues.ComputeOnlyCount += PerInstance.ComputeOnlyCount;
			for (const FString& ClassName: PerInstance.RequiresPCMClassNames)
			{
				bool bAlreadyIn = false;
				SeenPCMClasses.Add(ClassName, &bAlreadyIn);
				if (!bAlreadyIn)
				{
					OutIssues.RequiresPCMClassNames.Add(ClassName);
				}
			}
		}
	}
}

// Static Registration 

TSharedRef<IPropertyTypeCustomization> FComposableCameraTypeAssetReferenceCustomization::MakeInstance()
{
	return MakeShared<FComposableCameraTypeAssetReferenceCustomization>();
}

void FComposableCameraTypeAssetReferenceCustomization::Register(FPropertyEditorModule& PropertyEditorModule)
{
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FComposableCameraTypeAssetReference::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FComposableCameraTypeAssetReferenceCustomization::MakeInstance));
}

void FComposableCameraTypeAssetReferenceCustomization::Unregister(FPropertyEditorModule& PropertyEditorModule)
{
	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FComposableCameraTypeAssetReference::StaticStruct()->GetFName());
	}
}

// Header 

void FComposableCameraTypeAssetReferenceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& Utils)
{
	HeaderRow.NameContent()
	[PropertyHandle->CreatePropertyNameWidget()];
	// No custom value widget - the struct opens into CustomizeChildren below
	// like any normal struct. The warning lives among the children, not in
	// the collapsed header.
}

// Children 

void FComposableCameraTypeAssetReferenceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& Utils)
{
	// Accumulate LS-compatibility issues across all raw instances behind the
	// handle (one instance per selected actor in multi-select; usually one).
	FCompatibilityIssues Issues;
	GatherCompatibilityIssuesFromHandle(PropertyHandle, Issues);

	// Emit the warning banner as the first child row when anything flagged.
	//
	// We feed SWarningOrErrorBox's built-in Message slot rather than rolling
	// a custom Content layout - the built-in path uses SRichTextBlock with
	// AutoWrapText driven by the widget's own width, which is what actually
	// wraps to the Details column. A hand-rolled SVerticalBox inside.Content()
	// bypasses that width constraint and lets text run off the edge.
	if (Issues.HasAny())
	{
		TStringBuilder<1024> Msg;

		if (Issues.ComputeOnlyCount > 0)
		{
			Msg.Appendf(TEXT("This TypeAsset contains %d Compute node(s). Compute nodes are not evaluated in ")
				TEXT("Level Sequence playback - the component skips the BeginPlay compute chain entirely. ")
				TEXT("Any value the compute chain would publish must instead be re-sourced as an exposed ")
				TEXT("parameter for LS-driven cameras."),
				Issues.ComputeOnlyCount);
		}

		if (Issues.RequiresPCMClassNames.Num() > 0)
		{
			if (Msg.Len() > 0)
			{
				Msg.Append(TEXT("\n\n"));
			}
			Msg.Append(TEXT("This TypeAsset contains node(s) that require a PlayerCameraManager:"));
			for (const FString& ClassName: Issues.RequiresPCMClassNames)
			{
				Msg.Appendf(TEXT("\n %s"), *ClassName);
			}
			Msg.Append(TEXT("\nThese nodes are no-ops in Level Sequence evaluation (the component drives the ")
				TEXT("camera without a PlayerCameraManager). Consider using LS-compatible alternatives, ")
				TEXT("or reserve this TypeAsset for the PCM activation path only."));
		}

		ChildBuilder.AddCustomRow(LOCTEXT("CompatibilityWarningsFilter", "Level Sequence Compatibility"))
		.WholeRowContent()
		[SNew(SWarningOrErrorBox)
			.MessageStyle(EMessageStyle::Warning)
			.AutoWrapText(true)
			.Message(FText::FromString(FString(Msg.ToView())))];
	}

	// Default child rows: TypeAsset, Parameters, Variables. Without this loop
	// the struct would collapse to just the warning row and hide every field.
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Index);
		if (ChildHandle.IsValid())
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
