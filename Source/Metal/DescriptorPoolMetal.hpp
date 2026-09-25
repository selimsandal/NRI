// © 2026 NVIDIA Corporation

namespace nri {

DescriptorPoolMetal::DescriptorPoolMetal(DeviceMetal& device)
    : m_Device(device), m_Sets(device.GetStdAllocator()) {
}

DescriptorPoolMetal::~DescriptorPoolMetal() {
    Reset();
    if (m_ResourceHeap) {
        m_Device.RemoveResidency(m_ResourceHeap);
        m_ResourceHeap->release();
    }
    if (m_SamplerHeap) {
        m_Device.RemoveResidency(m_SamplerHeap);
        m_SamplerHeap->release();
    }
}

Result DescriptorPoolMetal::Create(const DescriptorPoolDesc& d) {
    m_SetCapacity = d.descriptorSetMaxNum;
    m_SamplerCapacity = d.samplerMaxNum;
    m_ResourceCapacity = d.mutableMaxNum + d.constantBufferMaxNum + d.textureMaxNum + d.storageTextureMaxNum + d.bufferMaxNum + d.storageBufferMaxNum + d.structuredBufferMaxNum + d.storageStructuredBufferMaxNum + d.accelerationStructureMaxNum + d.inputAttachmentMaxNum;
    if (m_ResourceCapacity)
        m_ResourceHeap = m_Device.GetNativeObject()->newBuffer(uint64_t(m_ResourceCapacity) * 24, MTL::ResourceStorageModeShared);
    if (m_SamplerCapacity)
        m_SamplerHeap = m_Device.GetNativeObject()->newBuffer(uint64_t(m_SamplerCapacity) * 24, MTL::ResourceStorageModeShared);
    if ((m_ResourceCapacity && !m_ResourceHeap) || (m_SamplerCapacity && !m_SamplerHeap))
        return Result::OUT_OF_MEMORY;
    if (m_ResourceHeap)
        m_Device.AddResidency(m_ResourceHeap);
    if (m_SamplerHeap)
        m_Device.AddResidency(m_SamplerHeap);

    return Result::SUCCESS;
}

Result DescriptorPoolMetal::AllocateSets(const PipelineLayoutMetal& layout, uint32_t setIndex, DescriptorSet** sets, uint32_t instanceNum, uint32_t variableNum) {
    const DescriptorSetMappingMetal& mapping = layout.GetDescriptorSetMapping(setIndex);
    uint32_t resources = mapping.resourceNum;
    uint32_t samplers = mapping.samplerNum;
    if (variableNum && !mapping.ranges.empty()) {
        const DescriptorRangeMappingMetal& last = mapping.ranges.back();
        if (last.sampler)
            samplers -= last.descriptorNum - variableNum;
        else
            resources -= last.descriptorNum - variableNum;
    }
    if (m_Sets.size() + instanceNum > m_SetCapacity || m_ResourceUsed + resources * instanceNum > m_ResourceCapacity || m_SamplerUsed + samplers * instanceNum > m_SamplerCapacity)
        return Result::OUT_OF_MEMORY;

    for (uint32_t i = 0; i < instanceNum; i++) {
        DescriptorSetMetal* set = Allocate<DescriptorSetMetal>(m_Device.GetAllocationCallbacks(), m_Device, m_ResourceHeap, m_SamplerHeap, m_ResourceUsed, m_SamplerUsed, mapping);
        if (!set)
            return Result::OUT_OF_MEMORY;
        m_Sets.push_back(set);
        sets[i] = (DescriptorSet*)set;
        m_ResourceUsed += resources;
        m_SamplerUsed += samplers;
    }

    return Result::SUCCESS;
}

Result DescriptorPoolMetal::Create(const DescriptorHeapDesc& desc) {
    DescriptorPoolDesc pool = {};
    pool.mutableMaxNum = desc.resourceDescriptorNum;
    pool.samplerMaxNum = desc.samplerDescriptorNum;

    return Create(pool);
}

Result DescriptorPoolMetal::WriteResourceDescriptors(const WriteResourceDescriptorsDesc* descs, uint32_t num) {
    for (uint32_t i = 0; i < num; i++)
        ((const DescriptorMetal*)descs[i].resource)->WriteEntry((uint8_t*)m_ResourceHeap->contents() + uint64_t(descs[i].descriptorIndex) * 24);

    return Result::SUCCESS;
}

Result DescriptorPoolMetal::WriteSamplerDescriptors(const WriteSamplerDescriptorsDesc* descs, uint32_t num) {
    for (uint32_t i = 0; i < num; i++)
        ((const DescriptorMetal*)descs[i].sampler)->WriteEntry((uint8_t*)m_SamplerHeap->contents() + uint64_t(descs[i].descriptorIndex) * 24);

    return Result::SUCCESS;
}

Result DescriptorPoolMetal::AllocateDescriptorSets(const PipelineLayout& layout, uint32_t setIndex, DescriptorSet** sets, uint32_t instanceNum, uint32_t variableNum) {
    return AllocateSets((const PipelineLayoutMetal&)layout, setIndex, sets, instanceNum, variableNum);
}

void DescriptorPoolMetal::Reset() {
    for (DescriptorSetMetal* set : m_Sets)
        Destroy(m_Device.GetAllocationCallbacks(), set);
    m_Sets.clear();
    m_ResourceUsed = m_SamplerUsed = 0;
}

DeviceMetal& DescriptorPoolMetal::GetDevice() const {
    return m_Device;
}

uint64_t DescriptorPoolMetal::GetResourceHeapAddress() const {
    return m_ResourceHeap ? m_ResourceHeap->gpuAddress() : 0;
}

uint64_t DescriptorPoolMetal::GetSamplerHeapAddress() const {
    return m_SamplerHeap ? m_SamplerHeap->gpuAddress() : 0;
}

void DescriptorPoolMetal::SetDebugName(const char* name) {
    NS::String* s = NS::String::string(name, NS::UTF8StringEncoding);
    if (m_ResourceHeap)
        m_ResourceHeap->setLabel(s);
    if (m_SamplerHeap)
        m_SamplerHeap->setLabel(s);
}

} // namespace nri
