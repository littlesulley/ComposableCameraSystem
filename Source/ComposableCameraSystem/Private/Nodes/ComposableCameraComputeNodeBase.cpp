// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraComputeNodeBase.h"

// Intentionally empty. All behavior for UComposableCameraComputeNodeBase lives
// in the header as an abstract base -ExecuteBeginPlay has an empty default
// implementation there, and everything else (pin system, Initialize, pin value
// accessors, internal variable accessors) is inherited unchanged from
// UComposableCameraCameraNodeBase.
//
// Concrete compute nodes live in sibling files under Nodes/ and supply their
// own ExecuteBeginPlay override.
