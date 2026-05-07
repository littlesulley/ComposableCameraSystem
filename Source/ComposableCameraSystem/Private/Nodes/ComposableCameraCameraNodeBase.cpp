// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCameraNodeBase.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "ComposableCameraSystemModule.h"

namespace
{
	/**
	 * Verify that `V` (resolved from an Object/Actor pin's data block slot)
	 * actually satisfies the target FObjectProperty's class constraint
	 * before writing it through `SetObjectPropertyValue`. The auto-resolve
	 * pipeline collapses every object-class pin into the generic Object /
	 * Actor `EComposableCameraPinType` for storage, so the runtime data
	 * block has no record of the original PropertyClass — a stale asset,
	 * a hand-edited connection, or a BP wildcard mismatch could deliver,
	 * say, a `UCurveVector` to a `TObjectPtr<UCurveFloat>` field. The raw
	 * `SetObjectPropertyValue` writes any UObject*-shaped pointer into
	 * the field's memory regardless of class; the next typed C++ access
	 * (`GetCurrentValue` etc.) then reinterprets bytes of the wrong type
	 * and crashes or corrupts state.
	 *
	 * Returns true if the assignment is type-safe and writes the value.
	 * On a mismatch, logs once and writes nullptr (the field's documented
	 * unset state) so the consumer's existing nullcheck path takes over
	 * rather than seeing a wrong-class instance.
	 */
	static void AssignObjectPropertyChecked(
		const FObjectPropertyBase* ObjectProp,
		void* ValuePtr,
		UObject* V,
		const TCHAR* PinKind,
		FName PinName)
	{
		UObject* WriteValue = IsValid(V) ? V : nullptr;
		if (WriteValue && ObjectProp)
		{
			if (const FClassProperty* ClassProp = CastField<FClassProperty>(ObjectProp))
			{
				// FClassProperty stores a UClass*. The "class constraint"
				// is `MetaClass` (the base class the stored UClass* must
				// inherit from). `IsChildOf(nullptr)` returns true so an
				// unset MetaClass means "any class".
				UClass* AsClass = Cast<UClass>(WriteValue);
				const bool bOk = AsClass
					&& (!ClassProp->MetaClass || AsClass->IsChildOf(ClassProp->MetaClass));
				if (!bOk)
				{
					UE_LOG(LogComposableCameraSystem, Warning,
						TEXT("ResolveAllInputPins: %s pin '%s' got a class '%s' that does not derive from required '%s'. Writing nullptr."),
						PinKind, *PinName.ToString(),
						AsClass ? *AsClass->GetName() : TEXT("(non-class UObject)"),
						ClassProp->MetaClass ? *ClassProp->MetaClass->GetName() : TEXT("UClass"));
					WriteValue = nullptr;
				}
			}
			else if (ObjectProp->PropertyClass && !WriteValue->IsA(ObjectProp->PropertyClass))
			{
				// Plain object-class constraint (FObjectProperty / Actor /
				// soft-object base). `IsA` walks the runtime class chain;
				// fails for sibling types or unrelated UObjects. Writing
				// the wrong-class instance through SetObjectPropertyValue
				// would corrupt the field — its consumers cast back to
				// the typed pointer and dereference.
				UE_LOG(LogComposableCameraSystem, Warning,
					TEXT("ResolveAllInputPins: %s pin '%s' got a '%s' but field expects '%s'. Writing nullptr."),
					PinKind, *PinName.ToString(),
					*WriteValue->GetClass()->GetName(),
					*ObjectProp->PropertyClass->GetName());
				WriteValue = nullptr;
			}
		}
		if (ObjectProp)
		{
			ObjectProp->SetObjectPropertyValue(ValuePtr, WriteValue);
		}
	}
}

namespace ComposableCameraSystem::Private
{
	/**
	 * Module-local cache of per-UClass pin binding tables.
	 *
	 * Built lazily in UComposableCameraCameraNodeBase::GetOrBuildPinBindings and
	 * reused by every instance of that class. Keyed by UClass* — entries become
	 * unreachable (but not crash-inducing) if a Blueprint class is recompiled;
	 * TSharedRef keeps payloads alive even if the key is replaced by REINST_.
	 *
	 * NOTE: BP recompile currently leaves a stale entry for the old UClass* and
	 * forces a rebuild against the new UClass*. This is safe (no dangling reads)
	 * but slightly leaky. Hooking FCoreUObjectDelegates::ReloadCompleteDelegate
	 * to evict entries is a follow-up.
	 */
	static FCriticalSection GPinBindingCacheCS;
	static TMap<const UClass*, TSharedRef<FComposableCameraNodePinBindingTable>> GPinBindingCache;

	/**
	 * Narrow-cast a normalized int64 enum value into the actual backing FProperty's
	 * underlying storage width and write it at ValuePtr.
	 *
	 * Supports both `FEnumProperty` (C++ `enum class`, whose underlying numeric
	 * property tells us the real width) and `FByteProperty` (legacy TEnumAsByte,
	 * always uint8). Silently no-ops on unknown property kinds — the binding
	 * builder already rejects anything else.
	 */
	inline void WriteEnumInt64ToProperty(const FProperty* Property, void* ValuePtr, int64 Value)
	{
		if (!Property || !ValuePtr)
		{
			return;
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			if (const FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty())
			{
				Underlying->SetIntPropertyValue(ValuePtr, Value);
			}
			return;
		}
		if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			// Narrow to uint8 (TEnumAsByte storage). Values outside 0..255 are
			// already filtered at write time by the editor, but clamp defensively.
			ByteProp->SetIntPropertyValue(ValuePtr, Value);
			return;
		}
	}
}

void UComposableCameraCameraNodeBase::Initialize(AComposableCameraCameraBase* InOwningCamera, AComposableCameraPlayerCameraManager* InPlayerCameraManager)
{
	OwningCamera = InOwningCamera;
	OwningPlayerCameraManager = InPlayerCameraManager;
#if CPUPROFILERTRACE_ENABLED
	CachedNodeClassName = GetClass()->GetName();
#endif

	// Auto-apply subobject pin values before the subclass's OnInitialize,
	// so that interpolator/subobject properties reflect any wired or exposed
	// overrides before the subclass builds typed instances from them.
	AutoApplySubobjectPinValues();

	// Resolve exposed/wired/overridden pin values into matching UPROPERTYs so
	// that OnInitialize implementations can read members directly — same as
	// the TickNode prologue does before OnTickNode.
	ResolveAllInputPins();

	// Reset so OnFirstTickNode fires again on the first tick of this activation.
	bHasHadFirstTick = false;

	OnInitialize();
}

DECLARE_CYCLE_STAT(TEXT("Node TickNode"),            STAT_CCS_Node_TickNode,            STATGROUP_CCS);
DECLARE_CYCLE_STAT(TEXT("Node ResolveAllInputPins"), STAT_CCS_Node_ResolveAllInputPins, STATGROUP_CCS);

void UComposableCameraCameraNodeBase::TickNode(float DeltaTime, const FComposableCameraPose CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_Node_TickNode);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_Node_TickNode);
#if CPUPROFILERTRACE_ENABLED
	FCpuProfilerTrace::FEventScope __NodeClassScope(CachedProfilerSpecId, *CachedNodeClassName, true, __FILE__, __LINE__);
#endif

	// Auto-resolve declared input pins into their matching UPROPERTY fields so
	// that OnTickNode can read members directly instead of calling GetInputPinValue<T>().
	// Subclasses that manage their own pin reads can opt out via ShouldAutoResolveInputPins.
	if (ShouldAutoResolveInputPins())
	{
		ResolveAllInputPins();
	}

	// OnFirstTickNode fires exactly once per activation, after pins are resolved
	// but before the main tick — the correct place to seed state from live pin values.
	if (!bHasHadFirstTick)
	{
		bHasHadFirstTick = true;
		OnFirstTickNode();
	}

	OnTickNode(DeltaTime, CurrentCameraPose, OutCameraPose);

#if WITH_EDITOR
	DebugPoseAfterTick = OutCameraPose;
	bDebugWasTickedThisFrame = true;
#endif
}

FGameplayTag UComposableCameraCameraNodeBase::GetOwningCameraTag() const
{
	return OwningCamera ? OwningCamera->CameraTag : FGameplayTag::EmptyTag;
}

void UComposableCameraCameraNodeBase::OnPreTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Intentionally empty. Override in C++ subclasses if pre-tick behavior is needed.
}

void UComposableCameraCameraNodeBase::OnPostTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// Intentionally empty. Override in C++ subclasses if post-tick behavior is needed.
}

// ─── Pin Gathering ─────────────────────────────────────────────────────

void UComposableCameraCameraNodeBase::GatherAllPinDeclarations(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Step 1: subclass-declared pins (virtual chain).
	GetPinDeclarations(OutPins);

	// Step 2: auto-discover Instanced subobject pins.
	AutoDeclareSubobjectPins(OutPins);
}

void UComposableCameraCameraNodeBase::AutoDeclareSubobjectPins(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (!Property->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			continue;
		}

		const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property);
		if (!ObjProp)
		{
			continue;
		}

		const UObject* Subobject = ObjProp->GetObjectPropertyValue(
			ObjProp->ContainerPtrToValuePtr<void>(this));

		DeclareSubobjectPins(Property->GetFName(), Subobject, OutPins);
	}
}

void UComposableCameraCameraNodeBase::AutoApplySubobjectPinValues()
{
	if (!RuntimeDataBlock)
	{
		return;
	}

	for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (!Property->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			continue;
		}

		FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property);
		if (!ObjProp)
		{
			continue;
		}

		UObject* Subobject = ObjProp->GetObjectPropertyValue(
			ObjProp->ContainerPtrToValuePtr<void>(this));

		ApplySubobjectPinValues(Property->GetFName(), Subobject);
	}
}

// ─── Top-level Pin Auto-Resolution ──────────────────────────────────────

const FComposableCameraNodePinBindingTable& UComposableCameraCameraNodeBase::GetOrBuildPinBindings() const
{
	using namespace ComposableCameraSystem::Private;

	UClass* Class = GetClass();

	{
		FScopeLock Lock(&GPinBindingCacheCS);
		if (const TSharedRef<FComposableCameraNodePinBindingTable>* Existing = GPinBindingCache.Find(Class))
		{
			return Existing->Get();
		}
	}

	// Build outside the lock first (GetPinDeclarations can run arbitrary native/BP
	// logic), then insert under the lock. If two threads race to build the same
	// class, we just discard the later table — the cached one is equally valid.
	TSharedRef<FComposableCameraNodePinBindingTable> NewTable = MakeShared<FComposableCameraNodePinBindingTable>();

	// 1. Gather pin declarations from the CDO. GetPinDeclarations is `const` and,
	//    by convention, should only read class-default state — this is the same
	//    assumption the editor relies on when building node asset palettes.
	const UComposableCameraCameraNodeBase* CDO = GetDefault<UComposableCameraCameraNodeBase>(Class);
	if (!CDO)
	{
		FScopeLock Lock(&GPinBindingCacheCS);
		GPinBindingCache.Add(Class, NewTable);
		return NewTable.Get();
	}

	TArray<FComposableCameraNodePinDeclaration> Pins;
	CDO->GetPinDeclarations(Pins);

	// 2. Index UPROPERTYs on the class by FName for O(1) lookup. Only top-level
	//    edit-visible properties that are NOT Instanced subobjects qualify —
	//    Instanced refs are handled by AutoApplySubobjectPinValues.
	TMap<FName, const FProperty*> PropertiesByName;
	PropertiesByName.Reserve(32);
	for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
	{
		const FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (Property->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			continue;
		}
		PropertiesByName.Add(Property->GetFName(), Property);
	}

	// 3. For every declared Input pin, look up the matching UPROPERTY and — if
	//    the types align — record a binding. Silently skip pins that have no
	//    backing UPROPERTY (those nodes fall back to GetInputPinValue<T>()).
	for (const FComposableCameraNodePinDeclaration& Pin : Pins)
	{
		if (Pin.Direction != EComposableCameraPinDirection::Input)
		{
			continue;
		}

		const FProperty* const* FoundProperty = PropertiesByName.Find(Pin.PinName);
		if (!FoundProperty || !*FoundProperty)
		{
			continue;
		}

		EComposableCameraPinType MappedType;
		UScriptStruct* MappedStruct = nullptr;
		UEnum* MappedEnum = nullptr;
		if (!TryMapPropertyToPinType(*FoundProperty, MappedType, MappedStruct, MappedEnum))
		{
			continue;
		}

		// Type mismatch between the declared pin and the UPROPERTY is a silent
		// skip. The existing pin validator (in the editor build) already flags
		// this loudly at asset-save time; we just refuse to write into the
		// wrong slot at runtime.
		if (MappedType != Pin.PinType)
		{
			continue;
		}
		if (Pin.PinType == EComposableCameraPinType::Struct && MappedStruct != Pin.StructType)
		{
			continue;
		}
		if (Pin.PinType == EComposableCameraPinType::Enum && MappedEnum != Pin.EnumType)
		{
			continue;
		}

		FComposableCameraNodePinBinding Binding;
		Binding.PinName = Pin.PinName;
		Binding.PinType = MappedType;
		Binding.StructType = MappedStruct;
		Binding.EnumType = MappedEnum;
		// BackingProperty is needed by the auto-resolve loop's runtime dispatch:
		// Enum uses it to narrow-cast int64 storage into the actual property
		// width (FByteProperty / FEnumProperty); Struct uses it to look up
		// the FStructProperty's UScriptStruct for CopyScriptStruct dispatch
		// (POD bytes vs FInstancedStruct slot); Actor / Object use it as the
		// FObjectPropertyBase to call SetObjectPropertyValue (TObjectPtr-
		// correct write + GC-token bookkeeping; see the matching dispatch
		// in ResolveAllInputPins). Other primitive types (Bool / Int32 /
		// Float / Vector / etc.) resolve via the templated TryResolveInputPin<T>
		// path and write directly through static_cast — no FProperty needed.
		Binding.BackingProperty =
			(MappedType == EComposableCameraPinType::Enum
				|| MappedType == EComposableCameraPinType::Struct
				|| MappedType == EComposableCameraPinType::Actor
				|| MappedType == EComposableCameraPinType::Object)
				? *FoundProperty
				: nullptr;
		Binding.FieldOffset = (*FoundProperty)->GetOffset_ForInternal();
		NewTable->InputBindings.Add(MoveTemp(Binding));
	}

	// 4. Publish under the lock. If a concurrent builder inserted first, prefer
	//    the earlier entry (deterministic; both tables describe the same class).
	{
		FScopeLock Lock(&GPinBindingCacheCS);
		if (const TSharedRef<FComposableCameraNodePinBindingTable>* Existing = GPinBindingCache.Find(Class))
		{
			return Existing->Get();
		}
		GPinBindingCache.Add(Class, NewTable);
		return NewTable.Get();
	}
}

void UComposableCameraCameraNodeBase::ResolveAllInputPins()
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_Node_ResolveAllInputPins);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_Node_ResolveAllInputPins);

	if (!RuntimeDataBlock)
	{
		return;
	}

	const FComposableCameraNodePinBindingTable& Table = GetOrBuildPinBindings();
	if (Table.InputBindings.Num() == 0)
	{
		return;
	}

	uint8* const NodeBase = reinterpret_cast<uint8*>(this);

	for (const FComposableCameraNodePinBinding& Binding : Table.InputBindings)
	{
		void* const ValuePtr = NodeBase + Binding.FieldOffset;

		// Type-dispatch: read from the data block and write into the node's UPROPERTY.
		// Mirrors ApplySubobjectPinValues; keep the two switches in sync when
		// adding new pin types.
		switch (Binding.PinType)
		{
		case EComposableCameraPinType::Bool:
			{ bool V; if (RuntimeDataBlock->TryResolveInputPin<bool>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<bool*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Int32:
			{ int32 V; if (RuntimeDataBlock->TryResolveInputPin<int32>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<int32*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Float:
			{ float V; if (RuntimeDataBlock->TryResolveInputPin<float>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<float*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Double:
			{ double V; if (RuntimeDataBlock->TryResolveInputPin<double>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<double*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector2D:
			{ FVector2D V; if (RuntimeDataBlock->TryResolveInputPin<FVector2D>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<FVector2D*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector3D:
			{ FVector V; if (RuntimeDataBlock->TryResolveInputPin<FVector>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<FVector*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector4:
			{ FVector4 V; if (RuntimeDataBlock->TryResolveInputPin<FVector4>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<FVector4*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Rotator:
			{ FRotator V; if (RuntimeDataBlock->TryResolveInputPin<FRotator>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<FRotator*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Transform:
			{ FTransform V; if (RuntimeDataBlock->TryResolveInputPin<FTransform>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<FTransform*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Actor:
			{
				// Route through FObjectPropertyBase::SetObjectPropertyValue
				// instead of `*static_cast<AActor**>(ValuePtr) = V`. The field
				// is declared `TObjectPtr<AActor>` per project rule; the
				// TObjectPtr storage layout may differ from a raw `AActor*`
				// in some engine configurations, and the GC integrity-token
				// bookkeeping (UE_GC_TRACK_OBJ_AVAILABLE in dev builds)
				// updates from inside SetObjectPropertyValue. A raw cast
				// write skips both, producing false-positive GC warnings and
				// (in TObjectPtr-instrumented builds) the wrong stored bytes.
				//
				// AssignObjectPropertyChecked also verifies V satisfies the
				// FObjectProperty's PropertyClass constraint before writing
				// — auto-resolve storage is class-erased to "Actor / Object",
				// so wrong-class deliveries (stale asset, BP wildcard) would
				// otherwise corrupt the typed field.
				AActor* V = nullptr;
				if (RuntimeDataBlock->TryResolveInputPin<AActor*>(RuntimeNodeIndex, Binding.PinName, V))
				{
					AssignObjectPropertyChecked(
						CastField<FObjectPropertyBase>(Binding.BackingProperty),
						ValuePtr, V, TEXT("Actor"), Binding.PinName);
				}
			}
			break;
		case EComposableCameraPinType::Object:
			{
				UObject* V = nullptr;
				if (RuntimeDataBlock->TryResolveInputPin<UObject*>(RuntimeNodeIndex, Binding.PinName, V))
				{
					AssignObjectPropertyChecked(
						CastField<FObjectPropertyBase>(Binding.BackingProperty),
						ValuePtr, V, TEXT("Object"), Binding.PinName);
				}
			}
			break;
		case EComposableCameraPinType::Name:
			{ FName V; if (RuntimeDataBlock->TryResolveInputPin<FName>(RuntimeNodeIndex, Binding.PinName, V)) { *static_cast<FName*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Enum:
			{
				// Data block always stores the normalized int64. Narrow-cast into
				// the actual backing FProperty's storage width via its numeric
				// property helper; supports both FEnumProperty (enum class) and
				// FByteProperty (TEnumAsByte).
				int64 V = 0;
				if (RuntimeDataBlock->TryResolveInputPin<int64>(RuntimeNodeIndex, Binding.PinName, V))
				{
					ComposableCameraSystem::Private::WriteEnumInt64ToProperty(Binding.BackingProperty, ValuePtr, V);
				}
			}
			break;
		case EComposableCameraPinType::Struct:
			{
				// Struct pins dispatch on the resolved source offset's storage
				// class. POD structs (where IsBytewiseSafeStruct accepts) live
				// in the byte-array Storage and Memcpy is correct; non-POD
				// structs (FString / TArray / object refs / delegates inside)
				// live in the typed FInstancedStruct slot pool and must use
				// CopyScriptStruct so embedded heap-owned members get a proper
				// per-property copy through their FProperty operator=.
				int32 Offset = INDEX_NONE;
				if (!RuntimeDataBlock->ResolveInputPinOffset(RuntimeNodeIndex, Binding.PinName, Offset))
				{
					break;
				}
				const FStructProperty* StructProp = CastField<FStructProperty>(Binding.BackingProperty);
				if (!StructProp || !StructProp->Struct)
				{
					break;
				}

				if (RuntimeDataBlock->IsStructSlotOffset(Offset))
				{
					// `TryGetStructSlot` over `GetStructSlotChecked`:
					// the offset comes from `ResolveInputPinOffset` /
					// the resolved pin source, which COULD be stale
					// across a runtime layout edit; a wrong offset
					// would otherwise hit Checked's fatal-error path
					// in Shipping (or a `check()`-strip out-of-bounds
					// read in old builds). Try-form fails-skip + the
					// silent no-op below is the same degradation the
					// templated `ReadValue<T>` path uses on shape
					// mismatch.
					if (const FInstancedStruct* Slot =
						RuntimeDataBlock->TryGetStructSlot(Offset, StructProp->Struct))
					{
						// CopyScriptStruct iterates the struct's properties and
						// invokes per-property operator= -- FString reuses its
						// existing allocator if capacity already fits, so the
						// per-frame steady state is no-alloc for stable values.
						// First-tick or grow events allocate once; documented
						// in TechDoc.md §7.2 alloc characteristic.
						StructProp->Struct->CopyScriptStruct(ValuePtr, Slot->GetMemory());
					}
				}
				else
				{
					// POD struct: byte-storage path.
					const int32 Size = StructProp->Struct->GetStructureSize();
					if (Size > 0
						&& Offset >= 0
						&& Offset + Size <= RuntimeDataBlock->Storage.Num())
					{
						FMemory::Memcpy(ValuePtr, RuntimeDataBlock->Storage.GetData() + Offset, Size);
					}
				}
			}
			break;
		case EComposableCameraPinType::Delegate:
			// Delegates are not POD — they bypass the data block entirely and are
			// written directly into the node's UPROPERTY at activation time via
			// ApplyDelegateBindings. Nothing to resolve per-frame here.
			break;
		}
	}
}

// ─── Subobject Pin Helpers ──────────────────────────────────────────────

void UComposableCameraCameraNodeBase::DeclareSubobjectPins(
	FName SubobjectPropertyName,
	const UObject* Subobject,
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	if (!Subobject)
	{
		return;
	}

	const FString SubobjectPrefix = SubobjectPropertyName.ToString() + TEXT(".");
	const FText SubobjectDisplayName = FText::FromString(
		FName::NameToDisplayString(SubobjectPropertyName.ToString(), /*bIsBool=*/ false));

	for (TFieldIterator<FProperty> PropIt(Subobject->GetClass()); PropIt; ++PropIt)
	{
		const FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Opt-out: subobject authors can tag properties with NoPinExposure.
		if (Property->HasMetaData(TEXT("NoPinExposure")))
		{
			continue;
		}

		EComposableCameraPinType PinType;
		UScriptStruct* StructType = nullptr;
		UEnum* EnumType = nullptr;
		if (!TryMapPropertyToPinType(Property, PinType, StructType, EnumType))
		{
			continue;
		}

		FComposableCameraNodePinDeclaration PinDecl;
		PinDecl.PinName = FName(*(SubobjectPrefix + Property->GetName()));
		PinDecl.DisplayName = FText::Format(
			NSLOCTEXT("ComposableCameraSystem", "SubobjectPinDisplayFmt", "{0} > {1}"),
			SubobjectDisplayName,
			Property->GetDisplayNameText());
		PinDecl.Direction = EComposableCameraPinDirection::Input;
		PinDecl.PinType = PinType;
		PinDecl.StructType = StructType;
		PinDecl.EnumType = EnumType;
		PinDecl.Tooltip = Property->GetToolTipText();

		// Serialize the current property value as the default string.
		FString ValueString;
		Property->ExportTextItem_Direct(ValueString, Property->ContainerPtrToValuePtr<void>(Subobject), nullptr, nullptr, PPF_None);
		PinDecl.DefaultValueString = MoveTemp(ValueString);

		OutPins.Add(MoveTemp(PinDecl));
	}
}

void UComposableCameraCameraNodeBase::ApplySubobjectPinValues(
	FName SubobjectPropertyName,
	UObject* Subobject)
{
	if (!Subobject || !RuntimeDataBlock)
	{
		return;
	}

	const FString SubobjectPrefix = SubobjectPropertyName.ToString() + TEXT(".");

	for (TFieldIterator<FProperty> PropIt(Subobject->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		if (Property->HasMetaData(TEXT("NoPinExposure")))
		{
			continue;
		}

		EComposableCameraPinType PinType;
		UScriptStruct* StructType = nullptr;
		UEnum* EnumType = nullptr;
		if (!TryMapPropertyToPinType(Property, PinType, StructType, EnumType))
		{
			continue;
		}

		const FName CompoundPinName(*(SubobjectPrefix + Property->GetName()));
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Subobject);

		// Type-dispatch: read from the data block and write into the subobject property.
		switch (PinType)
		{
		case EComposableCameraPinType::Bool:
			{ bool V; if (RuntimeDataBlock->TryResolveInputPin<bool>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<bool*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Int32:
			{ int32 V; if (RuntimeDataBlock->TryResolveInputPin<int32>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<int32*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Float:
			{ float V; if (RuntimeDataBlock->TryResolveInputPin<float>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<float*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Double:
			{ double V; if (RuntimeDataBlock->TryResolveInputPin<double>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<double*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector2D:
			{ FVector2D V; if (RuntimeDataBlock->TryResolveInputPin<FVector2D>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FVector2D*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector3D:
			{ FVector V; if (RuntimeDataBlock->TryResolveInputPin<FVector>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FVector*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Vector4:
			{ FVector4 V; if (RuntimeDataBlock->TryResolveInputPin<FVector4>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FVector4*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Rotator:
			{ FRotator V; if (RuntimeDataBlock->TryResolveInputPin<FRotator>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FRotator*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Transform:
			{ FTransform V; if (RuntimeDataBlock->TryResolveInputPin<FTransform>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FTransform*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Actor:
			{
				AActor* V = nullptr;
				if (RuntimeDataBlock->TryResolveInputPin<AActor*>(RuntimeNodeIndex, CompoundPinName, V))
				{
					// Route through AssignObjectPropertyChecked for
					// TObjectPtr-storage / GC-token correctness AND class
					// constraint verification — see the matching comment in
					// the top-level ResolveAllInputPins switch above.
					// Property is the iterator's current FProperty (we're
					// walking subobject members) so the typed accessor is
					// already in hand.
					AssignObjectPropertyChecked(
						CastField<FObjectPropertyBase>(Property),
						ValuePtr, V, TEXT("Subobject Actor"), CompoundPinName);
				}
			}
			break;
		case EComposableCameraPinType::Object:
			{
				UObject* V = nullptr;
				if (RuntimeDataBlock->TryResolveInputPin<UObject*>(RuntimeNodeIndex, CompoundPinName, V))
				{
					AssignObjectPropertyChecked(
						CastField<FObjectPropertyBase>(Property),
						ValuePtr, V, TEXT("Subobject Object"), CompoundPinName);
				}
			}
			break;
		case EComposableCameraPinType::Name:
			{ FName V; if (RuntimeDataBlock->TryResolveInputPin<FName>(RuntimeNodeIndex, CompoundPinName, V)) { *static_cast<FName*>(ValuePtr) = V; } }
			break;
		case EComposableCameraPinType::Enum:
			{
				// Mirrors ResolveAllInputPins: read the normalized int64 from the
				// data block and narrow-cast into the subobject property's actual
				// underlying width. Property is known here (we're iterating), so
				// we pass it directly to the narrow-cast helper.
				int64 V = 0;
				if (RuntimeDataBlock->TryResolveInputPin<int64>(RuntimeNodeIndex, CompoundPinName, V))
				{
					ComposableCameraSystem::Private::WriteEnumInt64ToProperty(Property, ValuePtr, V);
				}
			}
			break;
		case EComposableCameraPinType::Struct:
			{
				// Struct subobject pins follow the same POD-vs-typed dispatch
				// as the top-level auto-resolve loop above. The Property here
				// is the subobject's struct field (we're iterating subobject
				// properties), so its FStructProperty drives CopyScriptStruct.
				int32 Offset = INDEX_NONE;
				if (!RuntimeDataBlock->ResolveInputPinOffset(RuntimeNodeIndex, CompoundPinName, Offset))
				{
					break;
				}
				const FStructProperty* StructProp = CastField<FStructProperty>(Property);
				if (!StructProp || !StructProp->Struct)
				{
					break;
				}

				if (RuntimeDataBlock->IsStructSlotOffset(Offset))
				{
					// Try-form fails-skip on stale layout — see the
					// matching call site above for rationale.
					if (const FInstancedStruct* Slot =
						RuntimeDataBlock->TryGetStructSlot(Offset, StructProp->Struct))
					{
						StructProp->Struct->CopyScriptStruct(ValuePtr, Slot->GetMemory());
					}
				}
				else
				{
					const int32 Size = StructProp->Struct->GetStructureSize();
					if (Size > 0
						&& Offset >= 0
						&& Offset + Size <= RuntimeDataBlock->Storage.Num())
					{
						FMemory::Memcpy(ValuePtr, RuntimeDataBlock->Storage.GetData() + Offset, Size);
					}
				}
			}
			break;
		case EComposableCameraPinType::Delegate:
			// Delegates are not POD — they bypass the data block entirely and are
			// written directly into the node's UPROPERTY at activation time via
			// ApplyDelegateBindings. Nothing to resolve per-frame here.
			break;
		}
	}
}

// ─── Blueprint-callable Pin Value Accessors ─────────────────────────────

bool UComposableCameraCameraNodeBase::GetInputPinValueBool(FName PinName) const
{
	return GetInputPinValue<bool>(PinName);
}

int32 UComposableCameraCameraNodeBase::GetInputPinValueInt32(FName PinName) const
{
	return GetInputPinValue<int32>(PinName);
}

float UComposableCameraCameraNodeBase::GetInputPinValueFloat(FName PinName) const
{
	return GetInputPinValue<float>(PinName);
}

double UComposableCameraCameraNodeBase::GetInputPinValueDouble(FName PinName) const
{
	return GetInputPinValue<double>(PinName);
}

FVector UComposableCameraCameraNodeBase::GetInputPinValueVector(FName PinName) const
{
	return GetInputPinValue<FVector>(PinName);
}

FRotator UComposableCameraCameraNodeBase::GetInputPinValueRotator(FName PinName) const
{
	return GetInputPinValue<FRotator>(PinName);
}

FTransform UComposableCameraCameraNodeBase::GetInputPinValueTransform(FName PinName) const
{
	return GetInputPinValue<FTransform>(PinName);
}

AActor* UComposableCameraCameraNodeBase::GetInputPinValueActor(FName PinName) const
{
	return GetInputPinValue<AActor*>(PinName);
}

FName UComposableCameraCameraNodeBase::GetInputPinValueName(FName PinName) const
{
	return GetInputPinValue<FName>(PinName);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueBool(FName PinName, bool Value)
{
	SetOutputPinValue<bool>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueInt32(FName PinName, int32 Value)
{
	SetOutputPinValue<int32>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueFloat(FName PinName, float Value)
{
	SetOutputPinValue<float>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueDouble(FName PinName, double Value)
{
	SetOutputPinValue<double>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueVector(FName PinName, FVector Value)
{
	SetOutputPinValue<FVector>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueRotator(FName PinName, FRotator Value)
{
	SetOutputPinValue<FRotator>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueTransform(FName PinName, FTransform Value)
{
	SetOutputPinValue<FTransform>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueActor(FName PinName, AActor* Value)
{
	SetOutputPinValue<AActor*>(PinName, Value);
}

void UComposableCameraCameraNodeBase::SetOutputPinValueName(FName PinName, FName Value)
{
	SetOutputPinValue<FName>(PinName, Value);
}

// ─── Blueprint-callable Internal Variable Accessors ─────────────────────

float UComposableCameraCameraNodeBase::GetInternalVariableFloat(FName VariableName) const
{
	return GetInternalVariable<float>(VariableName);
}

FVector UComposableCameraCameraNodeBase::GetInternalVariableVector(FName VariableName) const
{
	return GetInternalVariable<FVector>(VariableName);
}

FName UComposableCameraCameraNodeBase::GetInternalVariableName(FName VariableName) const
{
	return GetInternalVariable<FName>(VariableName);
}

void UComposableCameraCameraNodeBase::SetInternalVariableFloat(FName VariableName, float Value)
{
	SetInternalVariable<float>(VariableName, Value);
}

void UComposableCameraCameraNodeBase::SetInternalVariableVector(FName VariableName, FVector Value)
{
	SetInternalVariable<FVector>(VariableName, Value);
}

void UComposableCameraCameraNodeBase::SetInternalVariableName(FName VariableName, FName Value)
{
	SetInternalVariable<FName>(VariableName, Value);
}

// ─── Enum CustomThunk Helpers ──────────────────────────────────────────
//
// Shared plumbing for the four inline DECLARE_FUNCTION thunks in the header
// (GetInputPinValueEnum / SetOutputPinValueEnum / GetInternalVariableEnum /
// SetInternalVariableEnum). The thunks each step a wildcard FProperty off the
// script stack and must normalize to/from the data block's canonical int64
// representation without caring whether the caller's property is FEnumProperty
// (enum class, underlying width uint8/int32/int64) or FByteProperty
// (TEnumAsByte, always uint8). Mirrors the enum branch of SetParameterBlockValue
// in ComposableCameraBlueprintLibrary.h.

bool UComposableCameraCameraNodeBase::ReadBPEnumPropertyAsInt64(const FProperty* ValueProperty, const void* ValuePtr, int64& OutValue) const
{
	if (!ValueProperty || !ValuePtr)
	{
		return false;
	}
	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(ValueProperty))
	{
		if (const FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty())
		{
			OutValue = Underlying->GetSignedIntPropertyValue(ValuePtr);
			return true;
		}
		return false;
	}
	if (const FByteProperty* ByteProp = CastField<FByteProperty>(ValueProperty))
	{
		if (ByteProp->GetIntPropertyEnum() != nullptr)
		{
			OutValue = ByteProp->GetSignedIntPropertyValue(ValuePtr);
			return true;
		}
	}
	return false;
}

void UComposableCameraCameraNodeBase::WriteEnumInt64ToBPProperty(const FProperty* OutValueProperty, void* OutValuePtr, int64 Value) const
{
	// Delegate to the private-namespace helper that the ResolveAllInputPins /
	// ApplySubobjectPinValues branches already use, so the narrow-cast logic
	// lives in exactly one place.
	ComposableCameraSystem::Private::WriteEnumInt64ToProperty(OutValueProperty, OutValuePtr, Value);
}
