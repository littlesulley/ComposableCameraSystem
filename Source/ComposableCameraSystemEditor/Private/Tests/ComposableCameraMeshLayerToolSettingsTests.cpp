// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ComposableCameraEditorStyle.h"
#include "EditorModeRegistry.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerToolSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerSelectionTest,
	"ComposableCameraSystem.Editor.MeshCamera.LayerSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerSelectionTest::RunTest(const FString& /*Parameters*/)
{
	UComposableCameraMeshLayerToolSettings* Settings =
		NewObject<UComposableCameraMeshLayerToolSettings>(GetTransientPackage());
	int32 ChangeNotificationCount = 0;
	Settings->OnLayerDataChanged.BindLambda(
		[&ChangeNotificationCount]()
		{
			++ChangeNotificationCount;
		});

	Settings->AddLayer();
	const FGuid FirstLayerId = Settings->GetActiveLayerId();
	TestTrue(TEXT("First Layer receives a stable GUID"), FirstLayerId.IsValid());
	TestEqual(TEXT("First added Layer becomes active"), Settings->ActiveLayerIndex, 0);

	Settings->AddLayer();
	const FGuid SecondLayerId = Settings->GetActiveLayerId();
	TestTrue(TEXT("Second Layer receives a different GUID"),
		SecondLayerId.IsValid() && SecondLayerId != FirstLayerId);
	TestTrue(TEXT("Added Layers receive distinct generated names"),
		Settings->Layers[0].Name != Settings->Layers[1].Name);
	TestEqual(TEXT("Second added Layer becomes active"), Settings->ActiveLayerIndex, 1);

	TestTrue(TEXT("Selecting a Layer row resolves by stable GUID"),
		Settings->SelectLayer(FirstLayerId));
	TestEqual(TEXT("Row selection updates internal paint index"), Settings->ActiveLayerIndex, 0);
	TestEqual(TEXT("Selection alone does not dirty Layer data"), ChangeNotificationCount, 2);
	const int32 IndexBeforeInvalidSelection = Settings->ActiveLayerIndex;
	TestFalse(TEXT("Unknown Layer GUID cannot become active"),
		Settings->SelectLayer(FGuid::NewGuid()));
	TestEqual(TEXT("Invalid selection preserves active Layer"),
		Settings->ActiveLayerIndex,
		IndexBeforeInvalidSelection);
	TestFalse(TEXT("Top Layer cannot move above the array"),
		Settings->MoveActiveLayer(-1));

	TestTrue(TEXT("Active Layer can move down"), Settings->MoveActiveLayer(1));
	TestEqual(TEXT("Moved Layer remains active"), Settings->ActiveLayerIndex, 1);
	TestTrue(TEXT("Move preserves selected Layer identity"),
		Settings->GetActiveLayerId() == FirstLayerId);
	TestTrue(TEXT("Move changes top-to-bottom visual and simultaneous-entry order"),
		Settings->Layers[0].LayerId == SecondLayerId
			&& Settings->Layers[1].LayerId == FirstLayerId);
	TestEqual(TEXT("Move dirties Layer data once"), ChangeNotificationCount, 3);
	TestFalse(TEXT("Bottom Layer cannot move below the array"),
		Settings->MoveActiveLayer(1));
	TestEqual(TEXT("Rejected move does not dirty Layer data"), ChangeNotificationCount, 3);

	TestTrue(TEXT("Active Layer can be removed"), Settings->RemoveActiveLayer());
	TestEqual(TEXT("One Layer remains"), Settings->Layers.Num(), 1);
	TestTrue(TEXT("Remaining Layer becomes active"),
		Settings->GetActiveLayerId() == SecondLayerId);
	TestEqual(TEXT("Remove dirties Layer data once"), ChangeNotificationCount, 4);
	TestFalse(TEXT("Last Layer cannot be removed"), Settings->RemoveActiveLayer());
	TestEqual(TEXT("Rejected remove does not dirty Layer data"), ChangeNotificationCount, 4);

	Settings->OnLayerDataChanged.Unbind();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraMeshLayerModeIconTest,
	"ComposableCameraSystem.Editor.MeshCamera.ModeIcon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerModeIconTest::RunTest(const FString& /*Parameters*/)
{
	const TSharedRef<IEditorModeFactory>* ModeFactory =
		FEditorModeRegistry::Get().GetFactoryMap().Find(
			FComposableCameraMeshLayerEdMode::ModeId);
	TestNotNull(TEXT("Mesh Camera Layer edit mode is registered"), ModeFactory);
	if (!ModeFactory)
	{
		return false;
	}

	const FEditorModeInfo ModeInfo = (*ModeFactory)->GetModeInfo();
	const FSlateIcon& ModeIcon = ModeInfo.IconBrush;
	TestTrue(TEXT("Mesh Camera Layer mode icon is configured"), ModeIcon.IsSet());

	const TSharedRef<FComposableCameraEditorStyle> Style =
		FComposableCameraEditorStyle::Get();
	TestNotNull(
		TEXT("Mesh Camera Layer normal icon resolves"),
		Style->GetOptionalBrush(ModeIcon.GetStyleName(), nullptr, nullptr));
	TestNotNull(
		TEXT("Mesh Camera Layer small icon resolves without fallback"),
		Style->GetOptionalBrush(ModeIcon.GetSmallStyleName(), nullptr, nullptr));
	return true;
}

#endif
