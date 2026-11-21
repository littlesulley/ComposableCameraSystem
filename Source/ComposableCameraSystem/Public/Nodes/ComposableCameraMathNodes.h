// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ComposableCameraMathNodes.generated.h"

class UCurveFloat;

UENUM(BlueprintType)
enum class EComposableCameraScalarOperation : uint8
{
	Add,
	Subtract,
	Multiply,
	Divide,
	Minimum,
	Maximum,
	Absolute
};

/** Stateless data operators on the per-frame Start chain. Place before consumers.
 * They preserve the incoming pose; disconnected operators are not evaluated on demand. */
UCLASS(Abstract, NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraScalarNodeBase : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
public:
	UComposableCameraScalarNodeBase() { PaletteCategory = TEXT("Math"); }
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Output")
	float Result = 0.f;

protected:
	/** Every evaluation publishes a value. Non-finite results become zero. */
	void PublishResult(float Value);
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Scalar", ToolTip = "Per-frame scalar arithmetic. Connect execution before result consumers. Does not change camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraScalarMathNode : public UComposableCameraScalarNodeBase
{
	GENERATED_BODY()
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	EComposableCameraScalarOperation Operation = EComposableCameraScalarOperation::Add;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float A = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float B = 0.f;
	/** Used for division when abs(B) <= UE_SMALL_NUMBER. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float DivideByZeroValue = 0.f;
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Clamp", ToolTip = "Clamps a scalar each frame. Reversed limits are sorted. Does not change camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraClampNode : public UComposableCameraScalarNodeBase
{
	GENERATED_BODY()
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float Value = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float Minimum = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float Maximum = 1.f;
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Remap Range", ToolTip = "Maps an input range to an output range each frame. Supports reversed ranges. Does not change camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraRemapNode : public UComposableCameraScalarNodeBase
{
	GENERATED_BODY()
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float Value = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float InputMin = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float InputMax = 1.f;
	/** Also used when the input range has zero/near-zero width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float OutputMin = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float OutputMax = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	bool bClampInput = true;
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Lerp", ToolTip = "Interpolates two scalar inputs each frame. Does not change camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraLerpNode : public UComposableCameraScalarNodeBase
{
	GENERATED_BODY()
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float A = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float B = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float Alpha = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	bool bClampAlpha = true;
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Sample Curve", ToolTip = "Samples a Curve Float at the supplied input each frame, using its extrapolation settings. Does not change camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraCurveSampleNode : public UComposableCameraScalarNodeBase
{
	GENERATED_BODY()
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	TObjectPtr<UCurveFloat> Curve;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float Input = 0.f;
	/** Used when Curve is unset or Input is not finite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float FallbackValue = 0.f;
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Select", ToolTip = "Selects one scalar input each frame. Does not control execution of upstream nodes or change camera pose."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraSelectNode : public UComposableCameraScalarNodeBase
{
	GENERATED_BODY()
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	bool bCondition = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float TrueValue = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float FalseValue = 0.f;
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Float To Double"))
class COMPOSABLECAMERASYSTEM_API UComposableCameraFloatToDoubleNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
public:
	UComposableCameraFloatToDoubleNode() { PaletteCategory = TEXT("Math"); }
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	float Value = 0.f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Output")
	double Result = 0.0;
};

UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Math: Double To Float", ToolTip = "Converts to float, saturating finite values to the float range. Non-finite input produces zero."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraDoubleToFloatNode : public UComposableCameraScalarNodeBase
{
	GENERATED_BODY()
public:
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputParameters")
	double Value = 0.0;
};
