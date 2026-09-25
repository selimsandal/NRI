// © 2026 NVIDIA Corporation

#pragma once

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <mutex>

#if NRI_ENABLE_METAL_SHADER_CONVERTER
#    include <metal_irconverter/metal_irconverter.h>
#endif

#include "SharedExternal.h"

namespace nri {

struct DeviceMetal;
struct MemoryMetal;
struct BufferMetal;
struct AccelerationStructureMetal;
struct TextureMetal;
struct QueueMetal;
struct FenceMetal;
struct CommandAllocatorMetal;
struct CommandBufferMetal;
struct DescriptorMetal;
struct DescriptorPoolMetal;
struct DescriptorSetMetal;
struct PipelineLayoutMetal;
struct PipelineMetal;
struct PipelineCacheMetal;
struct QueryPoolMetal;
struct SwapChainMetal;

} // namespace nri

#include "DeviceMetal.h"
