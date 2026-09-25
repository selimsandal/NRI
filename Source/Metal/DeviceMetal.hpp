// © 2026 NVIDIA Corporation

DeviceMetal::DeviceMetal(const CallbackInterface& callbacks, const AllocationCallbacks& allocationCallbacks)
    : DeviceBase(callbacks, allocationCallbacks), m_ResidencyReferences(GetStdAllocator()) {
    m_Desc.graphicsAPI = GraphicsAPI::METAL;
    m_Desc.nriVersion = NRI_VERSION;
}

DeviceMetal::~DeviceMetal() {
    for (QueueMetal* queue : m_Queues)
        Destroy(GetAllocationCallbacks(), queue);

    if (m_Residency)
        m_Residency->release();

#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (m_TessellatorTables)
        m_TessellatorTables->release();
#endif

    if (m_Device)
        m_Device->release();
}

Result DeviceMetal::Create(const DeviceCreationDesc& desc) {
    if (desc.enableGraphicsAPIValidation)
        NRI_REPORT_WARNING(this, "Metal API validation must be enabled before launch using MTL_DEBUG_LAYER=1 or Xcode's API Validation setting; enableGraphicsAPIValidation does not enable it");

    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device)
        return Result::UNSUPPORTED;

    Result result = Create(desc, device, nullptr);
    device->release();

    return result;
}

Result DeviceMetal::Create(const DeviceCreationDesc& desc, const DeviceCreationMetalDesc& metalDesc) {
    return Create(desc, (MTL::Device*)metalDesc.mtlDevice, metalDesc.mtl4Queues);
}

Result DeviceMetal::Create(const DeviceCreationDesc& desc, MTL::Device* device, void* const* queues) {
    m_Device = device;
    m_Device->retain();

    if (!m_Device->supportsFamily(MTL::GPUFamilyMetal4))
        return Result::UNSUPPORTED;

    auto* residencyDesc = MTL::ResidencySetDescriptor::alloc()->init();
    NS::Error* error = nullptr;
    m_Residency = m_Device->newResidencySet(residencyDesc, &error);
    residencyDesc->release();

    if (!m_Residency)
        return Result::OUT_OF_MEMORY;

    FillDesc(*desc.adapterDesc);
    memset(m_Desc.adapterDesc.queueNum, 0, sizeof(m_Desc.adapterDesc.queueNum));

    for (uint32_t i = 0; i < desc.queueFamilyNum; i++) {
        const auto& family = desc.queueFamilies[i];
        uint32_t type = (uint32_t)family.queueType;

        if (type >= 3 || family.queueNum != 1)
            return Result::UNSUPPORTED;

        Result result = queues && queues[type] ? CreateImplementation<QueueMetal>(m_Queues[type], family.queueType, (MTL4::CommandQueue*)queues[type]) : CreateImplementation<QueueMetal>(m_Queues[type], family.queueType);

        if (result != Result::SUCCESS)
            return result;

        m_Queues[type]->GetNativeObject()->addResidencySet(m_Residency);
        m_Desc.adapterDesc.queueNum[type] = 1;
    }

    FillFunctionTable(m_Core);

    return Result::SUCCESS;
}

void DeviceMetal::AddResidency(MTL::Allocation* allocation) {
    std::lock_guard<std::mutex> lock(m_ResidencyLock);

    if (m_ResidencyReferences[allocation]++ == 0)
        m_Residency->addAllocation(allocation);
}

void DeviceMetal::RemoveResidency(MTL::Allocation* allocation) {
    std::lock_guard<std::mutex> lock(m_ResidencyLock);
    auto it = m_ResidencyReferences.find(allocation);

    if (--it->second == 0) {
        m_Residency->removeAllocation(allocation);
        m_ResidencyReferences.erase(it);
    }
}

void DeviceMetal::CommitResidency() {
    std::lock_guard<std::mutex> lock(m_ResidencyLock);
    m_Residency->commit();
}

#if NRI_ENABLE_METAL_SHADER_CONVERTER
MTL::GPUAddress DeviceMetal::GetTessellatorTables() {
    std::lock_guard<std::mutex> lock(m_ResidencyLock);

    if (!m_TessellatorTables) {
        m_TessellatorTables = m_Device->newBuffer(IRRuntimeTessellatorTablesSize(), MTL::ResourceStorageModeShared);

        if (!m_TessellatorTables)
            return 0;

        IRRuntimeLoadTessellatorTables(m_TessellatorTables);
        m_Residency->addAllocation(m_TessellatorTables);
    }

    return m_TessellatorTables->gpuAddress();
}
#endif

Result DeviceMetal::GetQueue(QueueType type, uint32_t index, Queue*& queue) {
    queue = nullptr;

    if ((uint32_t)type >= 3 || !m_Queues[(uint32_t)type])
        return Result::UNSUPPORTED;

    if (index)
        return Result::INVALID_ARGUMENT;

    queue = (Queue*)m_Queues[(uint32_t)type];

    return Result::SUCCESS;
}

Result DeviceMetal::WaitIdle() {
    for (QueueMetal* queue : m_Queues) {
        if (queue) {
            Result result = queue->WaitIdle();

            if (result != Result::SUCCESS)
                return result;
        }
    }

    return Result::SUCCESS;
}

void DeviceMetal::Destruct() {
    Destroy(GetAllocationCallbacks(), this);
}

void DeviceMetal::FillDesc(const AdapterDesc& adapterDesc) {
    m_Desc.adapterDesc = adapterDesc;
    m_Desc.shaderModel = NriShaderModel(6, 8);
    m_Desc.viewport.maxNum = 16;
    m_Desc.viewport.boundsMin = -32768;
    m_Desc.viewport.boundsMax = 32767;
    m_Desc.dimensions.attachmentMaxDim = 16384;
    m_Desc.dimensions.attachmentLayerMaxNum = 2048;
    m_Desc.dimensions.texture1DMaxDim = 16384;
    m_Desc.dimensions.texture2DMaxDim = 16384;
    m_Desc.dimensions.texture3DMaxDim = 2048;
    m_Desc.dimensions.textureLayerMaxNum = 2048;
    m_Desc.dimensions.typedBufferMaxDim = 256 * 1024 * 1024;
    m_Desc.memory.allocationMaxSize = m_Device->recommendedMaxWorkingSetSize();
    m_Desc.memory.bufferMaxSize = m_Device->maxBufferLength();
    m_Desc.memory.allocationMaxNum = UINT32_MAX;
    m_Desc.memory.samplerAllocationMaxNum = 2048;
    m_Desc.memory.constantBufferMaxRange = (uint32_t)std::min<uint64_t>(m_Desc.memory.bufferMaxSize, UINT32_MAX);
    m_Desc.memory.storageBufferMaxRange = (uint32_t)std::min<uint64_t>(m_Desc.memory.bufferMaxSize, UINT32_MAX);
    m_Desc.memory.bufferTextureGranularity = 1;
    m_Desc.memoryAlignment.uploadBufferTextureRow = 256;
    m_Desc.memoryAlignment.uploadBufferTextureSlice = 256;
    m_Desc.memoryAlignment.bufferShaderResourceOffset = 16;
    m_Desc.memoryAlignment.constantBufferOffset = 4;
    m_Desc.pipelineLayout.descriptorSetMaxNum = 8;
    m_Desc.pipelineLayout.rootConstantMaxSize = 256;
    m_Desc.pipelineLayout.rootDescriptorMaxNum = 31;
    m_Desc.pipelineLayout.rootSamplerMaxNum = 16;
    m_Desc.descriptorHeap.resourceMaxNum = 1000000;
    m_Desc.descriptorHeap.samplerMaxNum = m_Device->supportsFamily(MTL::GPUFamilyApple9) ? 500000 : 996;
    m_Desc.descriptorHeap.rootConstantMaxSize = 256;
    m_Desc.descriptorHeap.rootDescriptorMaxNum = 31;
    m_Desc.descriptorHeap.rootSamplerMaxNum = 16;
    m_Desc.descriptorSet.samplerMaxNum = m_Desc.descriptorHeap.samplerMaxNum;
    m_Desc.descriptorSet.constantBufferMaxNum = 1000000;
    m_Desc.descriptorSet.storageBufferMaxNum = 1000000;
    m_Desc.descriptorSet.textureMaxNum = 1000000;
    m_Desc.descriptorSet.storageTextureMaxNum = 1000000;
    m_Desc.descriptorSet.updateAfterSet.samplerMaxNum = m_Desc.descriptorHeap.samplerMaxNum;
    m_Desc.descriptorSet.updateAfterSet.constantBufferMaxNum = 1000000;
    m_Desc.descriptorSet.updateAfterSet.storageBufferMaxNum = 1000000;
    m_Desc.descriptorSet.updateAfterSet.textureMaxNum = 1000000;
    m_Desc.descriptorSet.updateAfterSet.storageTextureMaxNum = 1000000;
    m_Desc.shaderStage.descriptorSamplerMaxNum = 16;
    m_Desc.shaderStage.descriptorConstantBufferMaxNum = 31;
    m_Desc.shaderStage.descriptorStorageBufferMaxNum = 1000000;
    m_Desc.shaderStage.descriptorTextureMaxNum = 1000000;
    m_Desc.shaderStage.descriptorStorageTextureMaxNum = 1000000;
    m_Desc.shaderStage.resourceMaxNum = 1000000;
    m_Desc.shaderStage.updateAfterSet.descriptorSamplerMaxNum = m_Desc.descriptorHeap.samplerMaxNum;
    m_Desc.shaderStage.updateAfterSet.descriptorConstantBufferMaxNum = 1000000;
    m_Desc.shaderStage.updateAfterSet.descriptorStorageBufferMaxNum = 1000000;
    m_Desc.shaderStage.updateAfterSet.descriptorTextureMaxNum = 1000000;
    m_Desc.shaderStage.updateAfterSet.descriptorStorageTextureMaxNum = 1000000;
    m_Desc.shaderStage.updateAfterSet.resourceMaxNum = 1000000;
    m_Desc.shaderStage.vertex.attributeMaxNum = 31;
    m_Desc.shaderStage.vertex.streamMaxNum = 25;
    m_Desc.shaderStage.vertex.outputComponentMaxNum = 124;
    m_Desc.shaderStage.fragment.inputComponentMaxNum = 124;
    m_Desc.shaderStage.fragment.attachmentMaxNum = 8;
    MTL::Size maxThreads = m_Device->maxThreadsPerThreadgroup();
    m_Desc.shaderStage.compute.dispatchMaxDim[0] = UINT32_MAX;
    m_Desc.shaderStage.compute.dispatchMaxDim[1] = UINT32_MAX;
    m_Desc.shaderStage.compute.dispatchMaxDim[2] = UINT32_MAX;
    m_Desc.shaderStage.compute.workGroupInvocationMaxNum = (uint32_t)maxThreads.width;
    m_Desc.shaderStage.compute.workGroupMaxDim[0] = (uint32_t)maxThreads.width;
    m_Desc.shaderStage.compute.workGroupMaxDim[1] = (uint32_t)maxThreads.height;
    m_Desc.shaderStage.compute.workGroupMaxDim[2] = (uint32_t)maxThreads.depth;
    m_Desc.shaderStage.compute.sharedMemoryMaxSize = 32 * 1024;
    m_Desc.shaderStage.task.dispatchMaxDim[0] = UINT32_MAX;
    m_Desc.shaderStage.task.dispatchMaxDim[1] = UINT32_MAX;
    m_Desc.shaderStage.task.dispatchMaxDim[2] = UINT32_MAX;
    m_Desc.shaderStage.task.workGroupInvocationMaxNum = (uint32_t)maxThreads.width;
    m_Desc.shaderStage.task.workGroupMaxDim[0] = (uint32_t)maxThreads.width;
    m_Desc.shaderStage.task.workGroupMaxDim[1] = (uint32_t)maxThreads.height;
    m_Desc.shaderStage.task.workGroupMaxDim[2] = (uint32_t)maxThreads.depth;
    m_Desc.shaderStage.task.sharedMemoryMaxSize = 32 * 1024;
    m_Desc.shaderStage.task.payloadMaxSize = 16 * 1024;
    m_Desc.shaderStage.task.dispatchWorkGroupMaxNum = UINT32_MAX;
    m_Desc.shaderStage.mesh.dispatchMaxDim[0] = UINT32_MAX;
    m_Desc.shaderStage.mesh.dispatchMaxDim[1] = UINT32_MAX;
    m_Desc.shaderStage.mesh.dispatchMaxDim[2] = UINT32_MAX;
    m_Desc.shaderStage.mesh.workGroupInvocationMaxNum = (uint32_t)maxThreads.width;
    m_Desc.shaderStage.mesh.workGroupMaxDim[0] = (uint32_t)maxThreads.width;
    m_Desc.shaderStage.mesh.workGroupMaxDim[1] = (uint32_t)maxThreads.height;
    m_Desc.shaderStage.mesh.workGroupMaxDim[2] = (uint32_t)maxThreads.depth;
    m_Desc.shaderStage.mesh.sharedMemoryMaxSize = 32 * 1024;
    m_Desc.shaderStage.mesh.outputVerticesMaxNum = 256;
    m_Desc.shaderStage.mesh.outputPrimitiveMaxNum = 256;
    // Apple7/Apple8 limit mesh grids to 1024 threadgroups per draw.
    m_Desc.shaderStage.mesh.dispatchWorkGroupMaxNum = m_Device->supportsFamily(MTL::GPUFamilyApple10) ? 4194303 : (m_Device->supportsFamily(MTL::GPUFamilyApple9) ? 1048575 : 1024);
    m_Desc.wave.laneMinNum = 32;
    m_Desc.wave.laneMaxNum = 32;
    m_Desc.wave.waveOpsStages = StageBits::ALL_SHADERS;
    m_Desc.wave.quadOpsStages = StageBits::FRAGMENT_SHADER | StageBits::COMPUTE_SHADER;
    m_Desc.wave.derivativeOpsStages = StageBits::FRAGMENT_SHADER | StageBits::COMPUTE_SHADER | StageBits::TASK_SHADER | StageBits::MESH_SHADER;
    // MTL4RenderCommandEncoder permits at most two amplified outputs.
    for (uint8_t count = 1; count <= 2 && m_Device->supportsVertexAmplificationCount(count); count++)
        m_Desc.other.viewMaxNum = count;
    m_Desc.other.drawIndirectMaxNum = UINT32_MAX;
    m_Desc.other.samplerAnisotropyMax = 16.0f;
    m_Desc.tiers.resourceBinding = 2;
    m_Desc.tiers.bindless = 2;
    m_Desc.tiers.memory = 1;
    m_Desc.tiers.sampleLocations = 1;
    m_Desc.features.descriptorHeap = true;
    m_Desc.features.swapChain = true;
    m_Desc.features.waitableSwapChain = true;
    m_Desc.features.presentFromCompute = true;
    m_Desc.features.layerBasedMultiview = m_Desc.other.viewMaxNum > 1;
    m_Desc.features.viewportBasedMultiview = m_Desc.other.viewMaxNum > 1;
    m_Desc.features.enhancedBarriers = true;
    m_Desc.features.getMemoryDesc2 = true;
    m_Desc.features.resourceAliasing = true;
    m_Desc.features.componentSwizzle = true;
    m_Desc.features.constantAlphaBlendFactors = true;
    m_Desc.features.independentFrontAndBackStencilReferenceAndMasks = true;
    m_Desc.features.dynamicDepthBias = true;
    m_Desc.features.depthBoundsTest = m_Device->supportsFamily(MTL::GPUFamilyApple10);
    m_Desc.features.rectColorClears = true;
    m_Desc.features.rectDepthStencilClears = true;
    m_Desc.features.rootConstantsOffset = true;
    m_Desc.features.nonConstantBufferRootDescriptorOffset = true;
    m_Desc.features.textureCompressionBC = m_Device->supportsBCTextureCompression();
    m_Desc.features.textureCompressionETC2 = true;
    m_Desc.features.textureCompressionASTC = true;
    m_Desc.features.shaderBytecodeMETALLIB = true;
    m_Desc.features.meshShader = true;
    m_Desc.features.drawIndirectCount = true;
    m_Desc.features.occlusion = true;
    m_Desc.features.timestamp = true;
    m_Desc.features.timestampCopyQueue = true;
    m_Desc.features.calibratedTimestamps = true;
    m_Desc.features.pipelineCache = true;
    m_Desc.features.pipelineCacheControl = true;
    m_Desc.other.timestampFrequencyHz = 1000000000;
    // Metal 4 devices meet Apple7's baseline; Converter also supports these
    // scalar types and SM6 wave/packed-dot intrinsics.
    m_Desc.shaderFeatures.nativeI16 = true;
    m_Desc.shaderFeatures.nativeF16 = true;
    m_Desc.shaderFeatures.nativeI64 = true;
    m_Desc.shaderFeatures.atomicsF32 = true;
    m_Desc.shaderFeatures.atomicsI64 = m_Device->supportsFamily(MTL::GPUFamilyApple9);
    m_Desc.shaderFeatures.barycentric = true;
    m_Desc.shaderFeatures.rasterizedOrderedView = true;
    m_Desc.shaderFeatures.storageReadWithoutFormat = true;
    m_Desc.shaderFeatures.storageWriteWithoutFormat = true;
    m_Desc.shaderFeatures.waveQuery = true;
    m_Desc.shaderFeatures.waveVote = true;
    m_Desc.shaderFeatures.waveShuffle = true;
    m_Desc.shaderFeatures.waveArithmetic = true;
    m_Desc.shaderFeatures.waveReduction = true;
    m_Desc.shaderFeatures.waveQuad = true;
    m_Desc.shaderFeatures.integerDotProduct = true;
    m_Desc.shaderFeatures.unnormalizedCoordinates = true;
    m_Desc.features.flexibleMultiview = m_Desc.other.viewMaxNum > 1;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    // Converter stage-in attributes start at slot 11 in Metal's 31-entry descriptor.
    m_Desc.shaderStage.vertex.attributeMaxNum = 20;
    m_Desc.features.shaderBytecodeDXIL = true;
    m_Desc.features.geometryShader = true;
    m_Desc.features.tessellationShader = true;
#endif
    // Metal 4 address-driven AS builds require Apple9, unlike legacy supportsRaytracing.
    if (m_Device->supportsFamily(MTL::GPUFamilyApple9)) {
        m_Desc.tiers.rayTracing = 2;
        m_Desc.memoryAlignment.scratchBufferOffset = 256;
        m_Desc.memoryAlignment.shaderBindingTable = 64;
        m_Desc.shaderStage.rayTracing.shaderGroupIdentifierSize = 32;
        m_Desc.shaderStage.rayTracing.shaderBindingTableMaxStride = 4096;
        m_Desc.shaderStage.rayTracing.recursionMaxDepth = 31;
        m_Desc.accelerationStructure.primitiveMaxNum = 1u << 24;
        m_Desc.accelerationStructure.geometryMaxNum = 1u << 20;
        m_Desc.accelerationStructure.instanceMaxNum = 1u << 20;
    }
}
