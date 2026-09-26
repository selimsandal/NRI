// © 2026 NVIDIA Corporation

namespace nri {

#if NRI_ENABLE_METAL_SHADER_CONVERTER
static inline IRShaderStage GetIRShaderStage(StageBits stage) {
    if (stage == StageBits::VERTEX_SHADER)
        return IRShaderStageVertex;
    if (stage == StageBits::FRAGMENT_SHADER)
        return IRShaderStageFragment;
    if (stage == StageBits::COMPUTE_SHADER)
        return IRShaderStageCompute;
    if (stage == StageBits::MESH_SHADER)
        return IRShaderStageMesh;
    if (stage == StageBits::TASK_SHADER)
        return IRShaderStageAmplification;
    if (stage == StageBits::TESS_CONTROL_SHADER)
        return IRShaderStageHull;
    if (stage == StageBits::TESS_EVALUATION_SHADER)
        return IRShaderStageDomain;
    if (stage == StageBits::GEOMETRY_SHADER)
        return IRShaderStageGeometry;

    return IRShaderStageInvalid;
}

static inline IRFormat GetIRVertexFormat(Format format) {
    switch (format) {
        case Format::R8_UNORM:
            return IRFormatR8Unorm;
        case Format::R8_SNORM:
            return IRFormatR8Snorm;
        case Format::R8_UINT:
            return IRFormatR8Uint;
        case Format::R8_SINT:
            return IRFormatR8Sint;
        case Format::RG8_UNORM:
            return IRFormatR8G8Unorm;
        case Format::RG8_SNORM:
            return IRFormatR8G8Snorm;
        case Format::RG8_UINT:
            return IRFormatR8G8Uint;
        case Format::RG8_SINT:
            return IRFormatR8G8Sint;
        case Format::RGBA8_UNORM:
            return IRFormatR8G8B8A8Unorm;
        case Format::RGBA8_SNORM:
            return IRFormatR8G8B8A8Snorm;
        case Format::RGBA8_UINT:
            return IRFormatR8G8B8A8Uint;
        case Format::RGBA8_SINT:
            return IRFormatR8G8B8A8Sint;
        case Format::R10_G10_B10_A2_UNORM:
            return IRFormatR10G10B10A2Unorm;
        case Format::R16_SFLOAT:
            return IRFormatR16Float;
        case Format::RG16_SFLOAT:
            return IRFormatR16G16Float;
        case Format::RGBA16_SFLOAT:
            return IRFormatR16G16B16A16Float;
        case Format::R32_UINT:
            return IRFormatR32Uint;
        case Format::R32_SINT:
            return IRFormatR32Sint;
        case Format::R32_SFLOAT:
            return IRFormatR32Float;
        case Format::RG32_UINT:
            return IRFormatR32G32Uint;
        case Format::RG32_SINT:
            return IRFormatR32G32Sint;
        case Format::RG32_SFLOAT:
            return IRFormatR32G32Float;
        case Format::RGB32_UINT:
            return IRFormatR32G32B32Uint;
        case Format::RGB32_SINT:
            return IRFormatR32G32B32Sint;
        case Format::RGB32_SFLOAT:
            return IRFormatR32G32B32Float;
        case Format::RGBA32_UINT:
            return IRFormatR32G32B32A32Uint;
        case Format::RGBA32_SINT:
            return IRFormatR32G32B32A32Sint;
        case Format::RGBA32_SFLOAT:
            return IRFormatR32G32B32A32Float;
        default:
            return IRFormatUnknown;
    }
}
#endif

static inline MTL::BlendFactor GetBlendFactorMetal(BlendFactor factor) {
    static constexpr MTL::BlendFactor factors[] = {MTL::BlendFactorZero, MTL::BlendFactorOne, MTL::BlendFactorSourceColor, MTL::BlendFactorOneMinusSourceColor, MTL::BlendFactorDestinationColor, MTL::BlendFactorOneMinusDestinationColor, MTL::BlendFactorSourceAlpha, MTL::BlendFactorOneMinusSourceAlpha, MTL::BlendFactorDestinationAlpha, MTL::BlendFactorOneMinusDestinationAlpha, MTL::BlendFactorBlendColor, MTL::BlendFactorOneMinusBlendColor, MTL::BlendFactorBlendAlpha, MTL::BlendFactorOneMinusBlendAlpha, MTL::BlendFactorSourceAlphaSaturated, MTL::BlendFactorSource1Color, MTL::BlendFactorOneMinusSource1Color, MTL::BlendFactorSource1Alpha, MTL::BlendFactorOneMinusSource1Alpha};

    return factors[(uint32_t)factor];
}

static inline void SetStencilMetal(MTL::StencilDescriptor* dst, const StencilDesc& src) {
    static constexpr MTL::StencilOperation operations[] = {MTL::StencilOperationKeep, MTL::StencilOperationZero, MTL::StencilOperationReplace, MTL::StencilOperationIncrementClamp, MTL::StencilOperationDecrementClamp, MTL::StencilOperationInvert, MTL::StencilOperationIncrementWrap, MTL::StencilOperationDecrementWrap};
    dst->setStencilCompareFunction(GetCompareMetal(src.compareOp));
    dst->setStencilFailureOperation(operations[(uint32_t)src.failOp]);
    dst->setDepthStencilPassOperation(operations[(uint32_t)src.passOp]);
    dst->setDepthFailureOperation(operations[(uint32_t)src.depthFailOp]);
    dst->setReadMask(src.compareMask);
    dst->setWriteMask(src.writeMask);
}

PipelineMetal::PipelineMetal(DeviceMetal& device) : m_Device(device), m_ShaderGroupIdentifiers(device.GetStdAllocator()) {
}

PipelineMetal::~PipelineMetal() {
    if (m_Render)
        m_Render->release();
    if (m_Compute)
        m_Compute->release();
    if (m_VisibleFunctionTable) {
        m_Device.RemoveResidency(m_VisibleFunctionTable);
        m_VisibleFunctionTable->release();
    }
    if (m_IntersectionFunctionTable) {
        m_Device.RemoveResidency(m_IntersectionFunctionTable);
        m_IntersectionFunctionTable->release();
    }
    if (m_DepthStencil)
        m_DepthStencil->release();
}

Result PipelineMetal::LoadFunction(const ShaderDesc& shader, MTL::Library*& library, MTL::Function*& function, const VertexInputDesc* vertexInput, MTL::Library** stageInLibrary, bool emulation, uint32_t sampleMask) {
    dispatch_data_t data = nullptr;
    const char* functionName = shader.entryPointName ? shader.entryPointName : "main";
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRMetalLibBinary* binary = nullptr;
    IRObject* output = nullptr;
    IRShaderReflection* reflection = nullptr;
    const bool isDxil = shader.size >= 4 && memcmp(shader.bytecode, "DXBC", 4) == 0;
    if (isDxil) {
        const IRShaderStage stage = GetIRShaderStage(shader.stage);
        if (stage == IRShaderStageInvalid)
            return Result::UNSUPPORTED;
        IRObject* input = IRObjectCreateFromDXIL((const uint8_t*)shader.bytecode, shader.size, IRBytecodeOwnershipNone);
        IRCompiler* compiler = IRCompilerCreate();
        IRCompilerSetCompatibilityFlags(compiler, IRCompatibilityFlagSamplerLODBias);
        IRCompilerSetGlobalRootSignature(compiler, m_Layout->GetRootSignature());
        IRCompilerEnableGeometryAndTessellationEmulation(compiler, emulation);
        IRCompilerSetSampleMask(compiler, sampleMask == ALL ? UINT32_MAX : sampleMask);
        if (stageInLibrary)
            IRCompilerSetStageInGenerationMode(compiler, IRStageInCodeGenerationModeUseSeparateStageInFunction);
        IRError* error = nullptr;
        output = IRCompilerAllocCompileAndLink(compiler, functionName, input, &error);
        IRObjectDestroy(input);
        if (!output) {
            IRCompilerDestroy(compiler);
            m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "DXIL conversion failed for %s (converter error %u)", functionName, error ? IRErrorGetCode(error) : 0);
            if (error)
                IRErrorDestroy(error);
            return Result::FAILURE;
        }
        if (error)
            IRErrorDestroy(error);
        binary = IRMetalLibBinaryCreate();
        if (!IRObjectGetMetalLibBinary(output, stage, binary)) {
            IRCompilerDestroy(compiler);
            IRMetalLibBinaryDestroy(binary);
            IRObjectDestroy(output);
            return Result::FAILURE;
        }
        reflection = IRShaderReflectionCreate();
        if (IRObjectGetReflection(output, stage, reflection)) {
            const char* convertedName = IRShaderReflectionGetEntryPointFunctionName(reflection);
            if (convertedName)
                functionName = convertedName;
            if (stage == IRShaderStageCompute) {
                IRVersionedCSInfo info = {};
                if (IRShaderReflectionCopyComputeInfo(reflection, IRReflectionVersion_1_0, &info)) {
                    m_ThreadGroup = MTL::Size(info.info_1_0.tg_size[0], info.info_1_0.tg_size[1], info.info_1_0.tg_size[2]);
                    IRShaderReflectionReleaseComputeInfo(&info);
                }
            } else if (stage == IRShaderStageMesh) {
                IRVersionedMSInfo info = {};
                if (IRShaderReflectionCopyMeshInfo(reflection, IRReflectionVersion_1_0, &info)) {
                    m_MeshGroup = MTL::Size(info.info_1_0.num_threads[0], info.info_1_0.num_threads[1], info.info_1_0.num_threads[2]);
                    m_MeshPayloadSize = info.info_1_0.max_payload_size_in_bytes;
                    IRShaderReflectionReleaseMeshInfo(&info);
                }
            } else if (stage == IRShaderStageAmplification) {
                IRVersionedASInfo info = {};
                if (IRShaderReflectionCopyAmplificationInfo(reflection, IRReflectionVersion_1_0, &info)) {
                    m_TaskGroup = MTL::Size(info.info_1_0.num_threads[0], info.info_1_0.num_threads[1], info.info_1_0.num_threads[2]);
                    m_MeshPayloadSize = std::max(m_MeshPayloadSize, info.info_1_0.max_payload_size_in_bytes);
                    IRShaderReflectionReleaseAmplificationInfo(&info);
                }
            } else if (stage == IRShaderStageVertex) {
                IRVersionedVSInfo info = {};
                if (IRShaderReflectionCopyVertexInfo(reflection, IRReflectionVersion_1_0, &info)) {
                    m_GeometryConfig.gsVertexSizeInBytes = info.info_1_0.vertex_output_size_in_bytes;
                    m_TessellationConfig.vsOutputSizeInBytes = info.info_1_0.vertex_output_size_in_bytes;
                    IRShaderReflectionReleaseVertexInfo(&info);
                }
            } else if (stage == IRShaderStageGeometry) {
                IRVersionedGSInfo info = {};
                if (IRShaderReflectionCopyGeometryInfo(reflection, IRReflectionVersion_1_0, &info)) {
                    m_GeometryConfig.gsMaxInputPrimitivesPerMeshThreadgroup = info.info_1_0.max_input_primitives_per_mesh_threadgroup;
                    m_TessellationConfig.gsMaxInputPrimitivesPerMeshThreadgroup = info.info_1_0.max_input_primitives_per_mesh_threadgroup;
                    m_TessellationConfig.gsInstanceCount = info.info_1_0.instance_count;
                    m_MeshPayloadSize = info.info_1_0.max_payload_size_in_bytes;
                    IRShaderReflectionReleaseGeometryInfo(&info);
                }
            } else if (stage == IRShaderStageHull) {
                IRVersionedHSInfo info = {};
                if (IRShaderReflectionCopyHullInfo(reflection, IRReflectionVersion_1_0, &info)) {
                    m_TessellationConfig.outputPrimitiveType = (IRRuntimeTessellatorOutputPrimitive)info.info_1_0.tessellator_output_primitive;
                    m_TessellationConfig.hsMaxPatchesPerObjectThreadgroup = info.info_1_0.max_patches_per_object_threadgroup;
                    m_TessellationConfig.hsInputControlPointCount = info.info_1_0.input_control_point_count;
                    m_TessellationConfig.hsMaxObjectThreadsPerThreadgroup = info.info_1_0.max_object_threads_per_patch;
                    m_TessellationConfig.hsMaxTessellationFactor = info.info_1_0.max_tessellation_factor;
                    IRShaderReflectionReleaseHullInfo(&info);
                }
            } else if (stage == IRShaderStageDomain) {
                IRVersionedDSInfo info = {};
                if (IRShaderReflectionCopyDomainInfo(reflection, IRReflectionVersion_1_0, &info)) {
                    if (!m_GeometryEmulation)
                        m_TessellationConfig.gsMaxInputPrimitivesPerMeshThreadgroup = info.info_1_0.max_input_prims_per_mesh_threadgroup;
                    IRShaderReflectionReleaseDomainInfo(&info);
                }
            }
        }
        if (stageInLibrary && reflection) {
            IRVersionedInputLayoutDescriptor layout = {};
            layout.version = IRInputLayoutDescriptorVersion_1;
            layout.desc_1_0.numElements = vertexInput ? vertexInput->attributeNum : 0;
            for (uint32_t i = 0; vertexInput && i < vertexInput->attributeNum; i++) {
                const VertexAttributeDesc& attribute = vertexInput->attributes[i];
                layout.desc_1_0.semanticNames[i] = attribute.d3d.semanticName;
                const bool perInstance = vertexInput->streams[attribute.streamIndex].stepRate == VertexStreamStepRate::PER_INSTANCE;
                layout.desc_1_0.inputElementDescs[i] = {attribute.d3d.semanticIndex, GetIRVertexFormat(attribute.format), vertexInput->streams[attribute.streamIndex].bindingSlot, attribute.offset, perInstance ? 1u : 0u, perInstance ? IRInputClassificationPerInstanceData : IRInputClassificationPerVertexData};
            }
            IRMetalLibBinary* stageIn = IRMetalLibBinaryCreate();
            if (!IRMetalLibSynthesizeStageInFunction(compiler, reflection, &layout, stageIn)) {
                IRMetalLibBinaryDestroy(stageIn);
                IRCompilerDestroy(compiler);
                IRShaderReflectionDestroy(reflection);
                IRMetalLibBinaryDestroy(binary);
                IRObjectDestroy(output);

                return Result::FAILURE;
            }
            NS::Error* stageInError = nullptr;
            *stageInLibrary = m_Device.GetNativeObject()->newLibrary(IRMetalLibGetBytecodeData(stageIn), &stageInError);
            IRMetalLibBinaryDestroy(stageIn);
        }
        IRCompilerDestroy(compiler);
        data = IRMetalLibGetBytecodeData(binary);
        m_Converted = true;
    }
#endif

    if (!data && shader.stage == StageBits::FRAGMENT_SHADER && sampleMask != ALL) {
        m_Device.ReportMessage(Message::ERROR, Result::UNSUPPORTED, __FILE__, __LINE__, "Native Metal shaders must implement sample masking with an MSL sample_mask output; pipeline sample masks require DXIL conversion");

        return Result::UNSUPPORTED;
    }

    if (!data)
        data = dispatch_data_create(shader.bytecode, shader.size, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NS::Error* error = nullptr;
    library = m_Device.GetNativeObject()->newLibrary(data, &error);
    if (!library)
        m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal library creation failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");

    if (library)
        function = library->newFunction(NS::String::string(functionName, NS::UTF8StringEncoding));
    if (library && !function)
        m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal entry point '%s' was not found", functionName);
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (reflection)
        IRShaderReflectionDestroy(reflection);
    if (binary)
        IRMetalLibBinaryDestroy(binary);
    if (output)
        IRObjectDestroy(output);
    if (!isDxil)
        dispatch_release(data);
#else
    dispatch_release(data);
#endif
    if (!library)
        return Result::FAILURE;

    return function ? Result::SUCCESS : Result::FAILURE;
}

Result PipelineMetal::Create(const ComputePipelineDesc& desc) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    m_Layout = (const PipelineLayoutMetal*)desc.pipelineLayout;
    MTL::Library* library = nullptr;
    MTL::Function* function = nullptr;
    Result result = LoadFunction(desc.shader, library, function);
    if (result == Result::SUCCESS) {
        NS::Error* error = nullptr;
        MTL::ComputePipelineDescriptor* pd = MTL::ComputePipelineDescriptor::alloc()->init();
        pd->setComputeFunction(function);
        MTL::BinaryArchive* archive = desc.cache ? ((PipelineCacheMetal*)desc.cache)->GetNativeObject() : nullptr;

        if (archive)
            pd->setBinaryArchives(NS::Array::array(archive));

        const auto options = (desc.flags & ComputePipelineBits::FAIL_ON_CACHE_MISS) ? MTL::PipelineOptionFailOnBinaryArchiveMiss : MTL::PipelineOptionNone;
        m_Compute = m_Device.GetNativeObject()->newComputePipelineState(pd, options, nullptr, &error);
        result = m_Compute ? Result::SUCCESS : Result::FAILURE;

        if (m_Compute && archive && !archive->addComputePipelineFunctions(pd, &error))
            result = Result::FAILURE;

        if (result != Result::SUCCESS && !(desc.flags & ComputePipelineBits::FAIL_ON_CACHE_MISS))
            m_Device.ReportMessage(Message::ERROR, result, __FILE__, __LINE__, "Metal compute pipeline creation or archive update failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");

        pd->release();

        if (!m_Converted)
            m_ThreadGroup = MTL::Size(desc.shader.threadGroupSizeX ? desc.shader.threadGroupSizeX : 1, desc.shader.threadGroupSizeY ? desc.shader.threadGroupSizeY : 1, desc.shader.threadGroupSizeZ ? desc.shader.threadGroupSizeZ : 1);
    }
    if (function)
        function->release();
    if (library)
        library->release();
    pool->release();

    return result;
}

#include "PipelineRayTracingMetal.hpp"

Result PipelineMetal::Create(const GraphicsPipelineDesc& desc) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    m_Layout = (const PipelineLayoutMetal*)desc.pipelineLayout;
    bool isMesh = false;
    m_Multiview = desc.outputMerger.multiview;
    m_ViewMask = desc.outputMerger.viewMask;
    bool hasGeometry = false;
    bool hasTessellation = false;
    bool hasFragment = false;
    bool hasNativeShaders = false;
    bool hasConvertedShaders = false;

    for (uint32_t i = 0; i < desc.shaderNum; i++) {
        const ShaderDesc& shader = desc.shaders[i];
        const bool isDxil = shader.size >= 4 && memcmp(shader.bytecode, "DXBC", 4) == 0;
        hasNativeShaders |= !isDxil;
        hasConvertedShaders |= isDxil;
        isMesh |= desc.shaders[i].stage == StageBits::MESH_SHADER;
        hasGeometry |= desc.shaders[i].stage == StageBits::GEOMETRY_SHADER;
        hasTessellation |= desc.shaders[i].stage == StageBits::TESS_CONTROL_SHADER || desc.shaders[i].stage == StageBits::TESS_EVALUATION_SHADER;
        hasFragment |= desc.shaders[i].stage == StageBits::FRAGMENT_SHADER;
    }

    const auto& features = m_Device.GetDesc().features;
    if ((isMesh && !features.meshShader) || (hasGeometry && !features.geometryShader) || (hasTessellation && !features.tessellationShader)) {
        pool->release();

        return Result::UNSUPPORTED;
    }

    // Converter does not implement SV_ViewID, and its stage-emulation ABI is not
    // the native MSL mesh/object ABI. Do not silently route native stages through it.
    if ((hasConvertedShaders && m_ViewMask && m_Multiview == Multiview::FLEXIBLE) || (hasNativeShaders && (hasGeometry || hasTessellation))) {
        pool->release();

        return Result::UNSUPPORTED;
    }

    if (!hasFragment && desc.multisample && desc.multisample->sampleMask != ALL) {
        pool->release();

        return Result::UNSUPPORTED;
    }
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    m_GeometryEmulation = hasGeometry;
    m_TessellationEmulation = hasTessellation;
    m_TessellationConfig.gsInstanceCount = 1;
    isMesh |= hasGeometry || hasTessellation;
    if (desc.inputAssembly.topology == Topology::TRIANGLE_STRIP_WITH_ADJACENCY) {
        pool->release();

        return Result::UNSUPPORTED;
    }
#else
    if (hasGeometry || hasTessellation || desc.inputAssembly.topology > Topology::TRIANGLE_STRIP) {
        pool->release();

        return Result::UNSUPPORTED;
    }
#endif
    MTL::RenderPipelineDescriptor* pd = isMesh ? nullptr : MTL::RenderPipelineDescriptor::alloc()->init();
    MTL::MeshRenderPipelineDescriptor* mpd = isMesh ? MTL::MeshRenderPipelineDescriptor::alloc()->init() : nullptr;
    Vector<MTL::Library*> libraries(m_Device.GetStdAllocator());
    Vector<MTL::Function*> functions(m_Device.GetStdAllocator());
    MTL::Library* stageInLibrary = nullptr;
    Result result = Result::SUCCESS;
    for (uint32_t i = 0; i < desc.shaderNum; i++) {
        MTL::Library* library = nullptr;
        MTL::Function* function = nullptr;
        result = LoadFunction(desc.shaders[i], library, function, desc.vertexInput, desc.shaders[i].stage == StageBits::VERTEX_SHADER && (hasGeometry || hasTessellation) ? &stageInLibrary : nullptr, hasGeometry || hasTessellation, desc.multisample ? desc.multisample->sampleMask : ALL);
        if (result != Result::SUCCESS) {
            if (function)
                function->release();
            if (library)
                library->release();

            break;
        }
        libraries.push_back(library);
        functions.push_back(function);
        if (desc.shaders[i].stage == StageBits::VERTEX_SHADER) {
            if (!isMesh)
                pd->setVertexFunction(function);
        } else if (desc.shaders[i].stage == StageBits::FRAGMENT_SHADER)
            isMesh ? mpd->setFragmentFunction(function) : pd->setFragmentFunction(function);
        else if (desc.shaders[i].stage == StageBits::MESH_SHADER) {
            mpd->setMeshFunction(function);
            if (!m_Converted)
                m_MeshGroup = MTL::Size(desc.shaders[i].threadGroupSizeX, desc.shaders[i].threadGroupSizeY, desc.shaders[i].threadGroupSizeZ);
        } else if (desc.shaders[i].stage == StageBits::TASK_SHADER) {
            mpd->setObjectFunction(function);
            if (!m_Converted)
                m_TaskGroup = MTL::Size(desc.shaders[i].threadGroupSizeX, desc.shaders[i].threadGroupSizeY, desc.shaders[i].threadGroupSizeZ);
        }
    }
    const uint32_t sampleNum = desc.multisample ? desc.multisample->sampleNum : 1;
    uint32_t amplificationCount = 0;
    for (uint32_t mask = m_ViewMask; mask; mask >>= 1)
        amplificationCount++;

    if (m_Multiview == Multiview::VIEWPORT_BASED)
        amplificationCount = m_Device.GetDesc().other.viewMaxNum;

    amplificationCount = std::max(1u, amplificationCount);
    if (isMesh) {
        // Metal mesh pipelines require a fragment function even for depth-only draws.
        if (result == Result::SUCCESS && !mpd->fragmentFunction()) {
            NS::Error* error = nullptr;
            auto* library = m_Device.GetNativeObject()->newLibrary(NS::String::string("#include <metal_stdlib>\nfragment void nri_depth_only() {}", NS::UTF8StringEncoding), nullptr, &error);
            auto* function = library ? library->newFunction(NS::String::string("nri_depth_only", NS::UTF8StringEncoding)) : nullptr;

            if (library)
                libraries.push_back(library);

            if (function) {
                functions.push_back(function);
                mpd->setFragmentFunction(function);
            } else
                result = Result::FAILURE;
        }
        mpd->setMaxVertexAmplificationCount(amplificationCount);
        mpd->setRasterSampleCount(sampleNum);
        mpd->setAlphaToCoverageEnabled(desc.multisample && desc.multisample->alphaToCoverage);
        if (hasGeometry || hasTessellation) {
            mpd->setMaxTotalThreadsPerMeshThreadgroup(256);
            mpd->setMaxTotalThreadsPerObjectThreadgroup(256);
        } else {
            mpd->setMaxTotalThreadsPerMeshThreadgroup(m_MeshGroup.width * m_MeshGroup.height * m_MeshGroup.depth);
            mpd->setRequiredThreadsPerMeshThreadgroup(m_MeshGroup);
            if (mpd->objectFunction()) {
                mpd->setMaxTotalThreadsPerObjectThreadgroup(m_TaskGroup.width * m_TaskGroup.height * m_TaskGroup.depth);
                mpd->setRequiredThreadsPerObjectThreadgroup(m_TaskGroup);
            }
        }
        mpd->setPayloadMemoryLength(hasGeometry || hasTessellation ? 16384 : m_MeshPayloadSize);
    } else {
        pd->setInputPrimitiveTopology(desc.inputAssembly.topology == Topology::POINT_LIST ? MTL::PrimitiveTopologyClassPoint : (desc.inputAssembly.topology <= Topology::LINE_STRIP ? MTL::PrimitiveTopologyClassLine : MTL::PrimitiveTopologyClassTriangle));
        pd->setMaxVertexAmplificationCount(amplificationCount);
        pd->setSampleCount(sampleNum);
        pd->setAlphaToCoverageEnabled(desc.multisample && desc.multisample->alphaToCoverage);
    }
    if (desc.vertexInput && !isMesh) {
        MTL::VertexDescriptor* vertex = MTL::VertexDescriptor::alloc()->init();
        for (uint32_t i = 0; i < desc.vertexInput->attributeNum; i++) {
            const VertexAttributeDesc& source = desc.vertexInput->attributes[i];
            MTL::VertexAttributeDescriptor* attribute = vertex->attributes()->object(i + (m_Converted ? 11 : 0));
            attribute->setFormat(GetVertexFormatMetal(source.format));
            attribute->setOffset(source.offset);
            attribute->setBufferIndex(desc.vertexInput->streams[source.streamIndex].bindingSlot + 6);
        }
        for (uint32_t i = 0; i < desc.vertexInput->streamNum; i++) {
            const VertexStreamDesc& source = desc.vertexInput->streams[i];
            MTL::VertexBufferLayoutDescriptor* layout = vertex->layouts()->object(source.bindingSlot + 6);
            layout->setStride(MTL::BufferLayoutStrideDynamic);
            layout->setStepFunction(source.stepRate == VertexStreamStepRate::PER_INSTANCE ? MTL::VertexStepFunctionPerInstance : MTL::VertexStepFunctionPerVertex);
            layout->setStepRate(1);
        }
        pd->setVertexDescriptor(vertex);
        vertex->release();
    }
    auto configureOutput = [&](auto* pipelineDesc) {
        for (uint32_t i = 0; i < desc.outputMerger.colorNum; i++) {
            const ColorAttachmentDesc& c = desc.outputMerger.colors[i];
            MTL::RenderPipelineColorAttachmentDescriptor* a = pipelineDesc->colorAttachments()->object(i);
            a->setPixelFormat(GetPixelFormatMetal(c.format));
            a->setBlendingEnabled(c.blendEnabled);
            MTL::ColorWriteMask mask = MTL::ColorWriteMaskNone;

            if (c.colorWriteMask & ColorWriteBits::R)
                mask |= MTL::ColorWriteMaskRed;

            if (c.colorWriteMask & ColorWriteBits::G)
                mask |= MTL::ColorWriteMaskGreen;

            if (c.colorWriteMask & ColorWriteBits::B)
                mask |= MTL::ColorWriteMaskBlue;

            if (c.colorWriteMask & ColorWriteBits::A)
                mask |= MTL::ColorWriteMaskAlpha;

            a->setWriteMask(mask);
            a->setSourceRGBBlendFactor(GetBlendFactorMetal(c.colorBlend.srcFactor));
            a->setDestinationRGBBlendFactor(GetBlendFactorMetal(c.colorBlend.dstFactor));
            a->setRgbBlendOperation((MTL::BlendOperation)c.colorBlend.op);
            a->setSourceAlphaBlendFactor(GetBlendFactorMetal(c.alphaBlend.srcFactor));
            a->setDestinationAlphaBlendFactor(GetBlendFactorMetal(c.alphaBlend.dstFactor));
            a->setAlphaBlendOperation((MTL::BlendOperation)c.alphaBlend.op);
        }
        if (desc.outputMerger.depthStencilFormat != Format::UNKNOWN) {
            const MTL::PixelFormat format = GetPixelFormatMetal(desc.outputMerger.depthStencilFormat);
            const FormatProps& props = GetFormatProps(desc.outputMerger.depthStencilFormat);
            if (props.isDepth)
                pipelineDesc->setDepthAttachmentPixelFormat(format);
            if (props.isStencil)
                pipelineDesc->setStencilAttachmentPixelFormat(format);
        }
    };
    if (isMesh)
        configureOutput(mpd);
    else
        configureOutput(pd);
    if (result == Result::SUCCESS) {
        NS::Error* error = nullptr;
        MTL::BinaryArchive* archive = desc.cache ? ((PipelineCacheMetal*)desc.cache)->GetNativeObject() : nullptr;

        if (archive) {
            if (isMesh)
                mpd->setBinaryArchives(NS::Array::array(archive));
            else
                pd->setBinaryArchives(NS::Array::array(archive));
        }

        const auto options = (desc.flags & GraphicsPipelineBits::FAIL_ON_CACHE_MISS) ? MTL::PipelineOptionFailOnBinaryArchiveMiss : MTL::PipelineOptionNone;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        if (hasGeometry || hasTessellation) {
            auto findLibrary = [&](StageBits stage) -> MTL::Library* {
                for (uint32_t i = 0; i < desc.shaderNum; i++) {
                    if (desc.shaders[i].stage == stage)
                        return libraries[i];
                }

                return nullptr;
            };
            auto findName = [&](StageBits stage) -> const char* {
                for (uint32_t i = 0; i < desc.shaderNum; i++) {
                    if (desc.shaders[i].stage == stage)
                        return desc.shaders[i].entryPointName ? desc.shaders[i].entryPointName : "main";
                }

                return nullptr;
            };
            // Use the converter runtime ABI, but compile here so archive miss control
            // and pipelines without a fragment shader follow the normal NRI path.
            auto specialize = [&](MTL::Library* library, const char* name, MTL::FunctionConstantValues* constants = nullptr) {
                MTL::Function* function = nullptr;

                if (library && name)
                    function = constants ? library->newFunction(NS::String::string(name, NS::UTF8StringEncoding), constants, &error) : library->newFunction(NS::String::string(name, NS::UTF8StringEncoding));

                if (function)
                    functions.push_back(function);
                else
                    result = Result::FAILURE;

                return function;
            };
            auto* constants = MTL::FunctionConstantValues::alloc()->init();
            constants->setConstantValue(&hasTessellation, MTL::DataTypeBool, NS::String::string("tessellationEnabled", NS::UTF8StringEncoding));
            const std::string objectName = std::string(findName(StageBits::VERTEX_SHADER)) + ".dxil_irconverter_object_shader";
            mpd->setObjectFunction(specialize(findLibrary(StageBits::VERTEX_SHADER), objectName.c_str(), constants));
            const uint32_t vertexSize = hasTessellation ? m_TessellationConfig.vsOutputSizeInBytes : m_GeometryConfig.gsVertexSizeInBytes;
            constants->setConstantValue(&vertexSize, MTL::DataTypeInt, NS::String::string("vertex_shader_output_size_fc", NS::UTF8StringEncoding));
            const bool streamOut = false;
            constants->setConstantValue(&streamOut, MTL::DataTypeBool, NS::String::string("streamOutEnabled", NS::UTF8StringEncoding));
            MTL::Function* objectFunctions[2] = {specialize(stageInLibrary, "irconverter_stage_in_shader"), nullptr};
            MTL::Function* meshFunctions[2] = {};

            if (hasTessellation) {
                constants->setConstantValue(&m_TessellationConfig.hsMaxTessellationFactor, MTL::DataTypeFloat, NS::String::string("max_tessellation_factor_fc", NS::UTF8StringEncoding));
                objectFunctions[1] = specialize(findLibrary(StageBits::TESS_CONTROL_SHADER), "irconverter_hull_shader", constants);
                meshFunctions[0] = specialize(findLibrary(StageBits::TESS_CONTROL_SHADER), "irconverter_tessellator", constants);
                meshFunctions[1] = specialize(findLibrary(StageBits::TESS_EVALUATION_SHADER), "irconverter_dxil_domain_shader");
            }

            if (hasGeometry)
                mpd->setMeshFunction(specialize(findLibrary(StageBits::GEOMETRY_SHADER), findName(StageBits::GEOMETRY_SHADER), constants));
            else {
                const char* passthrough = kIRTrianglePassthroughGeometryShader;

                if (m_TessellationConfig.outputPrimitiveType == IRRuntimeTessellatorOutputPoint)
                    passthrough = kIRPointPassthroughGeometryShader;
                else if (m_TessellationConfig.outputPrimitiveType == IRRuntimeTessellatorOutputLine)
                    passthrough = kIRLinePassthroughGeometryShader;

                mpd->setMeshFunction(specialize(findLibrary(StageBits::TESS_EVALUATION_SHADER), passthrough));
            }
            constants->release();

            if (result == Result::SUCCESS) {
                auto* linked = MTL::LinkedFunctions::alloc()->init();
                linked->setFunctions(NS::Array::array((const NS::Object* const*)objectFunctions, hasTessellation ? 2 : 1));
                mpd->setObjectLinkedFunctions(linked);
                linked->release();

                if (hasTessellation) {
                    linked = MTL::LinkedFunctions::alloc()->init();
                    linked->setFunctions(NS::Array::array((const NS::Object* const*)meshFunctions, 2));
                    mpd->setMeshLinkedFunctions(linked);
                    linked->release();
                }

                m_Render = m_Device.GetNativeObject()->newRenderPipelineState(mpd, options, nullptr, &error);
            }
        } else
#endif
            m_Render = isMesh ? m_Device.GetNativeObject()->newRenderPipelineState(mpd, options, nullptr, &error) : m_Device.GetNativeObject()->newRenderPipelineState(pd, options, nullptr, &error);
        result = m_Render ? Result::SUCCESS : Result::FAILURE;

        if (m_Render && archive) {
            if (!(isMesh ? archive->addMeshRenderPipelineFunctions(mpd, &error) : archive->addRenderPipelineFunctions(pd, &error)))
                result = Result::FAILURE;
        }

        if (!m_Render && !(desc.flags & GraphicsPipelineBits::FAIL_ON_CACHE_MISS))
            m_Device.ReportMessage(Message::ERROR, result, __FILE__, __LINE__, "Metal render pipeline creation failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");
    }
    MTL::DepthStencilDescriptor* dd = MTL::DepthStencilDescriptor::alloc()->init();
    dd->setDepthCompareFunction(GetCompareMetal(desc.outputMerger.depth.compareOp));
    dd->setDepthWriteEnabled(desc.outputMerger.depth.write);
    MTL::StencilDescriptor* front = MTL::StencilDescriptor::alloc()->init();
    MTL::StencilDescriptor* back = MTL::StencilDescriptor::alloc()->init();
    SetStencilMetal(front, desc.outputMerger.stencil.front);
    SetStencilMetal(back, desc.outputMerger.stencil.back);
    dd->setFrontFaceStencil(front);
    dd->setBackFaceStencil(back);
    front->release();
    back->release();
    m_DepthStencil = m_Device.GetNativeObject()->newDepthStencilState(dd);
    dd->release();
    if (pd)
        pd->release();
    if (mpd)
        mpd->release();
    if (stageInLibrary)
        stageInLibrary->release();
    for (MTL::Function* f : functions)
        f->release();
    for (MTL::Library* l : libraries)
        l->release();
    m_Cull = (MTL::CullMode)desc.rasterization.cullMode;
    m_Winding = desc.rasterization.frontCounterClockwise ? MTL::WindingCounterClockwise : MTL::WindingClockwise;
    m_Fill = desc.rasterization.fillMode == FillMode::WIREFRAME ? MTL::TriangleFillModeLines : MTL::TriangleFillModeFill;
    m_DepthClip = desc.rasterization.depthClamp ? MTL::DepthClipModeClamp : MTL::DepthClipModeClip;
    m_DepthBounds = desc.outputMerger.depth.boundsTest;
    m_SampleLocations = desc.multisample && desc.multisample->sampleLocations;
    m_DepthBias = desc.rasterization.depthBias;
    static constexpr MTL::PrimitiveType primitives[] = {MTL::PrimitiveTypePoint, MTL::PrimitiveTypeLine, MTL::PrimitiveTypeLineStrip, MTL::PrimitiveTypeTriangle, MTL::PrimitiveTypeTriangleStrip};
    if (desc.inputAssembly.topology <= Topology::TRIANGLE_STRIP)
        m_Primitive = primitives[(uint32_t)desc.inputAssembly.topology];
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    static constexpr IRRuntimePrimitiveType emulationPrimitives[] = {
        IRRuntimePrimitiveTypePoint, IRRuntimePrimitiveTypeLine, IRRuntimePrimitiveTypeLineStrip,
        IRRuntimePrimitiveTypeTriangle, IRRuntimePrimitiveTypeTriangleStrip,
        IRRuntimePrimitiveTypeLineWithAdj, IRRuntimePrimitiveTypeLineStripWithAdj,
        IRRuntimePrimitiveTypeTriangleWithAdj, IRRuntimePrimitiveTypeTriangleWithAdj,
        IRRuntimePrimitiveType1ControlPointPatchlist};
    m_EmulationPrimitive = emulationPrimitives[(uint32_t)desc.inputAssembly.topology];
#endif
    pool->release();

    return result;
}

DeviceMetal& PipelineMetal::GetDevice() const {
    return m_Device;
}

MTL::RenderPipelineState* PipelineMetal::GetRenderPipeline() const {
    return m_Render;
}

MTL::ComputePipelineState* PipelineMetal::GetComputePipeline() const {
    return m_Compute;
}

MTL::VisibleFunctionTable* PipelineMetal::GetVisibleFunctionTable() const {
    return m_VisibleFunctionTable;
}

MTL::IntersectionFunctionTable* PipelineMetal::GetIntersectionFunctionTable() const {
    return m_IntersectionFunctionTable;
}

MTL::ResourceID PipelineMetal::GetVisibleFunctionTableResourceID() const {
    return m_VisibleFunctionTable ? m_VisibleFunctionTable->gpuResourceID() : MTL::ResourceID{};
}

MTL::ResourceID PipelineMetal::GetIntersectionFunctionTableResourceID() const {
    return m_IntersectionFunctionTable ? m_IntersectionFunctionTable->gpuResourceID() : MTL::ResourceID{};
}

MTL::DepthStencilState* PipelineMetal::GetDepthStencilState() const {
    return m_DepthStencil;
}

MTL::PrimitiveType PipelineMetal::GetPrimitiveType() const {
    return m_Primitive;
}

MTL::CullMode PipelineMetal::GetCullMode() const {
    return m_Cull;
}

MTL::Winding PipelineMetal::GetWinding() const {
    return m_Winding;
}

MTL::TriangleFillMode PipelineMetal::GetFillMode() const {
    return m_Fill;
}

MTL::DepthClipMode PipelineMetal::GetDepthClipMode() const {
    return m_DepthClip;
}

bool PipelineMetal::IsDepthBoundsEnabled() const {
    return m_DepthBounds;
}

bool PipelineMetal::HasSampleLocations() const {
    return m_SampleLocations;
}

const DepthBiasDesc& PipelineMetal::GetDepthBias() const {
    return m_DepthBias;
}

MTL::Size PipelineMetal::GetThreadGroupSize() const {
    return m_ThreadGroup;
}

MTL::Size PipelineMetal::GetMeshThreadGroupSize() const {
    return m_MeshGroup;
}

MTL::Size PipelineMetal::GetTaskThreadGroupSize() const {
    return m_TaskGroup;
}

bool PipelineMetal::IsConverted() const {
    return m_Converted;
}

#if NRI_ENABLE_METAL_SHADER_CONVERTER
bool PipelineMetal::IsGeometryEmulation() const {
    return m_GeometryEmulation;
}

bool PipelineMetal::IsTessellationEmulation() const {
    return m_TessellationEmulation;
}

const IRRuntimeGeometryPipelineConfig& PipelineMetal::GetGeometryConfig() const {
    return m_GeometryConfig;
}

const IRRuntimeTessellationPipelineConfig& PipelineMetal::GetTessellationConfig() const {
    return m_TessellationConfig;
}

IRRuntimePrimitiveType PipelineMetal::GetEmulationPrimitive() const {
    return m_EmulationPrimitive;
}
#endif

const PipelineLayoutMetal& PipelineMetal::GetLayout() const {
    return *m_Layout;
}

void PipelineMetal::SetDebugName(const char* name) {
    // Pipeline state labels are immutable after creation.
    MaybeUnused(name);
}

} // namespace nri
