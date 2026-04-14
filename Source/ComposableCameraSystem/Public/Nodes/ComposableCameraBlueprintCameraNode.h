// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ComposableCameraBlueprintCameraNode.generated.h"

/**
 * Base class for user-authored camera nodes written in Blueprint.
 *
 * Subclass this in Blueprint to create custom camera nodes with:
 *   - Custom initialization logic  (override "InitializeNode")
 *   - Custom per-frame tick logic   (override "TickNode")
 *   - Custom pin declarations       (override "GetPinDeclarations")
 *
 * Blueprint subclasses are automatically discovered by the camera type
 * asset editor — they appear in the "Add Node" context menu alongside
 * built-in C++ nodes with no manual registration required.
 *
 * Pin values can be read/written from Blueprint via the type-specific
 * accessors inherited from UComposableCameraCameraNodeBase:
 *   - GetInputPinValueFloat, GetInputPinValueVector, etc.
 *   - SetOutputPinValueFloat, SetOutputPinValueVector, etc.
 *
 * Any EditAnywhere + BlueprintReadWrite UPROPERTY on a Blueprint subclass
 * that maps to a supported pin type (bool, int32, float, double, Vector2D,
 * Vector, Vector4, Rotator, Transform, Actor, Object) is treated as an
 * implicit pin with the UPROPERTY's default value. See the build validator
 * for details.
 *
 * This class is Abstract — users must subclass it in Blueprint; it cannot
 * be placed directly.
 */
UCLASS(Abstract, Blueprintable, ClassGroup = ComposableCameraSystem,
	meta = (DisplayName = "Blueprint Camera Node",
	        ToolTip = "Base class for user-authored camera nodes written in Blueprint."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraBlueprintCameraNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	// ─── Blueprint Utilities ─────────────────────────────────────────────

	/**
	 * Read an input pin value as Vector2D from Blueprint.
	 * (The base class exposes Float/Int32/Bool/Vector/Rotator/Transform/Actor
	 *  but omits Vector2D and Double — fill the gap here.)
	 */
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	FVector2D GetInputPinValueVector2D(FName PinName) const;

	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Node|Pins")
	void SetOutputPinValueVector2D(FName PinName, FVector2D Value);

	/**
	 * Convenience: get the current camera pose from the owning PCM.
	 * Useful in OnInitialize when the node needs the pose at activation time.
	 */
	UFUNCTION(BlueprintPure, Category = "ComposableCameraSystem|Node")
	FComposableCameraPose GetCurrentCameraPose() const;
};
