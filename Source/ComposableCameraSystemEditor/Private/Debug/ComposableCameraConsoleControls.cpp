// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraConsoleControls.h"

#include "Debug/ComposableCameraViewportDebug.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Parse.h"

#define LOCTEXT_NAMESPACE "ComposableCameraConsoleControls"

namespace
{
	IConsoleVariable* FindLiveVariable(const FString& Name)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name, false);
		return Variable && !Variable->TestFlags(ECVF_Unregistered) ? Variable : nullptr;
	}

	bool IsKnownToggle(const FString& Name)
	{
		// Legacy switches are int32 CVars. New bool CVars need no metadata entry.
		static const TCHAR* Names[] = {
			TEXT("CCS.Debug.Panel"), TEXT("CCS.Debug.Panel.Legend"),
			TEXT("CCS.Debug.Panel.Patches"), TEXT("CCS.Debug.Panel.PoseHistory"),
			TEXT("CCS.Debug.Panel.PoseHistory.Freeze"), TEXT("CCS.Debug.Panel.Warnings"),
			TEXT("CCS.Debug.Trace"), TEXT("CCS.Debug.Viewport"),
			TEXT("CCS.Debug.Viewport.AlwaysShow"), TEXT("CCS.Debug.Viewport.Nodes.All"),
			TEXT("CCS.Debug.Viewport.Transitions.All"), TEXT("CCS.Debug.Viewport.ShotZones")
		};
		for (const TCHAR* KnownName : Names)
		{
			if (Name == KnownName)
			{
				return true;
			}
		}
		return false;
	}

	EComposableCameraConsoleControlGroup ClassifyGroup(const FString& Name, bool bCommand)
	{
		if (bCommand)
		{
			return Name.StartsWith(TEXT("CCS.Editor."))
				? EComposableCameraConsoleControlGroup::EditorCommands
				: EComposableCameraConsoleControlGroup::RuntimeCommands;
		}
		if (Name.StartsWith(TEXT("CCS.Debug.Panel"))) return EComposableCameraConsoleControlGroup::Panel;
		if (Name == TEXT("CCS.Debug.Viewport.Transitions.All")) return EComposableCameraConsoleControlGroup::Viewport;
		if (Name.StartsWith(TEXT("CCS.Debug.Viewport.Transitions."))) return EComposableCameraConsoleControlGroup::Transitions;
		if (Name.StartsWith(TEXT("CCS.Debug.Viewport"))) return EComposableCameraConsoleControlGroup::Viewport;
		if (Name.StartsWith(TEXT("CCS.Debug.Trace"))) return EComposableCameraConsoleControlGroup::Trace;
		return EComposableCameraConsoleControlGroup::Other;
	}
}

TArray<FComposableCameraConsoleControl> FComposableCameraConsoleControls::Discover()
{
	TArray<FComposableCameraConsoleControl> Result;
	IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
		FConsoleObjectVisitor::CreateLambda([&Result](const TCHAR* Name, IConsoleObject* Object)
		{
			if (!Object || Object->TestFlags(ECVF_Unregistered) || Object->TestFlags(ECVF_CreatedFromIni)) return;
			FComposableCameraConsoleControl Control;
			Control.Name = Name;
			Control.Help = Object->GetHelp();
			int32 LastDot = INDEX_NONE;
			Control.Name.FindLastChar(TEXT('.'), LastDot);
			Control.Label = FName::NameToDisplayString(Control.Name.Mid(LastDot + 1), false);
			const bool bCommand = Object->AsCommand() != nullptr;
			Control.Group = ClassifyGroup(Control.Name, bCommand);
			Control.bRequiresGameWorld = bCommand && Control.Name.StartsWith(TEXT("CCS.Dump."));
			if (bCommand) Control.Kind = EComposableCameraConsoleControlKind::Command;
			else if (Object->IsVariableBool() || IsKnownToggle(Control.Name)) Control.Kind = EComposableCameraConsoleControlKind::Toggle;
			else if (Object->IsVariableInt()) Control.Kind = EComposableCameraConsoleControlKind::Integer;
			else if (Object->IsVariableFloat()) Control.Kind = EComposableCameraConsoleControlKind::Float;
			else Control.Kind = EComposableCameraConsoleControlKind::Text;

			for (const FComposableCameraViewportDebugLegendEntry& Legend : FComposableCameraViewportDebug::GetLegendEntries())
			{
				if (Legend.CVarName && Control.Name == Legend.CVarName)
				{
					Control.Kind = EComposableCameraConsoleControlKind::Toggle;
					Control.Color = FComposableCameraViewportDebugColors::ToLinearColor(Legend.Color);
					Control.Group = Legend.bIsTransition
						? EComposableCameraConsoleControlGroup::Transitions : EComposableCameraConsoleControlGroup::Nodes;
					break; // CompositionFraming has several legend colors but one switch.
				}
			}
			if (Control.Name == TEXT("CCS.Debug.Viewport")) Control.Label = TEXT("3D viewport debug");
			else if (Control.Name == TEXT("CCS.Debug.Panel")) Control.Label = TEXT("Camera HUD");
			else if (Control.Name == TEXT("CCS.Debug.Viewport.Nodes.All")) Control.Label = TEXT("All node gizmos");
			else if (Control.Name == TEXT("CCS.Debug.Viewport.Transitions.All")) Control.Label = TEXT("All transition gizmos");
			else if (Control.Name == TEXT("CCS.Debug.Panel.PoseHistory.Width")) Control.Label = TEXT("Pose history width");
			else if (Control.Name == TEXT("CCS.Debug.Panel.PoseHistory.Freeze")) Control.Label = TEXT("Freeze pose history");
			Result.Add(MoveTemp(Control));
		}), TEXT("CCS."));
	Result.Sort([](const FComposableCameraConsoleControl& A, const FComposableCameraConsoleControl& B)
	{
		return A.Group == B.Group ? A.Name < B.Name : A.Group < B.Group;
	});
	return Result;
}

FText FComposableCameraConsoleControls::GetGroupLabel(EComposableCameraConsoleControlGroup Group)
{
	switch (Group)
	{
	case EComposableCameraConsoleControlGroup::Viewport: return LOCTEXT("Viewport", "Viewport visualization");
	case EComposableCameraConsoleControlGroup::Panel: return LOCTEXT("Panel", "Camera HUD & pose history");
	case EComposableCameraConsoleControlGroup::Nodes: return LOCTEXT("Nodes", "Node gizmos");
	case EComposableCameraConsoleControlGroup::Transitions: return LOCTEXT("Transitions", "Transition gizmos");
	case EComposableCameraConsoleControlGroup::Trace: return LOCTEXT("Trace", "Trace recording");
	case EComposableCameraConsoleControlGroup::RuntimeCommands: return LOCTEXT("RuntimeCommands", "Runtime inspection");
	case EComposableCameraConsoleControlGroup::EditorCommands: return LOCTEXT("EditorCommands", "Editor actions");
	case EComposableCameraConsoleControlGroup::Other: return LOCTEXT("Other", "Other controls");
	case EComposableCameraConsoleControlGroup::Count: break;
	}
	return FText::GetEmpty();
}

bool FComposableCameraConsoleControls::CanSetValue(const FString& Name)
{
	const IConsoleVariable* Variable = FindLiveVariable(Name);
	return Variable && Variable->IsEnabled() && !Variable->TestFlags(ECVF_ReadOnly);
}

TOptional<FString> FComposableCameraConsoleControls::ReadValue(const FString& Name)
{
	const IConsoleVariable* Variable = FindLiveVariable(Name);
	return Variable ? TOptional<FString>(Variable->GetString()) : TOptional<FString>();
}

TOptional<bool> FComposableCameraConsoleControls::ReadToggle(const FString& Name)
{
	const IConsoleVariable* Variable = FindLiveVariable(Name);
	return Variable ? TOptional<bool>(Variable->GetBool()) : TOptional<bool>();
}

bool FComposableCameraConsoleControls::SetValue(const FString& Name, const FString& Value)
{
	if (!CanSetValue(Name)) return false;
	FindLiveVariable(Name)->Set(*Value, ECVF_SetByConsole);
	return true;
}

bool FComposableCameraConsoleControls::ResetValue(const FString& Name)
{
	if (!CanSetValue(Name)) return false;
	IConsoleVariable* Variable = FindLiveVariable(Name);
	Variable->Set(*Variable->GetDefaultValue(), ECVF_SetByConsole);
	return true;
}

bool FComposableCameraConsoleControls::CanExecute(const FComposableCameraConsoleControl& Control, const UWorld* World)
{
	IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(*Control.Name);
	return Object && !Object->TestFlags(ECVF_Unregistered) && Object->IsEnabled()
		&& Control.Kind == EComposableCameraConsoleControlKind::Command
		&& Object->AsCommand()
		&& (!Control.bRequiresGameWorld || (IsValid(World) && World->IsGameWorld() && !World->bIsTearingDown));
}

bool FComposableCameraConsoleControls::Execute(const FComposableCameraConsoleControl& Control,
	const FString& Arguments, UWorld* World, FOutputDevice& Output)
{
	if (!CanExecute(Control, World)) return false;
	IConsoleCommand* Command = IConsoleManager::Get().FindConsoleObject(*Control.Name)->AsCommand();
	if (!Command) return false;
	TArray<FString> Tokens;
	const TCHAR* Cursor = *Arguments;
	FString Token;
	while (FParse::Token(Cursor, Token, true)) Tokens.Add(Token);
	return Command->Execute(Tokens, World, Output);
}

#undef LOCTEXT_NAMESPACE
