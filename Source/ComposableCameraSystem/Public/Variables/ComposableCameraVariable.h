// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/TypeHash.h"

#include "ComposableCameraVariable.generated.h"

/**
 * The type of a camera variable. 
 */
UENUM()
enum class EComposableCameraVariableType
{
	Boolean,
	Integer32,
	Float,
	Double,
	Vector2d,
	Vector3d,
	Vector4d,
	Rotator3d,
	Transform3d,
	Actor,
	BlendableStruct
};

USTRUCT()
struct FComposableCameraVariableID
{
	GENERATED_BODY()

public:
	FComposableCameraVariableID() : Value(INVALID) {}

	uint32 GetValue() const { return Value; }

	bool IsValid() const { return Value != INVALID; }

	explicit operator bool() const { return IsValid(); }

	static FComposableCameraVariableID FromHashValue(uint32 Value)
	{
		return FComposableCameraVariableID(Value);
	}
	
public:
	friend bool operator<(FComposableCameraVariableID A, FComposableCameraVariableID B)
	{
		return A.Value < B.Value;
	}

	friend bool operator==(FComposableCameraVariableID A, FComposableCameraVariableID B)
	{
		return A.Value == B.Value;
	}

	friend bool operator!=(FComposableCameraVariableID A, FComposableCameraVariableID B)
	{
		return A.Value != B.Value;
	}

	friend uint32 GetTypeHash(FComposableCameraVariableID In)
	{
		return In.Value;
	}

	friend FArchive& operator<< (FArchive& Ar, FComposableCameraVariableID& In)
	{
		Ar << In.Value;
		return Ar;
	}

private:
	FComposableCameraVariableID(uint32 InValue) : Value(InValue) {}

	static constexpr uint32 INVALID = uint32(-1);
	
	UPROPERTY()
	uint32 Value;
};

USTRUCT()
struct FComposableCameraVariableDefinition
{
	GENERATED_BODY()

	/** Variable ID. */
	UPROPERTY()
	FComposableCameraVariableID VariableID;

	/** Variable type. */
	UPROPERTY()
	EComposableCameraVariableType VariableType = EComposableCameraVariableType::Boolean;

	/** Type of a blendable struct, only for when VariableType == BlendableStruct. */
	UPROPERTY()
	TObjectPtr<const UScriptStruct> BlendableStructType;

public:
#if WITH_EDITORONLY_DATA
	/** The name of the variable, for debugging purposes. */
	UPROPERTY()
	FString VariableName;
#endif

	/** Returns whether this definition has a valid variable ID. */
	bool IsValid() const
	{
		return VariableID.IsValid();
	}

	/** Implicit conversion to a camera variable ID. */
	operator FComposableCameraVariableID() const
	{
		return VariableID;
	}

	/** Creates a variant of this variable definition. */
	FComposableCameraVariableDefinition CreateVariant(const FString& VariantID) const
	{
		FComposableCameraVariableDefinition Variant( *this);
		Variant.VariableID = FComposableCameraVariableID::FromHashValue(HashCombineFast(VariableID.GetValue(), GetTypeHash(VariantID)));
#if WITH_EDITORONLY_DATA
		if (!VariableName.IsEmpty())
		{
			Variant.VariableName += FString::Format(TEXT("_{0}Variant"), { VariantID });
		}
#endif
		return Variant;
	}

	bool operator==(const FComposableCameraVariableDefinition& Other) const = default;
};

/**
 * Camera variables.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, CollapseCategories, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraVariable : public UObject
{
	GENERATED_BODY()

public:
	UComposableCameraVariable(const FObjectInitializer& ObjectInitializer);

	FComposableCameraVariableID GetVariableID() const;

	FComposableCameraVariableDefinition GetVariableDefinition() const;

	const FGuid& GetGuid() const { return Guid; }

	virtual EComposableCameraVariableType GetVariableType() const PURE_VIRTUAL(UComposableCameraVariable::GetVariableType, return EComposableCameraVariableType::Boolean;);
	virtual const uint8* GetDefaultValuePtr() const PURE_VIRTUAL(UComposableCameraVariable::GetDefaultValuePtr, return nullptr;);
	virtual void Reset() PURE_VIRTUAL(UComposableCameraVariable::Reset, return;);
	
	UFUNCTION(BlueprintCallable, Category = "ComposableCameraSystem|Variable")
	void ResetToDefaultValues()
	{
		Reset();
	}

#if WITH_EDITORONLY_DATA
	FString GetDisplayName() const;
#endif

#if WITH_EDITORONLY_DATA
	FText GetDisplayText() const;
	virtual FString FormatDefaultValue() const PURE_VIRTUAL(UComposableCameraVariable::FormatDefaultValue, return FString(););
#endif

public:
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	
public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere)
	FString DisplayName;
#endif

private:
	UPROPERTY()
	FGuid Guid;
};

/** Boolean camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UBooleanComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = bool;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	bool GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Boolean; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }

#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return LexToString(DefaultValue); }
#endif  

public:
	UPROPERTY(EditAnywhere)
	bool DefaultValue = false;

	UPROPERTY(Transient)
	bool RuntimeValue;
};

/** Float camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UFloatComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = float;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	float GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Float; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return LexToString(DefaultValue); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	float DefaultValue = 0;

	UPROPERTY(Transient)
	float RuntimeValue;
};

/** Double camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UDoubleComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = double;

	UFUNCTION(BlueprintPure)
	double GetRuntimeValue() const { return RuntimeValue; }
	
	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	double GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Double; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return LexToString(DefaultValue); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	double DefaultValue = 0;

	UPROPERTY(Transient)
	double RuntimeValue;
};

/** Integer32 camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UInteger32ComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = int32;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	int32 GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Integer32; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return LexToString(DefaultValue); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	int32 DefaultValue = 0;

	UPROPERTY(Transient)
	int32 RuntimeValue;
};

/** Vector2d camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UVector2dComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = FVector2d;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	FVector2d GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Vector2d; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return DefaultValue.ToString(); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	FVector2D DefaultValue { };

	UPROPERTY(BlueprintReadOnly, Transient)
	FVector2D RuntimeValue;
};

/** Vector3d camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UVector3dComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = FVector;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	FVector GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Vector3d; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return DefaultValue.ToString(); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	FVector DefaultValue {};

	UPROPERTY(Transient)
	FVector RuntimeValue {};
};

/** Vector4d camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UVector4dComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = FVector4;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	FVector4 GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Vector4d; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return DefaultValue.ToString(); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	FVector4 DefaultValue {};

	UPROPERTY(Transient)
	FVector4 RuntimeValue {};
};

/** Rotator3d camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class URotator3dComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = FRotator3d;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	FRotator3d GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Rotator3d; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return DefaultValue.ToString(); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	FRotator3d DefaultValue {};
	
	UPROPERTY(Transient)
	FRotator3d RuntimeValue {};
};

/** Transform3d camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UTransform3dComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = FTransform3d;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	FTransform3d GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Transform3d; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return DefaultValue.ToString(); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	FTransform3d DefaultValue {};

	UPROPERTY(Transient)
	FTransform3d RuntimeValue {};
};

/** Actor camera variable. */
UCLASS(ClassGroup = ComposableCameraSystem)
class UActorComposableCameraVariable : public UComposableCameraVariable
{
	GENERATED_BODY()

public:
	using ValueType = AActor*;

	void SetRuntimeValue(ValueType NewRuntimeValue) { RuntimeValue = NewRuntimeValue; }
	AActor* GetDefaultValue() const { return DefaultValue; }
	virtual EComposableCameraVariableType GetVariableType() const override { return EComposableCameraVariableType::Actor; }
	virtual const uint8* GetDefaultValuePtr() const override { return reinterpret_cast<const uint8*>(&DefaultValue); }
	virtual void Reset() override { RuntimeValue = DefaultValue; }
	virtual void PostLoad() override { Super::PostLoad(); RuntimeValue = DefaultValue; }
	
#if WITH_EDITOR
	virtual FString FormatDefaultValue() const override { return LexToString(DefaultValue->GetName()); }
#endif  

public:
	UPROPERTY(EditAnywhere, Category = ComposableCamera)
	AActor* DefaultValue { nullptr };

	UPROPERTY(Transient)
	AActor* RuntimeValue {};
};


