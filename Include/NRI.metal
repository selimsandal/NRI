#ifndef NRI_METAL
#define NRI_METAL

// Native Metal binding ABI. Resource and sampler heaps are bound at buffer(0)
// and buffer(1). Each heap entry has this 24-byte layout; shader argument-buffer
// declarations use typed pointers, textures and samplers in matching fields.
struct NriDescriptorEntry {
    ulong bufferAddress;
    ulong resourceId;
    ulong metadata; // buffer byte size, or sampler mip bias in the low 32 bits
};

// For native multiview shaders, bind at buffer(3). Index with [[amplification_id]]
// to obtain the NRI view index, including sparse masks. Flexible multiview shaders
// write [[render_target_array_index]] and/or [[viewport_array_index]] explicitly.
struct NriMultiview {
    uint viewIndices[32];
};

// Root constants, descriptors, and descriptor-table pointers are packed from
// byte offset zero in the buffer bound at buffer(2). After root constants, NRI
// aligns to 8 bytes and packs 64-bit root-descriptor addresses, then pointers to
// each nonempty resource/sampler descriptor-set table. The block is aligned to
// 16 bytes. Declare a matching constant structure, including padding.
// Within each table, fixed ranges retain declaration order and the variable-sized
// range follows them. Range offsets do not depend on the allocated variable count.
// Direct heaps are not fields of this root buffer. Buffers 4 and 5 are reserved
// for converted-shader draw metadata; vertex streams use buffer(6 + bindingSlot).
//
// NRI 1D textures are height-one Metal 2D textures. Use texture2d/texture2d_array,
// integer Y = 0 for reads/writes, and normalized Y = 0.5 for sampling.
// 3D shader views expose the full depth of their mip. TextureViewDesc slice ranges
// restrict attachments and storage clears, not shader access; address slices explicitly.
// Native fragment shaders implement sample masking with [[sample_mask]] and leave
// MultisampleDesc::sampleMask at ALL. Pipeline masks require a converted fragment shader.

// Native ray pipelines supply a RaygenIndirection kernel. This block is bound
// at buffer(3); the ordinary root data and heaps remain bound at buffers 2/0/1.
// Function-table indices come from WriteShaderGroupIdentifiers, not resource IDs.
// Indices start at 1; hit-group intersection entries follow the shader library.
// Native libraries define their visible-function signatures and must use matching
// signatures in every caller. A ray pipeline must contain either native or DXIL
// shaders, not both. NRI binds tables and dispatch data; native libraries implement
// traversal, payload/attribute storage, recursion and miss/hit/callable invocation.
// There is no implicit translation of native function signatures to the DXR ABI.
// Payload and attribute size declarations do not allocate native shader storage.
// Native shaders must respect the pipeline's recursion limit and skip flags.
// Metal's indirect-call stack remains at its default depth (1). Ray recursion is
// not the same as indirect-call nesting: native companions needing deeper ray
// recursion must manage it explicitly, without nested recursive indirect calls.
// The dispatch kernel must support both 8x8x1 and 1x1x1 threadgroups.
struct NriShaderIdentifier {
    ulong intersectionShaderHandle;
    ulong shaderHandle;
    ulong localRootSignatureSamplersBuffer;
    ulong reserved;
};

// Traversal libraries export the following intersection functions with [[host_name]],
// unless the corresponding pipeline skip flag is set:
//   irconverter.wrapper.intersection.function.triangle   (table index 0)
//   irconverter.wrapper.intersection.function.procedural (table index 1)
// Their intersection-function buffer(0) receives the visible-function table.
// Native intersection functions include any-hit logic; separate native intersection
// and any-hit functions in the same group are unsupported.
// An acceleration-structure descriptor addresses a 64-byte header: the first two
// ulong fields hold the instance-AS resource ID and per-instance hit-group contribution
// pointer; the remaining fields are reserved. Instance IDs and masks are preserved.
// A typed argument-buffer declaration for that header is:
//   struct Scene {
//       metal::raytracing::instance_acceleration_structure accelerationStructure;
//       constant uint* instanceContributions;
//       ulong reserved[6];
//   };
// Include <metal_raytracing> before using this declaration. Index contributions
// with the intersection's instance index, not its application-defined user ID.
// Companions are resolved in shader-library order; when several libraries export
// a companion, they must provide the same implementation and signatures.

struct NriShaderRecordRange {
    constant NriShaderIdentifier* address;
    ulong size;
};

struct NriShaderTableRange {
    constant NriShaderIdentifier* address;
    ulong size;
    ulong stride;
};

struct NriRayDispatchDesc {
    NriShaderRecordRange raygen;
    NriShaderTableRange miss;
    NriShaderTableRange hit;
    NriShaderTableRange callable;
    uint width;
    uint height;
    uint depth;
};

// These are GPU addresses and Metal resource IDs. Declare matching typed pointer
// and function-table fields in the shader's argument-buffer structure to use them.
// Byte offsets: dispatch 0, root 104, resources 112, samplers 120,
// visibleFunctions 128, intersectionFunctions 136, intersectionTables 144.
// This ABI uses the single intersectionFunctions table. intersectionTables is the
// optional multiple-table ABI and remains zero, as do the last two identifier fields.
// NRISamples/Shaders/MetalTests.metal contains a native dispatch fixture; it tests
// dispatch and shader-record selection, not acceleration-structure traversal.
struct NriRayDispatchArguments {
    NriRayDispatchDesc dispatch;
    ulong root;
    ulong resources;
    ulong samplers;
    ulong visibleFunctions;
    ulong intersectionFunctions;
    ulong intersectionTables;
};

static_assert(sizeof(NriDescriptorEntry) == 24, "Descriptor ABI mismatch");
static_assert(sizeof(NriShaderIdentifier) == 32, "Shader identifier ABI mismatch");
static_assert(__builtin_offsetof(NriShaderIdentifier, shaderHandle) == 8, "Shader handle ABI mismatch");
static_assert(sizeof(NriShaderRecordRange) == 16, "Shader record ABI mismatch");
static_assert(sizeof(NriShaderTableRange) == 24, "Shader table ABI mismatch");
static_assert(sizeof(NriRayDispatchDesc) == 104, "Ray dispatch ABI mismatch");
static_assert(__builtin_offsetof(NriRayDispatchDesc, width) == 88, "Ray dimensions ABI mismatch");
static_assert(sizeof(NriRayDispatchArguments) == 152, "Ray arguments ABI mismatch");
static_assert(__builtin_offsetof(NriRayDispatchArguments, root) == 104, "Ray root ABI mismatch");
static_assert(__builtin_offsetof(NriRayDispatchArguments, resources) == 112, "Resource heap ABI mismatch");
static_assert(__builtin_offsetof(NriRayDispatchArguments, samplers) == 120, "Sampler heap ABI mismatch");
static_assert(__builtin_offsetof(NriRayDispatchArguments, visibleFunctions) == 128, "Visible table ABI mismatch");
static_assert(__builtin_offsetof(NriRayDispatchArguments, intersectionFunctions) == 136, "Intersection table ABI mismatch");
static_assert(__builtin_offsetof(NriRayDispatchArguments, intersectionTables) == 144, "Multiple-table ABI mismatch");

#endif
