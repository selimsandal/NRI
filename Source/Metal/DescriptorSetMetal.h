// © 2026 NVIDIA Corporation
#pragma once

namespace nri {

struct DescriptorRangeMappingMetal {
    uint32_t offset = 0;
    uint32_t descriptorNum = 0;
    DescriptorType type = DescriptorType::MUTABLE;
    bool sampler = false;
};

struct DescriptorSetMappingMetal {
    DescriptorSetMappingMetal(StdAllocator<uint8_t>& allocator)
        : ranges(allocator) {
    }

    Vector<DescriptorRangeMappingMetal> ranges;
    uint32_t resourceNum = 0;
    uint32_t samplerNum = 0;
    uint32_t variableRange = UINT32_MAX;
};

struct DescriptorSetMetal final : public DebugNameBase {
    DescriptorSetMetal(DeviceMetal& device, MTL::Buffer* resourceHeap, MTL::Buffer* samplerHeap, uint32_t resourceOffset, uint32_t samplerOffset, const DescriptorSetMappingMetal& mapping);

    DeviceMetal& GetDevice() const;
    void GetOffsets(uint32_t& resourceOffset, uint32_t& samplerOffset) const;
    uint64_t GetResourceAddress() const;
    uint64_t GetSamplerAddress() const;
    void Update(uint32_t rangeIndex, uint32_t baseDescriptor, const Descriptor* const* descriptors, uint32_t num);
    void Copy(uint32_t dstRange, uint32_t dstBase, const DescriptorSetMetal& source, uint32_t srcRange, uint32_t srcBase, uint32_t num);
    static void UpdateDescriptorRanges(const UpdateDescriptorRangeDesc* descs, uint32_t num);
    static void Copy(const CopyDescriptorRangeDesc* descs, uint32_t num);

    void SetDebugName(const char*) NRI_DEBUG_NAME_OVERRIDE {
    }

private:
    uint8_t* GetEntry(const DescriptorRangeMappingMetal& range, uint32_t index) const;
    DeviceMetal& m_Device;
    MTL::Buffer* m_ResourceHeap;
    MTL::Buffer* m_SamplerHeap;
    const DescriptorSetMappingMetal& m_Mapping;
    uint32_t m_ResourceOffset;
    uint32_t m_SamplerOffset;
};

} // namespace nri
