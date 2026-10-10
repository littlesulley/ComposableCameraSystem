// Copyright 2026 Sulley. All Rights Reserved.
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/ComposableCameraShotEditorStyle.h"
#include "Customizations/ComposableCameraShotRetainedRowCustomization.h"

#include "DataAssets/ComposableCameraShot.h"
#include "Editor.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShotEditorParameterPanel"

namespace
{
TSharedPtr<IDetailTreeNode> FindShotParameterNode(const TArray<TSharedRef<IDetailTreeNode>>& Nodes, FName Name)
{
	for (const TSharedRef<IDetailTreeNode>& Node : Nodes)
	{
		if (const auto Handle = Node->CreatePropertyHandle(); Handle && Handle->GetProperty() && Handle->GetProperty()->GetFName() == Name)
			return Node;
		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children, true);
		if (auto Result = FindShotParameterNode(Children, Name)) return Result;
	}
	return nullptr;
}

bool IsInlineShotParameter(const TSharedPtr<IPropertyHandle>& Handle)
{
	const auto* Property = Handle ? CastField<FStructProperty>(Handle->GetProperty()) : nullptr;
	return Property && (Property->Struct == TBaseStructure<FVector>::Get()
		|| Property->Struct == TBaseStructure<FVector2D>::Get());
}

FName ShotParameterName(const TSharedRef<IDetailTreeNode>& Node)
{
	const auto Handle = Node->CreatePropertyHandle();
	return Handle && Handle->GetProperty() ? Handle->GetProperty()->GetFName() : Node->GetNodeName();
}
}

void SShotEditorParameterPanel::Construct(const FArguments& Args)
{
	Session = Args._Session;
	Section = Args._Section;
	SubjectIndex = Args._SubjectIndex;
	FPropertyRowGeneratorArgs GeneratorArgs;
	GeneratorArgs.NotifyHook = Args._NotifyHook;
	Generator = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(GeneratorArgs);
	FShotEditorRetainedRowCustomization::Register(*Generator);
	Generator->OnRowsRefreshed().AddSP(this, &SShotEditorParameterPanel::RequestRebuild);
	ChildSlot[SAssignNew(Body, SVerticalBox).IsEnabled_Lambda([this]() { return Session->CanEdit(); })];
	RefreshSource();
	Rebuild();
}

void SShotEditorParameterPanel::RefreshSource(bool bImmediate)
{
	// Keep the entire Shot as the root so target-index pickers can resolve Targets.
	// Regenerate handles after source/array edits while retaining expansion state.
	if (auto* Shot = Session->GetShot())
		Generator->SetStructure(MakeShared<FStructOnScope>(FComposableCameraShot::StaticStruct(), reinterpret_cast<uint8*>(Shot)));
	else Generator->SetStructure(TSharedPtr<FStructOnScope>());
	RequestRebuild();
	if (bImmediate && !Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive())) Rebuild();
}

SShotEditorParameterPanel::~SShotEditorParameterPanel()
{
	Generator->OnRowsRefreshed().RemoveAll(this);
}

void SShotEditorParameterPanel::RequestRebuild() { bRebuildPending = true; }

void SShotEditorParameterPanel::Tick(const FGeometry& Geometry, double Time, float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, Time, DeltaTime);
	// PropertyEditor owns its transaction. Keep native numeric controls alive until commit.
	if (Session->IsEditing() || (GEditor && GEditor->IsTransactionActive())) return;
	// Modes and booleans update retained Slate visibility/native enabled attributes.
	// Only genuine property-tree/source/collection changes replace controls.
	if (bRebuildPending) Rebuild();
}

TSharedPtr<IDetailTreeNode> SShotEditorParameterPanel::FindRoot(FName Name) const
{
	return FindShotParameterNode(Generator->GetRootTreeNodes(), Name);
}

TSharedPtr<IDetailTreeNode> SShotEditorParameterPanel::FindSubject() const
{
	const auto* Shot = Session->GetShot();
	if (!Shot || !Shot->Targets.IsValidIndex(SubjectIndex)) return nullptr;
	const auto Targets = FindRoot(GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Targets));
	const auto Handle = Targets ? Targets->CreatePropertyHandle() : nullptr;
	const auto Array = Handle ? Handle->AsArray() : nullptr;
	return Array ? Generator->FindTreeNode(Array->GetElement(SubjectIndex)) : nullptr;
}

void SShotEditorParameterPanel::AddLayerRows(FName RootName, TSharedRef<SVerticalBox> Rows)
{
	const auto Root = FindRoot(RootName);
	if (!Root) return;
	TArray<TSharedRef<IDetailTreeNode>> Children;
	Root->GetChildren(Children, true);
	for (const auto& Child : Children)
	{
		const FString Path = RootName.ToString() + TEXT(".") + ShotParameterName(Child).ToString();
		if (ShotParameterName(Child) == GET_MEMBER_NAME_CHECKED(FShotFocus, FocusAnchor)) ExpandedNodes.FindOrAdd(Path, true);
		Rows->AddSlot().AutoHeight()[NodeWidget(Child, Path)];
	}
}

TSharedRef<SWidget> SShotEditorParameterPanel::SubjectGroup(const FText& Label, const FString& Path, TSharedRef<SWidget> Content, bool bExpanded)
{
	ExpandedNodes.FindOrAdd(Path, bExpanded);
	return SNew(SExpandableArea).Tag(FName(*Path)).InitiallyCollapsed(!ExpandedNodes.FindRef(Path)).Padding(0.f)
		.BodyBorderImage(FAppStyle::GetBrush("NoBorder")).BodyBorderBackgroundColor(FLinearColor::Transparent)
		.OnAreaExpansionChanged_Lambda([this, Path](bool bOpen) { ExpandedNodes.Add(Path, bOpen); })
		.HeaderContent()[ComposableCameraSystem::ShotEditorStyle::SubsectionHeader(Label)]
		.BodyContent()[Content];
}

void SShotEditorParameterPanel::BuildSubject()
{
	const auto Subject = FindSubject();
	if (!Subject) return;
	const FString Prefix = FString::Printf(TEXT("Targets[%d]"), SubjectIndex);
	const TSharedRef<SVerticalBox> Pivot = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> Bounds = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> Preview = SNew(SVerticalBox);
	TArray<TSharedRef<IDetailTreeNode>> Children;
	Subject->GetChildren(Children, true);
	for (const auto& Child : Children)
	{
		const FName Name = ShotParameterName(Child);
		if (Name == GET_MEMBER_NAME_CHECKED(FComposableCameraShotTarget, Target))
		{
			TArray<TSharedRef<IDetailTreeNode>> TargetChildren;
			Child->GetChildren(TargetChildren, true);
			for (const auto& TargetChild : TargetChildren)
			{
				const auto Handle = TargetChild->CreatePropertyHandle();
				const FProperty* Property = Handle ? Handle->GetProperty() : nullptr;
				if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)) continue;
				const FName TargetName = Property->GetFName();
				// These identities use the session's binding-aware Actor/component/bone controls.
				if (TargetName == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, Actor)
					|| TargetName == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, ComponentName)
					|| TargetName == GET_MEMBER_NAME_CHECKED(FComposableCameraTargetInfo, BoneName)) continue;
				const bool bPreview = Property->GetMetaData(TEXT("Category")).StartsWith(TEXT("Target|Editor Preview"));
				(bPreview ? Preview : Pivot)->AddSlot().AutoHeight()
					[NodeWidget(TargetChild, Prefix + TEXT(".Target.") + TargetName.ToString())];
			}
		}
		else if (const auto Handle = Child->CreatePropertyHandle(); Handle && Handle->GetProperty() && Handle->GetProperty()->HasAnyPropertyFlags(CPF_Edit))
			Bounds->AddSlot().AutoHeight()[NodeWidget(Child, Prefix + TEXT(".") + Name.ToString())];
	}
	Body->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(0.f, 0.f, 0.f, 6.f)
		[SubjectGroup(LOCTEXT("SubjectPivot", "Pivot / orientation"), Prefix + TEXT(".PivotGroup"), Pivot, true)];
	Body->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(0.f, 0.f, 0.f, 6.f)
		[SubjectGroup(LOCTEXT("SubjectBounds", "Framing bounds / weight"), Prefix + TEXT(".BoundsGroup"), Bounds, true)];
	Body->AddSlot().AutoHeight().HAlign(HAlign_Fill)
		[SubjectGroup(LOCTEXT("SubjectPreview", "Preview model"), Prefix + TEXT(".PreviewGroup"), Preview, false)];
}

void SShotEditorParameterPanel::BuildMotion()
{
	const auto* Shot = Session->GetShot();
	if (!Shot) return;
	const TSharedRef<SVerticalBox> Response = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> Zones = SNew(SVerticalBox);
	const auto AddSpeeds = [this](const TSharedPtr<IDetailTreeNode>& Root, const FString& Prefix, TSharedRef<SVerticalBox> Rows, bool bIncludeEnabled = false)
	{
		if (!Root) return;
		TArray<TSharedRef<IDetailTreeNode>> Children;
		Root->GetChildren(Children, true);
		for (const auto& Child : Children)
		{
			const FName Name = ShotParameterName(Child);
			if (Name.ToString().EndsWith(TEXT("Speed")) || (bIncludeEnabled && Name == GET_MEMBER_NAME_CHECKED(FShotScreenZones, bEnabled)))
				Rows->AddSlot().AutoHeight()[NodeWidget(Child, Prefix + TEXT(".") + Name.ToString())];
		}
	};
	AddSpeeds(FindRoot(GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Placement)), TEXT("Placement"), Response);
	AddSpeeds(FindRoot(GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Lens)), TEXT("Lens"), Response);
	if (const auto Roll = FindRoot(GET_MEMBER_NAME_CHECKED(FComposableCameraShot, RollSpeed)))
		Response->AddSlot().AutoHeight()[NodeWidget(Roll.ToSharedRef(), TEXT("RollSpeed"))];
	const TSharedRef<SVerticalBox> FollowResponse = SNew(SVerticalBox).Visibility_Lambda([this]()
	{
		const auto* Current = Session->GetShot();
		return Current && Current->Placement.Mode == EShotPlacementMode::AnchorAtScreen ? EVisibility::Visible : EVisibility::Collapsed;
	});
	FollowResponse->AddSlot().AutoHeight().Padding(6.f, 4.f)[SNew(STextBlock).Text(LOCTEXT("FollowResponse", "Follow screen response")).Font(FAppStyle::GetFontStyle("BoldFont"))];
	AddSpeeds(FindRoot(GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementZones)), TEXT("Placement.PlacementZones"), FollowResponse, true);
	Zones->AddSlot().AutoHeight()[FollowResponse];
	AddSpeeds(FindRoot(GET_MEMBER_NAME_CHECKED(FShotAim, AimZones)), TEXT("Aim.AimZones"), Zones, true);
	Body->AddSlot().AutoHeight()
	[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(24.f, 16.f))
		+ SWrapBox::Slot()[Group(LOCTEXT("MotionResponse", "Distance / lens / roll response"), Response)]
		+ SWrapBox::Slot()[Group(LOCTEXT("MotionZones", "Screen response"), Zones)]];
}

TSharedRef<SWidget> SShotEditorParameterPanel::Group(const FText& Label, TSharedRef<SWidget> Content)
{
	return SNew(SBox).WidthOverride_Lambda([this]()
		{
			return ComposableCameraSystem::ShotEditorStyle::ColumnWidth(static_cast<float>(GetCachedGeometry().GetLocalSize().X) - 16.f);
		})[ComposableCameraSystem::ShotEditorStyle::Group(Label, Content)];
}

TSharedRef<SWidget> SShotEditorParameterPanel::NodeWidget(TSharedRef<IDetailTreeNode> Node, const FString& Path)
{
	FNodeWidgets Widgets = Node->CreateNodeWidgets();
	if (Path == TEXT("Placement.PlacementAnchor") || Path == TEXT("Aim.AimAnchor"))
		Widgets.NameWidget = SNew(STextBlock).Text(Path.StartsWith(TEXT("Placement")) ? LOCTEXT("FollowAnchor", "Follow anchor") : LOCTEXT("AimAnchor", "Aim anchor"));
	const auto Handle = Node->CreatePropertyHandle();
	const auto Visibility = FShotEditorRetainedRowCustomization::MakeVisibility(Handle);
#if WITH_DEV_AUTOMATION_TESTS
	if (Handle && Handle->GetProperty())
	{
		RenderedPropertyVisibility.Add(Path, Visibility);
		RenderedPropertyHandles.Add(Path, Handle);
	}
#endif
	const TSharedRef<SHorizontalBox> Columns = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[Widgets.EditConditionWidget.IsValid() ? Widgets.EditConditionWidget.ToSharedRef() : SNullWidget::NullWidget]
			+ SHorizontalBox::Slot().FillWidth(.40f).VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
			[Widgets.NameWidget.IsValid() ? Widgets.NameWidget.ToSharedRef() : SNullWidget::NullWidget]
			+ SHorizontalBox::Slot().FillWidth(.60f).VAlign(VAlign_Center)
			[Widgets.ValueWidget.IsValid() ? Widgets.ValueWidget.ToSharedRef() : SNullWidget::NullWidget];
	TSharedRef<SWidget> Row = Widgets.WholeRowWidget.IsValid() ? Widgets.WholeRowWidget.ToSharedRef() : StaticCastSharedRef<SWidget>(Columns);

	TArray<TSharedRef<IDetailTreeNode>> Children;
	if (!IsInlineShotParameter(Handle)) Node->GetChildren(Children, true);
	if (!Children.IsEmpty())
	{
		const TSharedRef<SVerticalBox> Nested = SNew(SVerticalBox);
		for (const auto& Child : Children)
			Nested->AddSlot().AutoHeight()[NodeWidget(Child, Path + TEXT(".") + ShotParameterName(Child).ToString())];
		return SNew(SExpandableArea).Tag(FName(*Path)).Visibility(Visibility).InitiallyCollapsed(!ExpandedNodes.FindRef(Path)).Padding(0.f)
			.OnAreaExpansionChanged_Lambda([this, Path](bool bExpanded) { ExpandedNodes.Add(Path, bExpanded); })
			.HeaderContent()[SNew(SBox).MinDesiredHeight(24.f).Padding(0.f, 1.f)[Row]]
			.BodyContent()[SNew(SBox).Padding(12.f, 0.f, 0.f, 4.f)[Nested]];
	}
	return SNew(SBox).Tag(FName(*Path)).Visibility(Visibility).MinDesiredHeight(24.f).Padding(0.f, 1.f)[Row];
}

void SShotEditorParameterPanel::Rebuild()
{
#if WITH_DEV_AUTOMATION_TESTS
	++RebuildCount;
	RenderedPropertyVisibility.Reset();
	RenderedPropertyHandles.Reset();
#endif
	bRebuildPending = false;
	Body->ClearChildren();
	const auto* Shot = Session->GetShot();
	if (!Shot)
	{
		Body->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("NoShot", "Create a shot from selected actors, or open an existing Shot.")).AutoWrapText(true)];
		return;
	}
	if (Section == EShotEditorParameterSection::Subject) { BuildSubject(); return; }
	if (Section == EShotEditorParameterSection::Motion) { BuildMotion(); return; }
	if (Section == EShotEditorParameterSection::LensFocus)
	{
		const TSharedRef<SVerticalBox> Lens = SNew(SVerticalBox);
		const TSharedRef<SVerticalBox> Focus = SNew(SVerticalBox);
		AddLayerRows(GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Lens), Lens);
		AddLayerRows(GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Focus), Focus);
		Body->AddSlot().AutoHeight()
		[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(24.f, 16.f))
			+ SWrapBox::Slot()[Group(LOCTEXT("Lens", "Lens"), Lens)]
			+ SWrapBox::Slot()[Group(LOCTEXT("Focus", "Focus"), Focus)]];
		return;
	}
	FName RootName;
	FText BehaviorLabel;
	switch (Section)
	{
	case EShotEditorParameterSection::Follow:
		RootName = GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Placement);
		BehaviorLabel = LOCTEXT("FollowBehavior", "Follow behavior");
		break;
	case EShotEditorParameterSection::LookAt:
		RootName = GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Aim);
		BehaviorLabel = LOCTEXT("AimBehavior", "Aim behavior");
		break;
	case EShotEditorParameterSection::LensFocus:
	case EShotEditorParameterSection::Motion:
	case EShotEditorParameterSection::Subject: return; // Already rendered above.
	}
	const auto Root = FindRoot(RootName);
	if (!Root) return;
	const TSharedRef<SVerticalBox> Behavior = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> Target = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> ScreenZones = SNew(SVerticalBox);
	TArray<TSharedRef<IDetailTreeNode>> Children;
	Root->GetChildren(Children, true);
	for (const auto& Child : Children)
	{
		const auto Handle = Child->CreatePropertyHandle();
		const FName Name = Handle && Handle->GetProperty() ? Handle->GetProperty()->GetFName() : Child->GetNodeName();
		const bool bAnchor = Name == GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementAnchor) || Name == GET_MEMBER_NAME_CHECKED(FShotAim, AimAnchor);
		const bool bZones = Name == GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementZones) || Name == GET_MEMBER_NAME_CHECKED(FShotAim, AimZones);
		const FString Path = RootName.ToString() + TEXT(".") + Name.ToString();
		if (bAnchor || bZones)
		{
			ExpandedNodes.FindOrAdd(Path, true);
			(bZones ? ScreenZones : Target)->AddSlot().AutoHeight()[NodeWidget(Child, Path)];
		}
		else Behavior->AddSlot().AutoHeight()[NodeWidget(Child, Path)];
	}
	if (Section == EShotEditorParameterSection::LookAt)
		for (FName Name : { GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Roll), GET_MEMBER_NAME_CHECKED(FComposableCameraShot, RollSpeed) })
			if (auto Node = FindRoot(Name)) Behavior->AddSlot().AutoHeight()[NodeWidget(Node.ToSharedRef(), Name.ToString())];
	const TSharedRef<SWrapBox> Groups = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(24.f, 16.f));
	if (Target->GetChildren()->Num() > 0)
		Groups->AddSlot()[Group(LOCTEXT("Anchor", "Anchor"), Target)];
	Groups->AddSlot()[Group(BehaviorLabel, Behavior)];
	if (ScreenZones->GetChildren()->Num() > 0)
	{
		const auto ZonesRoot = FindRoot(Section == EShotEditorParameterSection::Follow
			? GET_MEMBER_NAME_CHECKED(FShotPlacement, PlacementZones) : GET_MEMBER_NAME_CHECKED(FShotAim, AimZones));
		Groups->AddSlot()[SNew(SBox).Visibility(FShotEditorRetainedRowCustomization::MakeVisibility(ZonesRoot ? ZonesRoot->CreatePropertyHandle() : nullptr))
			[Group(LOCTEXT("ScreenZones", "Screen zones"), ScreenZones)]];
	}
	Body->AddSlot().AutoHeight()[Groups];
}

#if WITH_DEV_AUTOMATION_TESTS
void SShotEditorParameterPanel::RefreshForTesting() { RefreshSource(); Rebuild(); }
void SShotEditorParameterPanel::GetRenderedPropertyPathsForTesting(TSet<FString>& Paths) const
{
	Paths.Reset();
	for (const auto& Entry : RenderedPropertyVisibility)
		if (Entry.Value.Get() == EVisibility::Visible) Paths.Add(Entry.Key);
}
bool SShotEditorParameterPanel::SetValueForTesting(const FString& Path, bool Value)
{
	const auto* Handle = RenderedPropertyHandles.Find(Path);
	return Handle && (*Handle)->SetValue(Value) == FPropertyAccess::Success;
}
bool SShotEditorParameterPanel::SetValueForTesting(const FString& Path, uint8 Value)
{
	const auto* Handle = RenderedPropertyHandles.Find(Path);
	return Handle && (*Handle)->SetValue(Value) == FPropertyAccess::Success;
}
bool SShotEditorParameterPanel::SetFloatForTesting(FName Name, float Value)
{
	const auto Node = FindRoot(Name);
	const auto Handle = Node ? Node->CreatePropertyHandle() : nullptr;
	return Handle && Handle->SetValue(Value) == FPropertyAccess::Success;
}
bool SShotEditorParameterPanel::SetSubjectFloatForTesting(FName Name, float Value)
{
	const auto Subject = FindSubject();
	if (!Subject) return false;
	TArray<TSharedRef<IDetailTreeNode>> Children;
	Subject->GetChildren(Children, true);
	for (const auto& Child : Children)
		if (const auto Handle = Child->CreatePropertyHandle(); Handle && Handle->GetProperty() && Handle->GetProperty()->GetFName() == Name)
			return Handle->SetValue(Value) == FPropertyAccess::Success;
	return false;
}
void SShotEditorParameterPanel::GetVisiblePropertyNamesForTesting(TSet<FName>& Names) const
{
	FName RootName;
	switch (Section)
	{
	case EShotEditorParameterSection::Follow: RootName = GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Placement); break;
	case EShotEditorParameterSection::LookAt: RootName = GET_MEMBER_NAME_CHECKED(FComposableCameraShot, Aim); break;
	case EShotEditorParameterSection::LensFocus:
	case EShotEditorParameterSection::Motion:
	case EShotEditorParameterSection::Subject: return; // Use rendered paths for these multi-root sections.
	}
	const auto Root = FindRoot(RootName);
	if (!Root) return;
	TArray<TSharedRef<IDetailTreeNode>> Children;
	Root->GetChildren(Children, true);
	for (const auto& Child : Children)
	{
		if (auto Handle = Child->CreatePropertyHandle(); Handle && Handle->GetProperty())
		{
			if (FShotEditorRetainedRowCustomization::MakeVisibility(Handle).Get() == EVisibility::Visible) Names.Add(Handle->GetProperty()->GetFName());
		}
		else Names.Add(Child->GetNodeName());
	}
}
#endif

#undef LOCTEXT_NAMESPACE
