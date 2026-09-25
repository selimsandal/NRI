// © 2026 NVIDIA Corporation

CommandAllocatorMetal::~CommandAllocatorMetal() {
    Reset();
    if (m_Allocator)
        m_Allocator->release();
}

Result CommandAllocatorMetal::Create(const Queue& queue) {
    m_Queue = (QueueMetal*)&queue;
    m_Allocator = m_Device.GetNativeObject()->newCommandAllocator();

    return m_Allocator ? Result::SUCCESS : Result::FAILURE;
}

void CommandAllocatorMetal::Reset() {
    if (m_Allocator)
        m_Allocator->reset();

    for (UploadChunkMetal& chunk : m_UploadChunks) {
        m_Device.RemoveResidency(chunk.buffer);
        chunk.buffer->release();
    }
    m_UploadChunks.clear();
}

MTL::GPUAddress CommandAllocatorMetal::Upload(const void* data, uint64_t size, uint64_t alignment) {
    if (!size)
        return 0;

    UploadChunkMetal* chunk = m_UploadChunks.empty() ? nullptr : &m_UploadChunks.back();
    uint64_t offset = chunk ? Align(chunk->offset, alignment) : 0;
    if (!chunk || offset + size > chunk->size) {
        uint64_t chunkSize = std::max<uint64_t>(64 * 1024, Align(size, 4096));
        MTL::Buffer* buffer = m_Device.GetNativeObject()->newBuffer((NS::UInteger)chunkSize, MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined | MTL::ResourceHazardTrackingModeUntracked);
        if (!buffer)
            return 0;

        m_Device.AddResidency(buffer);
        m_UploadChunks.push_back({buffer, 0, chunkSize});
        chunk = &m_UploadChunks.back();
        offset = 0;
    }

    if (data)
        memcpy((uint8_t*)chunk->buffer->contents() + offset, data, (size_t)size);
    chunk->offset = offset + size;

    return chunk->buffer->gpuAddress() + offset;
}

void CommandAllocatorMetal::SetDebugName(const char* name) {
    // MTL4 allocator labels are immutable after creation.
    MaybeUnused(name);
}
