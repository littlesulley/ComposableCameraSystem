// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraConsoleControls.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FScopedConsoleControls
	{
		TArray<FString> Names;
		~FScopedConsoleControls()
		{
			for (const FString& Name : Names) IConsoleManager::Get().UnregisterConsoleObject(*Name, false);
		}
		const TCHAR* Add(const TCHAR* Name) { Names.Add(Name); return Name; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraConsoleDiscoveryTest,
	"ComposableCameraSystem.Editor.Debug.ConsoleControls.Discovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraConsoleDiscoveryTest::RunTest(const FString& /*Parameters*/)
{
	FScopedConsoleControls Scope;
	IConsoleManager& Manager = IConsoleManager::Get();
	const TCHAR* BoolName = Scope.Add(TEXT("CCS.Debug.EditWindowTests.Discovery.Bool"));
	const TCHAR* IntName = Scope.Add(TEXT("CCS.Debug.EditWindowTests.Discovery.Int"));
	const TCHAR* FloatName = Scope.Add(TEXT("CCS.Debug.EditWindowTests.Discovery.Float"));
	const TCHAR* TextName = Scope.Add(TEXT("CCS.Debug.EditWindowTests.Discovery.Text"));
	Manager.RegisterConsoleVariable(BoolName, true, TEXT("Test toggle"));
	Manager.RegisterConsoleVariable(IntName, 5, TEXT("Test count, not a toggle"));
	Manager.RegisterConsoleVariable(FloatName, 0.25f, TEXT("Test scalar"));
	Manager.RegisterConsoleVariable(TextName, TEXT("value"), TEXT("Test text"));
	const TArray<FComposableCameraConsoleControl> Controls = FComposableCameraConsoleControls::Discover();
	auto CheckKind = [this, &Controls](const TCHAR* Name, EComposableCameraConsoleControlKind Kind)
	{
		const FComposableCameraConsoleControl* Found = Controls.FindByPredicate(
			[Name](const FComposableCameraConsoleControl& Control) { return Control.Name == Name; });
		TestTrue(FString::Printf(TEXT("Discovery and classification: %s"), Name), Found && Found->Kind == Kind);
	};
	CheckKind(BoolName, EComposableCameraConsoleControlKind::Toggle);
	CheckKind(IntName, EComposableCameraConsoleControlKind::Integer);
	CheckKind(FloatName, EComposableCameraConsoleControlKind::Float);
	CheckKind(TextName, EComposableCameraConsoleControlKind::Text);
	CheckKind(TEXT("CCS.Debug.Panel.Page"), EComposableCameraConsoleControlKind::Integer);
	CheckKind(TEXT("CCS.Debug.Panel.Width"), EComposableCameraConsoleControlKind::Float);
	CheckKind(TEXT("CCS.Debug.Viewport.Nodes.All"), EComposableCameraConsoleControlKind::Toggle);
	CheckKind(TEXT("CCS.Dump.Camera"), EComposableCameraConsoleControlKind::Command);
	int32 RegisteredCount = 0;
	Manager.ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda(
		[this, &Controls, &RegisteredCount](const TCHAR* Name, IConsoleObject* Object)
		{
			if (!Object || Object->TestFlags(ECVF_Unregistered) || Object->TestFlags(ECVF_CreatedFromIni)) return;
			++RegisteredCount;
			TestTrue(FString::Printf(TEXT("Every CCS control appears: %s"), Name),
				Controls.ContainsByPredicate([Name](const FComposableCameraConsoleControl& Control) { return Control.Name == Name; }));
		}), TEXT("CCS."));
	TestEqual(TEXT("No duplicates, including multi-color legend entries"), Controls.Num(), RegisteredCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraConsoleValuesTest,
	"ComposableCameraSystem.Editor.Debug.ConsoleControls.LiveValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraConsoleValuesTest::RunTest(const FString& /*Parameters*/)
{
	FScopedConsoleControls Scope;
	IConsoleManager& Manager = IConsoleManager::Get();
	const TCHAR* Name = Scope.Add(TEXT("CCS.Debug.EditWindowTests.LiveValues.Value"));
	const TCHAR* ToggleName = Scope.Add(TEXT("CCS.Debug.EditWindowTests.LiveValues.Toggle"));
	const TCHAR* ReadOnlyName = Scope.Add(TEXT("CCS.Debug.EditWindowTests.LiveValues.ReadOnly"));
	IConsoleVariable* Variable = Manager.RegisterConsoleVariable(Name, 7, TEXT("Test value"));
	Manager.RegisterConsoleVariable(ToggleName, false, TEXT("Test toggle"));
	Manager.RegisterConsoleVariable(ReadOnlyName, 3, TEXT("Test read-only"), ECVF_ReadOnly);
	TestTrue(TEXT("Toggle writes through registry"), FComposableCameraConsoleControls::SetValue(ToggleName, TEXT("1")));
	TestTrue(TEXT("Toggle reflects enabled state"), FComposableCameraConsoleControls::ReadToggle(ToggleName).Get(false));
	TestTrue(TEXT("Toggle default reset"), FComposableCameraConsoleControls::ResetValue(ToggleName));
	TestFalse(TEXT("Toggle reflects disabled default"), FComposableCameraConsoleControls::ReadToggle(ToggleName).Get(true));
	TestTrue(TEXT("UI writes through registry"), FComposableCameraConsoleControls::SetValue(Name, TEXT("15")));
	TestEqual(TEXT("Console sees UI change"), Variable->GetInt(), 15);
	Variable->Set(21, ECVF_SetByConsole);
	TestEqual(TEXT("UI reads external console change"), FComposableCameraConsoleControls::ReadValue(Name).Get(FString()), FString(TEXT("21")));
	TestTrue(TEXT("Restore registered default"), FComposableCameraConsoleControls::ResetValue(Name));
	TestEqual(TEXT("Default preserved after console writes"), Variable->GetInt(), 7);
	TestFalse(TEXT("Read-only control rejects write"), FComposableCameraConsoleControls::SetValue(ReadOnlyName, TEXT("0")));
	Manager.UnregisterConsoleObject(Name, false);
	Scope.Names.Remove(Name);
	TestFalse(TEXT("Removed control has no readable value"), FComposableCameraConsoleControls::ReadValue(Name).IsSet());
	TestFalse(TEXT("Removed control rejects write"), FComposableCameraConsoleControls::SetValue(Name, TEXT("1")));
	TestFalse(TEXT("Removed control rejects reset"), FComposableCameraConsoleControls::ResetValue(Name));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraConsoleDispatchTest,
	"ComposableCameraSystem.Editor.Debug.ConsoleControls.CommandDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraConsoleDispatchTest::RunTest(const FString& /*Parameters*/)
{
	FScopedConsoleControls Scope;
	const TCHAR* Name = Scope.Add(TEXT("CCS.Dump.EditWindowTests.Dispatch"));
	TArray<FString> ReceivedArguments;
	UWorld* ReceivedWorld = nullptr;
	int32 InvocationCount = 0;
	IConsoleManager::Get().RegisterConsoleCommand(Name, TEXT("Test command"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([&](const TArray<FString>& Arguments, UWorld* World)
		{
			ReceivedArguments = Arguments;
			ReceivedWorld = World;
			++InvocationCount;
		}));
	const TArray<FComposableCameraConsoleControl> Controls = FComposableCameraConsoleControls::Discover();
	const FComposableCameraConsoleControl* Control = Controls.FindByPredicate(
		[Name](const FComposableCameraConsoleControl& Item) { return Item.Name == Name; });
	if (!TestNotNull(TEXT("Command discovered"), Control)) return false;
	UWorld* World = NewObject<UWorld>(GetTransientPackage());
	World->WorldType = EWorldType::PIE;
	FOutputDeviceNull Output;
	TestFalse(TEXT("Runtime command requires a game world"), FComposableCameraConsoleControls::Execute(*Control, TEXT(""), nullptr, Output));
	TestTrue(TEXT("Dispatch to selected world"), FComposableCameraConsoleControls::Execute(*Control, TEXT("\"Camera Tag\" second"), World, Output));
	TestEqual(TEXT("Exactly one invocation"), InvocationCount, 1);
	TestTrue(TEXT("World forwarded exactly"), ReceivedWorld == World);
	TestEqual(TEXT("Quoted arguments stay intact"), ReceivedArguments.Num(), 2);
	if (ReceivedArguments.Num() == 2) TestEqual(TEXT("Quoted tag"), ReceivedArguments[0], FString(TEXT("Camera Tag")));
	World->bIsTearingDown = true;
	TestFalse(TEXT("Ended PIE world rejects dispatch before GC"), FComposableCameraConsoleControls::CanExecute(*Control, World));
	World->bIsTearingDown = false;
	World->WorldType = EWorldType::Editor;
	TestFalse(TEXT("Editor world cannot run game-world dump"), FComposableCameraConsoleControls::CanExecute(*Control, World));
	IConsoleManager::Get().UnregisterConsoleObject(Name, false);
	Scope.Names.Remove(Name);
	TestFalse(TEXT("Unloaded command safely rejects dispatch"), FComposableCameraConsoleControls::Execute(*Control, TEXT(""), World, Output));
	TestEqual(TEXT("Rejected dispatch did not invoke callback"), InvocationCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerPreviewDumpTest,
	"ComposableCameraSystem.Editor.Debug.ConsoleControls.MeshLayerPreviewDump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPreviewDumpTest::RunTest(const FString& /*Parameters*/)
{
	const TArray<FComposableCameraConsoleControl> Controls = FComposableCameraConsoleControls::Discover();
	const FComposableCameraConsoleControl* Control = Controls.FindByPredicate(
		[](const FComposableCameraConsoleControl& Item)
		{
			return Item.Name == TEXT("CCS.Editor.MeshLayers.DumpPIEPreview");
		});
	if (!TestNotNull(TEXT("PIE preview diagnostic command is registered"), Control)) { return false; }
	TestTrue(TEXT("Preview diagnostics belong to editor actions"),
		Control->Kind == EComposableCameraConsoleControlKind::Command
		&& Control->Group == EComposableCameraConsoleControlGroup::EditorCommands);
	TestFalse(TEXT("Preview diagnostics inspect editor caches without a selected game world"), Control->bRequiresGameWorld);
	FOutputDeviceNull Output;
	TestTrue(TEXT("Preview diagnostic callback dispatches without a selected world"),
		FComposableCameraConsoleControls::Execute(*Control, TEXT(""), nullptr, Output));
	return true;
}

#endif
