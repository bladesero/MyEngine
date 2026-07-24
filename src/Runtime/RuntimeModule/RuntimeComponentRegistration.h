#pragma once

// Registers concrete component descriptors in deterministic module order.
// Only the Runtime composition root calls this function.
void RegisterRuntimeComponentTypes();
