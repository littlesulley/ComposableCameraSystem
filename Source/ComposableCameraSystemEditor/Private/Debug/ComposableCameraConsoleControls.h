// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FOutputDevice;
class UWorld;

enum class EComposableCameraConsoleControlKind : uint8
{
	Toggle,
	Integer,
	Float,
	Text,
	Command
};

enum class EComposableCameraConsoleControlGroup : uint8
{
	Viewport,
	Panel,
	Nodes,
	Transitions,
	Trace,
	RuntimeCommands,
	EditorCommands,
	Other,
	Count
};

/** Owned metadata only. Never retain registry pointers across module unloads. */
struct FComposableCameraConsoleControl
{
	FString Name;
	FString Label;
	FString Help;
	EComposableCameraConsoleControlKind Kind = EComposableCameraConsoleControlKind::Text;
	EComposableCameraConsoleControlGroup Group = EComposableCameraConsoleControlGroup::Other;
	FLinearColor Color = FLinearColor::White;
	bool bRequiresGameWorld = false;
};

/** Editor-only adapter over the existing console registry; no duplicate CVar state. */
class FComposableCameraConsoleControls
{
public:
	static TArray<FComposableCameraConsoleControl> Discover();
	static FText GetGroupLabel(EComposableCameraConsoleControlGroup Group);
	static bool CanSetValue(const FString& Name);
	static TOptional<FString> ReadValue(const FString& Name);
	static TOptional<bool> ReadToggle(const FString& Name);
	static bool SetValue(const FString& Name, const FString& Value);
	static bool ResetValue(const FString& Name);
	static bool CanExecute(const FComposableCameraConsoleControl& Control, const UWorld* World);
	/** Returns dispatch success, not the outcome of the command's void delegate. */
	static bool Execute(const FComposableCameraConsoleControl& Control, const FString& Arguments,
		UWorld* World, FOutputDevice& Output);
};
