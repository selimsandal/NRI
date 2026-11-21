// © 2026 NVIDIA Corporation

#ifndef NRI_METAL
#define NRI_METAL

/*
Native Metal shader ABI (metallib shaders, not converted from DXIL).

Argument table:
    buffer(0)       - resource heap, "NriDescriptorEntry" array
    buffer(1)       - sampler heap, "NriDescriptorEntry" array
    buffer(2)       - root data (see below)
    buffer(3)       - "NriMultiview" (multiview pipelines only) or "NriRayDispatchArguments" (ray tracing pipelines only)
    buffer(4-5)     - reserved (converted shaders)
    buffer(6 + N)   - vertex stream with "bindingSlot = N"
    attribute(i)    - "VertexInputDesc::attributes[i]" (array order)
    color(i)        - input attachment "i" (i.e. "float4 value [[color(1)]]")

Descriptors ("NriDescriptorEntry", declare heaps and tables as arrays of argument-buffer structures with typed fields):
  - buffer: "bufferAddress" - GPU address (view offset applied), "metadata" - see "NriGetBufferSize"
  - typed buffer: "resourceId" - "texture_buffer", "bufferAddress" and "metadata" as for buffers
  - texture: "resourceId"
  - sampler: "bufferAddress" - "sampler", "metadata" - mip bias (see "NriGetSamplerMipBias")
  - acceleration structure: "bufferAddress" - "NriAccelerationStructureHeader"

Root data (buffer(2)), declare a matching "constant" structure including padding:
  - "uint baseVertex, baseInstance" - if "ENABLE_DRAW_PARAMETERS_EMULATION" is set and the layout has "VERTEX_SHADER" stage
  - "uint drawIndex" - if "ENABLE_DRAW_INDEX_EMULATION" is set and the layout has "VERTEX_SHADER" stage
  - root constants, tightly packed
  - root descriptors (GPU addresses), aligned to 8 bytes
  - per descriptor set: a pointer to its resource table, then a pointer to its sampler table (each only if the set has such ranges,
    "MUTABLE" and "INPUT_ATTACHMENT" ranges don't count)
  - a pointer to the root sampler table ("NriDescriptorEntry" per root sampler), if "rootSamplerNum != 0"
  - the whole block is padded to 16 bytes
  - within a table ranges keep declaration order, the variable-sized range goes last (offsets don't depend on the allocated count),
    "MUTABLE" and "INPUT_ATTACHMENT" ranges keep their slots

Draw parameters:
  - "[[vertex_id]]" and "[[instance_id]]" include the base vertex / instance: subtract "[[base_vertex]]" / "[[base_instance]]"
    (or the root data fields) to get zero-based NRI IDs
  - "drawIndex" is 0 for direct "Draw" calls

Multiview:
  - "[[amplification_id]]" indexes "NriMultiview::viewIndices", which stores the view indices of the set bits of the view mask
    (sparse masks are supported)
  - "LAYER_BASED" and "VIEWPORT_BASED" offsets are applied by Metal, "FLEXIBLE" shaders write "[[render_target_array_index]]"
    and/or "[[viewport_array_index]]" themselves

Textures:
  - 1D textures are height-one 2D textures: use "texture2d(_array)", Y = 0 for reads / writes, normalized Y = 0.5 for sampling
  - 3D shader views expose the full depth of their mips, "TextureViewDesc" slice ranges apply only to attachments and storage clears

Sample mask:
  - fragment shaders opt into "MultisampleDesc::sampleMask" by combining "NriApplyPipelineSampleMask(shaderMask)" into their
    "[[sample_mask]]" output, otherwise a non-default "sampleMask" makes pipeline creation return "UNSUPPORTED"
  - function constant index 65535 is reserved

Ray tracing:
  - an acceleration-structure descriptor points to a 64-byte header ("IRRaytracingAccelerationStructureGPUHeader" compatible),
    written by TLAS builds and copies. Bind it as "constant NriAccelerationStructureHeader*"
  - inline ray tracing: use "metal::raytracing::intersector" with "NriAccelerationStructureHeader::accelerationStructure"
  - ray tracing pipelines accept DXIL (converted) or native shaders, not both in one pipeline
  - native ray generation, miss and closest-hit shaders are visible functions with the "NriRayFunction" signature:
        [[visible]] void Raygen(thread NriRayContext& context, thread uchar* payload)
    NRI's dispatch kernel calls ray generation with a null "payload". Ray generation declares its payload as a local
    variable and passes it to "NriTraceRay", which calls the closest-hit or miss shader with it. Those cast "payload"
    to the same structure ("thread Payload& p = *(thread Payload*)payload"), up to "RayTracingPipelineDesc::rayPayloadMaxSize"
  - visible functions can't bind arguments: "NriRayContext" carries the resource heap, sampler heap and root data
    ("buffer(0-2)" of the dispatch kernel) and the dispatch index / size. Hit shaders also get "NriRayContext::hit" and
    "NriRayContext::currentRay"
  - shader binding table records start with "NriShaderIdentifier" ("shaderGroupIdentifierSize" bytes, written by
    "WriteShaderGroupIdentifiers"). The hit record index follows DXR: "TopLevelInstance::shaderBindingTableRecordOffset"
    + "rayContributionToHitGroupIndex" + geometry index * "multiplierForGeometryContributionToHitGroupIndex"
  - not supported yet for native shaders: intersection, any-hit and callable shaders, "CmdDispatchRaysIndirect"
*/

#include <metal_stdlib>
#include <metal_raytracing>

struct NriDescriptorEntry {
    ulong bufferAddress;
    ulong resourceId;
    ulong metadata;
};

struct NriMultiview {
    uint viewIndices[32];
};

struct NriAccelerationStructureHeader {
    metal::raytracing::instance_acceleration_structure accelerationStructure;
    constant uint* instanceContributions; // indexed by the instance index, not by the user instance ID
    ulong reserved[6];
};

static_assert(sizeof(NriDescriptorEntry) == 24, "Descriptor ABI mismatch");
static_assert(sizeof(NriAccelerationStructureHeader) == 64, "Acceleration structure header ABI mismatch");

// Buffer metadata: bits 0-31 - view size in bytes, bits 32-39 - typed buffer element offset, bit 63 - typed buffer
inline uint NriGetBufferSize(ulong metadata) {
    return uint(metadata);
}

// "texture_buffer" views start at an aligned offset (the alignment depends on the device and format): add this to typed buffer element indices
inline uint NriGetTypedBufferElementOffset(ulong metadata) {
    return uint(metadata >> 32) & 0xFF;
}

// Pass as "metal::bias" when sampling with implicit LOD. 0 on Apple10+, where the sampler applies "mipBias" itself
// (earlier GPUs ignore "MTLSamplerDescriptor::lodBias")
inline float NriGetSamplerMipBias(ulong metadata) {
    return as_type<float>(uint(metadata));
}

constant uint NriPipelineSampleMask [[function_constant(65535)]];

inline uint NriApplyPipelineSampleMask(uint shaderSampleMask = 0xFFFFFFFFu) {
    return shaderSampleMask & NriPipelineSampleMask;
}

// Ray tracing pipelines (native shaders)
struct NriRayContext;

typedef void NriRayFunction(thread NriRayContext& context, thread uchar* payload);

// "shaderGroupIdentifierSize" bytes, the same size as "IRShaderIdentifier" (converted shaders)
struct NriShaderIdentifier {
    ulong intersectionShaderHandle; // visible function table index, 0 - none (not supported yet)
    ulong shaderHandle;             // visible function table index, 0 - none
    ulong reserved[2];
};

struct NriShaderTable {
    ulong startAddress;
    ulong size;
    ulong stride;
};

// "buffer(3)" of ray tracing pipelines
struct NriRayDispatchArguments {
    NriShaderTable raygen;
    NriShaderTable miss;
    NriShaderTable hit;
    NriShaderTable callable;
    uint width;
    uint height;
    uint depth;
    uint padding;
    metal::visible_function_table<NriRayFunction> functions;
};

// "ReportHit" data of the closest hit
struct NriHitInfo {
    float2 barycentrics;
    float distance;        // "RayTCurrent"
    uint instanceIndex;    // "InstanceIndex"
    uint instanceId;       // "InstanceID", "TopLevelInstance::instanceId"
    uint primitiveIndex;   // "PrimitiveIndex"
    uint geometryIndex;    // "GeometryIndex"
    uint isFrontFacing;    // "HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE"
};

struct NriRayContext {
    constant NriDescriptorEntry* resourceHeap; // "buffer(0)", cast to the shader's heap structure
    constant NriDescriptorEntry* samplerHeap;  // "buffer(1)"
    constant uchar* rootData;                  // "buffer(2)", cast to the shader's root data structure
    constant NriRayDispatchArguments* arguments;
    uint3 dispatchIndex;      // "DispatchRaysIndex"
    uint3 dispatchDimensions; // "DispatchRaysDimensions"
    metal::raytracing::ray currentRay; // "WorldRayOrigin", "WorldRayDirection", "RayTMin" (hit and miss shaders)
    NriHitInfo hit;                    // closest-hit shaders only
};

static_assert(sizeof(NriShaderIdentifier) == 32, "Shader identifier ABI mismatch");
static_assert(sizeof(NriRayDispatchArguments) == 120, "Ray dispatch arguments ABI mismatch");

// DXR "RAY_FLAG_*" values, "NriTraceRay" supports these
constant uint NRI_RAY_FLAG_NONE = 0x00;
constant uint NRI_RAY_FLAG_FORCE_OPAQUE = 0x01; // implied: any-hit shaders are not supported yet
constant uint NRI_RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH = 0x04;
constant uint NRI_RAY_FLAG_SKIP_CLOSEST_HIT_SHADER = 0x08;
constant uint NRI_RAY_FLAG_CULL_BACK_FACING_TRIANGLES = 0x10;
constant uint NRI_RAY_FLAG_CULL_FRONT_FACING_TRIANGLES = 0x20;

inline constant NriShaderIdentifier* NriGetShaderRecord(NriShaderTable table, uint index) {
    return (constant NriShaderIdentifier*)(table.startAddress + ulong(index) * table.stride);
}

// DXR "TraceRay" for triangle geometry: calls the closest-hit or miss shader with "payload" (the caller's context is not modified)
inline void NriTraceRay(thread NriRayContext& context, constant NriAccelerationStructureHeader& accelerationStructure, uint rayFlags,
    uint instanceInclusionMask, uint rayContributionToHitGroupIndex, uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex, metal::raytracing::ray r, thread uchar* payload) {
    using namespace metal::raytracing;

    intersector<triangle_data, instancing> trace;
    trace.assume_geometry_type(geometry_type::triangle);
    trace.force_opacity(forced_opacity::opaque);
    trace.accept_any_intersection((rayFlags & NRI_RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) != 0);

    if (rayFlags & NRI_RAY_FLAG_CULL_BACK_FACING_TRIANGLES)
        trace.set_triangle_cull_mode(triangle_cull_mode::back);
    else if (rayFlags & NRI_RAY_FLAG_CULL_FRONT_FACING_TRIANGLES)
        trace.set_triangle_cull_mode(triangle_cull_mode::front);

    const auto result = trace.intersect(r, accelerationStructure.accelerationStructure, instanceInclusionMask);

    NriRayContext callee = context;
    callee.currentRay = r;

    constant NriShaderIdentifier* record = nullptr;

    if (result.type == intersection_type::triangle) {
        if (rayFlags & NRI_RAY_FLAG_SKIP_CLOSEST_HIT_SHADER)
            return;

        callee.hit.barycentrics = result.triangle_barycentric_coord;
        callee.hit.distance = result.distance;
        callee.hit.instanceIndex = result.instance_id;
        callee.hit.instanceId = result.user_instance_id;
        callee.hit.primitiveIndex = result.primitive_id;
        callee.hit.geometryIndex = result.geometry_id;
        callee.hit.isFrontFacing = result.triangle_front_facing ? 1 : 0;

        const uint instanceContribution = accelerationStructure.instanceContributions[result.instance_id];
        const uint hitIndex = instanceContribution + rayContributionToHitGroupIndex + result.geometry_id * multiplierForGeometryContributionToHitGroupIndex;
        record = NriGetShaderRecord(context.arguments->hit, hitIndex);
    } else
        record = NriGetShaderRecord(context.arguments->miss, missShaderIndex);

    if (record->shaderHandle)
        context.arguments->functions[record->shaderHandle](callee, payload);
}

#endif
