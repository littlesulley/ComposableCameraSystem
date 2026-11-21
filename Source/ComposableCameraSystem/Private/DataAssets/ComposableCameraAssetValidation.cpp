// Copyright 2026 Sulley. All Rights Reserved.
#include "DataAssets/ComposableCameraAssetValidation.h"

#if WITH_EDITOR
#include "Actions/ComposableCameraActionBase.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraShotTemplateLibrary.h"
#include "DataAssets/ComposableCameraTransitionDataAsset.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Misc/DataValidation.h"
#include "Modules/ModuleManager.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCompositionFramingNode.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ComposableCameraAssetValidation"

namespace ComposableCameraAssetValidation
{
	static EDataValidationResult Finish(EDataValidationResult ParentResult, const FDataValidationContext& Context, uint32 InitialErrors)
	{
		return ParentResult == EDataValidationResult::Invalid || Context.GetNumErrors() > InitialErrors
			? EDataValidationResult::Invalid : EDataValidationResult::Valid;
	}

	static void Require(bool bValid, FDataValidationContext& Context, const FString& Path, const FText& Reason)
	{
		if (!bValid) Context.AddError(FText::Format(LOCTEXT("FieldError", "{0}: {1}"), FText::FromString(Path), Reason));
	}

	void ValidateSoftAsset(const FSoftObjectPath& Reference, const UClass* ExpectedClass,
		FDataValidationContext& Context, const FString& FieldPath)
	{
		if (Reference.IsNull())
		{
			Require(false, Context, FieldPath, LOCTEXT("AssetUnset", "Select an asset."));
			return;
		}
		if (const UObject* Loaded = Reference.ResolveObject())
		{
			Require(IsValid(Loaded) && Loaded->IsA(ExpectedClass), Context, FieldPath,
				LOCTEXT("AssetClass", "Referenced asset has an incompatible class."));
			return;
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const FSoftObjectPath Redirected = Registry.GetRedirectedObjectPath(Reference);
		const FAssetData Data = Registry.GetAssetByObjectPath(Redirected);
		if (!Data.IsValid())
		{
			if (Registry.IsLoadingAssets())
				Context.AddWarning(FText::Format(LOCTEXT("RegistryPending", "{0}: asset discovery is incomplete. Validate again after scanning finishes."), FText::FromString(FieldPath)));
			else
				Require(false, Context, FieldPath, LOCTEXT("AssetMissing", "Referenced asset was not found in the Asset Registry."));
			return;
		}
		if (Data.AssetClassPath == ExpectedClass->GetClassPathName()) return;
		if (const UClass* LoadedClass = Data.GetClass())
		{
			Require(LoadedClass->IsChildOf(ExpectedClass), Context, FieldPath, LOCTEXT("AssetClass", "Referenced asset has an incompatible class."));
			return;
		}
		TArray<FTopLevelAssetPath> Ancestors;
		if (Registry.GetAncestorClassNames(Data.AssetClassPath, Ancestors))
			Require(Ancestors.Contains(ExpectedClass->GetClassPathName()), Context, FieldPath, LOCTEXT("AssetClass", "Referenced asset has an incompatible class."));
		else
			Context.AddWarning(FText::Format(LOCTEXT("UnknownAssetClass", "{0}: asset exists, but class ancestry is unavailable. Load its class and validate again."), FText::FromString(FieldPath)));
	}

	void ValidateShot(const FComposableCameraShot& Shot, FDataValidationContext& Context)
	{
		// Validate authored structure, never resolve level actors or preview meshes.
		// An unbound subject is intentional in reusable Shot templates.
		const auto CheckIndex = [&](int32 Index, const FString& Path)
		{
			Require(Shot.Targets.IsValidIndex(Index), Context, Path, LOCTEXT("TargetIndex", "Target index is outside the Shot's Targets array."));
		};
		const auto CheckAnchor = [&](const FComposableCameraAnchorSpec& Anchor, const FString& Path)
		{
			switch (Anchor.Mode)
			{
			case EShotAnchorMode::SingleTarget:
				CheckIndex(Anchor.TargetIndex, Path);
				break;
			case EShotAnchorMode::WeightedWorldCentroid:
			{
				bool bHasWeight = false;
				for (int32 Index = 0; Index < Anchor.WeightedTargets.Num(); ++Index)
				{
					const auto& Entry = Anchor.WeightedTargets[Index];
					const FString EntryPath = FString::Printf(TEXT("%s.WeightedTargets[%d]"), *Path, Index);
					Require(FMath::IsFinite(Entry.Weight) && Entry.Weight >= 0.f, Context, EntryPath,
						LOCTEXT("Weight", "Weight must be finite and non-negative."));
					if (FMath::IsFinite(Entry.Weight) && Entry.Weight > 0.f)
					{
						CheckIndex(Entry.TargetIndex, EntryPath);
						bHasWeight |= Shot.Targets.IsValidIndex(Entry.TargetIndex);
					}
				}
				Require(bHasWeight, Context, Path, LOCTEXT("Centroid", "Weighted anchor needs at least one valid target with positive weight."));
				break;
			}
			case EShotAnchorMode::FixedWorldPosition:
				Require(!Anchor.WorldPosition.ContainsNaN(), Context, Path, LOCTEXT("Position", "Position must be finite."));
				break;
			default:
				Require(false, Context, Path, LOCTEXT("AnchorMode", "Unknown anchor mode."));
				break;
			}
		};
		const auto CheckNonNegative = [&](float Value, const TCHAR* Path)
		{
			Require(FMath::IsFinite(Value) && Value >= 0.f, Context, Path, LOCTEXT("NonNegative", "Value must be finite and non-negative."));
		};
		Require(Shot.Placement.Mode == EShotPlacementMode::FixedWorldPosition || Shot.Placement.Mode == EShotPlacementMode::AnchorOrbit
			|| Shot.Placement.Mode == EShotPlacementMode::AnchorAtScreen, Context, TEXT("Placement.Mode"), LOCTEXT("PlacementMode", "Unknown placement mode."));
		Require(Shot.Aim.Mode == EShotAimMode::NoOp || Shot.Aim.Mode == EShotAimMode::LookAtAnchor,
			Context, TEXT("Aim.Mode"), LOCTEXT("AimMode", "Unknown aim mode."));
		Require(Shot.Lens.FOVMode == EShotFOVMode::Manual || Shot.Lens.FOVMode == EShotFOVMode::SolvedFromBoundsFit,
			Context, TEXT("Lens.FOVMode"), LOCTEXT("LensMode", "Unknown FOV mode."));

		if (Shot.Placement.Mode == EShotPlacementMode::FixedWorldPosition)
		{
			Require(!Shot.Placement.FixedWorldPosition.ContainsNaN(), Context, TEXT("Placement.FixedWorldPosition"), LOCTEXT("Position", "Position must be finite."));
		}
		else
		{
			CheckAnchor(Shot.Placement.PlacementAnchor, TEXT("Placement.PlacementAnchor"));
			CheckNonNegative(Shot.Placement.Distance, TEXT("Placement.Distance"));
			CheckNonNegative(Shot.Placement.DistanceSpeed, TEXT("Placement.DistanceSpeed"));
			if (Shot.Placement.Mode == EShotPlacementMode::AnchorOrbit)
			{
				if (Shot.Placement.BasisFrame == EShotPlacementBasisFrame::InheritFromActor
					|| Shot.Placement.BasisFrame == EShotPlacementBasisFrame::TwoTargetAxis)
					CheckIndex(Shot.Placement.BasisActorIndex, TEXT("Placement.BasisActorIndex"));
				if (Shot.Placement.BasisFrame == EShotPlacementBasisFrame::TwoTargetAxis)
				{
					CheckIndex(Shot.Placement.BasisSecondaryTargetIndex, TEXT("Placement.BasisSecondaryTargetIndex"));
					Require(Shot.Placement.BasisActorIndex != Shot.Placement.BasisSecondaryTargetIndex, Context,
						TEXT("Placement.BasisFrame"), LOCTEXT("BasisPair", "Two-target basis needs two different target indices."));
				}
				Require(!Shot.Placement.LocalCameraDirection.ContainsNaN(), Context, TEXT("Placement.LocalCameraDirection"), LOCTEXT("Direction", "Direction must be finite."));
			}
		}
		// AnchorAtScreen supports both LookAtAnchor and NoOp in SolveShot.
		// NoOp skips the aim anchor; focus may still consume it independently below.
		if (Shot.Aim.Mode == EShotAimMode::LookAtAnchor) CheckAnchor(Shot.Aim.AimAnchor, TEXT("Aim.AimAnchor"));

		switch (Shot.Focus.Mode)
		{
		case EShotFocusMode::Manual: CheckNonNegative(Shot.Focus.ManualDistance, TEXT("Focus.ManualDistance")); break;
		case EShotFocusMode::FollowPlacementAnchor: CheckAnchor(Shot.Placement.PlacementAnchor, TEXT("Focus.PlacementAnchor")); break;
		case EShotFocusMode::FollowAimAnchor: CheckAnchor(Shot.Aim.AimAnchor, TEXT("Focus.AimAnchor")); break;
		case EShotFocusMode::FollowCustomAnchor: CheckAnchor(Shot.Focus.FocusAnchor, TEXT("Focus.FocusAnchor")); break;
		default: Require(false, Context, TEXT("Focus.Mode"), LOCTEXT("FocusMode", "Unknown focus mode.")); break;
		}
		if (Shot.Lens.FOVMode == EShotFOVMode::Manual)
			Require(FMath::IsFinite(Shot.Lens.ManualFOV) && Shot.Lens.ManualFOV > 0.f && Shot.Lens.ManualFOV < 180.f,
				Context, TEXT("Lens.ManualFOV"), LOCTEXT("FOV", "FOV must be finite and between 0 and 180 degrees, exclusive."));
		else
		{
			Require(FMath::IsFinite(Shot.Lens.FOVClamp.Min) && FMath::IsFinite(Shot.Lens.FOVClamp.Max)
				&& Shot.Lens.FOVClamp.Min > 0.f && Shot.Lens.FOVClamp.Max < 180.f && Shot.Lens.FOVClamp.Min <= Shot.Lens.FOVClamp.Max,
				Context, TEXT("Lens.FOVClamp"), LOCTEXT("FOVRange", "FOV limits must be ordered, finite and inside (0, 180)."));
			Require(FMath::IsFinite(Shot.Lens.DesiredViewportFillRatio) && Shot.Lens.DesiredViewportFillRatio > 0.f,
				Context, TEXT("Lens.DesiredViewportFillRatio"), LOCTEXT("Fill", "Viewport fill ratio must be finite and positive."));
			if (!Shot.Targets.ContainsByPredicate([](const auto& Target)
				{ return Target.BoundsShape != EShotTargetBoundsShape::None && Target.BoundsContributionWeight > 0.f; }))
				Context.AddWarning(LOCTEXT("NoBounds", "Lens uses bounds-fit but no target contributes bounds. The solver will use its fallback FOV."));
		}
		Require(FMath::IsFinite(Shot.Lens.Aperture) && Shot.Lens.Aperture > 0.f, Context, TEXT("Lens.Aperture"), LOCTEXT("Aperture", "Aperture must be finite and positive."));
		Require(FMath::IsFinite(Shot.Roll), Context, TEXT("Roll"), LOCTEXT("Roll", "Roll must be finite."));
		CheckNonNegative(Shot.RollSpeed, TEXT("RollSpeed"));
		CheckNonNegative(Shot.Lens.FOVSpeed, TEXT("Lens.FOVSpeed"));
		for (int32 Index = 0; Index < Shot.Targets.Num(); ++Index)
		{
			const auto& Target = Shot.Targets[Index];
			const FString Path = FString::Printf(TEXT("Targets[%d]"), Index);
			if (Target.BoundsShape == EShotTargetBoundsShape::ManualExtent)
				Require(!Target.ManualBoundsExtent.ContainsNaN() && Target.ManualBoundsExtent.GetMin() >= 0.0,
					Context, Path, LOCTEXT("Bounds", "Manual bounds extents must be finite and non-negative."));
			if (Target.BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds && Target.BoundsCachePolicy == EBoundsCachePolicy::Periodic)
				Require(Target.BoundsRefreshIntervalFrames > 0, Context, Path, LOCTEXT("Refresh", "Periodic bounds refresh interval must be positive."));
			if (Target.BoundsShape != EShotTargetBoundsShape::None)
				Require(FMath::IsFinite(Target.BoundsContributionWeight) && Target.BoundsContributionWeight >= 0.f,
					Context, Path, LOCTEXT("Weight", "Weight must be finite and non-negative."));
		}
	}

	void CollectTypeMessages(const UComposableCameraTypeAsset& Asset, TArray<FComposableCameraBuildMessage>& Messages)
	{
		const auto Add = [&](uint8 Severity, int32 NodeIndex, bool bCompute, FName PinName, const FText& Text)
		{
			auto& Message = Messages.AddDefaulted_GetRef();
			Message.Severity = Severity;
			Message.NodeIndex = NodeIndex;
			Message.bIsComputeChain = bCompute;
			Message.PinName = PinName;
			Message.Message = FText::Format(LOCTEXT("NodeMessage", "{0} node {1}, pin '{2}': {3}"),
				bCompute ? LOCTEXT("BeginPlay", "BeginPlay") : LOCTEXT("Start", "Start"),
				FText::AsNumber(NodeIndex), FText::FromName(PinName), Text);
		};
		const auto SameShape = [](const FComposableCameraNodePinDeclaration& A, const FComposableCameraNodePinDeclaration& B)
		{
			return A.PinType == B.PinType
				&& (A.PinType != EComposableCameraPinType::Struct || A.StructType == B.StructType)
				&& (A.PinType != EComposableCameraPinType::Enum || A.EnumType == B.EnumType)
				&& (A.PinType != EComposableCameraPinType::Delegate || A.SignatureFunction == B.SignatureFunction);
		};
		const auto ValidateChain = [&](const auto& Templates, const TArray<FComposableCameraPinConnection>& Connections, bool bCompute)
		{
			TArray<TArray<FComposableCameraNodePinDeclaration>> Pins;
			Pins.SetNum(Templates.Num());
			for (int32 Index = 0; Index < Templates.Num(); ++Index)
				if (Templates[Index]) Templates[Index]->GatherAllPinDeclarations(Pins[Index]);
			const auto FindPin = [&](int32 Index, FName Name, EComposableCameraPinDirection Direction) -> const FComposableCameraNodePinDeclaration*
			{
				return Pins.IsValidIndex(Index) ? Pins[Index].FindByPredicate([&](const auto& Pin)
					{ return Pin.PinName == Name && Pin.Direction == Direction; }) : nullptr;
			};
			for (const auto& Connection : Connections)
			{
				// Out-of-range indices and null nodes are reported by the shared base collector.
				if (!Templates.IsValidIndex(Connection.SourceNodeIndex) || !Templates.IsValidIndex(Connection.TargetNodeIndex)
					|| !Templates[Connection.SourceNodeIndex] || !Templates[Connection.TargetNodeIndex]) continue;
				const auto* Source = FindPin(Connection.SourceNodeIndex, Connection.SourcePinName, EComposableCameraPinDirection::Output);
				const auto* Target = FindPin(Connection.TargetNodeIndex, Connection.TargetPinName, EComposableCameraPinDirection::Input);
				if (!Source || !Target)
				{
					Add(2, Connection.TargetNodeIndex, bCompute, Connection.TargetPinName,
						LOCTEXT("StaleWire", "Connection references a missing input/output pin. Reconnect it after refreshing the node."));
					continue;
				}
				if (!SameShape(*Source, *Target))
				{
					Add(2, Connection.TargetNodeIndex, bCompute, Connection.TargetPinName,
						LOCTEXT("PinShape", "Connection storage types do not match. Float/Double connections require an explicit Math conversion node."));
					continue;
				}
				if (Target->PinType == EComposableCameraPinType::Actor || Target->PinType == EComposableCameraPinType::Object)
				{
					const auto* SourceProperty = FindFProperty<FObjectPropertyBase>(Templates[Connection.SourceNodeIndex]->GetClass(), Source->PinName);
					const auto* TargetProperty = FindFProperty<FObjectPropertyBase>(Templates[Connection.TargetNodeIndex]->GetClass(), Target->PinName);
					const UClass* GenericClass = Target->PinType == EComposableCameraPinType::Actor ? AActor::StaticClass() : UObject::StaticClass();
					const UClass* SourceClass = SourceProperty ? SourceProperty->PropertyClass.Get() : GenericClass;
					const UClass* TargetClass = TargetProperty ? TargetProperty->PropertyClass.Get() : GenericClass;
					if (SourceClass && TargetClass && !SourceClass->IsChildOf(TargetClass))
					{
						const bool bPossibleDowncast = TargetClass->IsChildOf(SourceClass);
						Add(bPossibleDowncast ? 1 : 2, Connection.TargetNodeIndex, bCompute, Connection.TargetPinName,
							FText::Format(bPossibleDowncast
								? LOCTEXT("ObjectDowncast", "Source class {0} does not guarantee {1}. Non-matching runtime objects will resolve to null.")
								: LOCTEXT("ObjectMismatch", "Source class {0} cannot satisfy target class {1}."),
								FText::FromString(SourceClass->GetName()), FText::FromString(TargetClass->GetName())));
					}
				}
			}
			if (!bCompute)
			{
				for (const auto& Parameter : Asset.ExposedParameters)
				{
					const auto* Target = FindPin(Parameter.TargetNodeIndex, Parameter.TargetPinName, EComposableCameraPinDirection::Input);
					FComposableCameraNodePinDeclaration Source;
					Source.PinType = Parameter.PinType; Source.StructType = Parameter.StructType;
					Source.EnumType = Parameter.EnumType; Source.SignatureFunction = Parameter.SignatureFunction;
					if (!Target || !SameShape(Source, *Target))
						Add(2, Parameter.TargetNodeIndex, false, Parameter.TargetPinName,
							LOCTEXT("StaleParameter", "Exposed parameter no longer matches its target pin. Re-expose it to refresh its type."));
				}
			}
		};
		ValidateChain(Asset.NodeTemplates, Asset.PinConnections, false);
		ValidateChain(Asset.ComputeNodeTemplates, Asset.ComputePinConnections, true);
		for (int32 Index = 0; Index < Asset.NodeTemplates.Num(); ++Index)
		{
			if (const auto* Framing = Cast<UComposableCameraCompositionFramingNode>(Asset.NodeTemplates[Index]))
			{
				FDataValidationContext ShotContext;
				ValidateShot(Framing->Shot, ShotContext);
				for (const auto& Issue : ShotContext.GetIssues())
					Add(Issue.Severity == EMessageSeverity::Error ? 2 : 1, Index, false, TEXT("Shot"), Issue.Message);
			}
		}
	}
}

EDataValidationResult UComposableCameraShotAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	const uint32 InitialErrors = Context.GetNumErrors();
	ComposableCameraAssetValidation::ValidateShot(Shot, Context);
	return ComposableCameraAssetValidation::Finish(Parent, Context, InitialErrors);
}

EDataValidationResult UComposableCameraShotTemplateLibrary::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	const uint32 InitialErrors = Context.GetNumErrors();
	TSet<FName> Names;
	for (int32 Index = 0; Index < Shots.Num(); ++Index)
	{
		const auto& Entry = Shots[Index];
		const FString Path = FString::Printf(TEXT("Shots[%d]"), Index);
		ComposableCameraAssetValidation::Require(!Entry.Name.IsNone() && !Names.Contains(Entry.Name), Context, Path,
			LOCTEXT("TemplateName", "Template names must be non-empty and unique."));
		ComposableCameraAssetValidation::Require(IsValid(Entry.Shot), Context, Path, LOCTEXT("TemplateShot", "Select a Shot asset."));
		Names.Add(Entry.Name);
	}
	return ComposableCameraAssetValidation::Finish(Parent, Context, InitialErrors);
}

EDataValidationResult UComposableCameraMeshProfile::IsDataValid(FDataValidationContext& Context) const
{
	using namespace ComposableCameraAssetValidation;
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	const uint32 InitialErrors = Context.GetNumErrors();
	Require(!NeedsTypeSelection(), Context, TEXT("Type"), LOCTEXT("ProfileMigration", "Legacy mixed Profile requires an explicit effect-family selection."));
	// Validate only the selected family. Resolve metadata, never synchronously load an asset.
	switch (Type)
	{
	case EComposableCameraMeshProfileType::CameraType:
		ValidateSoftAsset(Camera.CameraType.ToSoftObjectPath(), UComposableCameraTypeAsset::StaticClass(), Context, TEXT("Camera.CameraType"));
		if (!Camera.TransitionOverride.IsNull()) ValidateSoftAsset(Camera.TransitionOverride.ToSoftObjectPath(),
			UComposableCameraTransitionDataAsset::StaticClass(), Context, TEXT("Camera.TransitionOverride"));
		break;
	case EComposableCameraMeshProfileType::Modifier:
		if (ModifierAssets.IsEmpty()) Context.AddWarning(LOCTEXT("EmptyModifiers", "Modifier Profile has no Modifier assets and will have no effect."));
		for (int32 Index = 0; Index < ModifierAssets.Num(); ++Index)
			Require(IsValid(ModifierAssets[Index]), Context, FString::Printf(TEXT("ModifierAssets[%d]"), Index), LOCTEXT("ProfileModifier", "Select a Modifier asset or remove the empty entry."));
		break;
	case EComposableCameraMeshProfileType::Action:
		ValidateSoftAsset(Action.ActionAsset.ToSoftObjectPath(), UComposableCameraActionTypeAsset::StaticClass(), Context, TEXT("Action.ActionAsset"));
		break;
	case EComposableCameraMeshProfileType::Patch:
		ValidateSoftAsset(Patch.PatchAsset.ToSoftObjectPath(), UComposableCameraPatchTypeAsset::StaticClass(), Context, TEXT("Patch.PatchAsset"));
		break;
	default: Require(false, Context, TEXT("Type"), LOCTEXT("ProfileType", "Unknown Profile effect family.")); break;
	}
	return Finish(Parent, Context, InitialErrors);
}

EDataValidationResult UComposableCameraActionTypeAsset::IsDataValid(FDataValidationContext& Context) const
{
	using namespace ComposableCameraAssetValidation;
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	const uint32 InitialErrors = Context.GetNumErrors();
	Require(IsValid(ActionTemplate), Context, TEXT("ActionTemplate"), LOCTEXT("ActionTemplate", "Select an Action template."));
	if (IsValid(ActionTemplate))
	{
		if (ActionTemplate->ExpirationType & static_cast<uint8>(EComposableCameraActionExpirationType::Duration))
			Require(FMath::IsFinite(ActionTemplate->Duration) && ActionTemplate->Duration > 0.f, Context, TEXT("ActionTemplate.Duration"),
				LOCTEXT("ActionDuration", "Duration-enabled Actions require a finite positive duration."));
		if (ActionTemplate->ExecutionType == EComposableCameraActionExecutionType::PreNodeTick
			|| ActionTemplate->ExecutionType == EComposableCameraActionExecutionType::PostNodeTick)
			Require(ActionTemplate->TargetNodeClass != nullptr, Context, TEXT("ActionTemplate.TargetNodeClass"),
				LOCTEXT("ActionNode", "Node-scoped Actions require a target node class."));
	}
	return Finish(Parent, Context, InitialErrors);
}

EDataValidationResult UComposableCameraTransitionDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	const uint32 InitialErrors = Context.GetNumErrors();
	ComposableCameraAssetValidation::Require(IsValid(Transition), Context, TEXT("Transition"), LOCTEXT("Transition", "Select a Transition template."));
	return ComposableCameraAssetValidation::Finish(Parent, Context, InitialErrors);
}

EDataValidationResult UComposableCameraNodeModifierDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	using namespace ComposableCameraAssetValidation;
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	const uint32 InitialErrors = Context.GetNumErrors();
	for (int32 Index = 0; Index < Modifiers.Num(); ++Index)
	{
		const auto* Modifier = Modifiers[Index].Get();
		const FString Path = FString::Printf(TEXT("Modifiers[%d]"), Index);
		Require(IsValid(Modifier), Context, Path, LOCTEXT("Modifier", "Select a Modifier or remove the empty entry."));
		if (!IsValid(Modifier)) continue;
		if (Modifier->bUseCustomModifierClass)
		{
			Require(IsValid(Modifier->CustomModifier) && Modifier->CustomModifier.Get() != Modifier && Modifier->CustomModifier->NodeClass != nullptr, Context, Path,
				LOCTEXT("CustomModifier", "Custom Modifier requires an instance and a target node class."));
			continue;
		}
		if (!IsValid(Modifier->NodeTemplate) && Modifier->NodeClass == nullptr)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("ModifierNodeUnset", "{0}: No node type selected. This Modifier entry has no effect."), FText::FromString(Path)));
		}
		if (!IsValid(Modifier->NodeTemplate)) continue;
		for (FName Name : Modifier->OverrideProperties)
			Require(UComposableCameraModifierBase::IsNodePropertyOverridable(Modifier->NodeTemplate->GetClass()->FindPropertyByName(Name)),
				Context, Path + TEXT(".") + Name.ToString(), LOCTEXT("ModifierProperty", "Selected override property no longer exists or cannot be overridden."));
	}
	return Finish(Parent, Context, InitialErrors);
}

#undef LOCTEXT_NAMESPACE
#endif
