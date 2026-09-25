// © 2026 NVIDIA Corporation

QueryPoolMetal::~QueryPoolMetal() {
    if (m_CounterHeap)
        m_CounterHeap->release();
    if (m_VisibilityBuffer) {
        m_Device.RemoveResidency(m_VisibilityBuffer);
        m_VisibilityBuffer->release();
    }
}

Result QueryPoolMetal::Create(const QueryPoolDesc& desc) {
    m_Type = desc.queryType;
    m_Capacity = desc.capacity;
    if (m_Type == QueryType::TIMESTAMP || m_Type == QueryType::TIMESTAMP_COPY_QUEUE) {
        MTL4::CounterHeapDescriptor* heapDesc = MTL4::CounterHeapDescriptor::alloc()->init();
        heapDesc->setType(MTL4::CounterHeapTypeTimestamp);
        heapDesc->setCount(m_Capacity);
        NS::Error* error = nullptr;
        m_CounterHeap = m_Device.GetNativeObject()->newCounterHeap(heapDesc, &error);
        heapDesc->release();

        return m_CounterHeap ? Result::SUCCESS : Result::UNSUPPORTED;
    }
    if (m_Type == QueryType::OCCLUSION || m_Type == QueryType::ACCELERATION_STRUCTURE_SIZE || m_Type == QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE) {
        m_VisibilityBuffer = m_Device.GetNativeObject()->newBuffer(uint64_t(m_Capacity) * sizeof(uint64_t), MTL::ResourceStorageModePrivate | MTL::ResourceHazardTrackingModeUntracked);
        if (!m_VisibilityBuffer)
            return Result::OUT_OF_MEMORY;
        m_Device.AddResidency(m_VisibilityBuffer);

        return Result::SUCCESS;
    }

    return Result::UNSUPPORTED;
}

void QueryPoolMetal::Reset(uint32_t offset, uint32_t num) {
    if (m_CounterHeap)
        m_CounterHeap->invalidateCounterRange(NS::Range(offset, num));
    // Private visibility memory is reset by CmdResetQueries, where GPU access is legal.
}

void QueryPoolMetal::SetDebugName(const char* name) {
    NS::String* label = NS::String::string(name, NS::UTF8StringEncoding);
    if (m_CounterHeap)
        m_CounterHeap->setLabel(label);
    if (m_VisibilityBuffer)
        m_VisibilityBuffer->setLabel(label);
}
