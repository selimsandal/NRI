// © 2026 NVIDIA Corporation

QueueFeedbackMetal::~QueueFeedbackMetal() {
    NS::Error* lastError = error.load();

    if (lastError)
        lastError->release();

    dispatch_release(pending);
}

QueueMetal::~QueueMetal() {
    if (m_Queue) {
        m_Queue->removeResidencySet(m_Device.GetResidencySet());
        m_Queue->release();
    }
}

Result QueueMetal::Create(QueueType type) {
    m_Type = type;
    m_Queue = m_Device.GetNativeObject()->newMTL4CommandQueue();

    return m_Queue ? Result::SUCCESS : Result::FAILURE;
}

Result QueueMetal::Create(QueueType type, MTL4::CommandQueue* queue) {
    m_Type = type;
    m_Queue = queue;
    m_Queue->retain();

    return Result::SUCCESS;
}

Result QueueMetal::Commit(const MTL4::CommandBuffer* const* commandBuffers, uint32_t commandBufferNum) {
    if (m_Feedback->error.load())
        return Result::DEVICE_LOST;

    if (!commandBufferNum)
        return Result::SUCCESS;

    auto feedback = m_Feedback;
    auto* options = MTL4::CommitOptions::alloc()->init();
    options->addFeedbackHandler([feedback](MTL4::CommitFeedback* result) {
        if (NS::Error* error = result->error()) {
            error->retain();
            NS::Error* expected = nullptr;

            if (!feedback->error.compare_exchange_strong(expected, error))
                error->release();
        }
        dispatch_group_leave(feedback->pending);
    });
    dispatch_group_enter(feedback->pending);
    m_Queue->commit(commandBuffers, commandBufferNum, options);
    options->release();

    return Result::SUCCESS;
}

Result QueueMetal::WaitIdle() {
    const long pending = dispatch_group_wait(m_Feedback->pending, dispatch_time(DISPATCH_TIME_NOW, uint64_t(NRI_TIMEOUT_FENCE) * NSEC_PER_MSEC));

    if (NS::Error* error = m_Feedback->error.load()) {
        m_Device.ReportMessage(Message::ERROR, Result::DEVICE_LOST, __FILE__, __LINE__, "Metal submission failed: %s", error->localizedDescription()->utf8String());

        return Result::DEVICE_LOST;
    }

    if (pending)
        return Result::FAILURE;

    MTL::SharedEvent* event = m_Device.GetNativeObject()->newSharedEvent();
    if (!event)
        return Result::OUT_OF_MEMORY;

    m_Queue->signalEvent(event, 1);
    bool completed = event->waitUntilSignaledValue(1, NRI_TIMEOUT_FENCE);
    event->release();

    return completed ? Result::SUCCESS : Result::FAILURE;
}

struct HostTextureCopyLayoutMetal {
    uint64_t offset;
    uint32_t rowSize;
    uint32_t rowPitch;
    uint32_t rowNum;
    uint32_t slicePitch;
    uint32_t depth;
    MTL::Size size;
};

static HostTextureCopyLayoutMetal GetHostTextureCopyLayoutMetal(const TextureMetal& texture, const TextureRegionDesc& region, uint64_t& stagingSize) {
    const TextureDesc& textureDesc = texture.GetDesc();
    const FormatProps& formatProps = GetFormatProps(textureDesc.format);
    uint32_t width = region.width == WHOLE_SIZE ? std::max(1u, uint32_t(textureDesc.width) >> region.mipOffset) : region.width;
    uint32_t height = region.height == WHOLE_SIZE ? std::max(1u, uint32_t(textureDesc.height) >> region.mipOffset) : region.height;
    uint32_t depth = region.depth == WHOLE_SIZE ? std::max(1u, uint32_t(textureDesc.depth) >> region.mipOffset) : region.depth;

    HostTextureCopyLayoutMetal layout = {};
    layout.offset = Align(stagingSize, 256ull);
    layout.rowSize = ((width + formatProps.blockWidth - 1) / formatProps.blockWidth) * formatProps.stride;
    layout.rowPitch = Align(layout.rowSize, 256u);
    layout.rowNum = (height + formatProps.blockHeight - 1) / formatProps.blockHeight;
    layout.slicePitch = layout.rowPitch * layout.rowNum;
    layout.depth = depth;
    layout.size = MTL::Size(width, height, depth);
    stagingSize = layout.offset + uint64_t(layout.slicePitch) * depth;

    return layout;
}

Result QueueMetal::UploadHostMemoryToTexture(const UploadHostMemoryToTextureDesc* copyDescs, uint32_t copyDescNum) {
    if (!copyDescNum)
        return Result::SUCCESS;

    Result result = WaitIdle();
    if (result != Result::SUCCESS)
        return result;

    Scratch<HostTextureCopyLayoutMetal> layouts = NRI_ALLOCATE_SCRATCH(m_Device, HostTextureCopyLayoutMetal, copyDescNum);
    uint64_t stagingSize = 0;
    for (uint32_t i = 0; i < copyDescNum; i++)
        layouts[i] = GetHostTextureCopyLayoutMetal(*(TextureMetal*)copyDescs[i].dstTexture, copyDescs[i].dstRegion, stagingSize);

    MTL::Buffer* stagingBuffer = m_Device.GetNativeObject()->newBuffer(stagingSize, MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined);
    MTL4::CommandAllocator* allocator = m_Device.GetNativeObject()->newCommandAllocator();
    MTL4::CommandBuffer* commandBuffer = m_Device.GetNativeObject()->newCommandBuffer();
    if (!stagingBuffer || !allocator || !commandBuffer) {
        if (commandBuffer)
            commandBuffer->release();
        if (allocator)
            allocator->release();
        if (stagingBuffer)
            stagingBuffer->release();

        return Result::OUT_OF_MEMORY;
    }

    uint8_t* stagingData = (uint8_t*)stagingBuffer->contents();
    for (uint32_t i = 0; i < copyDescNum; i++) {
        const UploadHostMemoryToTextureDesc& copyDesc = copyDescs[i];
        const HostTextureCopyLayoutMetal& layout = layouts[i];
        uint32_t srcRowPitch = copyDesc.srcRowPitch ? copyDesc.srcRowPitch : layout.rowSize;
        uint32_t srcSlicePitch = copyDesc.srcSlicePitch ? copyDesc.srcSlicePitch : srcRowPitch * layout.rowNum;
        for (uint32_t z = 0; z < layout.depth; z++) {
            for (uint32_t y = 0; y < layout.rowNum; y++)
                memcpy(stagingData + layout.offset + uint64_t(z) * layout.slicePitch + uint64_t(y) * layout.rowPitch, (const uint8_t*)copyDesc.srcData + uint64_t(z) * srcSlicePitch + uint64_t(y) * srcRowPitch, layout.rowSize);
        }
    }

    m_Device.AddResidency(stagingBuffer);
    m_Device.CommitResidency();
    commandBuffer->beginCommandBuffer(allocator);
    commandBuffer->useResidencySet(m_Device.GetResidencySet());
    MTL4::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
    for (uint32_t i = 0; i < copyDescNum; i++) {
        const UploadHostMemoryToTextureDesc& copyDesc = copyDescs[i];
        const HostTextureCopyLayoutMetal& layout = layouts[i];
        encoder->copyFromBuffer(stagingBuffer, layout.offset, layout.rowPitch, layout.slicePitch, layout.size, ((TextureMetal*)copyDesc.dstTexture)->GetNativeObject(), copyDesc.dstRegion.layerOffset, copyDesc.dstRegion.mipOffset, MTL::Origin(copyDesc.dstRegion.x, copyDesc.dstRegion.y, copyDesc.dstRegion.z));
    }
    encoder->endEncoding();
    commandBuffer->endCommandBuffer();
    const MTL4::CommandBuffer* commandBuffers[] = {commandBuffer};
    result = Commit(commandBuffers, 1);

    if (result == Result::SUCCESS)
        result = WaitIdle();

    m_Device.RemoveResidency(stagingBuffer);
    commandBuffer->release();
    allocator->release();
    stagingBuffer->release();

    return result;
}

Result QueueMetal::ReadbackTextureToHostMemory(const ReadbackTextureToHostMemoryDesc* copyDescs, uint32_t copyDescNum) {
    if (!copyDescNum)
        return Result::SUCCESS;

    Result result = WaitIdle();
    if (result != Result::SUCCESS)
        return result;

    Scratch<HostTextureCopyLayoutMetal> layouts = NRI_ALLOCATE_SCRATCH(m_Device, HostTextureCopyLayoutMetal, copyDescNum);
    uint64_t stagingSize = 0;
    for (uint32_t i = 0; i < copyDescNum; i++)
        layouts[i] = GetHostTextureCopyLayoutMetal(*(TextureMetal*)copyDescs[i].srcTexture, copyDescs[i].srcRegion, stagingSize);

    MTL::Buffer* stagingBuffer = m_Device.GetNativeObject()->newBuffer(stagingSize, MTL::ResourceStorageModeShared);
    MTL4::CommandAllocator* allocator = m_Device.GetNativeObject()->newCommandAllocator();
    MTL4::CommandBuffer* commandBuffer = m_Device.GetNativeObject()->newCommandBuffer();
    if (!stagingBuffer || !allocator || !commandBuffer) {
        if (commandBuffer)
            commandBuffer->release();
        if (allocator)
            allocator->release();
        if (stagingBuffer)
            stagingBuffer->release();

        return Result::OUT_OF_MEMORY;
    }

    m_Device.AddResidency(stagingBuffer);
    m_Device.CommitResidency();
    commandBuffer->beginCommandBuffer(allocator);
    commandBuffer->useResidencySet(m_Device.GetResidencySet());
    MTL4::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
    for (uint32_t i = 0; i < copyDescNum; i++) {
        const ReadbackTextureToHostMemoryDesc& copyDesc = copyDescs[i];
        const HostTextureCopyLayoutMetal& layout = layouts[i];
        encoder->copyFromTexture(((TextureMetal*)copyDesc.srcTexture)->GetNativeObject(), copyDesc.srcRegion.layerOffset, copyDesc.srcRegion.mipOffset, MTL::Origin(copyDesc.srcRegion.x, copyDesc.srcRegion.y, copyDesc.srcRegion.z), layout.size, stagingBuffer, layout.offset, layout.rowPitch, layout.slicePitch);
    }
    encoder->endEncoding();
    commandBuffer->endCommandBuffer();
    const MTL4::CommandBuffer* commandBuffers[] = {commandBuffer};
    result = Commit(commandBuffers, 1);

    if (result == Result::SUCCESS)
        result = WaitIdle();

    if (result == Result::SUCCESS) {
        const uint8_t* stagingData = (const uint8_t*)stagingBuffer->contents();
        for (uint32_t i = 0; i < copyDescNum; i++) {
            const ReadbackTextureToHostMemoryDesc& copyDesc = copyDescs[i];
            const HostTextureCopyLayoutMetal& layout = layouts[i];
            uint32_t dstRowPitch = copyDesc.dstRowPitch ? copyDesc.dstRowPitch : layout.rowSize;
            uint32_t dstSlicePitch = copyDesc.dstSlicePitch ? copyDesc.dstSlicePitch : dstRowPitch * layout.rowNum;
            for (uint32_t z = 0; z < layout.depth; z++) {
                for (uint32_t y = 0; y < layout.rowNum; y++)
                    memcpy((uint8_t*)copyDesc.dstData + uint64_t(z) * dstSlicePitch + uint64_t(y) * dstRowPitch, stagingData + layout.offset + uint64_t(z) * layout.slicePitch + uint64_t(y) * layout.rowPitch, layout.rowSize);
            }
        }
    }

    m_Device.RemoveResidency(stagingBuffer);
    commandBuffer->release();
    allocator->release();
    stagingBuffer->release();

    return result;
}

void QueueMetal::SetDebugName(const char* name) {
    // MTL4 queue labels are immutable after creation.
    MaybeUnused(name);
}
