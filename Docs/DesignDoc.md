# ComposableCameraSystem — Design & Technical Reference

*Internal reference for the ComposableCameraSystem UE5.6 plugin.*
*In-design feature spec: [ShotBasedKeyframing.md](ShotBasedKeyframing.md) — Shot-based composition authoring (Composition Solver, Shot Editor, LS Section integration). Phase A (`FComposableCameraTargetInfo` extraction) is the next implementation step; see that doc's §6.*
*Last updated: 2026-05-06 (Twelfth review pass — P0 finished. The eleventh-pass symmetric mirror guard caught the GC-mirror crash path but left wider-T-into-narrower-slot overrun open: `WriteValue<FVector>(FloatPinOffset, V)` (12 B into 4 B), `WriteValue<FTransform>(BoolPinOffset, T)` (64 B into 1 B), `WriteValue<double>(IntVarOffset, D)` (8 B into 4 B) all bypassed the UObject-vs-non-UObject branch (neither side is a UObject pointer or a ref slot) and ran the oversized memcpy past the slot, clobbering adjacent storage. If the clobbered adjacent slot was a ref slot, a later read of that ref slot would return polluted bytes and the IsA check would deref garbage memory. Fix is the proper SlotShape metadata table the reviewer kept asking for: `FComposableCameraRuntimeDataBlock::SlotShapes` is a `TMap<int32, FSlotShape{PinType, Size, StructType}>` populated at `AllocateSlot` time for every byte-storage slot AND every struct-slot synthetic offset. `ReadValue<T>` and `WriteValue<T>` look up the shape, compare against compile-time `ExpectedPinTypeFor<T>()` (template-dispatched mapping from C++ type to `EComposableCameraPinType`), exact `sizeof(T)`, and exact `UScriptStruct` identity for struct slots, and refuse on any mismatch — unknown offset, type mismatch, size mismatch, or same-size cross-struct mismatch all return `T{}` (read) / no-op (write) before any memcpy or `CopyScriptStruct`. This subsumes the eleventh-pass mirror-direction check, which is now removed (redundant). Compile-time `ExpectedPinTypeFor` keeps the dispatch zero-cost on the type-match fast path; the runtime cost is one TMap::Find per templated access. The remaining P2 limitation in §7.5 ("Per-offset SlotSize table not yet tracked") is closed.) Prior: 2026-05-06 (Eleventh review pass — P0 follow-up, symmetric slot-shape guard. The tenth-pass guard only refused UObject-pointer T writes/reads against non-ref slots — the reverse direction stayed open: a `WriteValue<float>(actor_slot_offset, ...)` (e.g. `SetOutputPinValueFloat("ActorPinName")` typo) memcpy'd 4 bytes into the 8-byte Actor slot, then `RefreshReferenceSlot` read the mixed `float|leftover` 8 bytes back and registered the resulting fake pointer with the GC mirror. Next GC sweep dereferenced the bogus pointer → crash. Fix is symmetric: when T is NOT a UObject pointer, refuse the call if the offset IS in either reference-slot mirror. Combined with the existing UObject-T-into-non-ref-slot refusal, every cross-shape access is now blocked at the data-block boundary. Larger-struct-into-smaller-slot overruns (e.g. `WriteValue<FVector>` into a Float slot) are still possible — would require an explicit slot-size table; left as a follow-up since the GC-mirror crash path was the immediate risk.) Prior: 2026-05-06 (Tenth review pass — P0 slot-shape guard + P1 details delegate weak-capture. **P0**: `RuntimeDataBlock::ReadValue<T>` and `WriteValue<T>` for UObject-derived pointer T now refuse offsets that aren't registered as Object/Actor reference slots, BEFORE doing the byte-storage memcpy. The previous IsA-only guard (ninth pass) had a hole: a wrong-type access (e.g. `GetInputPinValue<AActor*>("ExistingFloatPinName")`) memcpy'd 8 bytes from a 4-byte float slot, reinterpreted the random adjacent bytes as a UObject pointer, and the IsA virtual call on that random bit pattern dereferenced garbage memory. Fix uses the existing `ActorReferenceSlots` / `ObjectReferenceSlots` mirror maps — populated by `RegisterReferenceSlot` at layout time for every Object/Actor pin slot — as the slot-shape oracle. Read: refuse if `!ActorReferenceSlots.Contains(Offset) && !ObjectReferenceSlots.Contains(Offset)`. Write: same predicate. Both branches are `if constexpr`-gated so primitives pay zero overhead. **P1**: 4 property-change delegate captures inside `FComposableCameraNodeGraphNodeDetails::CustomizeDetails` now use weak captures (`TWeakObjectPtr<UComposableCameraNodeGraphNode>` for the GraphNode, `TWeakPtr<IPropertyUtilities>` for the layout-refresh hook) instead of raw `this` / `&DetailBuilder`. The Details panel can tear down its `IDetailLayoutBuilder` and the customization while the underlying property handle survives (selection change + refresh cascade); a property edit then fires the lambda against a freed customization or a dangling `IDetailLayoutBuilder` reference. Switch from `IDetailLayoutBuilder::ForceRefreshDetails()` to `IPropertyUtilities::ForceRefresh()` — the utilities object lives at the SDetailsView level and outlives the layout. Widget-level lambdas (`IsChecked_Lambda` / `Text_Lambda` / etc.) keep `this` capture: their lifetime is tied to the widgets which are torn down with the customization, so they cannot fire post-destruction.) Prior: 2026-05-06 (Ninth review pass — one new P1 fix + rollback of one prior P2 misfire. **P1 (new)**: `RuntimeDataBlock::ReadValue<T>` for UObject-derived pointer T now performs a final `IsA(T::StaticClass())` check on the read pointer before returning. The eighth-pass `AssignObjectPropertyChecked` helper protected the auto-resolve UPROPERTY-write path, but `GetInputPinValue<T>` calls `TryResolveInputPin<T>` which calls `ReadValue<T>` — a node author writing `GetInputPinValue<UCurveFloat*>` directly bypassed the write-side guard entirely. With the data block storing every Object/Actor pin class-erased, a stale asset / hand-edited connection / Blueprint wildcard could deliver a wrong-class instance into the slot; the explicit-template-read path then memcpy'd the pointer bytes and returned them as the requested type, and the caller's typed deref read a vtable / fields of the wrong layout. Fix: `if constexpr` branch on `std::is_pointer_v<T> && std::is_base_of_v<UObject, std::remove_pointer_t<T>>` runs `IsA` after the byte-storage memcpy and returns `nullptr` on mismatch. Compile-time gate keeps non-UObject T (float / FVector / FName / int64) at zero overhead. **P2 (rollback)**: the eighth-pass dirty-sorted-cache for `LSComponent::ApplySequencerPatchOverlays` had a real correctness regression — TrackInstance OnAnimate calls `SetSequencerPatchOverlay` every frame on every active section's existing key, so `bWasNewEntry==false` left the cache un-dirtied. Designer-driven LayerIndex changes (which take the same per-frame Set path with no add/remove) therefore never invalidated the cache, leaving the order stale indefinitely. The original always-rebuild form is restored: per-frame snapshot + sort with a `TInlineAllocator<8>` keeps the typical small-N case stack-resident at sub-µs cost. The optimization wasn't worth the extra invariant. `SortedSequencerPatchOverlayKeys` member and `bSequencerPatchOverlaysSortDirty` flag removed.) Prior: 2026-05-06 (Eighth review pass — P0 + P1 + P2 follow-ups. **P0**: Object/Actor pin auto-resolve writes now verify the resolved value satisfies the target FObjectProperty's class constraint before assigning. The auto-resolve pipeline collapses every object-class pin into the generic `Actor`/`Object` `EComposableCameraPinType` for storage, so the runtime data block has no record of the original `PropertyClass` — a stale asset / hand-edited connection / BP wildcard could deliver a `UCurveVector` to a `TObjectPtr<UCurveFloat>` field, then the typed C++ access on the receiving node reads bytes of the wrong type and crashes. New `AssignObjectPropertyChecked` helper at file scope wraps the four object-write call sites in `ResolveAllInputPins` / `ApplySubobjectPinValues` (top-level + subobject × Actor + Object) — verifies `V->IsA(PropertyClass)` for plain `FObjectProperty` and `Cast<UClass>(V)->IsChildOf(MetaClass)` for `FClassProperty`. Mismatch → log + write `nullptr` (the field's documented unset state). The deeper class-constraint propagation through pin metadata + layout-phase validation remains a P2 follow-up; the runtime guard closes the actual exploit. **P1**: `ApplyDelegateBindings` now verifies the source `FScriptDelegate`'s bound function has a signature compatible with the target `FDelegateProperty::SignatureFunction` before assigning. `FScriptDelegate` carries only `(UObject*, FName)` — no signature record — so a stale BP / mistyped C++ caller could install a delegate whose `UFunction` has a different parameter layout than the target. The eventual `Execute` call would then walk the wrong parameter frame: `ProcessEvent` reads garbage args / corrupts callee stack / asserts. New per-binding lambda inside `ApplyDelegateBindings` resolves `SourceDelegate.GetUObject()->FindFunction(SourceDelegate.GetFunctionName())`, calls `SourceFunc->IsSignatureCompatibleWith(DelegateProp->SignatureFunction)`, and on mismatch logs + leaves the target unbound (also unbinds any stale binding from a previous activation). Empty source delegates pass through (the documented "unbind" idiom). **P2**: `LSComponent::ApplySequencerPatchOverlays` now uses a dirty-sorted-cache instead of rebuilding+sorting `SortedKeys` every frame. New `SortedSequencerPatchOverlayKeys` member + `bSequencerPatchOverlaysSortDirty` flag — Set on a NEW key marks dirty, per-frame Set on an existing key (the typical TrackInstance OnAnimate path) leaves cache valid; `RemoveSequencerPatchOverlay` and the in-Apply prune sweep also mark dirty. Steady-state per-frame cost drops to one TArray walk + zero allocations / sort. Trade-off: a designer toggling LayerIndex on a section already in the active set carries one frame of stale order until the next add/remove; rare in PIE and the alternative is paying full snapshot+sort every frame for every active overlay. The `DestroyInternalCamera` cleanup path also resets the cached sorted array to keep the cache in sync with the wipe.) Prior: 2026-05-06 (Seventh review pass — one P0 + three P2 fixes. **P0**: `LSComponent::SequencerShotOverrides` had the same TMap-with-TWeakObjectPtr-key reflection problem as `SequencerPatchOverlays` (which was fixed in the fourth pass) but had been left UPROPERTY-tagged. UE 5.6's UHT either errors on the construct or silently drops UObject reflection inside the entries — either way the inner `EnterTransition` TObjectPtr and the `Shot`-embedded actor refs were not GC-walked. Fix mirrors the patch overlay pattern: drop the UPROPERTY tag, walk each entry's USTRUCT graph manually in the existing `LSComponent::AddReferencedObjects` via `AddPropertyReferencesWithStructARO(FComposableCameraSequencerShotEntry::StaticStruct(), &Pair.Value)`. **P2 (1)**: `CollisionPushNode::ResolvedActorsToIgnore` and `DynamicDeocclusionTransition::ResolvedActorsToIgnore` are non-UPROPERTY UObject-pointer arrays on UCLASS members. Even though both are rebuilt from a TWeakObjectPtr snapshot at the start of every trace/Evaluate, the previous code left raw `AActor*` entries live across frames — invisible to GC, capable of dangling when an ignored actor was destroyed between frames. Both functions now `Reset()` the array before returning so the slot is empty whenever execution is outside the function and GC walks see no stale raw pointers. Capacity preserved; same hot-path semantics. **P2 (2)**: Three `TRACE_CPUPROFILER_EVENT_SCOPE_STR` callsites that allocated FStrings on the per-frame hot path are now caching the resolved label off the hot path. `AComposableCameraCameraBase::TickCamera` reads a new `CameraTagTraceName` populated at `Initialize`; `UComposableCameraTransitionBase::Evaluate` lazy-caches `TransitionClassTraceName` on first evaluate (immutable per instance); `UComposableCameraPatchManager::Apply` reads `Instance->PatchAssetTraceName` populated at AddPatch. Each was an FString-per-frame heap allocation in steady state; cumulative cost scaled with active camera count × tick rate. Insights timeline labels unchanged.) Prior: 2026-05-06 (Sixth review pass — three P2 fixes that close out the previously-deferred items. (1) `UMovieSceneComposableCameraShotSection` now caches the resolved `ShotAssetRef` and `EnterTransition` hard pointers in `Transient` `TObjectPtr` slots populated at `PostLoad` and `PostEditChangeProperty` (off the eval path, where blocking `LoadSynchronous` is acceptable). The eval-path readers `ResolveActiveShot` and `BuildEffectiveShot` now go through `ResolveCachedShotAsset` / `ResolveCachedEnterTransition`, which read the cache and fall back to a free `TSoftObjectPtr::Get()` lookup when the cache is null but the asset is already loaded — never blocking on a synchronous load on the game thread mid-frame. The TrackInstance also reads the cached transition directly. Closes the "Sequencer playback / scrubbing stalls when an asset is unloaded" failure path. (2) PCM `UpdateActions` no longer allocates a fresh `TSet` every tick. Replaced with a member-scoped `TArray<UComposableCameraActionBase*> CameraActionsRemovalScratch` reused via `Reset()` (preserves capacity); set semantics weren't load-bearing because the source TSet already guarantees no duplicates. Steady-state operation is allocation-free; previously even an empty action list paid one node-allocator hit per frame, and a populated list paid more plus rehash risk. (3) `UComposableCameraCollisionPushNode` no longer calls `GetAllActorsOfClass` per Tick. The ignore list is now snapshotted once at OnInitialize into `ActorsToIgnoreWeak: TArray<TWeakObjectPtr<AActor>>` and rebuilt per-Evaluate into the member-scoped `ResolvedActorsToIgnore: TArray<AActor*>` — same shape as the DynamicDeocclusion fix in the fourth pass. World-actor-count scaling on the trace hot path replaced by O(N_ignored) weak-ptr resolves; ignored actors that get destroyed mid-camera-life drop out silently rather than dangling. Trade-off: dynamically-spawned ignore-class actors created AFTER OnInitialize won't be in the list — acceptable for the standard "ignore the player capsule / specific level geo" use cases that drove this feature, and the snapshot can be refreshed by re-activating the camera if a designer needs it.) Prior: 2026-05-06 (Fifth review pass — two more P1 fixes (SkelMesh-cache pattern). `UComposableCameraCollisionPushNode` and `UComposableCameraReceivePivotActorNode` both cached `USkeletalMeshComponent*` as raw non-UPROPERTY pointers resolved once at OnInitialize, identical failure-mode to the LookAtNode case fixed in the previous pass: PivotActor is an input pin and can change every frame, the SkelMesh on that actor can be destroyed / re-spawned independently, and the raw cached pointer dangles. Both nodes migrated to `TWeakObjectPtr<USkeletalMeshComponent>` plus a `TWeakObjectPtr<AActor> LastResolvedPivotActor` cache key, with a per-node `ResolveSkelMeshFor*` namespace helper (cpp-local) doing lazy re-resolution in Tick when the active PivotActor differs from the last-resolved-against actor — common stable-actor case is a single weak-ptr equality check, no per-frame `GetComponentByClass` walk. `DrawNodeDebug` reads the cached weak ptr (cannot refresh — `const` method); stale-cache windows are bounded by one tick which is acceptable for a debug gizmo. Two further review items reiterated as already documented under TechDoc §7.5: Sequencer Shot section / EnterTransition `LoadSynchronous` on the eval path, and the per-frame allocation hot-spots in UpdateActions / Patch overlay sort / CollisionPushNode `GetAllActorsOfClass` / PatchManager trace-scope FString. Both deferred to a focused profiling-driven pass per the existing P2 doc entry.) Prior: 2026-05-06 (Fourth review pass — six fixes plus two acknowledged limitations. **P0 (3 critical correctness fixes):** (1) Object/Actor pin auto-resolve now writes via `FObjectPropertyBase::SetObjectPropertyValue` instead of `*static_cast<AActor**>(ValuePtr) = V`. The raw cast bypassed TObjectPtr storage conversion (the field is declared `TObjectPtr<AActor>` per project rule, layout may differ from raw `AActor*` in some configurations) and the GC integrity-token bookkeeping (UE_GC_TRACK_OBJ_AVAILABLE in dev builds). Four sites fixed: top-level + subobject paths × Actor + Object pins, in `UComposableCameraCameraNodeBase::ResolveAllInputPins` / `ApplySubobjectPinValues`. (2) `FComposableCameraParameterBlock::CopyRawTo` now requires exact-size + matching PinType. Old signature accepted `Found->Data.Num() <= DestSize` and silently memcpy'd whatever bytes were available — a stale row entry holding 4 B of Float Data under a name now bound to an 8 B Actor target slot left the upper 4 bytes whatever was already there, then `RefreshReferenceSlot` reinterpreted the 8-byte result as `AActor*` and registered a fake pointer with the GC mirror → next sweep crash. New signature requires `Found->PinType == ExpectedPinType && Data.Num() == DestSize`; cross-shape entries get a clean miss and the destination's prior zero-init keeps the slot empty rather than half-populated. Three call sites updated to pass the expected PinType. (3) Cached pose UObject references are now GC-walked. `FComposableCameraPose` carries an `FPostProcessSettings` member containing TObjectPtr references to materials / textures / WeightedBlendables; when the pose lives in a non-UPROPERTY field — leaf wrapper `CachedPose` (RefLeaf only — regular Leaf has no cache), inner wrapper `CachedBlendedPose`, director's `LastEvaluatedPose` / `PreviousEvaluatedPose`, context-stack entry `LastPose` — UE's reflection-based GC walk doesn't surface those refs and a material that's only referenced through a cached pose can be collected mid-frame, leaving a dangling TObjectPtr behind. Three places now walk the pose USTRUCT via `Collector.AddPropertyReferencesWithStructARO(FComposableCameraPose::StaticStruct(), &Pose)`: `UComposableCameraEvaluationTree::AddTreeReferencedObjects` (RefLeaf + Inner cached poses), new `UComposableCameraDirector::AddReferencedObjects` (LastEvaluatedPose + PreviousEvaluatedPose), and `UComposableCameraContextStack::AddReferencedObjects` (Entry.LastPose for both Entries and PendingDestroyEntries). **P1 (3 robustness fixes):** (4) `UComposableCameraLookAtNode::SkeletalMeshComponentForLookAtActor` migrated from raw `USkeletalMeshComponent*` to `TWeakObjectPtr<USkeletalMeshComponent>` with lazy re-resolution in Tick keyed on `LastResolvedLookAtActor`. The actor can now change every frame via input pin without paying a per-frame `GetComponentByClass` walk in the common case (actor stable), and a destroyed mesh component cannot dangle. (5) `UComposableCameraDynamicDeocclusionTransition::ActorsToIgnore` migrated from raw `TArray<AActor*>` to `TArray<TWeakObjectPtr<AActor>> ActorsToIgnoreWeak` with a `TArray<AActor*> ResolvedActorsToIgnore` rebuilt each Evaluate from the weak snapshot. Multi-frame transitions can now run while ignored actors are destroyed mid-blend without dangling pointers; the OnBeginPlay path also gained a `Reset()` so re-used transitions don't accumulate stale entries across activations. (6) `UComposableCameraLevelSequenceComponent::SequencerPatchOverlays` map key migrated from `TObjectPtr<UMovieSceneComposableCameraPatchSection>` to `TWeakObjectPtr<>` so a stale section that's been GC'd actually goes stale — a strong-ref key was keeping every Sequencer-side patch section alive forever, defeating the prune-on-tick path that exists precisely to clean up overlays whose source section has been destroyed. The TMap can no longer be UPROPERTY-tagged (TWeakObjectPtr keys are not reflectable), so a new `UComposableCameraLevelSequenceComponent::AddReferencedObjects` override walks each overlay's `Evaluator` actor and the UObject contents of `LatestParameters` (a `FComposableCameraParameterBlock`) explicitly via `AddPropertyReferencesWithStructARO`. The prune loop also rewrote to operate on weak keys end-to-end — passing `Pair.Key.Get()` then `Find(nullptr)` would have constructed a different-hash key and missed stale entries entirely. **P2 (acknowledged, deferred):** (7) Sequencer eval path's `LoadSynchronous` calls in Shot section / track instance during eval can stall the game/editor thread when an asset hasn't fully loaded; not fixed in this pass — proper fix requires moving the load to `PostLoad` / `PostEditChangeProperty` / track-input-added paths and caching `TObjectPtr` at that point. Tracked as known limitation in TechDoc §7.2. (8) Per-frame allocation hot spots (UpdateActions's `TSet ActionsToRemove` rebuild, Patch overlay's `SortedKeys` snapshot per frame, CollisionPushNode's `GetAllActorsOfClass`, PatchManager's `TRACE_CPUPROFILER_EVENT_SCOPE_STR(*Asset->GetName())` FString allocation) — each call site is independently small and the hot-path "no allocation" rule is project-wide, but a focused profiling-driven pass is the right fix shape rather than a speculative scrub here. Tracked as known limitation in TechDoc §7.2.) Prior: 2026-05-06 (Third review pass — two P0 hardening fixes. (1) `IsBytewiseSafeStruct` simplified to a strict whitelist + `STRUCT_IsPlainOldData` opt-in. The previous reflection-walk + sanity-check approach could not see non-UPROPERTY native C++ members of a USTRUCT, so a struct with even one UPROPERTY plus a hidden `FString` / `TArray` / `UObject*` could pass the walk and reach byte-array memcpy — shallow copy + dtor leak + GC blindness. The trailing-padding sanity check failed to catch the case when the hidden member's size was absorbed by alignment slack. New rule: only engine math types (FVector / FRotator / FTransform / FFloatInterval / FVector2D / FVector4) and structs whose author opted into `STRUCT_IsPlainOldData` via `template<> struct TStructOpsTypeTraits<MyStruct> : TStructOpsTypeTraitsBase2<MyStruct> { enum { WithIsPlainOldData = true }; };` are accepted; everything else routes through `FInstancedStruct` / the typed slot pool. The performance cost (one heap allocation per non-POD slot at activation + slightly slower per-frame `CopyScriptStruct`) is dwarfed by the safety win — and any user who wants bytewise transport can opt in with one line of trait code. Reflection walk + sanity heuristics removed entirely. (2) `SetVariable` exec entries now also validate the SERIALIZED `Entry.VariableSlotSize` against the variable's CURRENT type-derived size. Type-only validation didn't catch the case where source and variable types both happen to match now (e.g., both Float) but `Entry.VariableSlotSize` was recorded back when the variable was a different type (e.g., 48-byte Transform) and the editor sync didn't refresh the entry. Runtime would memcpy the stale large size from a small source slot — overflow into adjacent storage corrupts the next slot, including any `ActorReferenceSlots` / `ObjectReferenceSlots` GC mirror bytes that happen to live there. Fix: in `BuildRuntimeDataLayout`'s `ValidateSetVariableEntries`, after the `ArePinTypesCompatible` check passes, also compare `Entry.VariableSlotSize` against `GetVariableSlotSize(Var->VariableType, Var->StructType)`. Mismatch → invalidate the entry via the existing `InvalidSetVariable[Compute]ExecEntries` set. Log message asks the user to re-save the asset to refresh.) Prior: 2026-05-06 (Two follow-up fixes from second review pass. (1) `IsBytewiseSafeStruct` regression — the `ReflectedCount == 0` branch of the trailing-hidden-member sanity check was a no-op, so a USTRUCT with literally no UPROPERTY but a non-trivial sizeof slipped through. Concrete case: `USTRUCT() struct FBad { GENERATED_BODY(); FString S; };` — `S` is not UPROPERTY, the reflection walk sees zero properties and the previous code skipped the size check entirely, returning true. Downstream then memcpy'd FString bytes (shallow copy, dtor leak, GC blindness). Fix: when ReflectedCount==0, accept only if `GetStructureSize() == 1` (the C++ "every type has a unique address" minimum for genuinely empty structs). Anything larger is rejected. (2) `SetVariable` exec entries gained type validation. The earlier Phase 2 type-validation pass covered wired connections, exposed parameters, and variable getters but missed the SetVariable exec chain — runtime SetVariable handlers (camera + compute) would still call `CopySlot(SourceOffset, VarOffset, VariableSlotSize)` for stale entries whose source-pin type no longer matched the variable's type. The POD branch of CopySlot reads `VariableSlotSize` bytes from `SourceOffset` regardless of how many bytes actually live in the source slot, so a Float source (4B) wired to an Actor variable (8B target slot) memcpy'd 4 bytes past the float slot, then `RefreshReferenceSlot` reinterpreted the resulting 8 bytes as `AActor*` and registered the garbage pointer with the GC mirror — next GC sweep crash. Fix: `BuildRuntimeDataLayout` Phase 2 now walks `FullExecChain` + `ComputeFullExecChain`, validates each SetVariable entry's source pin type against its target variable type via the same `FindPinDecl` + `ArePinTypesCompatible` helpers used by the connection passes, and records the indices of failing entries into two new sets on the data block: `InvalidSetVariableExecEntries` and `InvalidSetVariableComputeExecEntries`. Runtime SetVariable handlers (both chains) now walk indexed and skip any entry whose index is in the corresponding set — one `TSet::Contains(int32)` per entry per tick on top of the existing early-out checks. Variable lookup is by name (matching the runtime InternalVariableOffsets keying) and walks InternalVariables before ExposedVariables. Activation-time validation only — no runtime cost beyond the empty-set hashed-bucket fast path on assets with no broken entries.) Prior: 2026-05-06 (`BuildRuntimeDataLayout` Phase 2 — every Add into `InputPinSourceOffsets` / `ExposedInputPinOffsets` is now type-validated against BOTH source and target pin declarations. The previous behavior validated only that the source offset existed in `OutputPinOffsets`, so a stale asset (saved before a pin was renamed / retyped), a hand-edited asset, or any future schema-bypass code path could wire a Float source into an Actor target — the runtime would then read sizeof(AActor*) bytes from a 4-byte float slot and dereference garbage as a UObject pointer, crashing inside `CopySlot`'s struct/POD discrimination check or at the first AActor::* call. Validation covers all four connection paths: (1) wired connections from camera-chain `PinConnections`, (2) wired connections from `ComputePinConnections` (compute chain), (3) `ExposedParameters` exposure (parameter mirror's PinType+StructType+EnumType vs target node's current pin declaration — catches "parameter exposed before pin retyped in C++"), (4) variable-getter connections in `VariableNodes` (variable's VariableType+StructType+EnumType vs consumer pin declaration — catches "variable retyped after Get node was wired"). Per-node pin declarations are cached via a `TMap<int32, TUniquePtr<TArray<FComposableCameraNodePinDeclaration>>>` keyed on the runtime NodeIndex space (NodeTemplates < ComputeNodeIndexBase ≤ ComputeNodeTemplates) so a high-pin-density graph doesn't repeatedly call `GatherAllPinDeclarations`. The TUniquePtr indirection is load-bearing: TMap::Add can rehash and move its values, but the heap-allocated TArray that TUniquePtr owns stays put, so a `const FComposableCameraNodePinDeclaration*` returned by FindPinDecl is stable across subsequent Adds. Type compatibility predicate compares PinType exactly; Struct pins additionally require matching StructType; Enum pins additionally require matching EnumType. Mismatches log a clear warning naming both endpoints and skip the Add — runtime then falls through to the pin's class-level default rather than reading garbage. Variable getter loop also gained a name-based fallback when the GUID resolution fails (legacy records with lost GUID), so type validation still gets the variable's metadata even on the fallback path.) Prior: 2026-05-06 (`IsBytewiseSafeStruct` — trailing-hidden-member sanity check added. After the existing engine-math fast-path, STRUCT_IsPlainOldData check, and reflection walk all accept a struct, the function now also compares `Struct->GetStructureSize()` against the layout end of the last reflected UPROPERTY (`max(offset + size)` padded up to the struct's natural `GetMinAlignment`). If the struct's actual size exceeds that padded end, there is a non-UPROPERTY tail member the reflection walk cannot see — reject. Catches the "USTRUCT with hidden FString / TArray / smart-ptr appended after the last UPROPERTY" authoring mistake (the most common shape of the issue). Hidden leading or interior non-UPROPERTY members are NOT caught — the compiler-generated layout shifts visible-member offsets to accommodate them, so reflection sees consistent offsets without obvious gaps. The canonical defense for those cases is the standard UE convention "every USTRUCT member must be UPROPERTY", required anyway for serialization / replication / our own parameter system to function correctly. Function header doc updated to call out the limitation explicitly.) Prior: 2026-05-06 (Defensive batch — four small parameter / node hardening fixes. (#4 K2 wildcard CustomThunk: `SetParameterBlockValue`'s Object/Actor branch now rejects `FSoftObjectProperty` / `FWeakObjectProperty` / `FLazyObjectProperty` via explicit `IsA<FObjectProperty>()` / `IsA<FClassProperty>()` filter — the previous `FObjectPropertyBase` cast accepted all four, then called `GetObjectPropertyValue` which resolves the soft/weak/lazy ptr to a raw UObject* at the moment of the call, stripping the soft semantics or writing a dangling ptr if the target was unloaded. Rejection throws a script exception with a clear message + `return`s to skip the empty-Entry `StoreValue` fallback that would otherwise commit garbage. Mirrors `TryMapPropertyToPinType`'s same rejection so authoring-time pin discovery and runtime CustomThunk dispatch agree.) (#6 `RelativeFixedPoseNode`: `RelativeActor` UPROPERTY migrated to `TObjectPtr<AActor>` per project rule; cached `SkeletalMeshComponentForRelativeActor` migrated from raw non-UPROPERTY pointer to `TWeakObjectPtr<USkeletalMeshComponent>` since the actor / mesh component can be destroyed or re-spawned mid-run independently of this node. Tick / DrawNodeDebug now `IsValid()`-check both pointers before deref. Header gains forward decl of USkeletalMeshComponent; cpp moves the SkeletalMeshComponent include outside `#if !UE_BUILD_SHIPPING`.) (#10 `FComposableCameraParameterBlock::SetStruct` early-return paths now wipe stale entries via a new public `RemoveValue(FName)` helper. Both rejection paths (null Struct/Memory caller-error AND CCS infrastructure-type defense against the BP wildcard mis-typing bug) previously left any prior POD / actor / object / delegate value live under the same name, so a downstream `Get<T>` would return stale data the caller intended to replace. The success path also routes through `RemoveValue` so the parallel-map clear is uniform. `RemoveValue` is public for callers that want an explicit "drop everything under this name" affordance.) (#11 DataTable activation override merge: `Params.DelegateValues.Add(...)` → `Params.SetDelegate(...)` so a delegate override on a DataTable-driven activation correctly clears any same-name POD / struct / actor / object value parsed from the row's text content. Previously the raw `.Add` left those parallel-map entries live; downstream HasValue / Get<T> by name could read a stale value the row authored before the delegate override took effect.) Prior: 2026-05-06 (RuntimeDataBlock + SetVariable struct-slot consistency. Three coupled fixes that round out the non-POD struct pin support landed 2026-05-05: (1) `FComposableCameraRuntimeDataBlock::IsValid` now accepts `StructSlots.Num() > 0` as a valid state — assets whose entire exposed surface is non-POD struct (every parameter / variable / output pin a USTRUCT containing FString / TArray / object refs) have empty byte `Storage` and `TotalSize=0`, but they are still meaningfully populated via the typed slot pool. The byte-pool-only check would mark such an asset invalid and cause debug introspection / FullExecChain dispatch to silently fall back to legacy paths. (2) `ZeroInitialize` now also resets each `StructSlots[i]` via `InitializeAs(SameType)` (destroys + default-constructs in place, preserving slot identity so existing offset tables stay valid; only values reset). Currently load-bearing only at allocation time where the existing `Storage.SetNumZeroed` + `RegisterStructSlot` loop already produce this state — but any future re-init caller (data-block reuse across reactivations, pooled allocators) would otherwise observe stale heap-owned struct state. (3) `FComposableCameraExecEntry::VariableSlotSize` gains a sentinel value `StructSlotSentinel = INT32_MAX` meaning "the variable lives in StructSlots; byte size does not apply". New helper `GetVariableSlotSize(PinType, StructType)` next to `GetPinTypeSize` returns the sentinel for non-POD struct, real bytes for POD. Editor-side `BuildVariableLookup` routes through it. Without the sentinel, `GetPinTypeSize` returned 0 for non-POD struct → editor wrote 0 → runtime SetVariable handler's `<= 0` early-out silently swallowed every Set on a non-POD struct variable. The sentinel passes the early-out; runtime then dispatches via `RuntimeDataBlock::CopySlot`, which keys on offset storage class (`IsStructSlotOffset`) and ignores the byte-size argument when the struct branch fires. Source/target storage-class mismatch is caught by CopySlot's `check(bSourceIsStruct == bTargetIsStruct)` — the editor's schema should prevent this, the assert is a runtime backstop.) Prior: 2026-05-06 (PathGuidedTransition lifecycle hardening. Three coupled fixes in one pass: (1) Rail validation moved up-front via `ResolveAndValidateRail` — `RailActor` is now sync-loaded (`LoadSynchronous`), and the function rejects null/unloaded rail, missing `RailSplineComponent`, or zero-point splines BEFORE any actor is spawned. The previous behavior spawned `IntermediateCamera` / `DebugSplineActor` first and then crashed inside `BuildInternalSpline`'s unguarded `Points[0]` access on bad data, leaving a half-constructed actor in the level. On validation failure the transition leaves `Rail = nullptr` and the existing `OnEvaluate` nullcheck hard-cuts to the target pose. (2) Spawned-actor cleanup unified — both `IntermediateCamera` (Inertialized) and `DebugSplineActor` (Auto) are now destroyed by a single `DestroySpawnedActors` helper invoked from two complementary paths: an `OnTransitionFinishesDelegate` lambda registered ONCE in `OnBeginPlay` immediately after spawn (covers the normal completion path regardless of whether `ExitTransition` was lazily constructed), and a `BeginDestroy` override (covers interrupted-mid-blend cases — camera destroyed, eval tree pruned, transition replaced before completion — where the delegate would never fire). The cleanup helper is `IsValid()`-guarded so the two paths can both run idempotently. The earlier code only registered a destroy lambda from inside the `ExitTransition` lazy-construction branch in `OnEvaluate`, so any GuideRange.Y >= 1 case or any `Type=Auto` case leaked the spawned actors. (3) All UPROPERTY UObject pointers on the transition (`DrivingTransition` / `IntermediateCamera` / `Rail` / `EnterTransition` / `ExitTransition` / `InternalSpline` / `DebugSplineActor` / `SplineMoveCurve`) migrated from raw pointer to `TObjectPtr<T>` per the project rule. New gotcha entry in TechDoc §7.2 documenting the validate-first / register-on-spawn / BeginDestroy-backstop / idempotent-helper pattern as the generic rule for any future transition that spawns world objects.) Prior: 2026-05-05 (Non-POD struct pin support landed -- the runtime data block now holds two parallel storage pools: byte `Storage` for POD pin values and a typed `StructSlots` array of `FInstancedStruct` for any USTRUCT containing FString / FText / TArray / TMap / TSet / object refs / interfaces / delegates anywhere in the property graph. Offsets >= `StructSlotsOffsetBase` (INT32_MAX/2) discriminate the pools; templated `ReadValue<T>` / `WriteValue<T>` dispatch via `if constexpr (TModels_V<CStaticStructProvider, T>)` plus a runtime offset check, so node call sites stay unified. ParameterBlock gains a parallel `TMap<FName, FInstancedStruct> StructValues` and a `SetStruct` setter; the K2 ActivateComposableCamera CustomThunk routes non-POD struct override pins through it. ApplyParameterBlock dispatches `CopyParamIntoSlot` / `SeedFromInitialValue` per role. Auto-resolve UPROPERTY shadow for non-POD struct uses `Property->CopyCompleteValue` (per-property operator= via FProperty graph); steady-state per-frame copy is no-alloc when embedded FString members fit existing capacity, with a bounded one-time alloc per member at first tick or content grow. Section 5 "Two Parameter Types" expanded with the POD-vs-typed dispatch contract. Prior: 2026-04-30 (Polish P.1 + P.2 batch.) (P.1 — `RefreshAutoBoundsCache` no longer calls `FindComponentByClass<USkeletalMeshComponent / UStaticMeshComponent>` per tick. Cached `mutable TWeakObjectPtr<UPrimitiveComponent> CachedBoundsMeshComponent` on `FComposableCameraShotTarget` (Transient UPROPERTY) holds the resolved component; revalidation is an O(1) weak-ptr `.Get()` + `GetOwner() == Actor` consistency check. Only first resolve and owner-swap recovery walk the component list. With `Live` cache policy + 4-target Shot at 60fps, eliminates ~240 component-list walks/sec on character actors with ~30 components each — the dominant per-tick cost outside the solver math itself. Other potential P.1 micro-targets (FPostProcessSettings copy in `ApplySolverResultToPose`'s `OutPose = UpstreamPose`, per-frame `GetEffectiveFieldOfView` resolve) deferred — would benefit from real profiler data first.) (P.2 — `BuildEffectiveShotForPreview` cache. The Shot Editor viewport client previously did a full `*ActiveShot` value copy + per-target Sequencer-binding override resolution per call (5+ calls per tick: HUD, 3D wire BBs, handles, hover tooltip, solver run). The override resolution dominates. Added `mutable FComposableCameraShot CachedEffectiveShot` + `bEffectiveShotCacheValid` / `bEffectiveShotCacheBuiltOk` flags on the viewport client, invalidated at the start of each `Tick(DeltaSeconds)`. First call per frame does the full work into the cache; subsequent calls within that frame skip override resolution and just memcpy out. Per-call struct copy (TArray heap alloc for Targets) is unchanged because callers own `OutShot` and may mutate it (`Draw()` does `RefreshAutoBoundsCache` per target on the local copy). Net: 5x fewer override-resolution passes per frame for typical Shot Editor scenes.) Prior: 2026-04-30 (Polish T.3 + P.3 + D.2 + T.4 batch.) (T.3 — TechDoc §3.25 / §3.26 gain ASCII data-flow diagrams: Composition Solver pipeline (Shot → SolvePlacement → SolveAim → SolveLens → SolveFocus → FShotSolveResult) and CompositionFramingNode tick flow (LSComponent push → SetActiveShotsFromSequencer → OnTickNode → ApplySolverResultToPose → optional Phase F secondary blend → OutPose). Single-page visual aids next-to existing prose, no behavior change.) (P.3 — Picard solver tuning exposed as `UComposableCameraProjectSettings` UPROPERTYs: `PicardMaxIterations` (default 16), `PicardConvergenceTolerance` (cm, default 0.01), `PicardRelaxation` (default 0.7). `SolveAnchorAtScreen` reads them once per call into locals — no per-iteration `GetDefault<>` cost. Projects with unusual scale ranges can retune without recompile; defaults preserve V2.1 hardening behavior.) (D.2 — Smarter Picard initial seed in `SolveAnchorAtScreen`. Old seed `OutCamPos = PlacementAnchor - Distance * InitForward` ignored authored `Placement.ScreenPosition` and started from a "PlSP centered" assumption. New seed pre-shifts laterally / vertically along tentative cam axes (`InitRight = WorldUp × InitForward`, `InitUp = InitForward × InitRight`) by `-PlSP_pre.X * 2·TanH·Distance` and `-PlSP_pre.Y * 2·TanV·Distance` — matches the lateral offset the iteration's own step uses internally, projected through tentative axes instead of converged-rotation axes. Reduces typical iter count by ~1-2 and rescues a class of off-center stress cases that previously hit `MaxIters` and hard-failed.) (T.4 — New `Docs/ExecutionFlowExamples.md` with §1 V2 OTS shot end-to-end — Hero (Placement) + Villain (Aim) over-the-shoulder, traced from designer authoring through Sequencer track instance, BuildEffectiveShot, LSComp tick, SolveShot, ProjectPoseToCineCamera, PCM dispatch. Cross-reference index of every subsystem touched + intentional-omission list (two-Shot blends, patches, PCM gameplay, patch+shot interaction). Covers PIE and editor-scrub variants and the aspect-ratio resolution path through the new `FGetActiveEditorViewport` hook.) Prior: 2026-04-30 (Solver aspect ratio finally honors `bConstrainAspectRatio` + actual editor viewport. Symptom: anchor screen positions in LS playback didn't match Shot Editor preview, and changing the level viewport size while `bConstrainAspectRatio = false` shifted anchor screen positions instead of holding them. Two roots: (1) `TryGetEffectiveViewportSize`'s step 3 (editor active viewport) was DOCUMENTED in the header but never IMPLEMENTED in the cpp — only fell back to 1920×1080 in editor scrub, so the solver ran with a wrong aspect when no GameViewport existed. (2) The helper had no notion of `UCineCameraComponent::bConstrainAspectRatio`: when constrained the renderer letterboxes to the filmback-derived `AspectRatio`, but the solver still used raw viewport size. **Fix in three layers**: (a) new `FGetActiveEditorViewport` editor hook in `EditorHooks.h` (declared runtime-side, bound by the editor module's StartupModule to `GEditor->GetActiveViewport()->GetSizeXY()`); (b) `TryGetEffectiveViewportSize` step 3 now consults the hook before the 1920×1080 fallback; (c) new helper `GetEffectiveAspectRatioForCineCamera(CineCam, OptionalPCM)` returns `CineCam->AspectRatio` when constrained, falls back to `GetEffectiveViewportAspectRatio` otherwise. The LS Component computes the aspect via the new helper using its `OutputCineCameraComponent` and pushes it to the framing node via the new `UComposableCameraCompositionFramingNode::SetExternalAspectRatioOverride` setter; `OnTickNode` prefers the override when > 0, otherwise falls back to the existing `OwningPlayerCameraManager`-keyed query (PCM path unchanged). Anchor screen positions now match across Shot Editor preview, LS editor scrub, PIE, and packaged game regardless of constraint state. Prior: 2026-04-30 (LSActor CineCamera follow-up — Details panel stack-overflow fix. The previous `OutputCineCameraComponent` UPROPERTY add on the actor crashed `SDetailsView::SetObjects` (infinite recursion in `UpdateSinglePropertyMapRecursive`) when designers clicked the LSActor's Track binding in Sequencer. Root cause: the same `UCineCameraComponent` instance was reachable via TWO UPROPERTY paths — the new `Actor::OutputCineCameraComponent` AND the existing `LSComponent::OutputCineCameraComponent` (also `VisibleAnywhere` / `BlueprintReadOnly`). The Details panel walker follows both paths, lands on the component twice, and recurses without cycle detection. Fix: drop edit/visible specifier on `LSComponent::OutputCineCameraComponent` (now plain `UPROPERTY()` — still GC-tracked, retained through serialization, but skipped by the Details panel walk). The actor-level UPROPERTY remains the canonical surface for editing CineCam optics; LSComponent's reference is an internal cache. Prior: 2026-04-30 (`ProjectPoseToCineCamera` extended to project Lens fields when a Sequencer Shot Section is actively driving the framing. The pose pipeline (`TypeAsset → Pose → CineCamera → PCM`) was previously truncated at the third arrow — only Position + Rotation projected onto the CineCamera, with FOV / Aperture / FocusDistance silently dropped per the original Phase B "CineCam owns optics" rule. With V2 Shot authoring designers explicitly set `Shot.Lens.Aperture` / `Shot.Focus.ManualDistance` / `Shot.Lens.ManualFOV` (or solved-from-bounds FOV) per Shot via the Shot Editor and reasonably expect those values to flow during LS playback. Fix: when `SequencerShotOverrides.Num() > 0` (a Section is overriding) AND `Pose.PhysicalCameraBlendWeight > 0`, also write `FieldOfView` (via `SetFieldOfView` so the Filmback math stays clean) / `Aperture` (`CurrentAperture`) / `FocusDistance` (`FocusSettings.ManualFocusDistance` only — `FocusMethod` left as-authored on the CineCam, so Tracking-mode authoring still wins). Outside Section ranges the gate keeps `ProjectPoseToCineCamera` at its Position+Rotation baseline, preserving the "free-standing LSActor doesn't steal CineCam optic authoring" invariant that motivated the original phase-B carve-out. Patch-overlay FOV write (`ApplySequencerPatchOverlays`) becomes redundant under the new path but stays harmless — it modifies `Pose` in place, so the projection reads the patched value either way. Prior: 2026-04-30 (LSActor CineCamera made designer-editable. `AComposableCameraLevelSequenceActor` previously created its `OutputCineCameraComponent` via `CreateDefaultSubobject` + assigned to `RootComponent` directly — without a dedicated UPROPERTY pointer, the Details panel's component-tree walk didn't pick the component up, so designers couldn't edit per-instance lens / filmback / aperture / post-process / focus settings. Added `UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UCineCameraComponent> OutputCineCameraComponent` on the actor; ctor now stores the subobject in the field then assigns to `RootComponent`. `VisibleAnywhere` locks the pointer (don't reassign to a different component) but the standard `EditAnywhere` flags on `UCineCameraComponent`'s own UPROPERTYs are honored when the user drills into the component — full lens / filmback / focus / post-process authoring surface unlocked. Existing saved instances pick up the field on load (ctor runs same as before, just storing the result in a tracked location). `LevelSequenceComponent->OutputCineCameraComponent` wiring unchanged — same value, just sourced from the new member instead of a local. Inherited by `AComposableCameraLevelSequenceShotActor` automatically.) Prior: 2026-04-26 (§ Node System — new CDO field `UComposableCameraCameraNodeBase::PaletteCategory` (FName, `EditDefaultsOnly`, default `"Misc"`) carries each node class's editor-palette subcategory. Pure editor metadata — no runtime semantics, doesn't affect evaluation, allocation, or pose math. Surfaces in the camera-editor "Add Node" context menu via `Camera Nodes|<SubCategory>` nesting (schema-side detail in EditorDesignDoc § Node Palette). All 29 built-in camera + compute nodes assigned categories per the V1 taxonomy: Pivot / Position / Rotation / Framing / Optics / Focus & Effects / Collision & Occlusion / Post Process / Composition / Math. Blueprint subclasses (`UComposableCameraBlueprintCameraNode` children) set their category via Class Defaults — symmetric authoring surface for C++ and BP, same idiom Niagara / Cascade / GameplayAbility use. Prior: 2026-04-26 (§ Built-in Nodes — `PivotRotateNode` added. Synchronises camera rotation to a `PivotActor`'s world rotation each frame, composing a per-pivot-local-frame `RotationOffset` (quaternion multiply, same convention as `USceneComponent::RelativeRotation` on a child attached to PivotActor's root — chosen over raw `FRotator` add to avoid gimbal artifacts on pivots with non-trivial pitch / roll) and easing through an optional Instanced `Interpolator` (rotator). Re-seed-current idiom each tick — same shape `AutoRotateNode` / `LookAtNode` soft-mode use. Snap mode when no interpolator is wired; pass-through when PivotActor is unresolved. Distinct role from `LookAtNode` (camera→target gaze) and `RelativeFixedPoseNode` (locks the entire pose, not just rotation). Full runtime details in TechDoc §5. Prior: 2026-04-25 (§11 Sequencer integration — unified to LS-Actor-bound overlay path. PlayerIndex / ContextName fields **removed** from `UMovieSceneComposableCameraPatchSection` — Sequencer-driven patches address solely through `TargetActorBinding` (FMovieSceneObjectBindingID → bound LS Actor's CineCamera). PIE path **also** routes through LS Component overlay (was: PCM/Director). All worlds (Editor, EditorPreview, PIE, Game, Standalone) → TrackInstance pushes overlays into the bound LS Component → LS Component's TickComponent applies them after InternalCamera tick, before projection. The ECS gate naturally handles "patch only visible while LS Actor is the active cut target". TrackInstance's `ActiveHandles` map + `OnInputAdded` PCM path **deleted** — TrackInstance is now purely a forwarder to LS Component (OnAnimate / OnInputRemoved / OnDestroyed). LS Component API renamed `SetEditorPreviewPatchOverlay` / `RemoveEditorPreviewPatchOverlay` → `SetSequencerPatchOverlay` / `RemoveSequencerPatchOverlay` and the editor-world gate in TickComponent is removed (overlays apply in all worlds). USTRUCT renamed `FComposableCameraEditorPreviewPatchOverlay` → `FComposableCameraSequencerPatchOverlay`. **BP `AddCameraPatch` library function unchanged** — it's the orthogonal gameplay path (overlays gameplay Director's RunningCamera, addressed via PlayerIndex/ContextName) and stays for runtime BP-driven patches. Two paths now have crisp roles: Sequencer Section → LS Actor camera (cinematic); BP `AddCameraPatch` → gameplay PCM (gameplay). Prior: 2026-04-25 (§11 Sequencer integration — editor preview overlay path. Patch sections now apply to a bound `AComposableCameraLevelSequenceActor`'s CineCamera while the Sequencer scrubber is in their range, mirroring how the LS Component itself previews in editor without entering PIE. New section field `TargetActorBinding: FMovieSceneObjectBindingID` binds the patch to its preview LS Actor (drag-drop from the binding row or use the picker). New LS Component API `SetEditorPreviewPatchOverlay` / `RemoveEditorPreviewPatchOverlay` — TrackInstance pushes (parameter block, envelope alpha, evaluator) per-frame in editor world; LS Component lazy-spawns the evaluator, applies overlays sorted by LayerIndex in its own TickComponent (after InternalCamera tick, before projection), writes the final pose's Position+Rotation+FOV onto the CineCamera so DollyZoom-style optic patches preview correctly. Envelope alpha computed stateless via new `Patches/ComposableCameraPatchEnvelope.{h,cpp}` helpers (`ApplyEase` shared with the runtime stateful machine, `ComputeStatelessAlpha` new — pure function of playhead vs section bounds + ease durations). PIE / Game path unchanged — TrackInstance still drives patches through PCM/Director; world-type dispatch in TrackInstance::OnInputAdded / OnInputRemoved / OnAnimate / OnDestroyed routes editor-world inputs into the LS Component overlay path and PIE inputs into the existing AddPatch/ExpirePatch/Apply path. 1-frame lag note: TrackInstance::OnAnimate may run after LS Component's TickComponent in a given frame, so overlay pushed in frame N applies in frame N+1 — visually imperceptible during scrub. `TargetActorBinding` is editor-preview-only; runtime addressing stays via `PlayerIndex` + `ContextName`. Prior: 2026-04-25 (§11 Sequencer integration — keyable per-parameter channels. Patch section now subclasses `UMovieSceneParameterSection` (engine's named-curve base, same one MaterialParameterSection / CustomPrimitiveDataSection use); each ExposedParameter / ExposedVariable can be promoted to a keyable channel via right-click section → "Camera Parameters → X". Promotion calls `Section->Add<X>ParameterKey(Name, currentTime, bagDefault)` which auto-creates the named curve struct + initial key. Per-frame OnAnimate samples channels at the input's current evaluation frame (resolved via `Linker->GetInstanceRegistry()->GetInstance(InstanceHandle).GetContext().GetTime()`); curves take priority over bag values, bag values are used as fallback for un-promoted parameters. Supported channel kinds: Scalar (Float/Double), Bool, Vector2D, Vector3D + Rotator (3-channel), Vector4 (RGBA via Color curves). Int32 / Enum / Object / Actor / Name / Struct / Transform / Delegate stay bag-only (no channel keying in V1.x). Replaces the earlier `Sequencer->KeyProperty` attempt which silently no-op'd because patch sections aren't object bindings (KeyProperty walks the binding system). Prior: 2026-04-25 (§11 Camera Patches — Stage 9 Sequencer integration shipped. New runtime triple under `Source/ComposableCameraSystem/{Public,Private}/MovieScene/`: `UMovieSceneComposableCameraPatchTrack` (root track, no object binding — patches address their target via section's PlayerIndex+ContextName, mirroring BP `AddCameraPatch`), `UMovieSceneComposableCameraPatchSection` (one per Patch activation window — holds PatchAsset, PlayerIndex, ContextName, FComposableCameraPatchActivateParams, plus Parameters/Variables FInstancedPropertyBag mirroring the asset's exposed surface), `UMovieSceneComposableCameraPatchTrackInstance` (per-section dispatch via `FMovieSceneTrackInstanceComponent` — OnInputAdded fires AddCameraPatch with section easing folded into envelope, OnAnimate per-frame rebuilds parameter block from current bag values + ApplyParameterBlockToActivePatch, OnInputRemoved fires ExpireCameraPatch with section ease-out as exit override, OnDestroyed force-expires all live handles for safe linker teardown). New runtime mid-life mutation API `UComposableCameraPatchManager::ApplyParameterBlockToActivePatch(Handle, ParameterBlock)` — re-applies a parameter block to a live evaluator's runtime data block via the source asset's `ApplyParameterBlock` (same path the LS Component uses on its per-tick re-sync); no-op on Exiting/Expired patches. Editor side: `FComposableCameraPatchTrackEditor` registers the track type, exposes "Add Track → Composable Camera Patch Track" in the root menu, paints sections by patch asset name; `FComposableCameraPatchSectionInterface::BuildSectionContextMenu` adds "Camera Parameters" / "Camera Variables" entries listing keyable bag leaves — click materialises stock property tracks on `Parameters.Value.{LeafName}` (stock evaluation animates the bag in place, runtime OnAnimate picks up the values). Pin-type → bag-descriptor pipeline extracted into shared `LevelSequence/ComposableCameraExposedBagUtils.{h,cpp}` so both the section and `FComposableCameraTypeAssetReference` use one canonical "exposed parameter → CPF_Edit | CPF_Interp bag entry" path. No new ECS gate needed — patches are not Spawnables, the existing gate instantiator (LS Components) is orthogonal. Single-key `SetPatchParameter(handle, name, value)` mutation API still deferred — Sequencer keys naturally produce complete parameter blocks. Prior: 2026-04-25 (§11 Camera Patches — Stage 8 BP API completion: `AddCameraPatch` gained an explicit `FName ContextName` parameter (NAME_None = active context, otherwise targets a context already on the stack — buried-context patches still tick but aren't user-visible until the context returns to the top); new `ExpireAllPatchesOnContext` BP function backed by `UComposableCameraPatchManager::ExpireAll` (soft sweep — each patch runs its own exit ramp, idempotent against already-Exiting/Expired entries); 4 BP-pure handle-introspection getters (`IsPatchActive` / `GetPatchPhase` / `GetPatchAlpha` / `GetPatchElapsedTime`), all weak-handle-safe so callers never need to null-check the handle. Brings the §12.1 surface of `PatchSystemProposal.md` to the implemented state. K2Node `UK2Node_AddCameraPatch` updated with a corresponding ContextName static pin (project-settings dropdown). Prior: 2026-04-25 (§11 Camera Patches — `InlineEditConditionToggle` restored on `FComposableCameraPatchActivateParams::bOverride*` flags. Compact node surface preserved (5 fewer pins than the no-meta variant); the original "all overrides on by default" footgun is addressed by a documented MakeStruct authoring rule rather than by removing the meta. Rule: hide value pins for fields you do NOT want to override (per-pin context menu or node's "Show Pin For …" toggles) → `bOverride*=false` → asset default. Mirrors `FPostProcessSettings::bOverride_*`. Full surface description in TechDoc §7.2. Prior: 2026-04-25 (§11 Camera Patches — bug fix: `InlineEditConditionToggle` removed from `FComposableCameraPatchActivateParams::bOverride*` flags. UE's BP `MakeStructHandler` was implicitly forcing those bools to TRUE whenever the gated value pin was exposed on the MakeStruct node (default behavior), bypassing the asset-default fallback path. Symptom: every patch placed via `Make FComposableCameraPatchActivateParams` got `ExpirationType=0` and lived forever. Fix preserves `EditCondition` (still gates value field in details panels) but the bool now renders as its own explicit pin in MakeStruct. Full failure-mode write-up in TechDoc §7.2. Prior: 2026-04-25 (§11 Camera Patches `FComposableCameraPatchActivateParams` — every overridable field now uses paired `bOverride*` + value idiom (`InlineEditConditionToggle`/`EditCondition`, same shape as `FPostProcessSettings::bOverride_*`). New toggles: `bOverrideEnterDuration` / `bOverrideExitDuration` / `bOverrideExpirationType` / `bOverrideDuration`, joining `bOverrideLayerIndex`. Replaces the float-zero / bitmask-zero sentinels that couldn't express "literal 0 wanted" without colliding with "fall back to asset". `PatchManager::AddPatch` Effective-resolution gated on the new bools. Prior: 2026-04-25 (§11 Camera Patches API surface — `AddCameraPatch` BP library function now takes `int32 PlayerIndex` (matches `ActivateComposableCameraFromTypeAsset`) instead of an explicit `AComposableCameraPlayerCameraManager*` argument; the function resolves the PCM internally via `GetComposableCameraPlayerCameraManager`. `FComposableCameraPatchActivateParams::LayerIndexOverride` (in-band `MIN_int32` sentinel) replaced with paired `bOverrideLayerIndex` + `LayerIndex` so the BP details panel shows a clean checkbox + clamped int rather than a `-2147483648` literal. Prior: 2026-04-25 (§11 Camera Patches public API — `AddCameraPatch` BP entry point reshaped: `FComposableCameraPatchActivateParams.Parameters` removed; the parameter block is now a separate argument to both the BP library function and the underlying `UComposableCameraPatchManager::AddPatch`, mirroring `FComposableCameraActivateParams` + `ActivateComposableCameraFromTypeAsset`. The library function is `BlueprintInternalUseOnly` — BP authors interact through the new `UK2Node_AddCameraPatch` (EditorDesignDoc §22 / §11), which generates typed dynamic pins for the asset's exposed parameters / exposed variables and expands into the call. Prior: 2026-04-25 (§11 Camera Patches — new top-level section. Camera Patch System lands as Director-scoped, time-bounded, additively-composable overlay primitive. Each Patch is a small CameraBase actor (subclass `UComposableCameraPatchTypeAsset` of TypeAsset, reuses graph editor unchanged) whose nodes read upstream pose and write modified pose, blended at a per-patch alpha into the running result. Director gains `PatchManager` subobject parallel to `EvaluationTree`; `Director::Evaluate` calls `PatchManager.Apply` after the tree produces its blended pose. 4-phase envelope (Entering/Active/Exiting/Expired) with 5-curve ease enum. Schedule channels: Duration / Manual / Condition (BP `CanRemain` event) / `bExpireOnCameraChange` flag (per-patch `RunningCameraAtAdd` snapshot baseline). LayerIndex sorted insert, push-order tiebreaker. End-of-Apply reverse-iter sweep destroys evaluators and compacts. New invariants #28-32. Existing §11-20 renumbered to §12-21; §16 ref in node compatibility section bumped to §17. New `EComposableCameraNodePatchCompatibility` enum + `GetPatchCompatibility()` BlueprintNativeEvent on node base; overrides on `RelativeFixedPose` / `MixingCameraNode` / `ViewTargetProxyNode` (Incompatible) and `ReceivePivotActorNode` (CompatibleWithCaveat); editor surfaces via `Build()`'s new `ValidateAdditional` virtual hook + existing inline node badge infrastructure. Editor: `FComposableCameraPatchTypeAssetCustomization` IDetailCustomization hides three inherited fields (`EnterTransition` / `ExitTransition` / `bDefaultPreserveCameraPose`) that have no Patch semantics. Distinct asset thumbnail / factory / AssetDefinition (warm-orange `#E08020`). BP entry points `AddCameraPatch` / `ExpireCameraPatch` (full typed K2 node deferred). Debug: in-viewport panel structured Patches region with phase-colored identity row + alpha bar + time bar (renders like Tree's transition progress); `CCS.Dump.Patches` console command + dump entries appended to `CCS.Dump.Stack` / `CCS.Dump.Tree`; `STAT_CCS_PatchManager_Apply` + `STAT_CCS_Patch_TickEvaluator` cycle counters; `TRACE_CPUPROFILER_EVENT_SCOPE_STR(asset name)` per-patch in Insights. V1 limitation: cross-context blends do not preserve source-side patches (V2 path: tree wrapper variant). Full design rationale in `PatchSystemProposal.md`. Prior: 2026-04-24 (§ Built-in Nodes — `AutoRotateNode` extended. New `EComposableCameraAutoRotateDirectionMode { Direction, ActorForward }` enum selects how the reference forward is resolved each frame — either the explicit `MainDirection` vector (prior behavior, kept as default) or `PrimaryActor->GetActorForwardVector()`. Both inputs are pins with mutually-exclusive `EditCondition` hiding so the Details panel only shows the field for the active mode; runtime picks the one the enum names and validates non-zero before proceeding. New `bInterruptOnUserInput` bool gates the entire user-interrupt path (CameraRotationInput read, InputInterruptCooldown arming, UsedCountAfterInputInterrupt increment); when false, only the beyond-range cooldown throttles auto-rotation. `bYawOnly=false` now triggers auto-rotation when ANY axis leaves its range (was AND — both axes had to be out before); the old conservative rule didn't match user intent that either axis leaving should engage re-centering. The two separate `RotateInterpolatorForYaw` / `RotateInterpolatorForPitch` (`BuildDoubleInterpolator`) are collapsed into a single `RotateInterpolator` built via `BuildRotatorInterpolator` — yaw and pitch now progress on the same curve so both axes reach the target together instead of yaw finishing while pitch lags. `UsedCountAfterInputInterrupt` still never auto-decays (single camera-instance budget); `BeyondValidRangeCooldownRemaining` arms on any re-entry into range, same as before. Prior: 2026-04-24 (§ Built-in Nodes — `HitchcockZoomNode` added to the summary table. Classic dolly-zoom effect; camera dollies along the camera→subject axis while FOV compensates to keep on-screen subject size constant, producing the Vertigo perspective warp. Captures `LockConstant = InitialDistance · tan(InitialFOV/2)` on first tick, then preserves that invariant by solving FOV ↔ Distance from whichever the author drives. Additive-delta curves (`Y(0) = 0` convention) keep curves portable across cameras with different initial FOVs. Rotation direction is left composable with upstream LookAt / CameraOffset — Hitchcock owns only radial distance + FOV. Once-only playback, matches the plugin's existing authoring shape without introducing new cyclic modes. Full runtime details in TechDoc §5. Prior: 2026-04-24 (§ Built-in Nodes — `FocusPullNode` added to the summary table. Dynamically drives `Pose.FocusDistance` from camera→target-actor distance. Single-responsibility: aperture + `PhysicalCameraBlendWeight` come from an upstream `LensNode` (LensNode writes `FocusDistance=-1` sentinel "leave for downstream", FocusPull owns the real value). Target resolution matches CollisionPush / OcclusionFade (PivotActor + optional BoneName / PivotZOffset). Optional `FFloatInterval` clamp and optional interpolator damping with first-frame snap. Modelled on Epic's `UAutoFocusCameraNode`, single-node shape because we don't carry a pose-level `TargetDistance` slot. Full runtime details in TechDoc §5. Prior: 2026-04-24 (§ Built-in Nodes — `VolumeConstraintNode` added to the summary table. Keep-inside-a-volume constraint for camera position; supports Box (OBB) and Sphere, single volume per node (multi-volume with priority is explicitly out of scope for MVP — swap cameras for different volumes). Shape either sourced from an actor's first `UShapeComponent` or defined inline in world space. Hard projection to the nearest boundary point when the upstream position is outside the volume; pass-through when inside. Optional per-axis `ClampInterpolator` (three 1D filter instances, one per world-space axis so dynamics stay decoupled) smooths three otherwise visible discontinuity modes: release snap when crossing outside→inside in one frame, corner face switch where the tangent direction of the clamped path flips past a box corner, and scripted teleports. When the interpolator is null the node is stateless and deterministic. Full runtime details in TechDoc §5. Prior: 2026-04-23 (§ Built-in Nodes — `OcclusionFadeNode` added to the summary table. Camera-node analogue to Epic's `UOcclusionMaterialCameraNode` with three intentional extensions: SkeletalMesh support (Epic misses it), optional component-tag filter (covers "walls and foliage both on ECC_Camera but only fade foliage"), and a parallel proximity-fade path that handles "camera gets too close to the player, fade the player" via a synchronous Pawn-channel overlap instead of requiring a second node. Fade look + timing live entirely in the swap material — node architecture is instant material swap + delta tracking + GC-pinned originals + mandatory cleanup on `BeginDestroy`. Complements `CollisionPushNode`: CollisionPush physically moves the camera around obstacles, OcclusionFade ghosts obstacles the camera shouldn't detour around. Full runtime details in TechDoc §5. Prior: § Built-in Nodes — `SpiralNode` added to the summary table. Position-only camera node that orbits a pivot on a helical path; orientation left to a downstream `LookAtNode`. Three Progress curves (X ∈ [0,1] normalized time, Y in absolute world units) for Radius / Height / Angle — direct-eval semantics matching SplineNode's `AutomaticMoveCurve`, so `Position(t)` is O(1) at any arbitrary t with no accumulated state. Arbitrarily shaped orbits (multi-peak ping-pong radii, arch heights, variable-speed sweeps) are all expressible directly via curve shape; PingPong needs only an X mirror — no Y sign flip — because all three curves are Progress. Spiral Space (Axis / Forward / Right) defined by `RotationAxis` and `ReferenceDirection` enums — `CameraInitialForward` is the seamless default, captured on the first tick after activation. `PivotOffset` is applied in Spiral Space, which is the feature that prevents substituting an upstream `PivotOffsetNode`: only SpiralNode knows its own axis frame. Full runtime details in TechDoc §5. Prior: Refinement to the `FComposableCameraPose::BlendBy` fix — the `MobileHQGaussian` branch now follows our "bool/enum target-wins at `OtherWeight > 0`" convention (same rule the Projection & aspect block uses for `ProjectionMode` / `ConstrainAspectRatio` / etc.) rather than mimicking the engine helper's 50 % `bShouldFlip` snap. Reasoning: `bMobileHQGaussian` IS a bool, and our convention rejects mid-blend bool snaps precisely because bools have no meaningful intermediate state. Both value and flag now overwrite together at `OtherWeight > 0`. Prior: Bug fix in `FComposableCameraPose::BlendBy` — engine's `FPostProcessUtils::BlendPostProcessSettings` has four PP fields it lerps/sets the value for but forgets to flip `bOverride_X = true` on the result (`DepthOfFieldFocalDistance`, `DepthOfFieldMatteBoxFlags`, `LensFlareTints`, `MobileHQGaussian`). User-visible symptom: during Gameplay → Level-Sequence transitions, DoF doesn't fade in — it snaps on at transition collapse time because the renderer sees `bOverride_DepthOfFieldFocalDistance = false` throughout the blend, ignores the value, and only honors the new override once `CollapseFinishedTransitions` replaces the blended pose with the proxy pose directly. Workaround in our BlendBy `|=`s the three value-already-written `Other.bOverride_*` flags into the result after the engine helper returns; `bMobileHQGaussian` (a bool) additionally has its value overwritten from `Other` at the same `OtherWeight > 0` gate. Full detail + future-fix checklist in TechDoc §7.2. Prior: §19 Debug HUD — Current Pose Physical group gained a CineCamera data-source branch. When the pose's `PhysicalCameraBlendWeight` is 0 (the common case for the proxy-via-CineCamera path — Sequencer Camera Cut Track → LS Actor → `UComposableCameraViewTargetProxyNode` writes only transform + FOV + PostProcess, never the pose's discrete Aperture / Focus / ISO / Shutter / Sensor slots), the panel now probes `PCM->GetViewTarget()` for a `UCineCameraComponent` and, if found, reads `CurrentFocalLength / CurrentAperture / CurrentFocusDistance / Filmback.Sensor* / PostProcessSettings.CameraISO|ShutterSpeed` directly off it. Header switches to `-- Physical (CineCamera) --` so the data source is visible without burning a row on a `Source:` line. ISO / Shutter respect their `bOverride_*` gates — unoverridden shows `auto` instead of an uninitialized slot value. Fixes the user-visible symptom: "Physical stays off when I'm in LS mode even though the CineCamera has real DoF / exposure settings". Prior: §19 Debug HUD — Current Pose region expanded into four labelled groups (Transform / Context / Projection / Physical) flowed across two columns via a new `bIsPose` structured path on `FRegionLines` (`PoseGroups`, `PoseBodyHeight`, `PoseLeftGroupCount` + `DrawPoseStructured`). Adds Forward vector, projection mode + FocalLength dual-mode annotation, ortho clip planes (orthographic only), and the full physical-camera block (Aperture / Focus / ISO / Shutter / Sensor) which collapses to a single `Status: off` line when `PhysicalCameraBlendWeight <= 0` — the common case. Two-column layout keeps region height at `max(LeftCol, RightCol)` so the panel does not grow by ~15 lines when every field is shown. `BuildPoseLines` → `BuildPoseGroups`; renderer adds a `bIsPose` branch alongside the existing `bIsStackAndTree` / `bIsLegend` structured paths. Prior: §16 Gate instantiator — editor-world bypass for per-entry gate flips. `EWorldType::Editor` / `EWorldType::EditorPreview` entries are now skipped in Phase 4. Sequencer editor scrub has two quirks the gate's close-on-inactive semantic actively fights with: (i) scrubbing out of the cut section evaluates the track instance at a pre-animate time one display-frame before the section start (tick -800 at 30 fps / 24000 tick-res), which legitimately reads `contains=no` and closes the component; (ii) after scrubbing back into the section, that pre-animate time can persist across idle frames (user hovering rather than actively dragging), so the gate never re-opens the component even though Sequencer IS writing the bag values (verified via Details-panel live inspection). Result: scrubbing in works once, scrubbing out → back in leaves the component permanently OFF for the rest of the session. Bypass keeps every editor-world component at OnRegister's default-ON so authoring / preview just works. PIE / Game / GamePreview still flow through the full close-on-inactive logic — the performance optimization only matters where many Spawnables coexist at runtime. Companion to the CutTrackInstanceCount guard (same-day): the two guards address different failure modes (runtime first-frame race vs. editor pre-animate sticky time) with different scopes (global early-return vs. per-entry continue). Prior: §16 Gate instantiator hardening — Phase 4 now only flips gates when `CutTrackInstanceCount > 0` this frame. Without this guard, two failure modes stranded LS Spawnables OFF: (a) first-frame race — gate links via `SpawnableBinding` a frame before `UMovieSceneCameraCutTrackInstance` registers via the TrackInstance system; old code saw empty `ActiveActors`, closed the freshly-OnRegistered Spawnable via `SetEvaluationEnabled(false)`, and on subsequent frames the re-open depended on cut-section binding resolution also being ready (one more tick of slop) — in PIE repro this window was missed and the Spawnable stayed OFF for the whole section; (b) sequences with no Camera Cut Track at all — `ActiveActors` always empty, Spawnable permanently gated OFF even when the LS component's pose-projection was meant to drive its CineCamera for external consumers (a different track's actor-bound cut, manual `SetViewTarget`, etc.). Guard reads "no observable cut track state" as "leave every tracked entry alone" — preserves the close-only invariant ("the gate still never opens an entry it can't reach") while refusing to close on a silent frame. Side effect: the previously-documented "first-frame transient tick before gate closes inactive entries" is gone — the first gate flip now waits until the cut track is actually readable. Prior: Bug fix companion to yesterday's deferred-destroy fix — inter-context activation now also implicitly pops any transient context it demotes from the top. `PCM::ActivateNewCamera` (both overloads) threads the activating blend transition back out via a new `OutTransition` parameter on `UComposableCameraDirector::ActivateNewCameraWithReferenceSource`'s raw-instance overload (the data-asset overload already had it), then calls a new `UComposableCameraContextStack::DemoteNonTopTransientContextsToPending(ActivatingTransition)`. That method walks every non-top entry, finds those whose `RunningCamera->IsTransient()` is true, moves them from `Entries` into `PendingDestroyEntries`, and binds their `DestroyAllCameras` + pending-bucket removal to `ActivatingTransition->OnTransitionFinishesDelegate` — identical cleanup timing to an explicit `PopContext(name)`. Camera-cut activations (null transition) destroy the demoted transient immediately; with no blend there is no RefLeaf chain that still needs the transient camera alive. Added Invariant #26 (old #26 Blend-partners renumbered to #27). Solves the "stranded Transient context in the stack" symptom the user observed after the previous fix stopped the destroyed-leaf error — transient contexts that get shoved below the top by `EnsureContext` now get cleaned up via the same pending-destroy mechanism the explicit pop path uses. Prior: Bug fix in `UComposableCameraEvaluationTree::OnActivateNewCameraWithReferenceSource` — inter-context activation no longer destroys the OLD RootNode's cameras immediately. New `PendingDestroyOldRoots` list holds the pre-replacement RootNode (via TSharedPtr) until the new transition's `OnTransitionFinishesDelegate` fires, then its cameras are destroyed. Added Invariant #25 documenting the rule. Fixes the scenario "activate A in Gameplay → push Transient/B → reactivate A in Gameplay": at step 3 the SourceDirector (Transient) had captured Gameplay's pre-step-3 root as a RefLeaf snapshot (at push time), so destroying it at step 3 left Transient's Tick walking into destroyed Leaf_A actors during the pop blend → `[leaf] (destroyed)` rows in both contexts' trees + `"RunningCamera is null or destroyed when evaluating leaf node."` spam. Deferral guarantees A_OLD stays `IsValid` until `CollapseFinishedTransitions` drops the RefLeaf branch of Gameplay's new root, after which A_OLD is unreachable from any live tree and destruction is safe. `DestroyAll()` flushes the stash as a backstop for transitions that never complete (context torn down mid-blend, transition replaced). `AddReferencedObjects` walks the stash so GC doesn't collect actors out from under us between stash-time and transition-finish. Prior: §19 Debug HUD — ReferenceLeaves now inline-expand their captured source tree in the debug panel's Stack+Tree region. Active-context transition `[ref] OldDirector` lines are followed by the referenced director's tree at one extra indent, dimmed 45 % toward neutral to signal "historical source snapshot". Makes inter-context blend debugging (Push / Pop mid-transition) self-explanatory without vertical-scrolling across contexts. Flows to `showdebug camera` and `CCS.Dump.*` automatically. Prior: §19 Debug HUD — `CCS.Dump.Stack` / `CCS.Dump.Tree` / `CCS.Dump.Camera [tag]` console commands added. Output goes to both the log (Display verbosity) and the system clipboard — bug reports become paste-ins. Consume the same snapshot pipeline the panel uses, so adding a new tree node kind flows to all three surfaces automatically. Prior: §19 Debug HUD — Running Camera panel region gained a compact Data Block stats subsection: Storage bytes, slot counts by source (output pins / params / vars / defaults), largest slot. Diagnostic only; reuses existing `OwnedRuntimeDataBlock` accessors. Prior: §19 Debug HUD — `stat CCS` cycle-counter group added (`STATGROUP_CCS`, declared in the module header). `SCOPE_CYCLE_COUNTER` sits next to every existing `TRACE_CPUPROFILER_EVENT_SCOPE` in the hot path (PCM DoUpdateCamera / UpdateActions, ContextStack / Director / EvaluationTree + InnerNode Evaluate, Camera TickCamera, Node TickNode + ResolveAllInputPins, Transition Evaluate, ModifierManager UpdateEffectiveModifiers). `stat CCS` now drives the in-viewport numeric overlay; Insights remains the timeline / flame-graph surface. Complementary, not redundant. Prior: §19 Debug HUD — Debug data pipeline consolidated: the snapshot (`BuildDebugSnapshot` on ContextStack / Director / EvaluationTree) is now the single source of truth for all three debug consumers (2D panel, `showdebug camera`, future dumps). Deleted `BuildDebugString` / `BuildNodeDebugString` from all three classes; `AComposableCameraPlayerCameraManager::DisplayDebug` now walks the snapshot and renders per-node text through a new `ComposableCameraDebug::AppendTreeNodeLine` helper in `Utils/ComposableCameraDebugFormatUtils.h`. New tree node kinds now require exactly one switch update in `AppendTreeNodeLine` (plus the enum + snapshot builders), not a parallel string-branch edit. Same change added zero-alloc `AppendX(FStringBuilderBase&, …)` variants alongside the existing `FormatX` FString helpers (panel and showdebug migrated to the builder path — ~30+ fewer per-frame allocations in a typical active-camera frame) and cached `Nodes.All` / `Transitions.All` CVar values once per tick in `FComposableCameraViewportDebug::TickDraw` / `Draw2DHUD` so the per-gizmo `ShouldShowAll*` helpers return a plain bool instead of an atomic CVar read. A separate `Docs/DebugRoadmap.md` tracks planned P1/P2 follow-ups. Prior: Bug fix in context-stack pop — the resumed camera is now the ORIGINAL pre-push instance (kept alive throughout the push period via the pushed context's RefLeaf), preserving all per-node state continuously. The old path destroyed the pre-push camera and spawned a fresh same-class instance at pop time, producing a visible "snap" as node interpolators / damping / spline progress reset on the first post-pop tick. New `UComposableCameraDirector::ResumeCurrentCameraWithReferenceSource` + `UComposableCameraEvaluationTree::OnResumeCurrentTreeWithReferenceSource` wrap the existing tree root instead of rebuilding from scratch. Push paths unchanged. Prior: Bug fix in runtime Evaluate — `UComposableCameraDirector::Evaluate` now has a reentrancy guard so cross-referenced context trees (A.tree→RefLeaf→B and B.tree→RefLeaf→A both live at the same time) don't infinite-recurse. Manifests as a stack overflow when a transient context pushed over gameplay has `Lifetime < TransitionTime` and auto-pops mid-push-transition. Reentrance short-circuits to `LastEvaluatedPose` — semantically equivalent to one side being `bFreezeSourceCamera=true` on that frame. Prior: §19 Debug HUD — Tier 2 #8 shipped: right-side Pose History panel. PCM captures last ~120 frames of pose (Position / Rotation / FOV / GameTime / ContextName) into a ring buffer after each `DoUpdateCamera`. Independent `UDebugDrawService("Game")` delegate renders 6 sparklines (Pos.X/Y/Z + Rot.P/Y/R) per-row auto-normalized. Mouse hover scrubs through history with a cursor line + full-pose tooltip. Gated by `CCS.Debug.Panel.PoseHistory`. Prior same day: §19 Debug HUD — Tier 2 #7 shipped: Panel's Tree region shows each InnerTransition row's timing curve as an inline sparkline (amber column-histogram fill up to TransitionProgress + muted columns ahead + thin cream polyline on top). Row height for transitions bumped from KLineH=13 to KTreeTransitionLineH=22 to fit the curve below the text. New `UComposableCameraTransitionBase::GetBlendWeightAt(float NormalizedTime) const` virtual exposes each concrete transition's timing-curve math; overrides on Smooth/Ease/Cubic/Cylindrical/Spline/PathGuided/DynamicDeocclusion, Linear/Inertialized/ViewTarget fall back to linear default. `FComposableCameraTreeNodeSnapshot::BlendCurveSamples` carries 25 pre-sampled points per transition from `BuildNodeDebugSnapshot`. Prior same day: §19 Debug HUD — sphere gizmos across all nodes and transitions are now SOLID TRANSLUCENT (UV-tessellated mesh through `DrawDebugMesh`) instead of wireframe (`DrawDebugSphere`). New `FComposableCameraViewportDebug::DrawSolidDebugSphere(World, Center, Radius, Color, Alpha, Segments, DepthPriority)` helper centralizes the pattern; alpha is a separate parameter so callsites keep passing plain `FColor::X` colors and pick the right translucency per gizmo (typical 100 for pivot markers, higher for emphasis, lower for large volume spheres). Prior same day: §19 Debug HUD — Panel's Running Camera node list now walks `FullExecChain` for correct execution-order display (was previously showing in `CameraNodes` array index order, which diverges from graph exec-pin wiring). `SetVariable` exec entries interleaved as subtle `-> set <var> = <pin>` lines. Falls back to linear `CameraNodes` walk when FullExecChain is empty, matching `TickCamera`'s fallback. Prior same day: §19 Debug HUD — Panel's Warnings region added (sits above Legend). Surfaces `LogComposableCamera*` warnings / errors inside the panel via a new `FComposableCameraLogCapture` `FOutputDevice` installed into `GLog` at module startup. Ring buffer (16 entries), thread-safe via `FCriticalSection`; region auto-hides when empty. Gated by `CCS.Debug.Panel.Warnings` (default 1). Shipping cost: zero (Install / Uninstall / Get guarded `#if !UE_BUILD_SHIPPING`, so the output device never registers and Serialize never runs). Prior same day: §19 Debug HUD — Panel's Running Camera region now lists per-node OUTPUT PIN VALUES beneath each node header, matching the data the editor-side `SnapshotDebugState` captures for the graph overlay. Closes the "editor has pin values, runtime doesn't" gap — development PIE / standalone builds can see pin flow without opening the camera graph. Prior same day: §19 Debug HUD — Panel's Actions region expanded from one-liner to three-line-per-action view (name+scope / exec phase + target node / expiration bits with live Duration progress). New `UComposableCameraActionBase::GetElapsedTime()` BlueprintPure getter supports Duration progress display. Prior same day: §19 Debug HUD — Panel's Modifier region upgraded from phase-1 placeholder into a structured two-section view: "Effective (N)" flat list of per-node-class winners + "All (M)" grouped by CameraTag → NodeClass → entries with [*] marker on the winner. Answers "did my modifier apply?" + "why is a different modifier winning?" in-panel. Added PCM `GetModifierManager()` accessor + ModifierManager `const GetModifierData()` overload to support read-only access. Prior same day: §19 Debug HUD — Legend region added to the debug panel. New sixth region (gated on `CCS.Debug.Panel.Legend`, default 1) lists a color swatch + label for every gizmo whose CVar is currently on, in a two-column transitions/nodes layout. Double-gated (requires `CCS.Debug.Panel 1` AND `CCS.Debug.Viewport 1` — otherwise labelling colors that aren't on screen would be misleading). Adaptive: empty columns hide their headers; both columns empty → region fully hidden, no title bar flicker. Prior same day: §19 Debug HUD — "Enable all" shortcut CVars `CCS.Debug.Viewport.Nodes.All` (covers both 3D and 2D HUD per-node gizmos) and `CCS.Debug.Viewport.Transitions.All` (all 9 per-transition gizmos). OR-ed with each per-item CVar; both still gated on master `CCS.Debug.Viewport 1`. No subtractive "all-except-X" semantics — users wanting selective control leave the All CVar off. Exposed via two static accessors `FComposableCameraViewportDebug::ShouldShowAllNodeGizmos / ShouldShowAllTransitionGizmos`. Prior: 2026-04-22 — per-transition paths are now REAL paths, not a misleading straight lerp baseline. The initial per-transition framework drew a shared white source→target line via `DrawStandardTransitionDebug`, but that misled for every spatially non-linear transition (Cylindrical, Inertialized, Spline, PathGuided). Baseline removed from the shared helper. Each concrete override now paints its own path: Linear/Smooth/Ease/Cubic keep a straight line (correct — their position IS a spatial lerp, only the timing differs); Cylindrical samples its arc via a new static helper `SampleCylindricalPathPosition`; Inertialized pre-samples 33 target-relative polynomial offsets at OnBeginPlay into a `TArray<FVector>` and adds the live target position at draw time; Spline reuses its `EvaluatePositionOnCurve` helper; PathGuided reuses its rail-spline sampler; DynamicDeocclusion keeps only the feeler rays (its path is shaped by dynamic trace offsets not predictable without actually tracing). Prior: 2026-04-21 — §19 Debug HUD — per-transition gizmos shipped. `UComposableCameraTransitionBase::DrawTransitionDebug(UWorld*, bool)` virtual + `#if !UE_BUILD_SHIPPING` `LastDebugSourcePose / LastDebugTargetPose / LastDebugBlendedPose` caches written by `FComposableCameraEvaluationTreeInnerNodeWrapper::Evaluate` each frame. New `UComposableCameraEvaluationTree::DrawTransitionsDebug` DFS walks Inner nodes and recurses into reference-leaves so inter-context blends surface both sides' in-flight transitions. Viewport ticker invokes it once per frame via `PCM->GetContextStack()->GetActiveDirector()->GetEvaluationTree()`. Nine concrete overrides (Linear / Smooth / Ease / Cubic / Inertialized / Cylindrical / Spline / PathGuided / DynamicDeocclusion), each with its own `CCS.Debug.Viewport.Transitions.<Name>` CVar defaulting to 0. Shared `DrawStandardTransitionDebug` paints green/blue/accent spheres + white lerp baseline + F8-only source/target frustums; Spline / PathGuided / DynamicDeocclusion layer extras on top (curve polyline / rail polyline / feeler rays). `SplineTransition::EvaluatePositionOnCurve` factored out so OnEvaluate and the 32-sample debug polyline share the same curve math. `ViewTargetTransition` intentionally excluded (hidden class, no user instance to debug). New `UComposableCameraDirector::GetEvaluationTree()` accessor supports the walk. Prior same day: §19 Debug HUD — 2D HUD overlay path added. `DrawNodeDebug2D(UCanvas*, APlayerController*)` virtual on `UComposableCameraCameraNodeBase` + matching `AComposableCameraCameraBase::DrawCameraDebug2D` walker + a second `UDebugDrawService::Register("Game", ...)` hook in `FComposableCameraViewportDebug`. ScreenSpacePivot and ScreenSpaceConstraints now draw a filled safe-zone rectangle, center marker, and projected pivot marker on the HUD during PIE possessed play (2D hook doesn't fire during F8). Both branches of `bConstrainAspectRatio` (simple / letterboxed via `ULocalPlayer::GetProjectionData`) preserved from the pre-framework `DrawDebugInfo` implementations. Prior same day: §19 Debug HUD — added gizmos for `ScreenSpacePivotNode` (teal sphere at resolved world pivot) and `ScreenSpaceConstraintsNode` (pink sphere at constrained actor). Earlier "no gizmo" decision reversed: a sphere at the world anchor doesn't answer the 2D safe-zone question, but it DOES confirm "the node is targeting the right actor/position" — narrow but useful. Both moved from the intentional-skip list into the shipped-overrides list. Prior same day: §19 Debug HUD — two more node gizmos: `ReceivePivotActorNode` (white sphere at pivot actor / bone) and `RelativeFixedPoseNode` (orange sphere at reference origin). Full list of intentional skips now enumerated in the "Intentionally without gizmos" bullet with rationale per node. Prior same day: §19 Debug HUD — two-tier gating + per-node opt-in CVars. Master `CCS.Debug.Viewport 1` now only enables the frustum (F8-gated) + the node-walk invocation; each per-node gizmo has its own CVar (`CCS.Debug.Viewport.<NodeName>`) defaulting to 0 that users opt into individually. Per-node gizmos now draw in BOTH possessed play and F8 eject (they don't occlude the viewpoint); only the frustum stays F8-gated. `ScreenSpacePivotNode` / `ScreenSpaceConstraintsNode` reverted: no 3D gizmo, since "pivot inside safe zone?" is a 2D question and a world-space sphere says nothing useful about it. `DrawCameraDebug` signature gained a `bDrawFrustum` flag so the ticker can split the two gates. Prior same day: §19 Debug HUD — per-node gizmo overrides shipped for `PivotOffsetNode` / `PivotDampingNode` / `LookAtNode` / `CollisionPushNode` / `SplineNode` / `ScreenSpacePivotNode` / `ScreenSpaceConstraintsNode`. Legacy `AComposableCameraPlayerCameraManager::bDrawDebugInformation` UPROPERTY and its gated debug draws across nodes + transitions (Cylindrical / PathGuided / DynamicDeocclusion / Spline) removed. Legacy 2D HUD overlays on the ScreenSpace nodes (OnHUDPostRender lambda, `DrawDebugInfo`, `DrawDebugHandle`) deleted. Per-transition gizmos can be re-added via a future `DrawTransitionDebug(UWorld*)` virtual on `UComposableCameraTransitionBase`. Prior same day: §19 Debug HUD — new subsection "In-World 3D Debug Draw". `FComposableCameraViewportDebug` registers a second independent `UDebugDrawService` delegate gated by `CCS.Debug.Viewport` CVar. Draws a yellow frustum at the running camera's pose via `DrawDebugCamera`. Two new `#if !UE_BUILD_SHIPPING` hooks on the camera hierarchy: `AComposableCameraCameraBase::DrawCameraDebug(UWorld*) const` walks nodes and invokes `UComposableCameraCameraNodeBase::DrawNodeDebug(UWorld*) const` on each — virtual with empty default, concrete nodes override to draw pivot spheres / look-at lines / collision traces / spline paths in world space. Panel (2D HUD) and viewport (3D world) are fully independent toggles. Prior same day: §19 Debug HUD: In-Viewport Debug Panel — visual cleanup. `FComposableCameraTreeNodeSnapshot` now carries `bIsLastSibling` + `AncestorLastFlagsBitmask` (uint32, bit L = "ancestor at depth L was last child"). Renderer draws proper geometric `├ / └ / │` connectors instead of the earlier "stem at every depth" approximation. Removed all overloaded glyph markers: active-context `-> ` prefix replaced with colored bullet rect; dominant-leaf `* ` prefix dropped (green underlay + cyan text suffice); reference leaf switched from `-> DirectorName` to `[ref] DirectorName` to avoid colliding with the active-context arrow. Prior same day: §19 Debug HUD: In-Viewport Debug Panel — Phase 2. New structured snapshot path: `UComposableCameraEvaluationTree::BuildDebugSnapshot` / `UComposableCameraDirector::BuildDebugSnapshot` / `UComposableCameraContextStack::BuildDebugSnapshot` produce a flat DFS pre-order `FComposableCameraContextStackSnapshot` consumed by the debug panel. The Context Stack & Tree region now draws transition progress bars (amber fill proportional to `TransitionProgress`), tree indent stems + elbow connectors, and a dominant-leaf highlight (green underlay + `*` prefix on the leaf reached by walking root → Right*). Snapshot structs live in `Public/Debug/ComposableCameraDebugPanelData.h`. The string path (`BuildDebugString`) is preserved in parallel for `showdebug camera`.) Prior: 2026-04-21 (§19 In-Viewport Debug Panel — new subsection. `FComposableCameraDebugPanel` registers with `UDebugDrawService` at module startup and draws a multi-region HUD overlay toggled via `CCS.Debug.Panel` (width via `CCS.Debug.Panel.Width`). Additive to the existing `showdebug camera` flow; data sources are shared public accessors and `BuildDebugString`. New public getter `AComposableCameraPlayerCameraManager::GetContextStack()` for debug/test access. Phase 1 uses line-level coloring of the existing string builders; phase 2 will replace with structured `BuildDebugSnapshot` for progress bars and tree connector lines.) Prior: 2026-04-20 (§10 Actions — PreNodeTick / PostNodeTick execution types implemented. Actions now dispatch in four shapes: two whole-camera delegates (PreCameraTick / PostCameraTick) and two node-scoped per-node arrays (PreNodeTickActions / PostNodeTickActions, filtered by `TargetNodeClass` with exact-class match, same rule as modifiers §9). §3 "Camera Tick" diagram updated to show the per-node hook points. Prior: V1.4: Explicit Level Sequence authoring path — `AComposableCameraLevelSequenceActor` Spawnable + `UComposableCameraLevelSequenceComponent` + `FComposableCameraTypeAssetReference` property bag; Sequencer track-editor menu extender; per-tick bag → RuntimeDataBlock sync. Camera evaluation now legal without a PCM — `UE::ComposableCameras::ConstructCameraFromTypeAsset` factored out of the PCM activation callback so both the PCM path and the LS component path share the same instantiation spine. ScreenSpace nodes made PCM-optional via `UE::ComposableCameras::TryGetEffectiveViewportSize`. Structural fix: CineCamera is now the LS Actor's RootComponent and the LS component is a sibling `UActorComponent` — mirrors `ACineCameraActor` so blended Camera Cut sections route through the same native fast path. V1.4 Phase G: ECS gate instantiator `UMovieSceneComposableCameraGateInstantiator` keyed on the engine's built-in `SpawnableBinding` (covers legacy + 5.5+ custom-binding spawnables in one path, no decorator / no editor hook / no saved-asset migration) closes the gate on tracked Spawnables whose owning actor isn't the Camera Cut Track's current target or a blend participant. `OnRegister` remains unconditionally on so non-Sequencer hosts fall back to always-on. Active set pulls both `GetInputs()` and each input section's ease-window blend partners from `Track->GetAllSections()`, recovering same-row-overlap authoring where engine truncation hides one participant. `OnUnregister` pairs with `OnRegister` for editor-world Spawnable teardown, preventing `InternalCamera` leaks on Save / sequence re-spawn.)*
*2026-05-12 addendum: the CCS Level Sequence CameraCut ECS gate has been removed. Spawn Tracks now own LS camera lifecycle; spawned LS components evaluate while their actor exists, then `OnUnregister` / `EndPlay` tear down the transient evaluator.*
*2026-05-12 addendum: LS-owned evaluators now use Sequencer-aware DeltaTime. Runtime spawned actors resolve the owning `UMovieSceneSequencePlayer` via spawnable annotation / spawn register and scale by `GetPlayRate()`; pure editor preview resolves the active `ISequencer` through `FGetEditorSequencerPlaybackDeltaTime` and scales by `GetPlaybackSpeed()`. Paused preview contributes zero DeltaTime.*
*2026-05-12 addendum: Phase F Shot overlap exits preserve prior-pose continuity. The LS component tracks both primary and secondary Sections; when the previous secondary becomes the new primary (A+B -> B), `CompositionFramingNode` promotes the secondary prior cache into primary instead of hard-seeding. True hard cuts still clear primary prior state; secondary swaps (A+B -> A+C) clear secondary prior state.*

*2026-05-11 addendum: `DirectionalMoveNode` now has a `Duration` input / UPROPERTY. Negative duration preserves the original infinite move; non-negative duration clamps travel time so the camera holds its final directional offset after `Duration` seconds.*

---

## 1. Vision and Design Philosophy

ComposableCameraSystem is a modular, composable camera framework for Unreal Engine 5.6. It replaces the monolithic "one camera manager does everything" pattern with a layered architecture where cameras are assembled from reusable nodes, blended through a tree-based evaluation system, and orchestrated across independent contexts.

### Core Design Principles

**Composability over inheritance.** Cameras are not defined by subclassing a monolithic camera class. Instead, a camera is a lightweight container that holds an ordered list of nodes. Each node is a single-responsibility operator (e.g., "look at target", "apply collision push", "control rotation from input"). New camera behaviors are created by composing different node combinations, not by writing new camera subclasses.

**Separation of concerns across two tiers.** The system separates macro-level mode switching (gameplay vs. cinematic vs. UI) from micro-level camera blending (transitioning between two cameras within the same mode). Tier 1 is the Context Stack; Tier 2 is the Evaluation Tree. This separation means that pushing a cinematic context doesn't destroy or interfere with the gameplay camera underneath — it simply suspends it.

**Pose-only transitions.** Transitions never reference cameras or Directors directly. They receive two poses (source and target) each frame and output a blended pose. This makes transitions fully reusable across any camera pair and any context boundary.

**Data-driven configuration.** Camera behavior is defined entirely through `UComposableCameraTypeAsset` data assets created in the visual editor. The camera base class is `NotBlueprintable` — Blueprint subclassing is forbidden. Designers create camera types by composing nodes, wiring pins, and exposing parameters in the type asset editor, then activate cameras from Blueprint using the custom K2 activation node.

**Reference-based inter-context blending.** When switching between contexts, the system doesn't freeze the old context's output. Instead, it creates a reference leaf node that captures a `TSharedPtr` snapshot of the old context's tree root and walks that captured subtree each frame (the cameras inside still tick live, but the topology is fixed at capture time). This produces seamless blending even when the source context's camera is still animating, and — because the snapshot doesn't follow later mutations to the source director — keeps the evaluation reachable graph a DAG with no cycles, even during pop-while-push-still-active.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                  AComposableCameraPlayerCameraManager           │
│                  (replaces APlayerCameraManager)                │
│                                                                 │
│  ┌──────────────────────┐  ┌──────────────────────────────┐     │
│  │  ModifierManager     │  │  CameraActions (TSet)        │     │
│  └──────────────────────┘  └──────────────────────────────┘     │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              UComposableCameraContextStack                │   │
│  │              (Tier 1: macro mode switching)               │   │
│  │                                                           │   │
│  │    ┌─────────┐  ┌─────────┐  ┌─────────────────┐         │   │
│  │    │ Base    │  │ Cutscene│  │ Active (top)    │ ← eval  │   │
│  │    │ Context │  │ Context │  │ Context         │   here   │   │
│  │    │         │  │         │  │                 │         │   │
│  │    │Director │  │Director │  │Director ────────┼───┐     │   │
│  │    └─────────┘  └─────────┘  └─────────────────┘   │     │   │
│  │                                                     │     │   │
│  │    PendingDestroyEntries: [...popped contexts...]   │     │   │
│  └─────────────────────────────────────────────────────┼─────┘   │
│                                                        │         │
│  ┌─────────────────────────────────────────────────────▼─────┐   │
│  │              UComposableCameraDirector                     │   │
│  │              (one per context)                             │   │
│  │                                                           │   │
│  │  ┌────────────────────────────────────────────────────┐   │   │
│  │  │         UComposableCameraEvaluationTree            │   │   │
│  │  │         (Tier 2: camera transition blending)       │   │   │
│  │  │                                                    │   │   │
│  │  │              [Inner: Transition]                    │   │   │
│  │  │               /              \                     │   │   │
│  │  │        [Leaf: CamA]    [Leaf: CamB] ← target      │   │   │
│  │  │        (source)        (running)                   │   │   │
│  │  └────────────────────────────────────────────────────┘   │   │
│  │                                                           │   │
│  │  RunningCamera, LastEvaluatedPose, PreviousEvaluatedPose  │   │
│  └───────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### Component Summary

| Component | Role |
|---|---|
| **PlayerCameraManager (PCM)** | Top-level integration point. Replaces UE's APlayerCameraManager. Owns the ContextStack, ModifierManager, and CameraActions. Drives the per-frame DoUpdateCamera loop. |
| **ContextStack** | LIFO stack of named contexts. Each context owns a Director. Only the top context is actively evaluated (unless referenced by a reference leaf). Handles push, pop, auto-pop of transient cameras, and pending-destroy lifecycle for popped contexts in transition. |
| **Director** | Per-context camera manager. Owns the EvaluationTree and tracks RunningCamera, LastEvaluatedPose, and PreviousEvaluatedPose. Handles camera creation, activation, reactivation, and resume. |
| **EvaluationTree** | Binary tree of nodes (with snapshot RefLeaves the reachable graph is a DAG, not strictly a tree). Leaf nodes wrap cameras, reference leaf nodes hold a `TSharedPtr` snapshot of another director's tree root, and inner nodes wrap transitions that blend their left (source) and right (target) children. Collapse logic promotes the target subtree when a transition finishes. |
| **CameraBase** | A camera actor holding an ordered array of CameraNodes. Executes nodes sequentially each tick to produce a pose. Supports transient lifetime. |
| **CameraNode** | Single-responsibility operator. Reads the current pose and pin values, applies its logic, and writes the modified pose. Nodes communicate through the RuntimeDataBlock pin system. |
| **Transition** | Pose-only blender. Receives source and target poses, maintains internal blend state, outputs blended pose. Has lifecycle hooks: OnBeginPlay (first frame setup), OnEvaluate (per-frame blend), OnFinished (cleanup). |
| **ModifierManager** | Manages modifier data assets. When modifiers change, computes effective per-node-class modifiers (highest priority wins) and may trigger camera reactivation. |

---

## 3. Per-Frame Evaluation Pipeline

Each frame, `AComposableCameraPlayerCameraManager::DoUpdateCamera(DeltaTime)` drives the full pipeline:

```
DoUpdateCamera(DeltaTime)
  │
  ├─ ContextStack->Evaluate(DeltaTime)
  │    │
  │    ├─ Check auto-pop: if active context's camera is transient and finished,
  │    │   pop the context (with transition to previous context)
  │    │
  │    ├─ ActiveDirector->Evaluate(DeltaTime)
  │    │    │
  │    │    ├─ EvaluationTree->Evaluate(DeltaTime)
  │    │    │    │
  │    │    │    ├─ Recursive DAG evaluation (per-frame memoization on all three node kinds):
  │    │    │    │    Leaf → Camera->TickCamera(DeltaTime) → returns pose
  │    │    │    │           (early-out if LastTickedFrameCounter == GFrameCounter)
  │    │    │    │    RefLeaf → SnapshotRoot->Evaluate(DeltaTime) → returns pose
  │    │    │    │           (walks captured TSharedPtr subtree; does NOT call back into a director)
  │    │    │    │    Inner → eval left, eval right, Transition->Evaluate(src, tgt) → blended pose
  │    │    │    │           (early-out with cached blend if reached twice in one frame)
  │    │    │    │
  │    │    │    └─ CollapseFinishedTransitions(RootNode)
  │    │    │         - If inner node's transition is finished: destroy left subtree, promote right
  │    │    │         - If inner node's transition is NOT finished: recurse into left subtree only
  │    │    │         - Leaf/RefLeaf: return as-is
  │    │    │
  │    │    ├─ PreviousEvaluatedPose = LastEvaluatedPose
  │    │    └─ LastEvaluatedPose = tree result
  │    │
  │    └─ Return pose
  │
  ├─ RunningCamera = ContextStack->GetRunningCamera()
  ├─ CurrentContext = ContextStack->GetActiveContextName()
  ├─ CurrentCameraPose = evaluated pose
  │
  ├─ UpdateActions(DeltaTime)  // tick camera actions
  │
  └─ Convert pose → FMinimalViewInfo → apply to viewport
```

### Camera Tick (inside TickCamera)

When a leaf node evaluates, it calls `Camera->TickCamera(DeltaTime)`:

```
TickCamera(DeltaTime)
  │
  ├─ LastFrameCameraPose = CameraPose  // save previous frame
  ├─ If transient: decrement RemainingLifeTime
  │
  ├─ OnActionPreTick.Broadcast(...)    // action pre-tick hook (whole-camera scope)
  ├─ OnPreTick.Broadcast(...)          // node pre-tick hooks
  │
  ├─ For each CameraNode (via FullExecChain, or linear fallback):
  │    For each action in PreNodeTickActions where TargetNodeClass matches Node's class:
  │      Action->OnExecute(DeltaTime, InOutPose, InOutPose)   // per-node pre hook
  │    Node->TickNode(DeltaTime, CurrentPose, OutPose)
  │    For each action in PostNodeTickActions where TargetNodeClass matches Node's class:
  │      Action->OnExecute(DeltaTime, InOutPose, InOutPose)   // per-node post hook
  │    CurrentPose = OutPose            // chain output to next node's input
  │  (FullExecChain also interleaves SetVariable entries between camera nodes)
  │
  ├─ OnPostTick.Broadcast(...)         // node post-tick hooks
  ├─ OnActionPostTick.Broadcast(...)   // action post-tick hook (whole-camera scope)
  │
  ├─ OnUpdateCamera(...)               // Blueprint override opportunity
  │
  └─ CameraPose = final pose
      return CameraPose
```

---

## 4. Context Stack (Tier 1)

### Purpose

The Context Stack manages camera "modes" — gameplay, cinematic, UI overlay, etc. Each mode is a named context with its own Director and evaluation tree, completely independent from other contexts.

### Lifecycle

Contexts are identified by `FName` and must be registered in `UComposableCameraProjectSettings::ContextNames`. The first entry is the base context, initialized during `PCM::InitializeFor` (PostInitializeComponents phase, before any actor's BeginPlay).

**Push**: `EnsureContext(ContextName)` checks if the context exists on the stack. If not, creates a new entry with a fresh Director and pushes it. If it exists already, returns the existing Director. Importantly, "ensure" means move-to-top if already present — position matters because only the top context is evaluated.

**Pop**: `PopContext(ContextName)` or `PopActiveContext()` removes a context. If the popped context is the active (top) one and a transition is provided, the system sets up an inter-context blend: the previous context's Director *resumes its existing running camera in place* via `ResumeCurrentCameraWithReferenceSource` — the running camera and its whole sub-tree are preserved, their per-node state (damping, interpolators, spline progress) continues without reset. The popped entry moves to `PendingDestroyEntries` and stays alive until the transition finishes. If the popped context is not the top one, it is removed immediately.

**Auto-pop**: When the active context's running camera is transient and its lifetime expires (`IsFinished() == true`), the context is automatically popped during `Evaluate()`.

**Base context protection**: The last remaining context (the base) cannot be popped.

### Inter-Context Transitions

There are two distinct inter-context flows, and they use different APIs. Mixing them up is a real source of bugs, so they are spelled out explicitly here.

#### The reference leaf is a SNAPSHOT, not a live pointer

Before either flow, fix this mental model: a `FComposableCameraEvaluationTreeReferenceLeafNodeWrapper` holds a `TSharedPtr<FComposableCameraEvaluationTreeNode> SnapshotRoot` — a shared pointer to the source director's tree root **at the moment the RefLeaf was created**. Subsequent mutations to the source director's tree (for example, the source being popped and having its own pop inner installed at its root) do NOT follow into the RefLeaf; the RefLeaf keeps pointing at the original captured subtree.

Two consequences:

- The evaluation reachable graph is a **DAG with no cycles**. Same original leaf may be reached via multiple paths (e.g. pop side + pop-source snapshot → push-source snapshot → same leaf), but there is never a back-edge that loops to an already-visited node.
- Multiple cameras visible in one frame may correspond to the same UObject instance via different paths. To keep per-node state (damping, interpolators, noise seeds, `RemainingLifeTime`) from double-advancing, `AComposableCameraCameraBase::TickCamera` memoizes on `GFrameCounter` — the first call in a frame ticks and caches, every subsequent same-frame call returns the cached pose. The same memoization principle applies to `FComposableCameraEvaluationTreeInnerNodeWrapper` (so a shared transition's `RemainingTime` advances once per frame) and `FComposableCameraEvaluationTreeReferenceLeafNodeWrapper` (skip the re-walk).

#### Push (A → B, new context on top) — live source, fresh target

1. B's Director activates a new camera with `ActivateNewCameraWithReferenceSource`.
2. B's evaluation tree is built with a root inner node:
   - Left child: reference leaf with `SnapshotRoot = A.Director->GetEvaluationTree()->GetRootNode()` captured right now — typically the `[Leaf] A` node.
   - Right child: leaf wrapping the freshly-spawned B camera.
   - Transition: the blend from A to B.
3. Each frame, the reference leaf re-evaluates the captured A subtree (ticking A's camera). The source director's own `Evaluate()` is **not called** — `A.Director` is untouched by this path. A stays animated because its leaf is live inside the snapshot.
4. When the transition finishes, `CollapseFinishedTransitions` promotes B's leaf to the root; the reference leaf is discarded. The TSharedPtr to A's leaf drops, but A.Director still holds its own reference, so A's leaf stays alive.

#### Pop (B popped, A below resumes) — live source, preserved target

1. A's Director calls `ResumeCurrentCameraWithReferenceSource` — this does NOT spawn a new camera. A's existing `RunningCamera` and its whole sub-tree are preserved.
2. A's evaluation tree is mutated in one place only: the current `RootNode` is wrapped as the right child of a new inner node.
   - Left child: reference leaf with `SnapshotRoot = B.Director->GetEvaluationTree()->GetRootNode()` captured right now — this may be a `[Leaf] B` (push already settled) or an `[Inner push] (RefLeaf → A, [Leaf] B)` (push still in flight).
   - Right child: A's preserved root (same TSharedPtr as before, un-mutated).
   - Transition: the pop blend from B to A.
3. B's entry moves to `PendingDestroyEntries`.
4. When the transition finishes, `CollapseFinishedTransitions` promotes A's preserved root back to the tree root; `OnTransitionFinishesDelegate` fires, A's `PendingDestroyEntry` is cleaned up, and `B.Director->DestroyAllCameras()` runs.

#### Why snapshot semantics work where live pointers don't

Under the old "RefLeaf holds a live `UComposableCameraDirector*`" scheme, a pop-while-push-still-active window produced a cycle: A.tree has `RefLeaf → B.Director`, B.tree still has `RefLeaf → A.Director` from the push, and `A.Evaluate → B.Evaluate → A.Evaluate` loops. Band-aid defenses (reentrancy guard returning `LastEvaluatedPose`, force-freezing the source on pop) either still leaked feedback through the cached-pose field or papered over the cycle by discarding a semantically meaningful frame of animation.

With snapshots: A's new pop-side RefLeaf captures B.tree.root **at pop time**, and that snapshot includes B's push-side `RefLeaf → A_old_leaf`. The `A_old_leaf` captured there is the raw A leaf (which is also the Right child of A.tree's new root) — the same TSharedPtr. So the DAG is:

```
A.new_root (Inner pop)
  ├─ Left  : RefLeaf ──► B.push_Inner
  │                        ├─ Left  : RefLeaf ──► A_old_leaf  (same ptr as below)
  │                        └─ Right : B_leaf
  └─ Right : A_old_leaf  (same ptr as above)
```

No self-reference. `A.Evaluate` walks its tree in one linear pass, `A_old_leaf` is reached twice but only ticks A once thanks to `LastTickedFrameCounter`, and `B.Evaluate` is never called from anywhere. Pop blend at the top uses a live B pose (from walking the snapshot) as source and a live A pose as target — exactly what "blend from what the player was seeing to A" means semantically.

#### Why the pop path preserves the existing camera

Spawning a fresh instance of A's camera class at pop time (the `ActivateNewCameraWithReferenceSource` approach used for pushes) would reset every node's internal state — damping history, interpolator `bStartFrame` flags, spline progress, noise seeds. A's camera has been ticking throughout B's lifetime via the push-side reference leaf snapshot, so there is continuous state to preserve; tearing it down at pop time produces a visible snap on the first post-pop frame. `ResumeCurrentCameraWithReferenceSource` only mutates the tree topology (wrapping the root), leaving every camera and node untouched.

### Stack Data Structure

```cpp
TArray<FComposableCameraContextEntry> Entries;          // LIFO: [0] = base, Last() = active
TArray<FComposableCameraContextEntry> PendingDestroyEntries;  // popped but in transition

struct FComposableCameraContextEntry {
    UComposableCameraDirector* Director;
    FName ContextName;
    FComposableCameraPose LastPose;
};
```

---

## 5. Evaluation Tree (Tier 2)

### Structure

The evaluation tree is a binary tree with three node types stored in a `TVariant`:

```cpp
TVariant<LeafNodeWrapper, ReferenceLeafNodeWrapper, InnerNodeWrapper>
```

**Leaf**: Wraps a single `AComposableCameraCameraBase*`. Evaluating it ticks the camera and returns its pose.

**Reference Leaf**: Holds a `TSharedPtr<FComposableCameraEvaluationTreeNode> SnapshotRoot` — a shared pointer to another director's tree root **captured at the moment the RefLeaf was created**. Evaluating it walks the captured subtree directly (so the source context's cameras still tick live) without ever calling back into the source director's `Evaluate`. A weak `DebugSourceDirector` pointer is kept only for label display. Does not own any cameras — the captured subtree's nodes are kept alive via `TSharedPtr`, and the camera UObjects inside are destroyed only by their home director's own `DestroyAllCameras`, not by this RefLeaf being discarded.

**Inner**: Wraps a `UComposableCameraTransitionBase*` and owns left (source) and right (target) children via `TSharedPtr`. Evaluation: evaluate left, evaluate right, pass both poses to the transition, return blended result.

### Camera Activation (Tree Building)

**With transition** (most common):
```
Before:          After:
  [CamA]           [Inner: Transition]
                    /              \
              [CamA: source]   [CamB: target]
```
The existing tree becomes the left subtree. A new leaf for the camera becomes the right subtree. A new inner node wrapping the transition becomes the root.

**Without transition** (camera cut):
The existing tree is destroyed and replaced with a single leaf for the new camera.

**With reference source** (inter-context):
```
  [Inner: Transition]
   /              \
[RefLeaf: OldDir]  [CamB: target]
```

**Nested activation** (intra-context under inter-context root):

When the root is an inter-context transition (left child is a reference leaf), new intra-context activations nest under the right subtree instead of wrapping the entire tree. This preserves the inter-context blend while allowing camera switches underneath:
```
Before:                          After activating CamB2:
  [InterCtx]                       [InterCtx]
   /        \                       /        \
[RefLeaf]  [CamB1]             [RefLeaf]  [IntraCtx]
                                            /       \
                                        [CamB1]   [CamB2]
```
When the inter-context transition finishes first, the tree collapses to just the right subtree (the intra-context blend). When the intra-context blend finishes, it collapses to the latest camera. This is relevant when gameplay code activates a new camera in the active context while an inter-context blend is still running.

### Collapse Logic

After evaluation, `CollapseFinishedTransitions` walks the tree:

- **Inner node, transition finished**: Destroy left subtree cameras, promote right subtree, fire `OnTransitionFinishesDelegate`. The inner node is replaced by its right child.
- **Inner node, transition NOT finished**: Recurse into both subtrees. The left side may have chained transitions that finish while this one is active. The right side may have nested intra-context transitions (from LS camera cuts) that finish independently.
- **Leaf / Reference Leaf**: Return as-is.

This produces a natural right-grows pattern: older cameras collapse away from the left while the rightmost path always leads to the current running camera.

### GC Integration

The tree uses `TSharedPtr` for node ownership (since `USTRUCT` cannot be `UPROPERTY` with shared pointers to itself). To prevent UObject garbage collection of cameras and transitions held in the tree, `AddReferencedObjects` walks the tree recursively and registers all UObjects with the collector.

---

## 6. Cameras

### AComposableCameraCameraBase

Cameras derive from `ACameraActor` and are the fundamental evaluation unit. The class is marked `NotBlueprintable` — Blueprint subclassing is forbidden. All camera behavior is defined through `UComposableCameraTypeAsset` data assets, which describe the node composition, pin connections, parameters, and transitions. At runtime, the system spawns the base class and populates it from the type asset.

A camera does not define behavior through virtual functions — instead, it holds an ordered array of `UComposableCameraCameraNodeBase*` nodes that execute sequentially.

Key properties:

- `CameraTag` (FGameplayTag): Identifier used by modifiers to target specific cameras
- `EnterTransition`: Fallback transition for resume/reactivation scenarios
- `CameraNodes[]`: Array parallel to `NodeTemplates` — `CameraNodes[i]` is the runtime duplicate of `NodeTemplates[i]`. Entries for nodes not referenced by the execution chain are nullptr (orphaned nodes are skipped during duplication to save memory and init cost). Per-frame execution is driven by `FullExecChain`, not array order.
- `ComputeNodes[]`: Array parallel to `ComputeNodeTemplates` — `ComputeNodes[i]` is the runtime duplicate of `ComputeNodeTemplates[i]`. Same skip-unconnected rule as `CameraNodes`: orphaned entries are nullptr. Populated by `OnTypeAssetCameraConstructed`, which registers each non-null duplicate into the shared runtime data block using the offset `NodeTemplates.Num() + i` so compute and camera node pins live in a single flat pin space at runtime.
- `CameraPose` / `LastFrameCameraPose`: Current and previous frame poses
- `bIsTransient`, `LifeTime`, `RemainingLifeTime`: Transient camera lifecycle
- `SourceTypeAsset` (`TObjectPtr<UComposableCameraTypeAsset>`, strong): The type asset that built this camera. Stored during `OnTypeAssetCameraConstructed` so `PCM::ReactivateCurrentCamera` can restore the pending state and fully reconstruct the camera on reactivation. Strong (not weak) because the camera physically depends on this asset to rebuild — a transiently-loaded source (soft path / DataTable row / BP local) would be reclaimed under a weak ref between activation and the next modifier-triggered reactivate, leaving an empty-shell camera. The runtime DataBlock still has to mark its slot `UScriptStruct` types independently because it isn't reflection-walked through this field.
- `SourceParameterBlock` (`FComposableCameraParameterBlock`): The caller-provided parameters applied at activation from a type asset. Stored alongside `SourceTypeAsset` for the same reactivation purpose.
- `TypeAssetNodeTemplateCount` (`int32`): The exact count of `TypeAsset->NodeTemplates` at construction time. Used as the base offset for compute-node pin keys in the RuntimeDataBlock (`compute node i` has pin key `NodeIndex = TypeAssetNodeTemplateCount + i`). Stored explicitly because `CameraNodes.Num()` can differ from `NodeTemplates.Num()` if `OnTypeAssetCameraConstructed` skips null templates during duplication.

**GC hazard — Actor/Object pins in RuntimeDataBlock:** The RuntimeDataBlock stores all values (including `AActor*` and `UObject*` pointers) as type-erased bytes in a flat `TArray<uint8>`. The garbage collector cannot see these pointers. If a referenced actor is destroyed, the data block retains a dangling pointer (not null). Nodes that read Actor pins per-frame via `GetInputPinValue<AActor*>()` must use `IsValid(Actor)` instead of `Actor != nullptr` before dereferencing. Nodes that receive Actor values through subobject pin application (`ApplySubobjectPinValues`) are safe — the value is written into a GC-tracked `UPROPERTY` during one-shot initialization, and the GC can null it if the actor is destroyed afterward.

### Lifecycle

Cameras are spawned from the base class with nodes duplicated from a `UComposableCameraTypeAsset` inside an OnPreBeginplay callback.

**Shared activation spine** (`Director::ActivateNewCamera`):

1. `SpawnActorDeferred<CameraClass>(InitialTransform)` — the actor is allocated but not yet running `BeginPlay`.
2. `ForceCameraPoses(Camera, InitialTransform)` — seeds `CameraPose` and `LastFrameCameraPose` so the first tick sees a consistent starting pose.
3. `Camera->Initialize(PlayerCameraManager)` — stores `CameraManager` and calls `InitializeNodes()`, which walks `CameraNodes` and per node: calls `Node->Initialize(OwningCamera, PCM)` (which in turn fires `OnInitialize` — the per-node one-shot hook) and wires the `OnPreTick` / `OnPostTick` delegates. It then walks `ComputeNodes` and calls `Node->Initialize` on each compute node without wiring tick delegates — compute nodes get the same pin-system plumbing as camera nodes but must not burn per-frame cycles. For type-asset cameras both arrays are still empty at this point and the loops are no-ops — the real per-node init happens in step 4.
4. `OnPreBeginplay.ExecuteIfBound(Camera)` — the activation callback, bound to `OnTypeAssetCameraConstructed`, which: (a) renames the camera actor to `Camera_<TypeAssetName>` (both internal `Rename` and editor-visible `SetActorLabel`) so it is identifiable in the World Outliner and debug logs; (b) clears both `CameraNodes` and `ComputeNodes`, pre-sizes each to match `NodeTemplates.Num()` / `ComputeNodeTemplates.Num()`, then duplicates only those templates whose index appears in the execution chain (`FullExecChain` or `ExecutionOrder`); orphaned nodes (not referenced by any exec entry) are left as nullptr to save memory and init cost while preserving index correspondence with the type asset's layout; (c) builds a flat `OwnedRuntimeDataBlock` whose slot count equals `NodeTemplates.Num() + ComputeNodeTemplates.Num()`, calls `SetRuntimeDataBlock` on each non-null camera node using index `i` and on each non-null compute node using the offset `NodeTemplates.Num() + i`; and (d) calls `Camera->InitializeNodes()` explicitly — this is the moment every non-null node's `Node::Initialize` (and therefore `OnInitialize_Implementation`) runs. The offset is the *only* place in the runtime or editor where the two NodeIndex spaces cross — in the editor's durable state they are strictly disjoint (`NodeTemplates` indices and `ComputeNodeTemplates` indices both start at zero and don't overlap).
5. `PlayerCameraManager->ApplyModifiers(Camera, true)` — effective per-node-class modifiers are applied. This runs *after* step 4 so that `CameraNodes` is fully populated before modifiers iterate it.
6. `Camera->FinishSpawning(InitialTransform)` — drives `AActor::BeginPlay`, which calls `BeginPlayCamera()`. When a `ComputeFullExecChain` is available, `BeginPlayCamera` walks that chain, executing compute nodes and interleaving `SetVariable` node evaluations to populate scratch variables. Otherwise it falls back to a linear walk of `ComputeNodes` in array order, calling `ExecuteBeginPlay()` on each non-null compute node. By the time this runs, per-node `Initialize` has already fired for every compute node (in step 4), so compute nodes are free to use the pin system, internal variables, and `OwningPlayerCameraManager->GetCurrentCameraPose()` from inside `ExecuteBeginPlay`. On a camera with no compute nodes the loop is a harmless no-op.
7. `EvaluationTree::OnActivateNewCamera(Camera, Transition)` — the new camera is wired into the tree as the right (target) subtree of a new inner node, or as a plain leaf if there is no transition.

After activation: each subsequent frame, `TickCamera()` executes the full node pipeline — either via `FullExecChain` (when available) with interleaved `SetVariable` dispatch, or via linear `CameraNodes` walk (fallback). On collapse (transition finished): the camera actor is destroyed via `DestroySubtreeCameras`.

**Per-node one-shot setup** — the rule is: everything a per-frame node needs to do exactly once per activation (caching `OwningCamera` / `OwningPlayerCameraManager`, instantiating internal objects, reading exposed parameters, seeding per-activation state) belongs in `OnInitialize_Implementation`, which runs from inside `Node::Initialize`. There is no longer a separate `OnBeginPlayNode` hook. Nodes that previously needed the outgoing camera's pose (what `BeginPlayNode` used to pass as `CurrentCameraPose`) can read the same value via `OwningPlayerCameraManager->GetCurrentCameraPose()` — this is exactly what `AActor::BeginPlay` passed into `BeginPlayCamera` before the refactor.

**Camera-level one-shot compute** — any logic that is *not* about a single node's own setup but is instead about shaping data shared across the camera (e.g., precomputing an offset transform, deriving a blend weight, reading gameplay state and publishing it as an internal variable downstream nodes consume) belongs in a compute node on the `ComputeNodes` chain. Compute nodes derive from `UComposableCameraComputeNodeBase` (itself a subclass of `UComposableCameraCameraNodeBase`) so they inherit the pin system and the `OnInitialize` hook, but the camera deliberately does not register them for `OnPreTick` / `OnPostTick`. They run exactly once per activation, from `BeginPlayCamera`, via `ExecuteBeginPlay()` — a plain `virtual` on `UComposableCameraComputeNodeBase` (not a `BlueprintNativeEvent` in 4a; promote to one if Blueprint authoring of compute nodes becomes a requirement). Execution order is array order in `ComputeNodes`, which for type-asset cameras is produced by the editor's dedicated `UComposableCameraBeginPlayStartGraphNode` sentinel and its linear exec chain (see `EditorDesignDoc.md §8` for the BeginPlay compute chain and its sync/rebuild phases).

### Transient Cameras

A camera can be marked transient with a fixed lifetime. When `RemainingLifeTime <= 0`, `IsFinished()` returns true, and the context stack's auto-pop mechanism fires. Transient cameras always live in their own context — they never share a context with persistent cameras. This design prevents a transient camera from destroying the persistent camera chain when it expires.

### FComposableCameraPose (Pose Structure)

`FComposableCameraPose` is the unit of data that flows along every edge of the evaluation tree. Every camera produces a full pose each frame; every transition takes two full poses and emits a full pose. Blueprint authors and C++ node authors both see the same struct. It is deliberately flat (no nesting) so that it can be passed by value cheaply along hot paths.

**Fields** (grouped by concern):

- **Transform**: `Position` (FVector), `Rotation` (FRotator).
- **FOV (dual-mode)**: `FieldOfView` (double, degrees) and `FocalLength` (float, mm). One of the two is the authoritative source per pose — see the FOV rules below.
- **Physical camera (CineCameraComponent analogs)**: `SensorWidth`, `SensorHeight`, `Aperture`, `FocusDistance`, `ShutterSpeed`, `ISO`, `DiaphragmBladeCount`, `SqueezeFactor`, `Overscan`.
- **Physical application weight**: `PhysicalCameraBlendWeight` (0..1). Scales how strongly the physical fields are written into post-process (DoF + exposure). A pose with weight 0 is effectively "physical-fields ignored at application time" even though its numeric fields still participate in blending.
- **Projection / aspect**: `ProjectionMode` (Perspective / Orthographic), `ConstrainAspectRatio` (bool), `OverrideAspectRatioAxisConstraint` (bool), `AspectRatioAxisConstraint` (enum), `OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`.
- **Post-process**: `PostProcessSettings` (FPostProcessSettings). Default-constructed with all `bOverride_*` flags off, meaning "no opinion". Nodes (e.g., a future PostProcessNode) set specific `bOverride_*` flags and values. During blending, `FPostProcessUtils::BlendPostProcessSettings` lerps all properties including override flags. At apply-time in `GetCameraViewFromCameraPose`, pose PP is layered on top of the camera component's baseline PP via `FPostProcessUtils::OverridePostProcessSettings` (only properties with `bOverride_*` true take effect), followed by physical camera settings as before.

**FOV dual-mode and the "resolve before blend" rule.** A pose can describe its FOV either directly in degrees (`FieldOfView > 0`, `FocalLength <= 0`) or via the physical camera (`FocalLength > 0`, `FieldOfView <= 0`). `GetEffectiveFieldOfView()` resolves either form to a single degrees value using the standard `2·atan(croppedSensorWidth · Overscan / (2 · FocalLength))` formula for the physical side. **Blending never interpolates raw `FieldOfView` or raw `FocalLength` directly.** Both sides are resolved to degrees first, the degrees values are lerped, and the result is written as a degrees-mode pose (`FocalLength = -1`). This is mandatory for two reasons: (1) focal length is non-linear in FOV (24mm → 35mm is visually much larger than 85mm → 96mm) — linear blending of mm would produce a visibly wrong curve; (2) when one side is in physical mode and the other in degrees mode, there is no meaningful way to lerp mm against mm-from-nothing. Node authors who set FOV from code must use `SetFieldOfViewDegrees(...)` (which clears the focal-length sentinel) so downstream consumers see an unambiguously degrees-mode pose.

**Blend semantics by field category:**
- Numeric physical fields (sensor, aperture, ShutterSpeed, ISO, DiaphragmBladeCount, SqueezeFactor, Overscan, PhysicalCameraBlendWeight): linear lerp.
- `FocusDistance`: `LerpOptional` — treats values `<= 0` as "unset". If both are valid, lerp; if only one is valid, pass it through; if neither, stay unset. This matches CineCamera's "-1 means use the tracking actor" convention.
- Transform and FOV: described above.
- `ProjectionMode`, `ConstrainAspectRatio`, `OverrideAspectRatioAxisConstraint`, `AspectRatioAxisConstraint`: snap-at-50% rule. These types are not meaningfully lerpable — a "half-ortho, half-perspective" pose is nonsense. The output takes the source value for `t < 0.5`, the target value for `t ≥ 0.5`.
- Orthographic fields (`OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`): linear lerp. Safe to interpolate even while `ProjectionMode` snaps.
- `PostProcessSettings`: blended via `FPostProcessUtils::BlendPostProcessSettings`. All properties (numerics and `bOverride_*` booleans) are interpolated; booleans snap at 50% like the projection fields. A camera with no PostProcess node has all overrides off — blending against it naturally fades the overridden properties toward their defaults and turns off the override flags at the 50% mark.

`BlendBy(Other, Alpha)` is the single entry point for blending one pose into another using all the rules above. Every transition's `OnEvaluate` should start its result pose from `CurrentSourcePose`, call `BlendBy(CurrentTargetPose, Alpha)`, and then overwrite only the fields it specifically customizes (e.g., a cylindrical transition overwrites Position and Rotation after BlendBy; an inertialized transition does the same with inertializer-derived values). Transitions must not hand-lerp individual pose fields — if they do, any new pose field added in the future will silently drop out of that transition.

**Pose is authoritative over CameraComponent.** For projection, aspect, and post-process, the pose holds the truth. `GetCameraViewFromCameraPose` copies `ProjectionMode`, `ConstrainAspectRatio`, and the override/axis fields from the pose into the outgoing `FMinimalViewInfo`. The `ACameraActor`'s `CameraComponent` contributes its `AspectRatio` numeric value and its `PostProcessSettings` as the base layer. The pose's `PostProcessSettings` is then applied on top via `FPostProcessUtils::OverridePostProcessSettings` (only overridden properties take effect), followed by `ApplyPhysicalCameraSettings` for DoF/exposure. This three-layer stack (component baseline → pose PP → physical camera) means cameras without a PostProcess node pass through the component's PP unchanged, while cameras with one can override specific properties without wiping the baseline. Nodes that want to change projection/aspect/PP do so by writing the pose, not by mutating the actor.

**Applying physical fields.** `ApplyPhysicalCameraSettings(PostProcessSettings, bOverrideBlendWeight)` writes DoF and exposure into a `FPostProcessSettings`, scaled by `PhysicalCameraBlendWeight` (or forced to full if `bOverrideBlendWeight == true`). PCM calls this after the camera component's baseline is captured, so per-frame physical authoring layers cleanly without wiping user-authored PP elsewhere on the component. `FocusDistance <= 0` is treated as "leave DoF focal distance unset" — only the other DoF fields are applied in that case.
---

## 7. Camera Nodes

### Execution Model

Nodes are the atomic units of camera behavior. Each node is a `UObject` subclass (instanced, edit-inline). The base class `UComposableCameraCameraNodeBase` is `NotBlueprintable` — all built-in node types are authored in C++. For user-authored nodes, `UComposableCameraBlueprintCameraNode` provides a `Blueprintable` abstract base that exposes `InitializeNode`, `TickNode`, and `GetPinDeclarations` as overridable Blueprint events. Blueprint subclasses are auto-discovered by the camera type asset editor via class iteration.

**Per-activation (once):** `Node::Initialize(OwningCamera, PCM)` is called from `Camera::InitializeNodes` — explicitly from `OnTypeAssetCameraConstructed` after nodes have been duplicated from the type asset. It caches `OwningCamera` / `OwningPlayerCameraManager`, auto-applies subobject pin values, and then fires `OnInitialize()` — a `BlueprintNativeEvent` whose `_Implementation` is where nodes instantiate internal objects, cache component refs, and seed per-activation state. C++ node subclasses override `OnInitialize_Implementation` and should call `Super::OnInitialize_Implementation()` when chaining.

**Per-tick (every frame):**

1. `OnPreTick()` fires for all nodes (via delegate)
2. For each node in order: `TickNode(DeltaTime, CurrentPose, OutPose)` — the node reads `CurrentPose`, applies its logic, writes `OutPose`
3. The output of one node becomes the input of the next (sequential pipeline)
4. `OnPostTick()` fires for all nodes

### Two Parameter Types

Nodes have two kinds of configurable values:

**Input Parameters**: The node's own UPROPERTY members. Authored in the type-asset editor. Two distinct roles:

- **Non-pin UPROPERTYs** — e.g. internal mode selectors, cached state, debug flags. Stay constant for the camera's lifetime unless a modifier or initializer touches them.
- **Pin-matched UPROPERTYs** — a UPROPERTY whose `FName` exactly matches a pin name declared in `GetPinDeclarations()`. Refreshed *every frame* before `OnTickNode` runs via the base class's auto-resolve (see "Top-level Pin Auto-Resolution" below). The UPROPERTY's authored value is used only as the bottom-of-chain fallback; any wire, exposed parameter, or per-instance default override wins.

**Pin Values**: Typed data slots declared via `GetPinDeclarations()` and stored in the `RuntimeDataBlock`. Input pins can be wired from another node's output, exposed as parameters on the activation K2 node, fall back to a per-instance default override, or — when matched to a UPROPERTY — fall back to the node's UPROPERTY value. Output pins write values that downstream nodes can read. This is how nodes communicate: one node writes an output pin (e.g., pivot position), and downstream nodes read it via their input pins.

The `RuntimeDataBlock` storage layer has two parallel pools: a contiguous `Storage` byte array for bytewise-safe pin values (numeric / Name / Vector / Rotator / Transform / FFloatInterval / engine math whitelist / explicit `STRUCT_IsPlainOldData` USTRUCTs) and a typed `StructSlots` array of `FInstancedStruct` for all other struct pin values. Generic user USTRUCTs are not accepted by reflected-property heuristics because non-UPROPERTY members are invisible to reflection. Offsets stored in the per-role lookup maps (`OutputPinOffsets`, `ExposedParameterOffsets`, `InternalVariableOffsets`, `ExposedInputPinOffsets`, `DefaultValueOffsets`, `InputPinSourceOffsets`) discriminate the two pools by magnitude — values `>= StructSlotsOffsetBase` (`INT32_MAX/2`) index into `StructSlots`, smaller values are real byte offsets in `Storage`. The dispatch lives in templated `ReadValue<T>` / `WriteValue<T>` via `if constexpr (TModels_V<CStaticStructProvider, T>)` plus a runtime offset check, so call sites stay unified — a node Tick emitting `WriteOutputPin<FMyStruct>(...)` works the same whether the struct is POD or not. Runtime slot metadata records `{PinType, Size, StructType}` for every offset; typed access refuses unknown offsets, pin-type mismatches, size mismatches, and same-size cross-struct reads/writes before memcpy or `CopyScriptStruct`. Non-template paths that need the same dispatch (the auto-resolve loop's Struct case, subobject pin resolution) call `ResolveInputPinOffset` to get the offset, then branch on `IsStructSlotOffset`.

For **input pins matched to a UPROPERTY**, subclass code can read the UPROPERTY member directly — no `GetInputPinValue<T>()` call is needed, because `TickNode` re-resolves the member before `OnTickNode_Implementation` runs. The auto-resolve writer uses memcpy for POD pins and `Property->CopyCompleteValue` for non-POD struct pins (which routes through each property's `operator=` so embedded `FString` / `TArray` / object refs get a proper per-property copy). Per-frame copy into a non-POD struct UPROPERTY is no-alloc once embedded heap-owned members fit their existing capacity — the warmed-up steady state is just memcpy of bytes into the existing FString buffer; first-tick or grow events allocate once and that allocation is bounded per camera lifetime. For **input pins with no UPROPERTY backing** (typically Blueprint-authored nodes that introduce pins not tied to a reflected field), subclasses still call `GetInputPinValue<T>("PinName")`. Output pins always go through `SetOutputPinValue<T>("PinName", Value)`.

### Top-level Pin Auto-Resolution

To keep subclass `OnTickNode` code free of repetitive `GetInputPinValue<T>(FName)` calls, the base `TickNode()` re-resolves every declared input pin that has a matching top-level UPROPERTY into that UPROPERTY each frame, before `OnTickNode_Implementation` fires. The flow is:

1. On first call for a given UClass, `GetOrBuildPinBindings()` walks the CDO's `GetPinDeclarations()` output, indexes the class's edit-visible top-level UPROPERTYs by FName, and emits one `FComposableCameraNodePinBinding` per pin that (a) is an Input, (b) has a matching UPROPERTY, and (c) has a pin type that matches the UPROPERTY's reflected type via `TryMapPropertyToPinType`. The table is cached module-locally keyed by `UClass*`.
2. `TickNode` calls `ShouldAutoResolveInputPins()` (default `true`) and, if allowed, calls `ResolveAllInputPins()` — a tight switch-dispatch loop that performs `RuntimeDataBlock->TryResolveInputPin<T>()` for every bound pin and writes the result directly into the node's field at the recorded byte offset.

Subobject property pins (compound names like `Interpolator.Speed`) are **not** touched by `ResolveAllInputPins` — they continue to be resolved once at `Initialize()` via `AutoApplySubobjectPinValues`, which matches the one-shot lifecycle of interpolator/subobject state.

**Opt-out.** Nodes that manage their own pin reads (rarely needed — e.g. nodes that keep a UPROPERTY as a non-pin cached state that is coincidentally named the same as a pin — can override `ShouldAutoResolveInputPins()` to return `false`.

**Requirements for a pin to participate.** The pin and UPROPERTY names must match exactly (case and — for bools — the `b` prefix). The UPROPERTY must be `EditAnywhere` (or otherwise carry `CPF_Edit`) and must not be an Instanced subobject reference. Type mismatches are silently skipped at build time; the editor-side pin validator flags them at asset-save.

**Cost model.** One unordered-map `Find` per pin per frame plus one typed memory write. No reflection in the hot path. The cache builder runs once per concrete UClass on first activation.

**Known limitation.** The cache is keyed by `UClass*`. When a Blueprint-subclassed node is recompiled, the old cache entry becomes unreachable but is not evicted until the editor process ends. The new class rebuilds fresh. No runtime correctness issue; a small memory growth in iterative BP authoring sessions.

**Blueprint authoring notes.** The binding builder walks the full UClass hierarchy via reflection, so Blueprint-added variables on `UComposableCameraBlueprintCameraNode` subclasses participate in auto-resolve exactly like C++ UPROPERTYs. The rules:

- A BP variable whose name exactly matches a declared pin (and whose type maps cleanly via `TryMapPropertyToPinType`) gets overwritten each frame by the resolved pin value. Authors can read the variable directly in the TickNode event graph without calling `GetInputPinValueFloat` / `GetInputPinValueVector2D` / etc.
- The BP variable must be **Instance Editable** (carry `CPF_Edit`). Non-editable BP variables are skipped by the builder and do not participate.
- Pin names declared in a BP `GetPinDeclarations` override must follow the same exact-FName-match rule as C++ — including the `b` prefix for booleans.
- If a pin-matched BP variable is used as mid-tick scratch storage (state that must persist across frames), override `ShouldAutoResolveInputPins` to return `false` on that BP subclass, or rename the variable so it no longer collides with the pin name and fall back to `GetInputPinValueX` for reads.
- BP recompile invalidates the cache entry for the old `UClass*`. The next camera activation after recompile rebuilds fresh bindings — no editor restart required.

### Built-in Nodes (Summary)

| Node | Purpose |
|---|---|
| `ReceivePivotActorNode` | Reads an actor's position and writes it to an output pin |
| `PivotOffsetNode` | Offsets the pivot position in world/actor/camera space |
| `CameraOffsetNode` | Applies an offset in camera-local space |
| `LookAtNode` | Rotates camera to face a target (hard or soft constraint) |
| `ControlRotateNode` | Reads Enhanced Input and applies yaw/pitch rotation |
| `AutoRotateNode` | Auto-rotates back toward a reference forward when yaw/pitch leave the valid range, rotating to the nearest boundary. Reference direction is either an explicit vector (`DirectionMode = Direction`) or an actor's forward vector (`DirectionMode = ActorForward`, via `PrimaryActor`). When `bYawOnly = false`, ANY axis leaving its range engages auto-rotation on both axes, driven as a unified rotation by a single `FRotator` interpolator so yaw and pitch land together. Optional user-input interrupt (`bInterruptOnUserInput`) with a cooldown and a per-instance max-interrupt count (`MaxCountAfterInputInterrupt`) for "player keeps fighting it, stop trying" semantics. |
| `PivotRotateNode` | Synchronises the camera's rotation to a `PivotActor`'s world rotation each frame, with a per-pivot-local-frame `RotationOffset` (composed as `PivotActor.Quat * RotationOffset.Quat` — same convention as `USceneComponent::RelativeRotation`) and an optional Instanced `Interpolator` (rotator). Standard interpolator idiom (re-seed Current ← live rotation, Target ← resolved target, advance one DeltaTime step) — matches `AutoRotateNode` / `LookAtNode` soft-mode. Snap mode when `Interpolator` is null; pass-through when `PivotActor` is null. Useful for vehicle / mount / cockpit cameras that should adopt the rig's heading with a fixed relative offset and smooth catch-up. Differs from `LookAtNode` (computes a *gaze* direction from camera→target) and from `RelativeFixedPoseNode` (locks the *full* pose, not just rotation). |
| `CollisionPushNode` | Dual-mode collision resolution: (1) **Trace collision** — casts a line or sphere trace from pivot to camera, pushing the camera toward the pivot on occlusion, with configurable exemption time. (2) **Self collision** — carries a sphere around the camera and, when the sphere overlaps an obstacle, pushes the camera to the **far side** of the obstacle via a reverse sphere sweep from beyond the camera back toward the pivot. Both modes share the same interpolator pair (push/pull) and ignored-actor list. |
| `OcclusionFadeNode` | Fades occluding / proximate primitives by swapping their materials for a user-supplied transparency material. Two independent detection paths feeding one swap pipeline: (A) **Line-of-sight occlusion** — async multi-sphere sweep from camera → PivotActor each frame; hits passing the component-tag + mesh-type filters get fade-marked. (B) **Proximity fade** — sphere overlap at the camera position; actors of `ProximityActorClass` (default APawn) within `ProximityRadius` get their fadable components marked. Delta tracking against `AppliedMaterialOverrides` means SetMaterial / CreateDynamicMaterialInstance only fires on entering/leaving the faded set. Fade shape + timing live entirely in the swap material's shader. Complements `CollisionPushNode` — CollisionPush physically moves the camera around obstacles, OcclusionFade ghosts obstacles you don't want the camera to move around (thin pillars, tagged foliage, the player body when camera zooms close). |
| `HitchcockZoomNode` | Dolly-zoom / Vertigo effect — camera dollies along the camera→subject axis while FOV changes in the opposite direction, keeping subject on-screen size constant while the background perspective warps. Captures `InitialDistance` / `InitialFOV` / `LockConstant = InitialDistance · tan(InitialFOV/2)` on first tick; each subsequent tick preserves the lock constant while solving one of (FOV, Distance) from the other. Driver enum picks which quantity the author controls: `FromFOVDelta` (author FOV trajectory, distance derives) or `FromDistanceDelta` (author dolly distance, FOV derives). Both curves are **additive deltas** (X=[0,1] normalized time, Y=0 at t=0), so they stay portable across cameras with different initial FOVs. Direction is resampled every tick from the upstream pose so upstream `LookAt` / `CameraOffset` still steer rotation during the effect — Hitchcock owns only the radial distance + FOV. Writes `FieldOfView` + clears `FocalLength` to -1 so the pose is in FOV-mode; pair with an upstream `LensNode` (set `bOverrideFieldOfViewFromFocalLength=false` so LensNode doesn't contest FOV) to keep aperture / filmback / physical blend weight intact. Once-only playback (no Loop/PingPong); optional distance clamp is a safety rail against pathological curve shapes. |
| `FocusPullNode` | Dynamically drives the pose's `FocusDistance` from the distance to a target actor (PivotActor + optional BoneName / PivotZOffset, same pattern as CollisionPush / OcclusionFade). Single-responsibility — only writes `FocusDistance`. Aperture / blade count / `PhysicalCameraBlendWeight` come from an upstream `LensNode`; the intended pairing is `LensNode(FocusDistance=-1) → FocusPullNode(drives FocusDistance dynamically)`. Optional `FFloatInterval` clamp and optional interpolator for focus-pull damping (first-frame seed bypasses the lerp so initial focus snaps to the real distance). When `bEnableFocusPull` is false the node is a pass-through this tick — useful for Blueprint-driven "focus hold" (ADS, cinematic freeze, etc.). |
| `VolumeConstraintNode` | Constrains the camera to stay inside a single Box or Sphere volume. Volume source is either an actor with a `UShapeComponent` (FromActor, first shape wins) or inline world-space definition (VolumeCenter / Rotation / BoxExtents / SphereRadius). Each tick, if the upstream position is outside the volume it's projected to the nearest boundary point (OBB: per-axis local-space clamp; Sphere: center + normalized delta × radius). Optional `ClampInterpolator` (instanced interpolator, three per-axis 1D instances) smooths the output to eliminate release snaps at the boundary and crease artifacts when nearest-point face switches across a corner. When the interpolator is null the node is stateless and deterministic. Chain placement: after position-writing nodes (`CameraOffset` / `LookAt`) that produce the desired position, before `CollisionPush` so collision resolves on the already-clamped input. |
| `FieldOfViewNode` | Sets FOV in degrees (via `SetFieldOfViewDegrees`), optionally dynamic based on actor scale. Writes to `FComposableCameraPose::FieldOfView` and clears `FocalLength`. |
| `LensNode` | Authors physical-lens parameters on the pose: `FocalLength`, `Aperture`, `FocusDistance`, `DiaphragmBladeCount`, `PhysicalCameraBlendWeight`. When `bOverrideFieldOfViewFromFocalLength` is true, also clears `FieldOfView` so the pose resolves FOV from `FocalLength + SensorWidth`. Gates DoF / auto-exposure post-process contribution via `PhysicalCameraBlendWeight`. |
| `FilmbackNode` | Authors sensor and aspect-ratio parameters on the pose: `SensorWidth`, `SensorHeight`, `SqueezeFactor`, `Overscan`, `ConstrainAspectRatio`, `OverrideAspectRatioAxisConstraint`, `AspectRatioAxisConstraint`. Sensor dimensions feed into the pose's focal-length-mode FOV resolution. |
| `OrthographicNode` | Switches the pose into orthographic projection and authors `OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`. Transitions snap `ProjectionMode` at 50% blend weight per the `BlendBy()` contract. |
| `RotationConstraints` | Constrains yaw/pitch within defined ranges |
| `ScreenSpacePivotNode` | Keeps pivot within screen-space bounds |
| `PivotDampingNode` | Dampens pivot position changes |
| `RelativeFixedPoseNode` | Maintains pose relative to a transform or actor |
| `DirectionalMoveNode` | Moves from `InitialTransform` along a camera-local `Direction` at `Speed`. `Duration < 0` moves forever; `Duration >= 0` clamps travel time and then holds the final offset. |
| `TwoPointMoveNode` | Moves from `SourceTransform` to `TargetTransform` over `Duration`, optionally shaped by a normalized float curve. |
| `SplineNode` | Places camera on a spline (multiple spline math backends) |
| `SpiralNode` | Positions the camera on a helical path around a pivot; orientation left for a downstream `LookAtNode`. Parameterized by three Progress curves (X ∈ [0,1] normalized time, Y in absolute world units): `RadiusCurve` (cm), `HeightCurve` (cm signed along Axis), `AngleCurve` (degrees) — direct-eval semantics same as SplineNode's `AutomaticMoveCurve`, so `Position(t)` is O(1) at any t with no accumulated state. Spiral Space defined by `RotationAxis` (WorldUp / PivotActorUp / Custom) and `ReferenceDirection` (WorldX / PivotActorForward / CameraInitialForward / Custom). Pivot comes from either an Actor or a raw Vector; `PivotOffset` is applied in Spiral Space so "orbit around the character's head" tracks actor rotation. PlayMode: Once / Loop / PingPong. Useful for victory-cam orbits, death-cam rises, ending pull-aways. |
| `ImpulseResolutionNode` | Resolves impulse forces from volumes |
| `MixingCameraNode` | Mixes output from multiple cameras |
| `PostProcessNode` | Applies `FPostProcessSettings` to the pose via `FPostProcessUtils::OverridePostProcessSettings`. Configured entirely in the Details panel (no pins) — works like a PostProcessVolume scoped to a single camera type. Only properties whose `bOverride_*` flag is true take effect; all others pass through from the component baseline or earlier nodes. Multiple PostProcess nodes compose in execution order (later overrides win). |
| `ViewTargetProxyNode` | Lightweight internal node that reads `FMinimalViewInfo` from a target actor's `UCameraComponent` each tick and converts it into `FComposableCameraPose`. **Not user-facing** — created programmatically by `PlayCutsceneSequence`. Supports two modes: static target (reads from a specific actor) or LS polling (walks the CameraCut track each tick to find the active camera at the current playback position). Captures transform, FOV, projection, and post-process from the target actor's evaluated camera state. |

### Level Sequence Compatibility (per node class)

Every node declares via `GetLevelSequenceCompatibility() const` (a `BlueprintNativeEvent`, so BP-authored nodes can override it alongside native ones — C++ overrides go on `GetLevelSequenceCompatibility_Implementation()`) whether it is safe to evaluate through the LS authoring path (§17 explicit path — PCM is null there). Three buckets:

| Bucket | Semantics | Built-in nodes |
|---|---|---|
| `Compatible` (default) | Evaluates correctly without a PCM. | All nodes except those listed below. |
| `RequiresPCM` | Hard-depends on PCM (e.g. spawns child cameras through it). No-ops in LS with a warning surfaced by the Details-panel customization on `FComposableCameraTypeAssetReference`. | `UComposableCameraMixingCameraNode`. |
| `ComputeOnly` | Never evaluated in LS — the whole compute chain is skipped because BeginPlay already ran with empty arrays before nodes were populated. Designers should re-source any value the compute node would publish as an exposed parameter instead. | Every subclass of `UComposableCameraComputeNodeBase`. |

The `ScreenSpaceConstraintsNode` and `ScreenSpacePivotNode` used to be `RequiresPCM` in V1.4's first pass because they resolved viewport size through `PCM->GetOwningPlayerController()->GetViewportSize()`. V1.4 extracted that resolution into `UE::ComposableCameras::TryGetEffectiveViewportSize` (PCM → `GEngine->GameViewport` → 1920×1080 fallback), which lets those nodes run in LS with pixel-exact accuracy in PIE / packaged builds and a 16:9 fallback during editor-world Spawnable preview.

### Built-in Compute Nodes

Compute nodes run once at camera activation (from `BeginPlayCamera`) and publish results that downstream camera nodes consume every frame. They live on the BeginPlay exec chain.

| Node | Purpose |
|---|---|
| `ComputeDistanceToActorNode` | Measures the distance and direction between two actors at activation time. Use to scale boom arm length, set initial FOV, or derive blend weights from actor proximity. Inputs: ActorA, ActorB (Actor). Outputs: Distance (Float), Direction (Vector3D). |

### Typical Node Composition (Third-Person Camera)

```
1. ReceivePivotActorNode     → writes PivotPosition from character
2. PivotOffsetNode           → offsets pivot upward (shoulder height)
3. CameraOffsetNode          → offsets camera behind and to the side
4. ControlRotateNode         → reads player input for orbit rotation
5. CollisionPushNode         → pushes camera forward on wall collision
6. LookAtNode (soft)         → soft look-at toward pivot
7. FieldOfViewNode           → sets FOV with optional dynamic zoom
```

### Subobject Property Pin Exposure

Nodes that own **Instanced UObject subobjects** (e.g. `CollisionPushNode` owns a `PushInterpolator` and `PullInterpolator` of type `UComposableCameraInterpolatorBase*`) sometimes need to let callers override the subobject's individual parameters at runtime — for example, changing the interpolation speed or spring stiffness based on game state. Exposing the entire subobject as a pin doesn't work: Instanced objects carry runtime state, lifecycle ownership, and don't fit the data-flow model that pins are designed for (see §12 Interpolators for the full rationale).

The solution is **subobject property pin exposure**: the base class automatically enumerates individual properties of every `Instanced` UObject UPROPERTY as first-class pins. These pins participate in the normal resolution chain (wire → exposed parameter → per-instance override → UPROPERTY fallback) and the resolved values are written back into the subobject's UPROPERTYs before the node's first tick.

#### Pin Naming Convention

Subobject pins use **dot-separated compound names**: `SubobjectPropertyName.FieldName`.

Examples for `CollisionPushNode`:
- `PushInterpolator.Speed` (Float) — from `UComposableCameraIIRInterpolator::Speed`
- `PushInterpolator.UseFixedStep` (Bool) — from `UComposableCameraIIRInterpolator::bUseFixedStep`
- `PullInterpolator.DampTime` (Float) — from `UComposableCameraSimpleSpringInterpolator::DampTime`

The dot separator is legal in `FName` and visually conveys hierarchy. The prefix avoids collisions when two subobjects expose a property of the same name (e.g. both `PushInterpolator.Speed` and `PullInterpolator.Speed` coexist without ambiguity).

#### Auto-Discovery via GatherAllPinDeclarations

All callers (editor graph nodes, type-asset builder, runtime data-block allocator) use the non-virtual `GatherAllPinDeclarations()` instead of `GetPinDeclarations()`. This method:

1. Calls the virtual `GetPinDeclarations()` chain to collect subclass-declared pins (PivotActor, PivotPosition, etc.).
2. Auto-iterates every `UPROPERTY` on the node class that has the `Instanced` flag (`CPF_InstancedReference` + `CPF_Edit`).
3. For each such property, resolves the subobject pointer and calls `DeclareSubobjectPins()` to append child-property pins.

Node subclasses do **not** need to call `DeclareSubobjectPins` manually — it happens automatically for every Instanced property. The only cases where manual calls are needed are unusual subobject relationships that reflection cannot discover (e.g. subobjects stored in TArray containers).

Under the hood, `DeclareSubobjectPins` does:
1. Iterates the subobject's `EditAnywhere` UPROPERTYs via `TFieldIterator<FProperty>`
2. Maps each property type to `EComposableCameraPinType` via `TryMapPropertyToPinType`. Properties whose type has no pin mapping are silently skipped.
3. Generates a pin declaration with:
   - `PinName` = `SubobjectPropertyName.FieldPropertyName` (e.g. `PushInterpolator.Speed`)
   - `DisplayName` = `SubobjectDisplayName > FieldDisplayName` (e.g. `Push Interpolator > Speed`)
   - `Direction` = Input
   - `PinType` = mapped type
   - `DefaultValueString` = the subobject property's current value serialized to string
4. Properties tagged with `meta=(NoPinExposure)` on the subobject class are skipped, giving subobject authors an opt-out for properties that should never be externally driven.

Generic `UScriptStruct` properties can map to Struct pins. Bytewise-safe structs (engine math whitelist plus explicit `STRUCT_IsPlainOldData`) use byte storage; all other user structs use owned `FInstancedStruct` slots so constructors, destructors, copies, and embedded UObject references stay visible to the runtime data-block lifetime / GC path. Reflected-property heuristics are deliberately not used, because non-UPROPERTY native members would be invisible and could make an unsafe struct look bytewise-safe.

**Dynamic pin set:** when the user changes the Instanced subobject's class in the editor (e.g. switches `PushInterpolator` from `IIRInterpolator` to `SpringDamperInterpolator`), the available sub-properties change. The graph node must call `ReconstructPins()` in response, which re-queries `GatherAllPinDeclarations` and rebuilds the pin array. `ReconstructPins` uses `MovePersistentDataFromOldPin` to carry wired connections onto name-matched new pins; pins that no longer exist (e.g. `PushInterpolator.Speed` after switching to a class without `Speed`) lose their wires silently — the same behavior as any other pin removal in the UE graph system. Old per-instance overrides for pins that no longer exist are pruned during `SyncPhase_RebuildNodePinOverrides`.

#### Auto-Apply at Runtime

`Initialize()` (the non-virtual wrapper on `UComposableCameraCameraNodeBase`) calls `AutoApplySubobjectPinValues()` before dispatching to `OnInitialize()`. This auto-iterates every Instanced property and writes resolved pin values into the subobject's UPROPERTYs. By the time the subclass's `OnInitialize_Implementation` runs, the subobject properties already reflect any wired or exposed overrides — the subclass can immediately build typed instances (e.g. `BuildDoubleInterpolator()`) from the up-to-date config.

Under the hood, `ApplySubobjectPinValues` (called for each Instanced property):
1. Iterates the subobject's `EditAnywhere` UPROPERTYs
2. For each property that has a pin mapping, calls `TryResolveInputPin` with the compound name
3. If resolved, writes the value into the subobject's UPROPERTY via typed pointer write
4. If not resolved, the UPROPERTY retains its authored value (the Instanced editor default)

#### Editor: Details Panel Integration

The Details panel customization (`FComposableCameraNodeGraphNodeDetails`) handles Instanced subobject properties automatically:

1. Detects subobject pin prefixes by scanning `InputPinNameToIndex` for compound pin names containing dots.
2. For each Instanced property whose prefix matches:
   - Adds the parent property row via `AddExternalObjectProperty` with `CustomWidget(false)` to suppress native child expansion — only the class picker is visible.
   - Adds each of the subobject's `EditAnywhere` child properties as separate external-object rows below the parent.
   - For children whose compound pin name exists in `InputPinNameToIndex`, the row is customized with inline "As Pin" checkbox and "[Exposed]" chip, matching the same layout as top-level pin-matched properties.
3. **Subobject class change → pin reconstruction:** the `ForceRefreshDetails` callback on the parent property handle triggers when the user changes the class picker; the detail panel rebuilds, re-queries `GatherAllPinDeclarations`, and the new child properties appear with correct pin matching.

#### Invariants

- **Naming uniqueness.** The compound name `SubobjectPropertyName.FieldName` must be unique across all pins on the node. Since subobject UPROPERTY names are unique within a UClass and the prefix disambiguates across subobjects, this is guaranteed by construction.
- **Null subobject safety.** `DeclareSubobjectPins` and `ApplySubobjectPinValues` silently return if the subobject pointer is null (the user hasn't assigned an interpolator class yet). This means no pins are declared for null subobjects, which is correct.
- **No hot-path allocations.** `ApplySubobjectPinValues` follows the same constraint as the rest of the evaluation pipeline: no `new`, no `TArray` reallocation, no `FString` formatting. Property iteration is pointer arithmetic on the `UClass` metadata; `TryResolveInputPin` is a map lookup; value writes are typed memcpy.

---

## 8. Transitions

### Base Architecture

All transitions derive from `UComposableCameraTransitionBase`. They are pose-only operators — they never hold references to cameras or Directors.

**Lifecycle**:
1. `TransitionEnabled(InitParams)`: Called once when the transition is first wired into the tree. Receives `CurrentSourcePose`, `PreviousSourcePose`, and `DeltaTime` at the moment of creation.
2. First `Evaluate()` frame: `OnBeginPlay()` fires before `OnEvaluate()`. This is where transitions set up internal state (spline control points, inertialization polynomials, etc.) using both the InitParams and the live source/target poses.
3. Each subsequent frame: `RemainingTime` is decremented, `Percentage` is updated, `OnEvaluate()` computes the blended pose from the live source and target poses.
4. When `RemainingTime <= 0`: `TransitionFinished()` is called, which sets `bFinished = true`, fires `OnFinished()`, and broadcasts `OnTransitionFinishesDelegate`.

### InitParams and Velocity

`FComposableCameraTransitionInitParams` captures the source state at the moment of transition creation:
- `CurrentSourcePose`: The blended output the player was seeing (from `Director::LastEvaluatedPose`)
- `PreviousSourcePose`: The previous frame's blended output (from `Director::PreviousEvaluatedPose`)
- `DeltaTime`: Frame delta time

This data is critical for velocity-based transitions (inertialization) which need `(Current - Previous) / DeltaTime` to compute initial velocity.

### Transition Resolution

When switching from camera A (built from type asset `SourceTypeAsset`) to camera B (built from `TargetTypeAsset`), the transition is resolved through a five-tier priority chain implemented in `AComposableCameraPlayerCameraManager::ResolveTransition`:

1. **Caller-supplied override** — the `TransitionOverride` parameter passed to `ActivateNewCameraFromTypeAsset`, `PopCameraContext`, or `TerminateCurrentCameraContext`. Always wins when non-null.
2. **Transition table lookup** — the project-level `UComposableCameraTransitionTableDataAsset` referenced from `UComposableCameraProjectSettings::TransitionTable`. Performs **exact-match only** on (Source, Target) pairs — no wildcards. First matching entry in declaration order wins.
3. **Source's ExitTransition** — `SourceTypeAsset->ExitTransition`. The source camera declares how to leave. Useful for cameras that always exit with a specific transition regardless of target (puzzle cameras, UI overlays, cinematic cameras).
4. **Target's EnterTransition** — `TargetTypeAsset->EnterTransition`. The target camera declares how to enter. This is the existing per-type-asset default.
5. **Hard cut** — no transition; the new camera appears instantly.

The table (tier 2) intentionally does not support wildcards. Per-camera ExitTransition and EnterTransition (tiers 3 and 4) serve as the per-camera fallbacks, covering the "always leave/enter this camera a certain way" use cases that wildcards would otherwise handle — but without the priority conflict that a global wildcard would create by shadowing all per-camera transitions.

Both the in-context activation path (`ActivateNewCameraFromTypeAsset`) and the inter-context pop path (`PopActiveContextInternal`) use the same resolution chain. The `ResolveTransition` helper returns a raw `UComposableCameraTransitionBase*` (owned by the type asset or table entry); callers `DuplicateObject` before mutating.

**`UComposableCameraTransitionTableDataAsset`** is a data asset holding a `TArray<FComposableCameraTransitionTableEntry>`. Each entry has: `SourceTypeAsset` (soft ptr, required), `TargetTypeAsset` (soft ptr, required), and an instanced `Transition`. The asset is referenced from `UComposableCameraProjectSettings::TransitionTable` and is intended as a project-wide routing table for explicit camera-pair transitions. The lookup is O(N) over the entries array; for typical table sizes (tens of entries) this is negligible since it only runs on camera switches, not per-frame. The asset validates entries via `IsDataValid()`: null Source or Target emits an Error (entry is ignored at runtime), null Transition emits a Warning (falls through to lower priority tiers). `UpdateDisplayTitle()` renders inline warnings in the Details panel for each invalid entry.

**`UComposableCameraTransitionDataAsset`** is a thin wrapper around a single instanced `UComposableCameraTransitionBase*`. It exists so transitions can be referenced as standalone data assets (e.g., by the transition table, or as `TransitionOverride` on a DataTable row) without inlining the transition into the referencing object. Registered in the Content Browser under the "Composable Camera System" category with a warm orange color and a custom SVG thumbnail.

**`UComposableCameraNodeModifierDataAsset`** wraps modifier instances for data-driven management. Also registered in the Content Browser under "Composable Camera System" with a purple color and custom SVG thumbnail. See §9 (Modifiers) for runtime details.

### Built-in Transitions

| Transition | Method |
|---|---|
| `LinearTransition` | Linear interpolation (lerp) |
| `CubicTransition` | Cubic easing (smooth start/end) |
| `EaseTransition` | EaseInOut with configurable exponent |
| `SmoothTransition` | Hermite smooth step: `t²(3-2t)` or smoother step: `t³(t(6t-15)+10)` |
| `CylindricalTransition` | Arc around a pivot derived from ray intersection |
| `InertializedTransition` | Physics-based inertialization using 5th-order polynomials for position and rotation. Supports auto-computed transition time from max acceleration. Optional additive curve for shape control. |
| `SplineTransition` | Camera follows a computed spline (Hermite, Bezier, Catmull-Rom, or Arc) with configurable evaluation curves |
| `PathGuidedTransition` | Three-phase transition: Enter (inertialized blend onto spline) → Spline (follow a rail actor) → Exit (inertialized blend to target). Uses an intermediate camera on the rail. |
| `DynamicDeocclusionTransition` | Wraps a driving transition, dynamically adjusting blend weight based on ray-trace visibility |

### Inertialized Transition (Deep Dive)

The inertialized transition computes 5th-order polynomial blends for position (3 axes independently or combined) and rotation (yaw, pitch, roll independently). The polynomial `P(t)` is constructed so that:
- `P(0) = offset` (initial position/rotation error)
- `P'(0) = velocity` (initial velocity from source camera motion)
- `P(T) = 0`, `P'(T) = 0`, `P''(T) = 0` (smooth arrival at target with zero velocity and acceleration)

This produces a physically plausible blend that respects the source camera's momentum.

### PathGuidedTransition Architecture

A three-phase approach for cinematic path transitions:

1. **Enter Phase**: An `InertializedTransition` blends from the source camera's live pose to the spline's start point. Created in `OnBeginPlay` using `InitParams` for velocity data.
2. **Spline Phase**: The intermediate camera follows the rail spline, driven by a `DrivingTransition` and optional `SplineMoveCurve`.
3. **Exit Phase**: An `InertializedTransition` blends from the spline's end point to the target camera. Created lazily at the moment the spline phase ends, using `IntermediateCamera->CameraPose` and `IntermediateCamera->LastFrameCameraPose` for velocity data.

`BuildInternalSpline` duplicates the rail's spline component and prepends/appends control points to connect the source and target positions with C1-continuous tangents.

### Lifecycle and failure modes

The transition spawns world Actors (`IntermediateCamera` for `Inertialized`, `DebugSplineActor` for `Auto`), which means input validation and cleanup are both load-bearing.

- **Input validation runs before any spawn.** `ResolveAndValidateRail` in `OnBeginPlay` sync-loads the `RailActor` soft pointer and rejects null / unloaded rail, missing `RailSplineComponent`, or zero-point splines. On failure, no actors are spawned; the transition leaves `Rail = nullptr` and `OnEvaluate`'s nullcheck hard-cuts to the target pose.
- **Spawned-actor cleanup has two paths.** The normal completion path runs through an `OnTransitionFinishesDelegate` lambda registered once in `OnBeginPlay` immediately after spawn — this fires via `TransitionFinished`'s broadcast regardless of which sub-transitions (`EnterTransition` / lazy `ExitTransition`) were actually constructed. The interrupted path (camera destroyed mid-blend, eval tree pruned, transition replaced) is caught by an overridden `BeginDestroy`. Both paths invoke the same `DestroySpawnedActors` helper, which is `IsValid()`-guarded so double-fire is a no-op.

This pattern (validate first, register cleanup once at spawn, override `BeginDestroy` as backstop, keep the cleanup helper idempotent) is the generic rule for any future transition that spawns world objects. See TechDoc §7.2 for the rationale.

---

## 9. Modifiers

### Purpose

Modifiers provide a way to dynamically alter node parameters at runtime without creating new camera subclasses. A modifier targets a specific node class and overrides its parameters before the node ticks.

### Architecture

```
UComposableCameraModifierBase (abstract, Blueprintable)
  - NodeClass: TSubclassOf<UComposableCameraCameraNodeBase>
  - ApplyModifier(): Blueprint-implementable

UComposableCameraNodeModifierDataAsset
  - Wraps modifier instances for data-driven management

UComposableCameraModifierManager
  - AddModifier() / RemoveModifier()
  - Tracks all active modifiers
  - Computes effective modifiers (highest priority per node class)
  - On change: may trigger camera reactivation with transition
  - Owns the GC root for all stored modifiers + assets via
    static AddReferencedObjects override (the entry struct is a plain
    C++ type living inside non-reflected nested TMaps, so reflection
    alone does not reach the pointers).
```

### Priority System

When multiple modifiers target the same node class, only the highest-priority one is active. When the effective modifier set changes (add/remove), `OnModifierChanged()` on the PCM checks if the running camera is affected and reactivates it with an appropriate transition.

### Reactivation and Type-Asset Cameras

Camera reactivation spawns a fresh camera of the same class and fires the same `OnPreBeginplayEvent` callback that was used during the original activation. For type-asset cameras, this callback is `OnTypeAssetCameraConstructed`, which reads `PendingTypeAsset` and `PendingParameterBlock` from the PCM to reconstruct the camera (duplicate nodes, build data block, wire exec chains).

To support this, each type-asset camera stores its source on construction:

- `SourceTypeAsset` (`TObjectPtr<UComposableCameraTypeAsset>`, strong): the type asset that built this camera. Strong by design — see §5 GC hazard discussion: weak would let GC reclaim a transiently-loaded source asset between activation and reactivate, producing an empty-shell rebuild.
- `SourceParameterBlock` (`FComposableCameraParameterBlock`): the caller-provided parameters applied at activation.

`PCM::ReactivateCurrentCamera` restores these into `PendingTypeAsset` / `PendingParameterBlock` before calling through to the Director, so `OnTypeAssetCameraConstructed` sees valid state and fully reconstructs the new camera. The new camera is stamped with the same source info, so subsequent reactivations also work.

The same restore-and-callback pattern is used during context pop. `ContextStack::PopActiveContextInternal` calls `PCM::PrepareResumeCallback(ResumingCamera)`, which checks `SourceTypeAsset` on the resuming camera, restores the pending state if present, and returns a callback bound to `OnTypeAssetCameraConstructed`. For non-type-asset cameras it returns an empty delegate. This ensures the new camera spawned by `Director::ActivateNewCameraWithReferenceSource` during context pop is fully reconstructed from the type asset, not left as an empty shell.

---

## 10. Actions

Camera actions are persistent or camera-scoped behaviors that run around camera evaluation. Each action declares an `ExecutionType` that picks one of four hook points:

| `ExecutionType` | Fires | Dispatch |
|---|---|---|
| `PreCameraTick` | Once per frame, before any node ticks | Bound to `Camera->OnActionPreTick` multicast delegate |
| `PostCameraTick` | Once per frame, after all nodes tick | Bound to `Camera->OnActionPostTick` multicast delegate |
| `PreNodeTick` | Immediately before a matching node's `TickNode` | Iterated by TickCamera against `PreNodeTickActions`, filtered by `TargetNodeClass` |
| `PostNodeTick` | Immediately after a matching node's `TickNode` | Iterated by TickCamera against `PostNodeTickActions`, filtered by `TargetNodeClass` |

Node-scoped actions take an additional `TargetNodeClass: TSubclassOf<UComposableCameraCameraNodeBase>` and fire once per matching node instance on the camera (exact-class match, same rule as modifiers — §9). If `TargetNodeClass` is unset or no matching node exists, the action is silently ignored. Mutations to the pose during a PreNodeTick action feed into the upcoming `TickNode` input; mutations during a PostNodeTick action feed into the next node's input.

```
UComposableCameraActionBase (abstract)
  ├─ UComposableCameraMoveToAction    — smooth move to target position
  ├─ UComposableCameraRotateToAction  — smooth rotate to target
  └─ UComposableCameraResetPitchAction — reset pitch to zero
```

Actions are managed on the PCM. Camera-scoped actions (`bOnlyForCurrentCamera`) are automatically expired when the camera transitions away. Persistent actions survive across camera switches — `AComposableCameraPlayerCameraManager::BindCameraActionsForNewCamera` re-binds persistent actions onto each newly activated camera (whole-camera actions via delegates, node-scoped actions via `Camera->RegisterNodeAction`).

Expiration for all four execution types is driven centrally by `PCM::UpdateActions`, which runs once per frame before `ContextStack->Evaluate` and removes any action whose `OnCanExecute` returned false. Node-scoped actions do not require any per-node expiration bookkeeping.

---

## 11. Camera Patches

Patches are time-bounded, additively-composable overlays. They sit at the Director level (parallel to the EvaluationTree) and let designers apply effects like a 2-second DollyZoom or a 4-second pivot drift on top of whatever camera is currently active, without authoring a new CameraTypeAsset and without going through the Transition machinery.

### Conceptual model

A Patch is a small CameraBase actor whose node graph reads the upstream pose and writes a modified pose. Each Patch has:

- A **layer index** for ordering when multiple patches stack.
- An **envelope** (Entering → Active → Exiting → Expired) that drives a per-patch alpha in [0, 1] over time, shaped by an ease enum (Linear / EaseIn / EaseOut / EaseInOut / Smooth).
- A **schedule** — bitmask of expiration channels (Duration / Manual / Condition) plus an `bExpireOnCameraChange` flag — that decides when the patch leaves Active.
- A live **evaluator actor** spawned from the Patch's TypeAsset (a subclass of `UComposableCameraTypeAsset`).

The Director's per-frame pipeline is updated to:

```
Director::Evaluate(DeltaTime)
  pose  = EvaluationTree.Evaluate(DeltaTime)        ← unchanged
  pose  = PatchManager.Apply(DeltaTime, pose)       ← NEW
  LastEvaluatedPose = pose
```

`PatchManager.Apply` walks `ActivePatches` (sorted by `LayerIndex` ascending, push-order tiebreaker) and for each:

1. **AdvanceEnvelope** — phase machine updates `CurrentAlpha`.
2. **CheckSchedule** — Active-phase only; checks Duration / Condition / OnCameraChange channels. A triggering channel calls a shared `TransitionPatchToExiting` helper that snapshots `CurrentAlpha` into `ExitStartAlpha` so a half-faded-in patch fades out from where it was, not from 1.
3. If not Expired, the evaluator runs `TickWithInputPose(DeltaTime, RunningResult)` to produce its evaluated pose; `RunningResult.BlendBy(Evaluated, CurrentAlpha)` chains it.
4. **End-of-pass sweep** — reverse-iter compaction destroys evaluators of Expired patches and removes them from `ActivePatches`.

Each Patch's evaluator is its own `AComposableCameraCameraBase`, constructed via the LS-compatible `Initialize(nullptr)` + `UE::ComposableCameras::ConstructCameraFromTypeAsset` spine. Patches don't participate in PCM-level Action dispatch and aren't visible to the EvaluationTree.

### Why Director-level (not Camera-level, not Tree-level)

Director-scoping survives `RunningCamera` changes. The motivating scenario:

> Add a 2 s DollyZoom Patch at T=0s. Activate AimCamera (1 s blend) at T=0.5s. The DollyZoom keeps applying through the blend and onto AimCamera's pose, expiring naturally at T=2.0s.

Camera-level scoping would tie the patch to FollowCamera's actor lifetime — the patch would die when `CollapseFinishedTransitions` destroys FollowCamera at T=1.5s. Tree-level scoping (a new TVariant alternative) is the cleaner long-term home but requires reworking the binary tree's "left=source, right=target" semantics and the collapse pass; deferred to V2 (see "Cross-context behavior" below).

### Relationship to Modifier and Action

Orthogonal. Patches do NOT replace either:

- **Modifier** (§9) edits parameters of existing nodes on the running camera and triggers reactivation. No envelope, no duration. Use for "boss arena makes default FOV wider".
- **Action** (§10) hooks a function around camera/node tick. No node graph, no per-frame evaluator. Use for "fire a callback when this node ticks".
- **Patch** adds new logic on top of the running camera with its own envelope and lifetime. Use for "do a DollyZoom for the next 2 s".

A Patch *can* express what a Modifier can (a Patch with a single `+10 FOV` node ≈ an FOV Modifier of `+10`), but the lifecycle semantics differ and both tools survive in V1.

### Cross-context behavior (V1 limitation)

A patch active on the gameplay context is NOT preserved across an inter-context blend into a cutscene context. The cutscene Director's RefLeaf walks the gameplay tree's snapshot directly without calling `gameplay.Director.Evaluate()`, so `gameplay.PatchManager.Apply` is skipped during the blend window. Documented and intentionally accepted in V1; the V2 fix is to promote the Patch overlay into the tree as a new wrapper variant. See `PatchSystemProposal.md` §10 for the V2 sketch.

### Architecture additions

```
Director (per context)
  ├─ EvaluationTree                 — unchanged
  ├─ RunningCamera, LastEvaluatedPose, PreviousEvaluatedPose
  └─ PatchManager                   — NEW
       └─ ActivePatches : TArray<UComposableCameraPatchInstance*>
            ├─ Evaluator           — AComposableCameraCameraBase actor
            ├─ Phase / CurrentAlpha / ExitStartAlpha
            ├─ Schedule (ExpirationType bitmask, Duration, bExpireOnCameraChange)
            ├─ RunningCameraAtAdd  — per-patch baseline for OnCameraChange
            └─ Handle              — caller-facing weak wrapper
```

The Director destroys its PatchManager via the standard UPROPERTY GC chain. Synchronous teardown on context pop / Director shutdown goes through `Director::DestroyAllCameras → PatchManager.DestroyAll`, which calls `Evaluator->Destroy()` on each Patch's actor.

### Public API surface (V1)

- `UComposableCameraBlueprintLibrary::AddCameraPatch(WC, PlayerIndex, PatchAsset, ContextName, Params, ParameterBlock) → UComposableCameraPatchHandle*` — `BlueprintInternalUseOnly`. Surface mirrors `ActivateComposableCameraFromTypeAsset` (PlayerIndex resolves PCM internally; parameter block is separate). `ContextName == NAME_None` targets the active context (typical case); a non-None name targets a specific context that must already be on the stack — patches on a buried context still tick (their Director's `Evaluate` runs as long as the context lives) but are not user-visible until the context returns to the top. BP authors interact through `UK2Node_AddCameraPatch` (EditorDesignDoc §22). The runtime `PatchManager::AddPatch` takes the parameter block as a separate argument too.
- `UComposableCameraBlueprintLibrary::ExpireCameraPatch(Handle, ExitDurationOverride = -1)` — flip a single patch to Exiting via its envelope.
- `UComposableCameraBlueprintLibrary::ExpireAllPatchesOnContext(WC, PlayerIndex, ContextName, ExitDurationOverride = -1)` — bulk soft-expire every active patch on the named (or active) context's PatchManager. Each patch runs its own exit ramp; mid-Entering patches fade out from their current alpha rather than popping. Backed by `UComposableCameraPatchManager::ExpireAll`.
- BP-pure handle introspection (weak-handle-safe — null/stale handle → defaulted return, no need to null-check at every call site): `IsPatchActive(Handle) → bool`, `GetPatchPhase(Handle) → EComposableCameraPatchPhase`, `GetPatchAlpha(Handle) → float`, `GetPatchElapsedTime(Handle) → float`. Match the §12.1 surface of `PatchSystemProposal.md`.
- `UComposableCameraPatchManager::ApplyParameterBlockToActivePatch(Handle, ParameterBlock)` — mid-life parameter mutation. Re-applies a parameter block onto the live evaluator's runtime data block via the source asset's `ApplyParameterBlock` (same path the LS Component uses on its per-tick re-sync). Drives the Sequencer track's per-frame `OnAnimate` re-sync; safe to call every frame on the same handle. No-op on null / stale handle, on Patches in Exiting / Expired phase, or when the evaluator has no runtime data block yet. The single-key `SetPatchParameter(handle, name, value)` analog is still deferred — Sequencer is the only current driver for keyed patch parameters and it produces complete parameter blocks naturally.

### Sequencer integration

Patches are authored in Sequencer through a dedicated track triple living under `Source/ComposableCameraSystem/{Public,Private}/MovieScene/`:

```
UMovieSceneComposableCameraPatchTrack       ← root-level track (no object binding)
 └── UMovieSceneComposableCameraPatchSection : UMovieSceneParameterSection
      ├── PatchAsset             : UComposableCameraPatchTypeAsset*
      ├── TargetActorBinding     : FMovieSceneObjectBindingID  ← bound LS Actor (sole addressing)
      ├── Params                 : FComposableCameraPatchActivateParams (envelope / layer)
      ├── Parameters             : FInstancedPropertyBag  ← from PatchAsset.ExposedParameters (static defaults / fallback)
      ├── Variables              : FInstancedPropertyBag  ← from PatchAsset.ExposedVariables (static defaults / fallback)
      ├── ScalarParameterNamesAndCurves   ┐
      ├── BoolParameterNamesAndCurves     │ inherited from UMovieSceneParameterSection;
      ├── Vector2DParameterNamesAndCurves │ each entry = a named keyable channel that
      ├── VectorParameterNamesAndCurves   │ takes priority over the bag fallback for
      └── ColorParameterNamesAndCurves    ┘ the matching ExposedParameter name

Single per-frame path (runs in all worlds — Editor / EditorPreview / PIE / Game):

  TrackInstance::OnAnimate(per frame):
    for each in-range section:
      LSComp = section.TargetActorBinding → LS Actor → LS Component
      if LSComp:
        params = section.BuildParameterBlock(currentFrame)             ← channel sample
        alpha  = PatchEnvelope::ComputeStatelessAlpha(currentFrame, …) ← pure function
        LSComp->SetSequencerPatchOverlay(section, params, alpha)

  LS Component::TickComponent (after InternalCamera tick, before projection):
    for each registered overlay (sorted by section's LayerIndex):
      apply parameter block to overlay.Evaluator's runtime data block
      eval = Evaluator->TickWithInputPose(dt, Pose)
      Pose.BlendBy(eval, overlay.Alpha)
    write Pose.Position + Pose.Rotation + Pose.FOV → CineCamera        ← FOV here so DollyZoom previews

Sequencer patches follow their host LS Component lifetime: they apply while the spawned LS Actor exists and ticks. PCM/Director-side patches
(BP `AddCameraPatch`) are a separate orthogonal path on the gameplay camera.
(BP `AddCameraPatch`) are a separate orthogonal path on the gameplay camera.

UMovieSceneComposableCameraPatchTrackInstance — engine UMovieSceneTrackInstanceSystem dispatch:
  OnInputAdded   → resolve PCM + PatchManager + build initial parameter block → AddCameraPatch
  OnAnimate      → per-input re-sync: rebuild block from current bag values → ApplyParameterBlockToActivePatch
  OnInputRemoved → ExpireCameraPatch (section ease-out as exit-duration override)
  OnDestroyed    → ExpirePatch(handle, 0.f) on every still-live entry — defensive teardown
```

Section easing folds into the patch envelope: when the user has *not* overridden `EnterDuration` / `ExitDuration` on `Params`, the section's `Easing.GetEaseInDuration` / `GetEaseOutDuration` (converted to seconds via the owning movie scene's `TickResolution`) is forwarded to the runtime via `bOverrideEnterDuration` / `bOverrideExitDuration = true`. Dragging the section's ease handles in Sequencer thus directly reshapes the live patch envelope. Already-overridden values are left alone — the designer can pin a specific enter/exit time without losing it to whatever ease was authored.

Root-track binding (no per-actor `ObjectBinding`) keeps the track addressable through the same `(PlayerIndex, ContextName)` pair that BP `AddCameraPatch` uses, so the Sequencer surface is a 1:1 mapping of the runtime call. Patches still target the Director resolved through the active context (or an explicitly named one); the section is the authoring artifact, the Director is the runtime owner.

Editor-side: `FComposableCameraPatchTrackEditor` registers the track type with Sequencer's track-editor system, surfaces "Add Track → Composable Camera Patch Track" in the root menu, paints sections by their patch asset name, and exposes per-section bag-leaf keying via `BuildSectionContextMenu` — right-click a section → "Camera Parameters" / "Camera Variables" lists every keyable leaf, click materialises a stock `UMovieSceneFloatTrack` / `UMovieSceneObjectPropertyTrack` / etc. on the path `Parameters.Value.{LeafName}` (or `Variables.Value.{LeafName}`). Sequencer's stock evaluation animates the bag's backing FProperty in place each frame; the runtime `OnAnimate` picks up the animated values and pushes them through `ApplyParameterBlockToActivePatch` without any custom channel implementation.

Pin-type → bag-descriptor mapping is shared between this section and `FComposableCameraTypeAssetReference` via the new `LevelSequence/ComposableCameraExposedBagUtils.{h,cpp}` helpers — a single canonical "exposed parameter → CPF_Edit | CPF_Interp bag entry" pipeline so any future pin-type addition flows through both surfaces uniformly.

No gate path is needed here: patches are not Spawnables and do not drive viewport lifetime. They attach to the already-spawned LS Component and follow that component's normal tick.

Full design rationale (asset-class subclassing decision, envelope as enum vs. instanced Transition, layer override mechanism, V2 paths) lives in `PatchSystemProposal.md`.

---

## 12. Interpolators

Interpolators provide smooth value blending used by nodes for damping, spring physics, and smooth transitions.

```
UComposableCameraInterpolatorBase (abstract)
  ├─ UComposableCameraIIRInterpolator           — exponential damping (IIR filter)
  ├─ UComposableCameraSimpleSpringInterpolator   — simple spring physics
  └─ UComposableCameraSpringDamperInterpolator   — second-order spring-damper system
```

Each interpolator can build typed instances: `TCameraInterpolator<Double>`, `TCameraInterpolator<Vector2D>`, `TCameraInterpolator<Vector3D>`, `TCameraInterpolator<Quat>`, `TCameraInterpolator<Rotator>`.

Interpolators are Instanced UObject subobjects owned by their parent node. Their individual parameters (e.g. `Speed`, `DampTime`, `Frequency`) are **automatically exposed as pins** by `GatherAllPinDeclarations()` — see §7 "Subobject Property Pin Exposure". No per-node boilerplate is needed; any node that declares an `Instanced` interpolator property gets subobject pins for free. This allows callers to override interpolation behavior at runtime without replacing the entire interpolator object — the interpolator class (IIR vs. Spring vs. SpringDamper) remains a design-time choice, while its tuning parameters flow through the standard pin resolution chain.

---

## 13. Spline Math

The plugin implements multiple spline types for use in SplineNode and SplineTransition:

| Spline | Implementation | Use Case |
|---|---|---|
| `BuiltInSpline` | Wrapper for UE's USplineComponent (cubic Bezier) | CameraRig_Rail integration |
| `BezierSpline` | Custom cubic Bezier | SplineTransition Bezier mode |
| `CubicHermiteSpline` | Hermite interpolation with explicit tangents | SplineTransition Hermite mode, SplineNode |
| `BasicSpline` | B-spline | SplineNode |
| `NURBSpline` | Non-uniform rational B-spline | SplineNode |

All implement `IComposableCameraSplineInterface` for uniform evaluation.

---

## 14. Blueprint Integration

### Blueprint Function Library

`UComposableCameraBlueprintLibrary` provides the primary Blueprint API:

- `ActivateComposableCameraFromTypeAsset(...)`: Internal entry point for type-asset-based camera activation (called by the custom K2 node, not exposed in the BP palette)
- `ActivateComposableCameraFromDataTable(WorldContextObject, PlayerIndex, DataTable, RowName)`: Internal entry point for DataTable-based camera activation (called by the custom K2 node, not exposed in the BP palette). All activation params (pose preservation, transient flags, lifetime) come from the row's `FComposableCameraActivateParams ActivationParams` field — callers do not supply them separately
- `TerminateCurrentCamera(...)`: Pops the active context
- `PopCameraContext(...)`: Pops a named context
- `AddModifier(...)` / `RemoveModifier(...)`: Runtime modifier management
- `AddAction(...)` / `ExpireAction(...)`: Runtime action management
- `SetComposableCameraVariableRuntimeValue(...)` / `GetComposableCameraVariableRuntimeValue(...)`: Type-safe variable access via custom thunks

Note: The legacy class-based activation functions (`ActivateComposableCameraByClass`, `CreateComposableCameraByClass`) have been removed. All camera activation now goes through type assets or DataTable rows. The internal `PCM::CreateNewCamera` and `PCM::ActivateNewCamera` functions remain for use by internal subsystems (e.g., the MixingCameraNode).

### Context Name Dropdown

Context names configured in project settings are exposed to Blueprint via `GetContextNames` metadata on `FName` parameters, providing a dropdown selector in the editor.

---

## 15. Initialization and Timing

### Boot Sequence

1. **PCM Constructor**: Creates ContextStack and ModifierManager as default subobjects
2. **PCM::InitializeFor** (PostInitializeComponents phase): Reads project settings and calls `ContextStack->EnsureContext()` for the base context. This runs before any actor's BeginPlay.
3. **PCM::BeginPlay**: Currently empty — all critical init is in InitializeFor to avoid BeginPlay ordering issues between the PCM and level actors.
4. **Gameplay code activates cameras via K2 node / DataTable**: Can safely happen in any actor's BeginPlay because the base context is already initialized.

### Why InitializeFor, Not BeginPlay

UE does not guarantee BeginPlay order across actors. If a level actor's BeginPlay fires before the PCM's BeginPlay, camera activation would find an empty context stack and crash. Moving base context initialization to `InitializeFor` (which runs during `PostInitializeComponents`, guaranteed before any BeginPlay) eliminates this race condition.

---

## 16. Project Settings

`UComposableCameraProjectSettings` (accessible via Project Settings > Composable Camera System):

- `ContextNames` (TArray<FName>): Registered context names. The first entry is the base context, auto-initialized on startup. At least one context must be defined. All context operations validate names against this list.

---

## 17. Level Sequence Integration and Implicit Camera Activation

### Architecture: Implicit Camera Activation via SetViewTarget

CCS supports two camera activation paths:

1. **Explicit activation** — `ActivateCamera` / `ActivateNewCameraFromTypeAsset`. The user explicitly specifies which camera, context, and transition.
2. **Implicit activation** — The PCM's `SetViewTarget` override. When external code calls `SetViewTarget` on an actor with a `UCameraComponent`, the PCM automatically creates a transient proxy camera wrapping that actor and activates it in the current context's director. If `FViewTargetTransitionParams.BlendTime > 0`, the blend params are converted into a `UComposableCameraViewTargetTransition` (which delegates to `FViewTargetTransitionParams::GetBlendAlpha()` for the blend curve).

The `SetViewTarget` override always calls `Super::SetViewTarget(NewViewTarget, FViewTargetTransitionParams())` — empty transition params, so the PCM's built-in `PendingViewTarget` blend is never used. CCS handles all blending through its own evaluation tree.

This design follows the same pattern as Epic's `AGameplayCamerasPlayerCameraManager::SetViewTarget`, which also strips transition params and converts them into its own blend node system (`UViewTargetTransitionParamsBlendCameraNode`).

### Level Sequence Integration via Implicit Activation

Level Sequences fire `SetViewTarget` on the PCM via the engine's CameraCut handler (`FCameraCutGameHandler::SetCameraCut()` in `MovieSceneCameraCutGameHandler.cpp`). The call chain is: `UMovieSceneCameraCutTrackInstance::OnAnimate()` → `FCameraCutAnimator::AnimateBlendedCameraCut()` → `FCameraCutGameHandler::SetCameraCut()` → `CameraManager->SetViewTarget()`.

With implicit activation, each `SetViewTarget` from the engine creates a **new transient proxy camera** in the cutscene context. This means:

- **Hard CameraCut** (no easing): `SetViewTarget(B1)` → proxy camera B1 created, no transition (hard cut).
- **Blended CameraCut** (easing overlap): `SetViewTarget(B2, {BlendTime=0.5s, EaseInOut})` → proxy camera B2 created with a 0.5s `UComposableCameraViewTargetTransition`. The evaluation tree blends between B1 and B2 using CCS's normal transition machinery.
- **Intra-LS camera blends are fully supported** — each camera in the LS becomes a separate CCS camera in the evaluation tree, and CCS handles the blend through its own pose-only transition system.

### Implicit Activation for Arbitrary Actors

The same `SetViewTarget` path handles non-LS use cases:

- `SetViewTargetWithBlend(SecurityCamera, 1.0s)` → proxy camera wrapping the security camera, 1.0s transition.
- `SetViewTarget(SomeActor)` → proxy camera wrapping SomeActor, hard cut.
- `SetViewTarget(Pawn)` where the pawn has no `UCameraComponent` → no proxy created, CCS continues evaluating normally.

It is the user's responsibility to return to the previous camera via explicit `ActivateCamera` when done.

### Components

| Component | Role |
|---|---|
| `ViewTargetProxyNode` | Lightweight node that reads `FMinimalViewInfo` from a static target actor's `UCameraComponent` each tick. Created programmatically by the PCM's `SetViewTarget` override, not user-facing. |
| `UComposableCameraViewTargetTransition` | Transition that wraps `FViewTargetTransitionParams` and delegates to `GetBlendAlpha()` for the blend curve. Created programmatically by the PCM's `SetViewTarget` override when `BlendTime > 0`. |
| `UAsyncPlayCutsceneSequence` | Blueprint-callable async action. Pushes a cutscene context, creates `ULevelSequencePlayer`, and starts playback. Engine CameraCut events flow through `SetViewTarget` → implicit activation, creating proxy cameras in the cutscene context. Pops the context when LS ends. |
### End-to-End Flow (PlayCutsceneSequence)

1. **Blueprint calls `PlayCutsceneSequence`**: pushes a cutscene context (with inter-context transition from gameplay), creates the LS player, starts playback. Tree:
   ```
   InterContextTransition
   ├── ReferenceLeaf → Gameplay
   └── InitialCamera (empty, awaiting first SetViewTarget)
   ```

2. **First CameraCut fires**: Engine calls `SetViewTarget(CamA)` → PCM creates proxy camera wrapping CamA, activates in cutscene director (hard cut, replacing the initial camera). Tree:
   ```
   InterContextTransition
   ├── ReferenceLeaf → Gameplay
   └── ProxyCamera_CamA (ViewTargetProxyNode → CamA)
   ```

3. **Each tick**: CamA's `CineCameraActor` animates via Sequencer. The proxy node reads `GetCameraView()` from CamA's `UCameraComponent` and outputs the pose.

4. **Blended CameraCut (CamA → CamB with 0.5s ease)**: Engine calls `SetViewTarget(CamB, {BlendTime=0.5s})` → PCM creates proxy camera wrapping CamB with a `ViewTargetTransition`. Tree:
   ```
   InterContextTransition
   ├── ReferenceLeaf → Gameplay
   └── ViewTargetTransition (0.5s)
       ├── ProxyCamera_CamA
       └── ProxyCamera_CamB
   ```

5. **Intra-LS blend completes**: `ViewTargetTransition` finishes → `CollapseFinishedTransitions` promotes right subtree, destroys ProxyCamera_CamA. Tree simplifies to ProxyCamera_CamB.

6. **LS ends**: `OnFinished` fires → pops cutscene context → inter-context transition back to Gameplay.

### Explicit LS Authoring Path (V1.4)

The implicit-activation path above is the right answer when an existing gameplay flow wants to hand the viewport to a Level Sequence that references **pre-built camera actors** (CineCameraActors, user-authored cameras). It treats the LS as an external animation source.

A parallel path solves the complementary problem: **authoring a CCS camera directly inside a Level Sequence**, with its exposed parameters keyable on Sequencer tracks. This is the only path that gives designers per-parameter animation of a composable camera.

**Components:**

```
AComposableCameraLevelSequenceActor        (Spawnable only, NotPlaceable)
 ├── UCineCameraComponent (Output)                       ← RootComponent, Camera Cut Track target
 └── UComposableCameraLevelSequenceComponent              (sibling UActorComponent; pure logic)
      ├── FComposableCameraTypeAssetReference
      │    ├── TypeAsset           : UComposableCameraTypeAsset*
      │    ├── Parameters          : FInstancedPropertyBag  ← from ExposedParameters
      │    └── Variables           : FInstancedPropertyBag  ← from ExposedVariables
      ├── OutputCineCameraComponent : TObjectPtr<UCineCameraComponent>  ← reference to root
      ├── InternalCamera           : AComposableCameraCameraBase*  (transient)
      └── Tick loop                 (editor + PIE both)
```

Structure mirrors `ACineCameraActor`: the CineCamera is the Actor's RootComponent, so every native UE path (`FindComponentByClass<UCameraComponent>`, Camera Cut Track, viewport Pilot, `PCM::SetViewTarget`'s implicit-activation filter) resolves to the root immediately — same fast path as a plain CineCameraActor. The LevelSequenceComponent is a plain `UActorComponent` (not a SceneComponent) — a logic-only driver with no transform of its own, holding a reference back to the root CineCamera.

**Authoring flow:**

1. Designer opens a Level Sequence, right-clicks → `Add Spawnable → ComposableCameraLevelSequenceActor`.
2. Selects the Spawnable's binding, Details panel shows `TypeAssetReference.TypeAsset`. Picks a TypeAsset.
3. `RebuildBagsFromTypeAsset` regenerates the `Parameters` and `Variables` bags to mirror the TypeAsset's exposed surface; existing values under matching names are preserved (`MigrateToNewBagStruct`).
4. Designer adds a Camera Cut Track pointing at the Spawnable, and (optionally) a component track for `LevelSequenceComponent`. Right-click `+Track` on the component binding → **Camera Parameters** / **Camera Variables** sections (added by `FComposableCameraLevelSequenceComponentTrackEditor`) list every keyable bag leaf. One click → stock `UMovieSceneFloatTrack` / `UMovieSceneDoubleVectorTrack` / etc. materializes on the path `TypeAssetReference.Parameters.Value.{ParamName}`.
5. Designer keys values over time; Sequencer's stock property-track evaluation writes directly into the bag's backing FProperty each frame.

**Runtime evaluation (editor + PIE, same pipeline):**

1. Sequencer spawns the `AComposableCameraLevelSequenceActor` via its spawn register when the binding's Spawn track enters range. Component's `OnRegister` calls `SetEvaluationEnabled(true)`, preserving the default-on behavior while deferring the zero-delta warm-up to the next component tick.
2. First `TickComponent`:
   - `EnsureInternalCamera` spawns a transient `AComposableCameraCameraBase` into the same world. `Initialize(nullptr)` runs; `ConstructCameraFromTypeAsset(Camera, TypeAsset, BuiltBlock)` duplicates node templates, builds the runtime data block, applies parameter values from the bag, fires per-node `OnInitialize`. The compute chain is a no-op because `BeginPlayCamera` already fired with an empty array before nodes were populated.
3. Each subsequent tick:
   - Rebuild an `FComposableCameraParameterBlock` from the current bag values.
   - `TypeAsset->ApplyParameterBlock(*RuntimeDataBlock, Block)` re-syncs the bag into the data block (cheap — O(exposed-parameter count), no allocations).
   - Resolve Sequencer-aware DeltaTime: runtime spawned actors ask the owning `UMovieSceneSequencePlayer` for `GetPlayRate()`, while pure editor preview asks the active `ISequencer` through the runtime/editor hook.
   - `InternalCamera->TickCamera(SequencerAwareDeltaTime)` produces a pose, so history-based nodes (for example `TwoPointMove` and `DirectionalMove`) follow Level Sequence playback speed instead of raw world tick speed.
   - `ProjectPoseToCineCamera(Pose)` writes **position and rotation only** onto the child `UCineCameraComponent`. Optical fields (FocalLength / Aperture / Filmback / Focus / PostProcess) are left for the designer to control directly on the CineCamera, either in Details or via Sequencer's native property tracks — this is the explicit design point: LS cameras own their optics on the CineCamera, CCS owns spatial behavior.
4. Sequencer's Camera Cut Track picks up the Spawnable's `UCineCameraComponent` via `FindCameraComponent` and routes the viewport through it. No special handshake with CCS runtime is needed.
5. When the Spawn track exits range, Sequencer destroys the actor. `EndPlay` calls `DestroyInternalCamera` which releases the internal evaluator.

**Decoupling from PCM:** nothing in this path touches `AComposableCameraPlayerCameraManager`. The existing PCM-driven gameplay stack runs in parallel in the same world. CCS cameras activated through gameplay code live on their own context stack; LS-driven cameras live on the LS actor's isolated evaluator. The Camera Cut Track mediates which one the viewport sees at any moment.

**Node compatibility surface:** some nodes hard-depend on the PCM (e.g. `MixingCameraNode` calls `PCM->CreateNewCamera` at init; compute nodes are ComputeOnly by design). Each node declares its stance via `UComposableCameraCameraNodeBase::GetLevelSequenceCompatibility() → EComposableCameraNodeLevelSequenceCompatibility`. The method is a `BlueprintNativeEvent` so BP-authored camera nodes (subclasses of `UComposableCameraBlueprintCameraNode`) can override it alongside C++ nodes. A Details-panel customization (`FComposableCameraTypeAssetReferenceCustomization`) inspects the bound TypeAsset's nodes and surfaces a yellow warning banner listing incompatible classes — designer-facing signal that those nodes will silently no-op in the LS path.

### Spawn Track-owned LS camera lifecycle (Phase G cleanup)

The LS-authoring path uses the Sequencer Spawn Track as the sole lifecycle authority. A spawned `AComposableCameraLevelSequenceActor` owns a child `UCineCameraComponent` plus a sibling `UComposableCameraLevelSequenceComponent`; while the Spawnable actor exists, the component evaluates its TypeAsset every tick and projects position / rotation into the CineCamera. When the Spawn track exits range, Sequencer destroys the actor and `OnUnregister` / `EndPlay` destroy the transient internal evaluator.

There is no CCS MovieScene ECS gate in this path. CameraCut still decides which CineCamera reaches the viewport, but it no longer opens or closes CCS evaluation. If a sequence wants a camera to stop evaluating, key its Spawn Track false or destroy the owning actor.

First-frame behavior stays deterministic: `OnRegister` calls `SetEvaluationEnabled(true)` and defers a zero-delta first evaluation to the next component tick, after Sequencer property tracks have written the current bag values. `SetSequencerShotOverride` may still force a same-frame `EvaluateOnce(0.f)` when a Shot section first appears, because TrackInstance order can deliver the override after the component tick for that frame.

Subsequent non-zero evaluations resolve DeltaTime from Sequencer ownership rather than blindly using component tick DeltaTime. Runtime playback uses the spawnable annotation plus the owning player's spawn register to find the `UMovieSceneSequencePlayer`; editor preview uses `FGetEditorSequencerPlaybackDeltaTime` bound by the editor module to read the active `ISequencer` playback speed. Paused Sequencer preview returns zero DeltaTime.

---

## 18. Key Invariants

1. **Only the top context is evaluated** — unless a reference leaf in the top context's tree reaches into a lower context.
2. **Transitions are pose-only** — they never hold camera or Director references. The tree structure handles the wiring.
3. **Right subtree is always the target** — `CollapseFinishedTransitions` always promotes right, destroys left.
4. **Collapse recurses into both subtrees** — the right subtree can contain nested intra-context transitions (from same-context activations under an inter-context root) that need collapsing independently of the left side.
5. **Transient cameras always get their own context** — prevents lifetime expiry from destroying persistent cameras.
6. **Base context cannot be popped** — there must always be at least one context on the stack.
7. **PendingDestroyEntries stay alive until their transition finishes** — the reference leaf in the active context's tree holds a `TSharedPtr` snapshot of the popped director's tree; the pending-destroy director stays registered on the stack (but is never called through `Evaluate`) until the transition's `OnTransitionFinishesDelegate` fires cleanup.
8. **TVariant nodes must handle all three types** — when adding new node types or modifying the variant, every branching function (Visit, switch, if-chain) must be updated for all variants.
9. **EnsureContext is move-to-top, not just existence-check** — position in the stack determines which context is active.
10. **Per-node one-shot setup belongs in `OnInitialize`, not `BeginPlayCamera`** — `UComposableCameraCameraNodeBase::OnInitialize` is a `BlueprintNativeEvent` (retained for the `_Implementation` pattern even though nodes are `NotBlueprintable`); C++ subclasses override `OnInitialize_Implementation` and must chain `Super::OnInitialize_Implementation()`. Nodes that need the outgoing camera pose at setup time read it from `OwningPlayerCameraManager->GetCurrentCameraPose()`.
11. **`InitializeNodes` may run twice on the type-asset activation path** — once with an empty `CameraNodes` / `ComputeNodes` array during `SpawnActorDeferred → Camera::Initialize` (no-op), then again from `OnTypeAssetCameraConstructed` after the node arrays have been duplicated from the type asset. Any future per-node init work must remain idempotent across this double-call, or the path must be refactored to call `InitializeNodes` exactly once.
12. **Compute nodes must not run per-frame** — `UComposableCameraComputeNodeBase` inherits from `UComposableCameraCameraNodeBase` for the pin system but is executed exactly once per activation, from `BeginPlayCamera`, via `ExecuteBeginPlay`. `InitializeNodes` deliberately skips tick-delegate registration for entries in `ComputeNodes`; don't add them back, and don't override `OnTickNode_Implementation` on compute node subclasses — it will never be called.
13. **Variable nodes (Get/Set) are allowed on both compute and camera chains** — `SetVariable` nodes may appear on either the compute chain (to populate initial values from evaluated data at activation) or the camera chain (to write computed values during ticking). When `FullExecChain` is available, both chain types support interleaved variable writes; without it, linear `CameraNodes` walk still allows Get/Set node execution at any position.
14. **Pose blending must go through `BlendBy`** — transitions never hand-lerp individual fields of `FComposableCameraPose`. The correct pattern is to initialize the output pose from `CurrentSourcePose`, call `OutPose.BlendBy(CurrentTargetPose, Alpha)`, then overwrite only the fields the transition explicitly computes itself (e.g. cylindrical / inertialized Position and Rotation). This guarantees that every existing and future pose field (FOV, physical camera, projection, aspect, orthographic) participates in the blend.
15. **FOV must be resolved to degrees before blending** — a pose is FOV-ambiguous as long as both `FieldOfView` and `FocalLength` are independently in play. `BlendBy` resolves each side via `GetEffectiveFieldOfView()` first, lerps the degrees values, and writes the output as a degrees-mode pose (`FocalLength = -1`). Nodes that author FOV from code must call `SetFieldOfViewDegrees(...)` — assigning directly to `FieldOfView` leaves a stale `FocalLength` and produces mixed-mode poses that corrupt downstream blending.
16. **Projection mode and aspect booleans use target-wins-immediately, never lerp** — these fields have no meaningful interpolation. `BlendBy` adopts the target's value as soon as the blend starts (`OtherWeight > 0`). This applies to `ProjectionMode`, `ConstrainAspectRatio`, `OverrideAspectRatioAxisConstraint`, and `AspectRatioAxisConstraint`. This ensures that a camera which sets e.g. `ConstrainAspectRatio = true` takes effect at the start of the transition, not mid-blend. Orthographic scalar fields (`OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`) still lerp normally.
17. **The pose is authoritative over the `UCameraComponent` for projection, aspect, and post-process** — `GetCameraViewFromCameraPose` reads projection mode, aspect constraint booleans, and the axis-constraint enum off the pose. Post-process is a three-layer stack: (1) the camera component's designer-authored `PostProcessSettings` as the baseline, (2) the pose's `PostProcessSettings` layered on top via `FPostProcessUtils::OverridePostProcessSettings` (only properties with `bOverride_*` true take effect), (3) physical camera settings (`ApplyPhysicalCameraSettings`) for DoF/exposure on top. A camera with no PostProcess node has all overrides off — layers 2 and 3 are no-ops and the component baseline passes through unchanged. Nodes that want to change projection/aspect/PP do so by writing the pose, not by mutating the actor.
18. **Enum slots are normalized to int64 in the data block** — `EComposableCameraPinType::Enum` always reads / writes through the `int64` storage path (`GetPinTypeSize` returns 8, `GetPinTypeAlignment` returns 8). Producers that publish to an enum slot must cast through `int64` regardless of the source UPROPERTY's underlying width (uint8 / int32 / int64), and consumers that pull from an enum slot must narrow-cast back to the destination property's width using `FEnumProperty::GetUnderlyingProperty()` (modern `enum class`) or `FByteProperty::GetIntPropertyEnum()` (legacy `TEnumAsByte`). The K2 thunk in `ComposableCameraBlueprintLibrary` does this normalization automatically using `FProperty` type detection on the wired pin; bypassing it (e.g. memcpy of the native enum into the data block) silently corrupts adjacent slots once the underlying width is anything other than 8 bytes. Persistence (DataTable rows, variable initial values, exposed-parameter defaults) stores the value as the authored entry name string (e.g. `"EMyEnum::Alpha"`) — this is enum-renumbering safe and SCM-friendly. The runtime parser resolves names through `UEnum::GetValueByNameString`, and the debug HUD renders values back through `UEnum::GetNameStringByValue` whenever the slot's `UEnum*` is known, falling back to the raw integer otherwise so debug output never silently lies about the slot.
19. **Name slots transport `FName` as POD** — `EComposableCameraPinType::Name` uses the same memcpy-based fast path as the numeric / vector types because `FName` is an 8-byte POD comparison index. Writes go through `WriteValue<FName>` and copies preserve the index without touching the global name table. There is no string allocation on the hot path; `FString` was deliberately excluded from the pin type set for the same reason.
20. **Delegate pins bypass the data block entirely** — `EComposableCameraPinType::Delegate` has zero size in the runtime data block (`GetPinTypeSize` returns 0). Delegates are not POD and cannot be memcpy'd into the byte array. Instead, the `FComposableCameraParameterBlock` carries them in a parallel `DelegateValues` map (`TMap<FName, FScriptDelegate>`), and `UComposableCameraTypeAsset::ApplyDelegateBindings` writes them into the target node's `FDelegateProperty` UPROPERTY via reflection at activation time. The per-frame auto-resolve path (`GetOrBuildPinBindings`, `ResolveAllInputPins`) skips delegate-typed properties because `TryMapPropertyToPinType` returns false unless the caller explicitly opts in via the 5th `OutSignatureFunction` parameter. `ApplySubobjectPinValues` carries a no-op `case Delegate:` for switch exhaustiveness. In the K2 `ExpandNode`, unconnected delegate pins are skipped entirely — an unbound `FScriptDelegate` is a no-op and emitting a `SetParameterBlockValue` call for it would be wasted work. The `FComposableCameraNodePinDeclaration` for a delegate pin carries a `SignatureFunction` pointer (the `UFunction*` generated by `DECLARE_DYNAMIC_DELEGATE_*`) so the editor pin conversion (`MakeEdGraphPinTypeFromCameraPinType`) can produce a properly typed `PC_Delegate` pin with a `PinSubCategoryMemberReference` that the K2 schema uses for wiring validation. `FComposableCameraExposedParameter` stores the same `SignatureFunction` for the K2 ActivateComposableCamera node to consume.
21. **Get variable nodes are transparent at runtime** — Get variable graph nodes (`UComposableCameraVariableGraphNode` with `bIsSetter == false`) have no runtime identity. They don't appear in `NodeTemplates`, `ComputeNodeTemplates`, or any exec chain. Instead, `BuildRuntimeDataLayout` resolves their connections directly: for each consumer input pin wired to a Get node's output, `InputPinSourceOffsets` maps the consumer's input key to the variable's `InternalVariableOffsets` slot. The consumer reads the variable value via the normal `TryResolveInputPin` wired-connection path — priority 1 — with zero per-frame overhead from the getter itself. This means the consumer's input pin resolves to the same storage slot that `ApplyParameterBlock` / `InitialValueString` seeding writes into. **Exception:** if the same consumer pin is also an ExposedParameter target, the `ExposedInputPinOffsets` entry takes semantic priority and the variable getter connection is skipped — exposing a pin breaks the wire, so a coexisting getter connection is stale.

22. **Source camera freeze is a leaf-level flag, not a camera-level flag** — `FComposableCameraActivateParams::bFreezeSourceCamera` controls whether the outgoing (source) camera stops ticking during a transition. The flag is threaded through `Director::ActivateNewCamera*` → `EvaluationTree::OnActivateNewCamera*` and applied via `FreezeSubtree`, which recursively sets `bFrozen` on every leaf and reference-leaf in the left (source) subtree. A frozen leaf returns `RunningCamera->CameraPose` (its last evaluated pose) without calling `TickCamera`; a frozen reference leaf returns `ReferencedDirector->GetLastEvaluatedPose()` without re-evaluating the source context's tree. The freeze flag lives on the wrapper structs (`FComposableCameraEvaluationTreeLeafNodeWrapper::bFrozen`, `FComposableCameraEvaluationTreeReferenceLeafNodeWrapper::bFrozen`), not on the camera actor — a camera can be frozen in one tree position while still being alive. `ResumeCamera` and `ReactivateCurrentCamera` do not expose `bFreezeSourceCamera` and default to `false`. The auto-pop path in the context stack also defaults to `false` (the transient camera is finished and freezing it would be meaningless).

23. **Spline and path-guided transitions draw debug inline** — `UComposableCameraSplineTransition::DrawDebugSpline` and `UComposableCameraPathGuidedTransition::OnEvaluate` call `DrawDebugPoint` directly in each loop iteration rather than collecting points into an intermediate `TArray<FVector>`. The CatmullRom spline type uses a virtual-index lambda (`GetVirtualControlPoint`) to access the `{ZeroVector, ControlPoints[0..N-1], EndVec}` sequence without copying the `ControlPoints` array or calling `Insert()`. These patterns eliminate per-frame heap allocations in the transition evaluation hot path. The sole justified per-frame allocation remaining in the node/transition layer is `MixingCameraNode::OnTickNode`'s `TArray<float> Weights` returned from the delegate — the caller owns that allocation and the contract requires a by-value return.

24. **LS component lifetime follows the Spawn Track** — `UComposableCameraLevelSequenceComponent::OnRegister` calls `SetEvaluationEnabled(true)` for every spawned LS Actor, and no CCS MovieScene gate flips it per CameraCut. `SetEvaluationEnabled(false)` remains a local teardown / external-host switch and destroys the transient internal camera. OFF->ON defers a one-shot zero-delta evaluation to the next component tick so Sequencer property tracks can write current bag values first. After that warm-up, LS-owned evaluators tick with Sequencer-aware DeltaTime so time-history nodes respect Level Sequence playback speed.

25. **Inter-context activation defers OLD-root destruction until the new transition finishes** — `UComposableCameraEvaluationTree::OnActivateNewCameraWithReferenceSource` does NOT call `DestroySubtreeCameras(RootNode)` when it installs the new Inner. Instead it stashes the pre-replacement RootNode in `PendingDestroyOldRoots` and registers a weak lambda on the new transition's `OnTransitionFinishesDelegate` that destroys the stash when the blend completes. Rationale: if the `SourceDirector` was previously PUSHED onto us, its RefLeaf captured OUR old RootNode as a `TSharedPtr` at push time — destroying those leaves immediately would leave the SourceDirector's Tick walking through now-destroyed `Leaf.RunningCamera` actors during the blend (observable as `[leaf] (destroyed)` in the panel + `"RunningCamera is null or destroyed when evaluating leaf node."` errors). By the time the new transition finishes, `CollapseFinishedTransitions` has dropped the RefLeaf branch of the new root — the stash is no longer reachable from this director's tree and can be destroyed. Backstop: `DestroyAll()` flushes any still-pending stashes so transitions that never complete (context popped mid-blend, transition replaced by another activation) don't leak. This is the activate-new counterpart to the pop-side `ResumeCurrentCameraWithReferenceSource` fix (invariant #7's pending-destroy machinery); together they guarantee camera actors stay valid for the entire lifetime of any RefLeaf snapshot that can reach them.

26. **Transient contexts demoted from the top by `EnsureContext` move-to-top are implicitly popped** — `UComposableCameraContextStack::Evaluate`'s auto-pop loop inspects ONLY the top entry. A transient context that was on top at push time but later gets shoved below by `EnsureContext` reordering `Entries` to bring an existing context back to the top would, without further handling, be stranded: its running camera is transient but no longer at the top so auto-pop never sees it, its `RemainingLifeTime` expiring does nothing, and the context leaks until explicit `PopContext(name)`. `PCM::ActivateNewCamera` closes this gap — after a successful inter-context activation (bContextSwitched path) it calls `ContextStack->DemoteNonTopTransientContextsToPending(ActivatingTransition)`, which walks every non-top entry, identifies those whose `RunningCamera->IsTransient()` is true, moves them from `Entries` into `PendingDestroyEntries`, and binds cleanup to `ActivatingTransition->OnTransitionFinishesDelegate` (camera-cut activations destroy immediately — no blend means no RefLeaf chain needs the transient camera alive). Semantics match an explicit `PopContext` for each demoted transient: its director + camera stays GC-alive for the duration of the new blend, continues to tick through the activation tree's RefLeaf snapshot, then gets `DestroyAllCameras` + removed from the pending bucket exactly when the transition resolves. Non-transient contexts that happen to be below the top are NOT touched — they are designer-managed (e.g. a UI context suspended behind gameplay) and stay suspended. This rule is the activation-side counterpart to invariants #7 and #25: together they guarantee that every transient camera actor's destruction timing is deterministic and no RefLeaf snapshot ever walks into a destroyed leaf.

27. **CameraCut blend correctness depends on Spawn Track overlap authoring** — because CCS no longer gates LS components by CameraCut active set, any camera that should contribute to a cut blend must remain spawned for the whole authored blend window. Same-row / dual-row overlap authoring should keep both LS Actors alive long enough for their CineCameraComponents to provide fresh poses to the engine CameraCut blend.

28. **Patches live on the Director, not the EvaluationTree** — `UComposableCameraPatchManager` is a Director subobject; `Director::Evaluate` calls `PatchManager.Apply` after `EvaluationTree.Evaluate`. Cross-context blends route through the source tree's RefLeaf snapshot WITHOUT invoking the source Director's `Evaluate`, so source-side patches do not contribute to the inter-context blend's source pose. Documented limitation; V2 promotes Patch into the tree as a new wrapper variant (see `PatchSystemProposal.md` §10).

29. **Patch evaluators construct PCM-independently** — `UComposableCameraPatchManager::AddPatch` spawns the evaluator with `Initialize(nullptr)`, the same path the LS Component uses (§17 / TechDoc §3.14). Skips `BindCameraActionsForNewCamera` by construction, so Patch evaluators carry no Action delegates / arrays. PCM-level Action dispatch never crosses into a Patch — a `TargetNodeClass` matching a node inside an active Patch would not fire (PatchSystemProposal §16.9).

30. **`ActivePatches` ordering is `(LayerIndex asc, PushSequence asc)`, sorted-inserted at AddPatch** — `Apply` iterates in this order; later-layer patches see earlier-layer patches' output as their upstream. PushSequence is a monotonic counter on PatchManager. Older patches at equal layers run earlier (push-order tiebreaker). Layer override at AddPatch (`Params.LayerIndexOverride != MIN_int32`) wins over the asset's `DefaultLayerIndex`.

31. **Patch envelope's exit ramp scales by `ExitStartAlpha` snapshot** — `ExpirePatch` (manual) and `CheckPatchScheduleExpiration` (Duration / Condition / OnCameraChange channels) both go through a shared `TransitionPatchToExiting` helper that snapshots `CurrentAlpha` into `ExitStartAlpha` before flipping Phase to Exiting. The Exiting branch computes alpha as `ExitStartAlpha · (1 - ease(t))`, so a Patch retired mid-Entering fades from where it had reached, not from 1. Short-circuit to Expired when `ExitStartAlpha <= 0` or `ExitDuration <= 0` — no wasted invisible frames.

32. **Patch removal from `ActivePatches` happens at end of `Apply`, never mid-iteration** — `ExpirePatch` only flips Phase to Exiting (or directly to Expired in the short-circuit case); the actual array compaction + evaluator destruction happens in the reverse-iter sweep at the tail of `Apply`. This makes `ExpirePatch` reentrancy-safe — a Patch's node tick calling `ExpirePatch` does NOT invalidate the for-loop iterator. Patch evaluator destruction calls `AActor::Destroy()` then `Evaluator = nullptr`; the `UComposableCameraPatchInstance` itself is GC-collected on the next pass once the array no longer references it. (The remaining latent footgun is `AddPatch` during `Apply` — `Insert` may reallocate `ActivePatches` and invalidate the iterator. Stage-2-era residual; will be addressed alongside the V2 deferred-add queue if a real callsite needs it.)

33. **Shot overlap exits promote prior state, hard cuts reseed** -- in the LS Shot Section path, the per-shot prior caches belong to Section identity, not to the temporary primary/secondary role. When an authored overlap exits A+B -> B, the LS component marks that the new primary was the previous secondary and `CompositionFramingNode` promotes `LastSecondaryOutput*` into `LastPrimaryOutput*`. This preserves framing-zone / damping continuity on the first post-blend frame. A true hard cut (changed primary that was not the previous secondary) still clears primary prior state so the new Shot hard-seeds at its authored screen position. A changed secondary (A+B -> A+C) clears secondary prior state so C does not inherit B's zone/damping cache.

---

## 19. Module Structure

```
ComposableCameraSystem.uplugin
  ├─ ComposableCameraSystem          (Runtime, PreDefault)
  │    Public/
  │      Core/         — PCM, Director, ContextStack, EvaluationTree, ModifierManager
  │      Cameras/      — CameraBase, CameraPose, ActivateParams
  │      Nodes/        — All camera node types
  │      Transitions/  — All transition types
  │      Modifiers/    — Modifier base and data assets
  │      Interpolator/ — Interpolator base and implementations
  │      Math/         — Spline implementations
  │      DataAssets/   — TypeAsset, TransitionTable, Modifier, Transition data assets;
  │      │               ParameterTableRow (DataTable row struct)
  │      Actions/      — Camera action types
  │      AsyncActions/ — Async curve evaluators
  │      Utils/        — BlueprintLibrary, ProjectSettings, ImpulseShapes
  │    Private/
  │      (mirrors Public structure)
  │      Tests/        — Automation tests
  │
  ├─ ComposableCameraSystemEditor    (Editor, PostEngineInit)
  │    Public/ & Private/
  │      Editors/         — Graph editors, asset editor hosts
  │      Toolkits/        — FAssetEditorToolkit subclasses
  │      Factories/       — UFactory subclasses (TypeAsset, TransitionTable, Modifier, Transition)
  │      AssetTools/      — UAssetDefinition subclasses (Content Browser integration,
  │      │                  display names, colors, category, double-click actions)
  │      Customizations/  — IPropertyTypeCustomization / IDetailCustomization
  │      Widgets/         — Reusable Slate widgets
  │      ScriptedActions/ — Scripted asset actions
  │    Resources/
  │      Content/Icons/   — SVG icons for ClassIcon / ClassThumbnail brushes
  │
  └─ ComposableCameraSystemUncookedOnly  (UncookedOnly, Default)
       — Custom K2 nodes, graph pin widgets, graph panel pin factory

Dependencies: EngineCameras, EnhancedInput, ActorSequence
```

### Content Browser Registration

All four primary data asset types are registered via `UAssetDefinitionDefault` subclasses in the editor module's `AssetTools/` folder. Each registers a display name, color, and Content Browser category ("Composable Camera System"), and most open a custom editor on double-click.

| Asset Type | Display Name | Color | Factory |
|---|---|---|---|
| `UComposableCameraTypeAsset` | Composable Camera Type | Teal/Cyan | `UComposableCameraTypeAssetFactory` |
| `UComposableCameraTransitionTableDataAsset` | Camera Transition Table | Soft blue | `UComposableCameraTransitionTableFactory` |
| `UComposableCameraNodeModifierDataAsset` | Camera Node Modifier | Purple (160,90,200) | `UComposableCameraModifierFactory` |
| `UComposableCameraTransitionDataAsset` | Camera Transition | Warm orange (220,140,50) | `UComposableCameraTransitionFactory` |

Custom SVG thumbnails are registered through `FComposableCameraEditorStyle` using the `ClassIcon.ClassName` / `ClassThumbnail.ClassName` naming convention. The style set is initialized in `FComposableCameraSystemEditorModule::StartupModule()` and provides 16×16 class icons and 64×64 thumbnails for all four asset types.

---

## 20. Debug HUD

The system provides an on-screen debug display via `AComposableCameraPlayerCameraManager::DisplayDebug`, triggered by UE's `showdebug` console command.

### Enabling

In-game console: `showdebug camera` (triggers the PCM's DisplayDebug override).

### HUD Sections

The debug HUD displays the following sections, top to bottom:

1. **Camera Pose**: Current position, rotation, FOV, aspect ratio
2. **Running Camera**: Class name, gameplay tag, transient status with lifetime countdown
3. **Camera Nodes**: Ordered list of all nodes on the running camera (by index and class name)
4. **Context Stack & Evaluation Tree**: Full stack dump from top to bottom, with each context's Director showing its evaluation tree structure. Tree nodes are color-coded:
   - Green: Leaf nodes (cameras)
   - Yellow: Transition inner nodes (with class name, percentage, elapsed/total time)
   - Orange: Reference leaf nodes (inter-context Director references)
   - Red: Pending destroy entries (popped contexts in transition)
   - White: Active context marker (`->`)
5. **Camera Actions**: All active actions with scope (camera-scoped vs. persistent)
6. **Modifiers**: All registered modifiers grouped by camera tag and node class, plus effective (highest-priority) modifiers

### Implementation

All debug consumers (the 2D panel, `showdebug camera`, and the `CCS.Dump.*`
console commands) read from a single structured-snapshot pipeline:

- `UComposableCameraEvaluationTree::BuildDebugSnapshot` — flattens the tree DFS
  pre-order into a `TArray<FComposableCameraTreeNodeSnapshot>`.
- `UComposableCameraDirector::BuildDebugSnapshot` — wraps the tree snapshot
  with running-camera label + last pose.
- `UComposableCameraContextStack::BuildDebugSnapshot` — emits contexts top →
  base (active first), then pending-destroy entries.

Text rendering for `showdebug camera` goes through
`ComposableCameraDebug::AppendTreeNodeLine` (in `Utils/ComposableCameraDebugFormatUtils.h`),
which formats a single snapshot node into a caller-provided `FStringBuilderBase`.
The panel uses the same snapshot but renders geometric primitives instead of
text. There is no parallel "debug string" pipeline — adding a new tree node
kind is one switch statement in `AppendTreeNodeLine` plus the snapshot enum,
not two parallel branch sites.

### In-Viewport Debug Panel

Alongside the `showdebug camera` path, the plugin offers a richer in-viewport overlay built on `UDebugDrawService`:

- **Toggle**: console variable `CCS.Debug.Panel 0|1`. Layout width is controlled by `CCS.Debug.Panel.Width` (fraction of screen width, 0.15–0.60, default 0.32).
- **Lifecycle**: `FComposableCameraDebugPanel::Initialize()` / `Shutdown()` are called from `FComposableCameraSystemModule::StartupModule` / `ShutdownModule`. Registration happens once, unconditionally — the draw delegate early-outs when the CVar is 0, so the cost of "disabled" is a single CVar read per viewport per frame.
- **Rendering hook**: `UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate)`. The delegate fires once per local viewport with a `UCanvas*` + `APlayerController*`. The panel resolves the PCM via `PC->PlayerCameraManager` and casts to `AComposableCameraPlayerCameraManager`; non-composable PCMs are skipped silently.
- **Layout**: a single left-aligned panel subdivided into vertically stacked regions, each with a title bar, border, and content area. Region order is fixed in phase 1 and covers: (1) Current Pose, (2) Context Stack & Evaluation Tree, (3) Running Camera (class, tag, lifetime, nodes, exposed parameters, internal variables), (4) Actions, (5) Modifiers (placeholder — defer to `showdebug camera` for the full breakdown).
- **Current Pose region layout**: four labelled groups flowed across **two columns** (driven by `bIsPose` + `PoseGroups` on `FRegionLines`, rendered by `DrawPoseStructured`). Left column: **Transform** (Position / Rotation / Forward) and **Context** (active context name). Right column: **Projection** (Mode, FOV — annotated with `(from Nmm)` when the pose is in `FocalLength` dual-mode; Aspect; Ortho W / Near / Far only when Mode is Orthographic) and **Physical** with three data sources (see below). Two-column layout keeps the region height to the max of the two column line-counts, avoiding a ~15-line single-column tower on poses with Physical active.
- **Physical group — three-way data source**: the pose's Physical block is only authoritative when a LensNode (or similar) has driven `PhysicalCameraBlendWeight > 0`. In the proxy-via-CineCamera path (e.g. Sequencer Camera Cut Track → LS Actor → `UComposableCameraViewTargetProxyNode` writing the pose) the CineCamera has already baked DoF / exposure into `PostProcessSettings` itself, so the pose's `PhysicalCameraBlendWeight` stays 0 and the raw Aperture / Focus / ISO / Shutter / Sensor fields remain at struct defaults — displaying those would be misleading. The Physical group therefore discriminates:
  1. **`PhysicalCameraBlendWeight > 0`** — pose-driven. Header `-- Physical --`, rows: Weight / Aperture / Focus (`auto` when `FocusDistance <= 0` sentinel) / ISO / Shutter / Sensor, all read from `CurrentCameraPose`.
  2. **`PhysicalCameraBlendWeight == 0` AND `PCM->GetViewTarget()` has a `UCineCameraComponent`** — CineCamera-driven. Header `-- Physical (CineCamera) --` (suffix makes the data source visible without spending a row on a `Source:` line), rows: Focal (from `CurrentFocalLength` — the Projection group's `(from Nmm)` annotation is absent here because the proxy node writes FOV in degrees-mode) / Aperture (`CurrentAperture`) / Focus (`CurrentFocusDistance`) / ISO (`PostProcessSettings.CameraISO` if `bOverride_CameraISO`, else `auto`) / Shutter (`CameraShutterSpeed` with same override gate) / Sensor (`Filmback.SensorWidth × SensorHeight`).
  3. **neither** — collapsed single row `Status: off`.
- **Data sources**: same read-only public accessors already used by `DisplayDebug`. The Context Stack & Tree region consumes `UComposableCameraContextStack::BuildDebugSnapshot` (the structured path described below) — this is the single source of truth; no parallel string path exists. The PCM exposes `GetContextStack()` so the debug panel does not need to be a friend of the PCM class.
- **Relationship to the ShowDebug HUD**: the panel is additive, not a replacement. `showdebug camera` still works and remains the canonical "stacks with other engine showdebug categories" workflow. The panel targets richer layout and per-region toggling that cannot be expressed inside `FDisplayDebugManager`'s vertical text flow.

**Structured snapshot path.** The Context Stack & Tree region consumes a structured snapshot (same snapshot the `showdebug camera` text path walks):

- `UComposableCameraEvaluationTree::BuildDebugSnapshot(TArray<FComposableCameraTreeNodeSnapshot>&)` — flattens the tree DFS pre-order (root at depth 0, Left before Right under each Inner). Per-node fields: Kind (Leaf / ReferenceLeaf / InnerTransition), Depth, DisplayLabel, bDestroyed, bIsTransient / LifeElapsed / LifeTotal, TransitionProgress / TransitionElapsed / TransitionTotal. Also tags `bIsDominantLeaf = true` on the single node you reach by walking root → Right → Right → ... (i.e. the node that would survive if every in-flight transition collapsed).
- `UComposableCameraDirector::BuildDebugSnapshot(FComposableCameraContextSnapshot&)` — fills RunningCameraDisplay / LastPose + delegates to the tree.
- `UComposableCameraContextStack::BuildDebugSnapshot(FComposableCameraContextStackSnapshot&)` — emits contexts top-to-base, then pending-destroy entries flagged `bIsPendingDestroy = true`.

The snapshot structs live in `Public/Debug/ComposableCameraDebugPanelData.h` and have no reflection / no `WITH_EDITOR` guard — they are plain C++ data, always available.

This drives three visual features the string path could not express:

1. **Transition progress bars.** Inner-transition lines get a translucent amber fill underlay proportional to `TransitionProgress`, drawn behind the text.
2. **Proper geometric tree connectors.** Each tree node carries `bIsLastSibling` + `AncestorLastFlagsBitmask` (a 32-bit mask where bit L is set iff the ancestor at depth L was the last child of its parent). The renderer draws `├ / └ / │` connectors as filled rects: continuation stem `│` at column L when bit (L+1) is 0; at the node's own column, a half-height stem + horizontal tick for `└` (last sibling) or a full-height stem + tick for `├` (middle child). Geometric rendering means no dependency on box-drawing font glyphs.
3. **Dominant-leaf highlight.** The leaf tagged `bIsDominantLeaf` draws with a translucent green underlay and the text in `CActiveMarker` (bright cyan). No text prefix is used — the underlay plus color shift alone disambiguate.

Visual markers are kept deliberately narrow to avoid glyph collisions:
- **Active context**: small filled-rect bullet to the left of the name + `CActiveMarker` text color. No `-> ` prefix (that glyph is reserved for nothing — dropped from the UI).
- **Base context**: `(base)` suffix on the context name.
- **Pending-destroy context**: `[pending]` prefix + `CDestroyed` red.
- **Reference leaf**: explicit `[ref] DirectorName` prefix in `CRefLeaf` orange.
- **Dominant leaf**: green underlay + cyan text. No text prefix.
- **Transition node**: `%` + `(elapsed/total)s` suffix + amber progress-bar underlay.

`showdebug camera` renders the same snapshot as plain text through
`ComposableCameraDebug::AppendTreeNodeLine`. Adding a new tree node kind means
updating the `EComposableCameraTreeNodeKind` enum and the one switch in
`AppendTreeNodeLine`; both the 2D panel and the text HUD pick up the new kind
automatically.

### In-World 3D Debug Draw

Complementing the 2D HUD panel, a separate facility draws world-space debug primitives for the currently running camera:

- **Two-tier gating**:
    - `CCS.Debug.Viewport 0|1` is the master switch. When 1 it turns on the camera FRUSTUM (with an F8 gate — see below) and invokes each node's `DrawNodeDebug` hook. **It does not turn per-node gizmos on by itself.**
    - Per-node gizmos are controlled by PER-NODE CVars: `CCS.Debug.Viewport.PivotOffset`, `CCS.Debug.Viewport.PivotDamping`, `CCS.Debug.Viewport.LookAt`, `CCS.Debug.Viewport.CollisionPush`, `CCS.Debug.Viewport.Spline`. Each defaults to 0. Users enable only the gizmos they need for the bug in front of them.
    - `CCS.Debug.Viewport.AlwaysShow 1` forces the frustum even while possessing (frustum-only override; per-node gizmos don't need it).
- **Frustum auto-hide (F8 gate)**: the frustum is the one piece of debug draw that actually occludes the scene when the player is looking through the camera, so it only fires while `GEditor->bIsSimulatingInEditor` is true (F8 eject / Simulate mode). Per-node gizmos have no F8 gate — they typically live out in the world in front of the camera, and are valuable during live gameplay. So the policy is: frustum → "observe from outside" only; node gizmos → "any time their CVar is on".
- **Lifecycle**: `FComposableCameraViewportDebug::Initialize() / Shutdown()` from the runtime module. Registers an `FTSTicker::GetCoreTicker()` delegate. Ticker body compiles to nothing in shipping (`#if !UE_BUILD_SHIPPING`).
- **Rendering pathway**: the ticker finds the PIE/Game world, pulls the first PCM's running camera, and calls `DrawCameraDebug(World, bDrawFrustum)`. The bool is computed once per frame from the F8 gate + AlwaysShow override; the node walk inside `DrawCameraDebug` always happens. Draws go through the world's LineBatcher — visible in every viewport that renders the world, including the editor viewport during F8 eject. (An earlier iteration used `UDebugDrawService::Register("Game", ...)` but that hook does not fire from editor viewports during F8 eject.) Runtime module adds a `Target.bBuildEditor` conditional `UnrealEd` dependency to access `GEditor->bIsSimulatingInEditor`; shipping builds don't link `UnrealEd` and the WITH_EDITOR block is stripped.
- **Default content**: a yellow frustum pyramid drawn at the running camera's `CameraPose` via `DrawDebugCamera`. During a transition the running camera is the most recent activation target — this shows the target's frustum, not the blended PCM output. Seeing both source and target simultaneously is a follow-up (would require walking the active director's tree).
- **Extension point**: two virtuals on the camera hierarchy, both `#if !UE_BUILD_SHIPPING`:
    - `AComposableCameraCameraBase::DrawCameraDebug(UWorld*) const` — non-virtual; draws the frustum + iterates `CameraNodes`, calling each node's `DrawNodeDebug`.
    - `UComposableCameraCameraNodeBase::DrawNodeDebug(UWorld*) const` — virtual with an empty default. Concrete nodes override to draw their own gizmos. Nodes read current-frame pin values through the usual `GetInputPinValue<T>()` / pin-bound UPROPERTY path — the hook fires after TickNode so those values reflect the most recent evaluation.
- **Shipped node overrides** (each with its own CVar, default 0):
    - `PivotOffsetNode` (`CCS.Debug.Viewport.PivotOffset`) — yellow sphere at the post-offset pivot (mirrored in `LastComputedPivot` since output pins aren't re-readable by name).
    - `PivotDampingNode` (`CCS.Debug.Viewport.PivotDamping`) — magenta sphere at the damped pivot (reads existing `LastPivotPosition` state).
    - `LookAtNode` (`CCS.Debug.Viewport.LookAt`) — cyan sphere at the target (resolves `ByPosition` / `ByActor` / socket the same way the tick path does). Possessed play: sphere only. F8 eject: adds a thin cyan line from camera to target.
    - `CollisionPushNode` (`CCS.Debug.Viewport.CollisionPush`) — green/red trace sphere at the pivot (bTraceUseSphere) or small point (line trace), red sphere at the hit location when blocked. Possessed play: spheres only. F8 eject: adds the full pivot→camera trace line and the cyan self-collision sphere around the camera. Cache members (`LastTraceStart/End/HitLocation`, `LastSelfSphereCenter`, `bLastTraceBlocked`) populated in `FindCollisionPoint`.
    - `SplineNode` (`CCS.Debug.Viewport.Spline`) — violet polyline sampled 64 times via `IComposableCameraSplineInterface::GetWorldSpacePositionByDistanceOnSpline` + violet sphere at the camera's current position.
    - `ReceivePivotActorNode` (`CCS.Debug.Viewport.ReceivePivotActor`) — white sphere at the pivot actor's location (or bone socket when `bUseBoneForPivot`).
    - `RelativeFixedPoseNode` (`CCS.Debug.Viewport.RelativeFixedPose`) — orange sphere at the reference origin (the authored `RelativeTransform` location, or the `RelativeActor` / socket when in actor mode). Answers "what am I positioning relative to?".
    - `ScreenSpacePivotNode` (`CCS.Debug.Viewport.ScreenSpacePivot`) — teal sphere in 3D at the resolved world pivot, PLUS a 2D HUD overlay (safe-zone rectangle, its center marker, and the projected pivot marker). The 2D overlay mirrors the node's own screen-space math: two branches for `bConstrainAspectRatio = false` (whole viewport) and `= true` (letterboxed, uses `ULocalPlayer::GetProjectionData` to offset for the pillar-boxed game area).
    - `ScreenSpaceConstraintsNode` (`CCS.Debug.Viewport.ScreenSpaceConstraints`) — pink sphere in 3D at the constrained actor, PLUS the same two-branch 2D safe-zone overlay.
- **Two debug pipelines, one service.** The viewport debug now registers TWO UDebugDrawService hooks: (a) a 3D hook on the world LineBatcher (ticker-driven, fires in both PIE possessed and F8 eject via the always-running FTSTicker path); (b) a 2D hook on the "Game" show flag channel (fires during `UGameViewportClient::Draw`, so PIE possessed + standalone only, NOT F8 eject). Nodes implement `DrawNodeDebug(UWorld*, bool)` for the 3D path and `DrawNodeDebug2D(UCanvas*, APlayerController*)` for the 2D path. Both gated by the same per-node CVar so one toggle covers both views of the same node.
- **Intentionally without gizmos** (documented in headers): `ComputeDistanceToActorNode` (compute-chain, only runs at BeginPlay), `ControlRotateNode` / `AutoRotateNode` (pure rotation state, no clean spatial primitive), `CameraOffsetNode` (offset is derivative of the camera pose itself — the frustum already shows the result), `ImpulseResolutionNode` (impulse shapes are actors with their own collision visuals), `RotationConstraints` (yaw/pitch range cones hard to draw cleanly), `ViewTargetProxyNode` (internal `Hidden` proxy), `MixingCameraNode` (multi-camera blend — no single spatial anchor), `LensNode` / `FilmbackNode` / `FieldOfViewNode` / `OrthographicNode` / `PostProcessNode` (pure camera-parameter nodes — frustum already reflects their effect).
- **Legacy flag removed.** The old `AComposableCameraPlayerCameraManager::bDrawDebugInformation` UPROPERTY is deleted. It gated pre-framework debug draws inside a handful of nodes and transitions; all usages have migrated to `DrawNodeDebug` (nodes) or `DrawTransitionDebug` (transitions — see next bullet).
- **Per-transition gizmos** have a parallel framework to per-node ones:
    - `UComposableCameraTransitionBase::DrawTransitionDebug(UWorld*, bool bViewerIsOutsideCamera) const` — virtual with empty default, fires every frame for every transition currently living in the eval tree. Each concrete transition's override self-gates on its own `CCS.Debug.Viewport.Transitions.<Name>` CVar (Linear / Smooth / Ease / Cubic / Inertialized / Cylindrical / Spline / PathGuided / DynamicDeocclusion).
    - Pose snapshots for the override's consumption are cached on the transition by the one call site that sees all three at once: `FComposableCameraEvaluationTreeInnerNodeWrapper::Evaluate` writes `LastDebugSourcePose` / `LastDebugTargetPose` / `LastDebugBlendedPose` under `#if !UE_BUILD_SHIPPING`. Value-type copies; no UPROPERTY; shipping builds strip them entirely.
    - Tree walking happens through `UComposableCameraEvaluationTree::DrawTransitionsDebug(UWorld*, bool)`, a DFS that recurses into reference-leaves so inter-context blends also surface the source context's in-flight intra-context transitions. The viewport ticker invokes this once per PCM via `PCM->GetContextStack()->GetActiveDirector()->GetEvaluationTree()->DrawTransitionsDebug(...)`.
    - Standard visualization lives on a shared base-class helper `DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor)` — green source sphere + blue target sphere + accent progress sphere at the blended position, plus F8/SIE-only half-scale source/target frustums. The helper is deliberately silent about the PATH between source and target — that's per-transition-type (see next bullet).
    - Each transition draws its own PATH polyline in accent color on top of the standard markers: Linear/Smooth/Ease/Cubic use a straight `DrawDebugLine` (they lerp position linearly in space — only the timing curve differs), Cylindrical samples 32 points along the cylindrical arc via a static `SampleCylindricalPathPosition` helper, Inertialized pre-samples 33 target-relative offsets at OnBeginPlay (`DebugPathOffsets`) and adds the live target position at draw time, Spline reuses `EvaluatePositionOnCurve(t, start, end)`, PathGuided samples the rail spline 32 times, DynamicDeocclusion draws only feeler rays (its path is shaped by accumulating dynamic trace offsets and is not predictable without actually running the traces).
    - `ViewTargetTransition` (the `Hidden, NotBlueprintable` engine SetViewTarget bridge) is deliberately not exposed — no user-authored instance to debug, a CVar would just be clutter.
    - Gizmos disappear the moment the transition's Inner node collapses (blend finished), which is exactly when the debug data stops being meaningful.
- **Legacy HUD overlays removed.** `ScreenSpacePivotNode` and `ScreenSpaceConstraintsNode` used to register a `HUD->OnHUDPostRender` lambda to paint a 2D safe-zone rectangle + projected-pivot marker. That entire path (the lambda, the `FDelegateHandle DrawDebugHandle` member, the `DrawDebugInfo` function, the `BeginDestroy` override) is gone — 2D screen-space overlays belong to a future 2D debug panel, not to per-node gizmos.

This is the "if it feels wrong but I don't know why" debugger: enable `CCS.Debug.Viewport 1`, fly the editor camera outside the player's head, and see where the camera's actually pointing and what each node is doing in world space. The panel tells you *which* nodes ran; the viewport draw tells you *where their output lives*.

### Editor Debug Snapshot

`AComposableCameraCameraBase` also exposes `SnapshotDebugState()` (`#if WITH_EDITOR`), which produces an `FComposableCameraDebugSnapshot` containing per-node active state, the camera pose after each node, and formatted output pin values. This is consumed by the Camera Type Asset editor's debug ticker during PIE to drive visual overlays on the graph. See EditorDesignDoc Section 20 for the full editor-side architecture.

---

## 21. Testing

Tests live in `Private/Tests/` and use UE's automation test framework:

- **EvaluationTreeTests**: Tree building, collapse logic (chained collapse, source-destroyed collapse), multi-level transitions
- **BugFixTests**: Regression tests for specific fixes (SmoothStep/SmootherStep correctness, null-safety guards)
- **ExecutionMockTests**: End-to-end scenario mocks (gameplay→cutscene flow, rapid switches, cut-interrupts-transition, nested transitions, blend verification, inter-context transitions, empty tree safety)

---

## Appendix A: Common Patterns

### Activating a Camera from Blueprint (via K2 Node)

The custom K2 node `UK2Node_ActivateComposableCamera` is placed in a Blueprint graph.
The designer picks a Camera Type Asset, and the K2 node dynamically generates typed
pins for each exposed parameter and exposed variable. At compile time, the node expands
into a call to `ActivateComposableCameraFromTypeAsset` with the parameter block populated.

```
Activate Composable Camera
  Camera Type Asset: ThirdPersonCamera
  Context Name: "Gameplay"          // auto-ensures this context
  Transition Override: (none)       // uses type asset's default transition
  // Activation Params (struct pin):
  bPreserveCameraPose: true
  bIsTransient: false
  // Exposed params shown as typed pins:
  FollowActor: PlayerPawnRef
  ArmLength: 300.0
```

### Pushing a Cinematic Context

```
Activate Composable Camera
  Camera Type Asset: CinematicCamera
  Context Name: "Cinematic"         // pushes new context on top
  Transition Override: InertializedTransitionAsset
  bIsTransient: true
  LifeTime: 5.0
// After 5 seconds: auto-pop back to Gameplay with default transition
```

### Returning from Cinematic

Automatic (transient camera expires) or manual:
```
TerminateCurrentCamera(
  TransitionOverride: FadeTransitionAsset
)
```

---

## Appendix B: Composable Camera Editor

The visual editor is the primary design surface for camera types. The full editor design is documented in **[EditorDesignDoc.md](EditorDesignDoc.md)**.

Key concepts:

- **Camera Type Assets** (`UComposableCameraTypeAsset`): Data assets defining camera node composition, exposed parameters, internal / exposed variables, and default transitions. This is the only supported way to define cameras. Type assets maintain two execution order structures: `ComputeExecutionOrder` (linear order of compute nodes) and `ComputeFullExecChain` (hierarchical structure interleaving compute nodes with `SetVariable` writes). At runtime, `BeginPlayCamera` and `TickCamera` prefer `FullExecChain` when available (supporting interleaved variable dispatch), falling back to linear array walks for legacy type-asset data saved before the exec chain was introduced.
- **Visual Node Graph**: Linear-chain node editor with typed data pins, forward-only wiring, and visible data flow between nodes.
- **Four-category Parameter System** — the author-time taxonomy exposes four distinct value kinds that all share the same runtime `FComposableCameraRuntimeDataBlock`:
  - **Wired data pins** — inter-node values. Producers are other nodes in the chain; the layout step assigns each output pin a slot and each connected input reads from that slot.
  - **Exposed parameters** — caller-facing, one-shot. The K2 node and DataTable row both present them as input pins; their value lands in the `ParameterBlock` at activation and is copied into the slot once. Not readable or writable from inside the graph at runtime (they're a one-shot input, not a persistent variable).
  - **Internal variables** — camera-level persistent slots that are *only* readable / writable from inside the graph via Get / Set variable nodes. The caller cannot reach them; their initial value comes from the variable's `InitialValueString`, applied at activation.
  - **Exposed variables** — identical to internal variables at runtime (same slot map, same Get / Set node behavior), but the caller *may* override the initial value at activation time through the same `ParameterBlock` keyspace used by exposed parameters. After activation they remain writable from inside the graph like any internal variable. This is the natural middle ground: "node-only state that the author also wants to expose as an activation-time knob."
  - Name uniqueness is enforced across the union of exposed parameters, internal variables, and exposed variables, since all three share the runtime's `FName` keyspace inside the data block / ParameterBlock. See `UComposableCameraTypeAsset::Build()`.
- **Custom K2 Node**: `UK2Node_ActivateComposableCamera` dynamically generates Blueprint pins matching the selected camera type's exposed parameters AND exposed variables (both flow through the same `DynamicParameterPinNames` array and the same `SetParameterBlockValue` expansion path, so the author sees one unified set of "activation-time knobs").
- **DataTable Integration**: Camera parameters and exposed variables can be stored in DataTable rows for AI and data-driven camera selection. Rows use `FComposableCameraParameterTableRow` (a fixed USTRUCT carrying a `TMap<FName, FString>` of serialized values) and are activated via `UComposableCameraBlueprintLibrary::ActivateComposableCameraFromDataTable`. Both exposed parameters and exposed variables share the row's keyspace — the runtime walks both sets at activation, routing each value into the correct slot. The shared string→typed-value parser lives at `FComposableCameraParameterBlock::ApplyStringValue` and is reused by the editor-side row customization so anything typed in the editor round-trips identically at runtime. See **[EditorDesignDoc.md §12](EditorDesignDoc.md)** for the full layering.
