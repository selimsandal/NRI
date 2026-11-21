// © 2026 NVIDIA Corporation

#pragma once

namespace nri {

// Compute kernels in "Shaders/InternalMetal.metal"
enum class InternalKernelMetal : uint8_t {
    FILTER_DRAWS,
    PREPARE_DRAW_ROOTS,
    EMULATE_DRAWS,
    CLEAR_STORAGE_BUFFER,
    CONVERT_INSTANCES,
    COPY_TOP_LEVEL_HEADER,
    COPY_WORDS,
    PREPARE_RAYS_INDIRECT,

    // "CLEAR_STORAGE_TEXTURE + dimension * 3 + type", see "GetClearStorageKernel"
    CLEAR_STORAGE_TEXTURE,

    MAX_NUM = CLEAR_STORAGE_TEXTURE + 6 * 3
};

// Host mirrors of the ray-tracing helper kernel argument structures ("buffer(3)")
struct ConvertInstancesArgsMetal {
    MTL::GPUAddress src; // "TopLevelInstance" array
    MTL::GPUAddress dst; // "MTLIndirectAccelerationStructureInstanceDescriptor" array
    MTL::GPUAddress header;
    uint64_t accelerationStructure;
    uint32_t instanceNum;
    uint32_t padding;
};

struct CopyTopLevelHeaderArgsMetal {
    MTL::GPUAddress srcContributions;
    MTL::GPUAddress dstHeader;
    uint64_t dstAccelerationStructure;
    uint32_t num;
    uint32_t padding;
};

struct CopyWordsArgsMetal {
    MTL::GPUAddress src;
    MTL::GPUAddress dst;
};

struct PrepareRaysIndirectArgsMetal {
    MTL::GPUAddress src;      // "DispatchRaysIndirectDesc"
    MTL::GPUAddress dst;      // "IRDispatchRaysArgument::DispatchRaysDesc"
    MTL::GPUAddress dispatch; // "MTLDispatchThreadsIndirectArguments"
};

// Native ray tracing pipelines ("NRI.metal"): shader binding table records start with "NriShaderIdentifier"
struct ShaderIdentifierMetal {
    uint64_t intersectionShaderHandle; // visible function table index, 0 - none
    uint64_t shaderHandle;             // visible function table index, 0 - none
    uint64_t reserved[2];
};

struct ShaderTableMetal {
    MTL::GPUAddress startAddress;
    uint64_t size;
    uint64_t stride;
};

// "NriRayDispatchArguments", bound at "ARGUMENT_SLOT_RAY_DISPATCH" for "nri_ray_dispatch"
struct RayDispatchArgumentsMetal {
    ShaderTableMetal raygen;
    ShaderTableMetal miss;
    ShaderTableMetal hit;
    ShaderTableMetal callable;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t padding;
    MTL::ResourceID functions; // "visible_function_table<NriRayFunction>"
};

static_assert(sizeof(ShaderIdentifierMetal) == 32, "Must match 'NriShaderIdentifier' ('NRI.metal')");
static_assert(sizeof(RayDispatchArgumentsMetal) == 120, "Must match 'NriRayDispatchArguments' ('NRI.metal')");

// "TopLevelInstanceBits" 0-3 match "MTLAccelerationStructureInstanceOptions", micromap bits are dropped by the kernel
static_assert(sizeof(TopLevelInstance) == 64, "Unexpected 'TopLevelInstance' size");
static_assert(sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor) == 72, "Unexpected 'MTLIndirectAccelerationStructureInstanceDescriptor' size");
static_assert((uint32_t)TopLevelInstanceBits::TRIANGLE_CULL_DISABLE == MTL::AccelerationStructureInstanceOptionDisableTriangleCulling, "Instance flag mismatch");
static_assert((uint32_t)TopLevelInstanceBits::TRIANGLE_FLIP_FACING == MTL::AccelerationStructureInstanceOptionTriangleFrontFacingWindingCounterClockwise, "Instance flag mismatch");
static_assert((uint32_t)TopLevelInstanceBits::FORCE_OPAQUE == MTL::AccelerationStructureInstanceOptionOpaque, "Instance flag mismatch");
static_assert((uint32_t)TopLevelInstanceBits::FORCE_NON_OPAQUE == MTL::AccelerationStructureInstanceOptionNonOpaque, "Instance flag mismatch");

// Internal shader variants by color format type
enum class ColorTypeMetal : uint8_t {
    FLOAT,
    UINT,
    SINT
};

static inline ColorTypeMetal GetColorType(Format format) {
    const FormatProps& props = GetFormatProps(format);

    if (!props.isInteger)
        return ColorTypeMetal::FLOAT;

    return props.isSigned ? ColorTypeMetal::SINT : ColorTypeMetal::UINT;
}

struct ClearPipelineKeyMetal {
    MTL::PixelFormat colors[8] = {}; // Metal 4 pipelines don't include depth / stencil formats
    uint8_t colorNum = 0;
    uint8_t colorIndex = 0;
    uint8_t sampleNum = 1;
    ColorTypeMetal colorType = ColorTypeMetal::FLOAT;
    PlaneBits planes = PlaneBits::NONE;
};

struct ClearPipelineMetal {
    ClearPipelineKeyMetal key;
    MTL::RenderPipelineState* pipeline = nullptr;
    MTL::DepthStencilState* depthStencil = nullptr;
};

struct ResolvePipelineMetal {
    MTL::RenderPipelineState* pipeline = nullptr;
    MTL::PixelFormat format = MTL::PixelFormatInvalid;
    ColorTypeMetal colorType = ColorTypeMetal::FLOAT;
    bool isArray = false;
};

// Device-level internal shaders ("Shaders/InternalMetal.metal"), pipelines are created on first use. Thread-safe
struct InternalShadersMetal {
    InternalShadersMetal(DeviceMetal& device);
    ~InternalShadersMetal();

    inline MTL4::FunctionDescriptor* GetDepthOnlyFragmentFunction() const {
        return m_DepthOnlyFragmentFunction;
    }

    // "nri_ray_dispatch", the compute function of native ray tracing pipelines (the caller releases it)
    inline MTL4::FunctionDescriptor* NewRayDispatchFunction() const {
        return NewFunction("nri_ray_dispatch");
    }

    Result Create();
    MTL::ComputePipelineState* GetKernel(InternalKernelMetal kernel);
    ClearPipelineMetal GetClearPipeline(const ClearPipelineKeyMetal& key); // returned by value, since the cache can grow concurrently
    MTL::RenderPipelineState* GetResolvePipeline(MTL::PixelFormat format, ColorTypeMetal colorType, bool isArray);

private:
    MTL4::FunctionDescriptor* NewFunction(const char* name) const;
    const ClearPipelineMetal* FindClearPipeline(const ClearPipelineKeyMetal& key) const;
    MTL::RenderPipelineState* FindResolvePipeline(MTL::PixelFormat format, ColorTypeMetal colorType, bool isArray) const;

    DeviceMetal& m_Device;
    MTL::Library* m_Library = nullptr;
    MTL4::FunctionDescriptor* m_DepthOnlyFragmentFunction = nullptr;
    std::atomic<MTL::ComputePipelineState*> m_Kernels[(size_t)InternalKernelMetal::MAX_NUM] = {};
    Vector<ClearPipelineMetal> m_ClearPipelines;
    Vector<ResolvePipelineMetal> m_ResolvePipelines;
    std::mutex m_KernelLock;                // kernel creation
    std::shared_mutex m_RenderPipelineLock; // render pipeline lookups (shared) and creation (exclusive)
};

} // namespace nri
