// © 2026 NVIDIA Corporation

struct MetalShaderIdentifier {
    uint64_t intersectionShaderHandle;
    uint64_t shaderHandle;
    uint64_t localRootSignatureSamplersBuffer;
    uint64_t padding;
};

#if NRI_ENABLE_METAL_SHADER_CONVERTER
static inline IRShaderStage GetIRRayTracingShaderStage(StageBits stage) {
    if (stage == StageBits::RAYGEN_SHADER)
        return IRShaderStageRayGeneration;
    if (stage == StageBits::MISS_SHADER)
        return IRShaderStageMiss;
    if (stage == StageBits::INTERSECTION_SHADER)
        return IRShaderStageIntersection;
    if (stage == StageBits::CLOSEST_HIT_SHADER)
        return IRShaderStageClosestHit;
    if (stage == StageBits::ANY_HIT_SHADER)
        return IRShaderStageAnyHit;
    if (stage == StageBits::CALLABLE_SHADER)
        return IRShaderStageCallable;

    return IRShaderStageInvalid;
}
#endif

Result PipelineMetal::Create(const RayTracingPipelineDesc& desc) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    m_Layout = (const PipelineLayoutMetal*)desc.pipelineLayout;
    const ShaderLibraryDesc& shaderLibrary = *desc.shaderLibrary;
    Vector<MTL::Library*> libraries(m_Device.GetStdAllocator());
    Vector<MTL::Function*> functions(m_Device.GetStdAllocator());
    libraries.resize(shaderLibrary.shaderNum + desc.shaderGroupNum, nullptr);
    functions.resize(libraries.size(), nullptr);

#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRRayTracingPipelineConfiguration* configuration = IRRayTracingPipelineConfigurationCreate();
    IRRayTracingPipelineConfigurationSetMaxAttributeSizeInBytes(configuration, desc.rayHitAttributeMaxSize);
    IRRayTracingPipelineConfigurationSetMaxRecursiveDepth(configuration, (int)desc.recursionMaxDepth);
    IRRayTracingPipelineConfigurationSetRayGenerationCompilationMode(configuration, IRRayGenerationCompilationVisibleFunction);
    IRRayTracingPipelineConfigurationSetIntersectionFunctionCompilationMode(configuration, IRIntersectionFunctionCompilationVisibleFunction);
    IRRaytracingPipelineFlags flags = IRRaytracingPipelineFlagNone;
    if (desc.flags & RayTracingPipelineBits::SKIP_TRIANGLES)
        flags = (IRRaytracingPipelineFlags)(flags | IRRaytracingPipelineFlagSkipTriangles);
    if (desc.flags & RayTracingPipelineBits::SKIP_AABBS)
        flags = (IRRaytracingPipelineFlags)(flags | IRRaytracingPipelineFlagSkipProceduralPrimitives);
    IRRayTracingPipelineConfigurationSetPipelineFlags(configuration, flags);

    IRCompiler* compiler = IRCompilerCreate();
    IRCompilerSetGlobalRootSignature(compiler, m_Layout->GetRootSignature());
    IRCompilerSetRayTracingPipelineConfiguration(compiler, configuration);
    IRCompilerSetMinimumGPUFamily(compiler, IRGPUFamilyApple7); // M2 and newer.

    auto loadBinary = [&](IRMetalLibBinary* binary, const char* name, MTL::Library*& library, MTL::Function*& function) {
        NS::Error* error = nullptr;
        library = m_Device.GetNativeObject()->newLibrary(IRMetalLibGetBytecodeData(binary), &error);
        if (library)
            function = library->newFunction(NS::String::string(name, NS::UTF8StringEncoding));

        return function != nullptr;
    };
#endif

    Result result = Result::SUCCESS;
    for (uint32_t i = 0; i < shaderLibrary.shaderNum; i++) {
        const ShaderDesc& shader = shaderLibrary.shaders[i];
        // Intersection and any-hit code is linked per hit group below: one intersection
        // shader can appear with different any-hit shaders in different groups.
        if (shader.stage == StageBits::INTERSECTION_SHADER || shader.stage == StageBits::ANY_HIT_SHADER)
            continue;
        const char* entry = shader.entryPointName ? shader.entryPointName : "main";
        const bool isDxil = shader.size >= 4 && memcmp(shader.bytecode, "DXBC", 4) == 0;
        if (!isDxil) {
            result = LoadFunction(shader, libraries[i], functions[i]);
            if (result != Result::SUCCESS)
                break;
            continue;
        }

#if NRI_ENABLE_METAL_SHADER_CONVERTER
        IRObject* input = IRObjectCreateFromDXIL((const uint8_t*)shader.bytecode, shader.size, IRBytecodeOwnershipNone);
        IRError* error = nullptr;
        IRObject* output = IRCompilerAllocCompileAndLink(compiler, entry, input, &error);
        IRMetalLibBinary* binary = IRMetalLibBinaryCreate();
        IRShaderReflection* reflection = IRShaderReflectionCreate();
        const IRShaderStage stage = GetIRRayTracingShaderStage(shader.stage);
        const char* metalName = entry;
        if (output && IRObjectGetReflection(output, stage, reflection)) {
            const char* reflectedName = IRShaderReflectionGetEntryPointFunctionName(reflection);
            metalName = reflectedName ? reflectedName : entry;
        }
        if (!output || !IRObjectGetMetalLibBinary(output, stage, binary) || !loadBinary(binary, metalName, libraries[i], functions[i]))
            result = Result::FAILURE;
        if (error)
            IRErrorDestroy(error);
        IRShaderReflectionDestroy(reflection);
        IRMetalLibBinaryDestroy(binary);
        if (output)
            IRObjectDestroy(output);
        IRObjectDestroy(input);
#else
        result = Result::UNSUPPORTED;
#endif
        if (result != Result::SUCCESS)
            break;
    }

    for (uint32_t i = 0; result == Result::SUCCESS && i < desc.shaderGroupNum; i++) {
        const ShaderDesc* intersection = nullptr;
        const ShaderDesc* anyHit = nullptr;
        for (uint32_t index : desc.shaderGroups[i].shaderIndices) {
            if (!index)
                continue;
            const ShaderDesc& shader = shaderLibrary.shaders[index - 1];
            if (shader.stage == StageBits::INTERSECTION_SHADER)
                intersection = &shader;
            else if (shader.stage == StageBits::ANY_HIT_SHADER)
                anyHit = &shader;
        }
        if (!intersection && !anyHit)
            continue;

        const uint32_t slot = shaderLibrary.shaderNum + i;
        auto isDxil = [](const ShaderDesc* shader) {
            return !shader || (shader->size >= 4 && memcmp(shader->bytecode, "DXBC", 4) == 0);
        };
        if (!isDxil(intersection) || !isDxil(anyHit)) {
            // Native intersection entries carry their own any-hit logic.
            if (intersection && anyHit) {
                result = Result::UNSUPPORTED;
                break;
            }
            result = LoadFunction(intersection ? *intersection : *anyHit, libraries[slot], functions[slot]);
            continue;
        }

#if NRI_ENABLE_METAL_SHADER_CONVERTER
        IRObject* intersectionIR = intersection ? IRObjectCreateFromDXIL((const uint8_t*)intersection->bytecode, intersection->size, IRBytecodeOwnershipNone) : nullptr;
        IRObject* anyHitIR = anyHit ? IRObjectCreateFromDXIL((const uint8_t*)anyHit->bytecode, anyHit->size, IRBytecodeOwnershipNone) : nullptr;
        const char* intersectionEntry = intersection ? (intersection->entryPointName ? intersection->entryPointName : "main") : nullptr;
        const char* anyHitEntry = anyHit ? (anyHit->entryPointName ? anyHit->entryPointName : "main") : nullptr;
        IRCompilerSetHitgroupType(compiler, intersection ? IRHitGroupTypeProceduralPrimitive : IRHitGroupTypeTriangles);
        IRError* error = nullptr;
        IRObject* output = IRCompilerAllocCombineCompileAndLink(compiler, intersectionEntry, intersectionIR, anyHitEntry, anyHitIR, &error);
        IRMetalLibBinary* binary = IRMetalLibBinaryCreate();
        IRShaderReflection* reflection = IRShaderReflectionCreate();
        const IRShaderStage stage = intersection ? IRShaderStageIntersection : IRShaderStageAnyHit;
        const char* entry = intersection ? intersectionEntry : anyHitEntry;
        if (output && IRObjectGetReflection(output, stage, reflection)) {
            const char* name = IRShaderReflectionGetEntryPointFunctionName(reflection);
            if (name)
                entry = name;
        }
        if (!output || !IRObjectGetMetalLibBinary(output, stage, binary) || !loadBinary(binary, entry, libraries[slot], functions[slot]))
            result = Result::FAILURE;
        if (error)
            IRErrorDestroy(error);
        IRShaderReflectionDestroy(reflection);
        IRMetalLibBinaryDestroy(binary);
        if (output)
            IRObjectDestroy(output);
        if (intersectionIR)
            IRObjectDestroy(intersectionIR);
        if (anyHitIR)
            IRObjectDestroy(anyHitIR);
#else
        result = Result::UNSUPPORTED;
#endif
    }

    // Native libraries can supply these ABI entry points, avoiding a Converter dependency.
    auto loadCompanion = [&](const char* name, MTL::Library*& library, MTL::Function*& function) {
        for (MTL::Library* candidate : libraries) {
            if (!candidate)
                continue;
            function = candidate->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
            if (function) {
                library = candidate->retain();
                return true;
            }
        }
        return false;
    };

    // The indirection kernel allows every ray-generation record in the SBT to be selected at dispatch time.
    MTL::Library* dispatchLibrary = nullptr;
    MTL::Function* dispatchFunction = nullptr;
    if (result == Result::SUCCESS && !loadCompanion("RaygenIndirection", dispatchLibrary, dispatchFunction)) {
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        IRMetalLibBinary* binary = IRMetalLibBinaryCreate();
        if (!IRMetalLibSynthesizeIndirectRayDispatchFunction(compiler, binary) || !loadBinary(binary, "RaygenIndirection", dispatchLibrary, dispatchFunction))
            result = Result::FAILURE;
        IRMetalLibBinaryDestroy(binary);
#else
        result = Result::UNSUPPORTED;
#endif
    }

    Vector<MTL::Function*> linkedFunctions(m_Device.GetStdAllocator());
    for (MTL::Function* function : functions) {
        if (function)
            linkedFunctions.push_back(function);
    }
    MTL::Library* triangleLibrary = nullptr;
    MTL::Library* proceduralLibrary = nullptr;
    MTL::Function* triangleFunction = nullptr;
    MTL::Function* proceduralFunction = nullptr;
    auto synthesizeIntersection = [&](bool procedural, const char* name, MTL::Library*& library, MTL::Function*& function) {
        if (loadCompanion(name, library, function))
            return true;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        IRCompilerSetHitgroupType(compiler, procedural ? IRHitGroupTypeProceduralPrimitive : IRHitGroupTypeTriangles);
        IRMetalLibBinary* binary = IRMetalLibBinaryCreate();
        const bool success = IRMetalLibSynthesizeIndirectIntersectionFunction(compiler, binary) && loadBinary(binary, name, library, function);
        IRMetalLibBinaryDestroy(binary);
        return success;
#else
        MaybeUnused(procedural);
        return false;
#endif
    };
    if (result == Result::SUCCESS && !(desc.flags & RayTracingPipelineBits::SKIP_TRIANGLES))
        result = synthesizeIntersection(false, "irconverter.wrapper.intersection.function.triangle", triangleLibrary, triangleFunction) ? Result::SUCCESS : Result::FAILURE;
    if (result == Result::SUCCESS && !(desc.flags & RayTracingPipelineBits::SKIP_AABBS))
        result = synthesizeIntersection(true, "irconverter.wrapper.intersection.function.procedural", proceduralLibrary, proceduralFunction) ? Result::SUCCESS : Result::FAILURE;
    if (triangleFunction)
        linkedFunctions.push_back(triangleFunction);
    if (proceduralFunction)
        linkedFunctions.push_back(proceduralFunction);

    if (result == Result::SUCCESS) {
        MTL::LinkedFunctions* linked = MTL::LinkedFunctions::alloc()->init();
        linked->setFunctions(NS::Array::array((const NS::Object* const*)linkedFunctions.data(), linkedFunctions.size()));
        MTL::ComputePipelineDescriptor* pipelineDesc = MTL::ComputePipelineDescriptor::alloc()->init();
        pipelineDesc->setComputeFunction(dispatchFunction);
        pipelineDesc->setLinkedFunctions(linked);
        NS::Error* error = nullptr;
        MTL::BinaryArchive* archive = desc.cache ? ((PipelineCacheMetal*)desc.cache)->GetNativeObject() : nullptr;

        if (archive)
            pipelineDesc->setBinaryArchives(NS::Array::array(archive));

        const auto options = (desc.flags & RayTracingPipelineBits::FAIL_ON_CACHE_MISS) ? MTL::PipelineOptionFailOnBinaryArchiveMiss : MTL::PipelineOptionNone;
        m_Compute = m_Device.GetNativeObject()->newComputePipelineState(pipelineDesc, options, nullptr, &error);
        result = m_Compute ? Result::SUCCESS : Result::FAILURE;

        if (m_Compute && archive && !archive->addComputePipelineFunctions(pipelineDesc, &error))
            result = Result::FAILURE;

        pipelineDesc->release();
        linked->release();
    }

    const uint32_t functionCount = (uint32_t)functions.size() + 1;
    if (result == Result::SUCCESS) {
        MTL::VisibleFunctionTableDescriptor* tableDesc = MTL::VisibleFunctionTableDescriptor::alloc()->init();
        tableDesc->setFunctionCount(functionCount);
        m_VisibleFunctionTable = m_Compute->newVisibleFunctionTable(tableDesc);
        tableDesc->release();
        if (m_VisibleFunctionTable) {
            m_Device.AddResidency(m_VisibleFunctionTable);
            for (uint32_t i = 0; i < functions.size(); i++) {
                if (functions[i])
                    m_VisibleFunctionTable->setFunction(m_Compute->functionHandle(functions[i]), i + 1);
            }
        } else {
            result = Result::OUT_OF_MEMORY;
        }

        MTL::IntersectionFunctionTableDescriptor* intersectionDesc = MTL::IntersectionFunctionTableDescriptor::alloc()->init();
        intersectionDesc->setFunctionCount(2);
        m_IntersectionFunctionTable = m_Compute->newIntersectionFunctionTable(intersectionDesc);
        intersectionDesc->release();
        if (m_IntersectionFunctionTable) {
            m_Device.AddResidency(m_IntersectionFunctionTable);
            if (triangleFunction)
                m_IntersectionFunctionTable->setFunction(m_Compute->functionHandle(triangleFunction), 0);
            if (proceduralFunction)
                m_IntersectionFunctionTable->setFunction(m_Compute->functionHandle(proceduralFunction), 1);
            m_IntersectionFunctionTable->setVisibleFunctionTable(m_VisibleFunctionTable, 0);
        } else {
            result = Result::OUT_OF_MEMORY;
        }
    }

    m_ShaderGroupIdentifiers.resize(desc.shaderGroupNum * sizeof(MetalShaderIdentifier));
    for (uint32_t i = 0; result == Result::SUCCESS && i < desc.shaderGroupNum; i++) {
        MetalShaderIdentifier identifier = {};
        const ShaderGroupDesc& group = desc.shaderGroups[i];
        for (uint32_t j = 0; j < 3; j++) {
            if (!group.shaderIndices[j])
                continue;
            const uint32_t shaderIndex = group.shaderIndices[j] - 1;
            const StageBits stage = shaderLibrary.shaders[shaderIndex].stage;
            if (stage == StageBits::INTERSECTION_SHADER || stage == StageBits::ANY_HIT_SHADER)
                identifier.intersectionShaderHandle = shaderLibrary.shaderNum + i + 1;
            else
                identifier.shaderHandle = group.shaderIndices[j];
        }
        memcpy(m_ShaderGroupIdentifiers.data() + i * sizeof(identifier), &identifier, sizeof(identifier));
    }

    if (dispatchFunction)
        dispatchFunction->release();
    if (dispatchLibrary)
        dispatchLibrary->release();
    if (triangleFunction)
        triangleFunction->release();
    if (triangleLibrary)
        triangleLibrary->release();
    if (proceduralFunction)
        proceduralFunction->release();
    if (proceduralLibrary)
        proceduralLibrary->release();
    for (uint32_t i = 0; i < functions.size(); i++) {
        if (functions[i])
            functions[i]->release();
        if (libraries[i])
            libraries[i]->release();
    }
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRCompilerDestroy(compiler);
    IRRayTracingPipelineConfigurationDestroy(configuration);
#endif
    pool->release();
    m_Converted = true;

    return result;
}

Result PipelineMetal::WriteShaderGroupIdentifiers(uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, uint32_t dstStride, void* dst) const {
    const uint32_t identifierSize = sizeof(MetalShaderIdentifier);
    const uint8_t* src = m_ShaderGroupIdentifiers.data() + baseShaderGroupIndex * identifierSize;
    uint8_t* destination = (uint8_t*)dst;
    for (uint32_t i = 0; i < shaderGroupNum; i++) {
        memcpy(destination, src, identifierSize);
        src += identifierSize;
        destination += dstStride;
    }

    return Result::SUCCESS;
}
