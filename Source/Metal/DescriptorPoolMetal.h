// © 2026 NVIDIA Corporation
#pragma once

namespace nri {

struct DescriptorPoolMetal final : public DebugNameBase {
    DescriptorPoolMetal(DeviceMetal& device);
    ~DescriptorPoolMetal();
    Result Create(const DescriptorPoolDesc& desc);
    Result Create(const DescriptorHeapDesc& desc);
    Result WriteResourceDescriptors(const WriteResourceDescriptorsDesc* descs, uint32_t num);
    Result WriteSamplerDescriptors(const WriteSamplerDescriptorsDesc* descs, uint32_t num);
    Result AllocateSets(const PipelineLayoutMetal& layout, uint32_t setIndex, DescriptorSet** sets, uint32_t instanceNum, uint32_t variableNum);
    Result AllocateDescriptorSets(const PipelineLayout& layout, uint32_t setIndex, DescriptorSet** sets, uint32_t instanceNum, uint32_t variableNum);
    void Reset();
    DeviceMetal& GetDevice() const;
    uint64_t GetResourceHeapAddress() const;
    uint64_t GetSamplerHeapAddress() const;
    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

private:
    DeviceMetal& m_Device;
    Vector<DescriptorSetMetal*> m_Sets;
    MTL::Buffer* m_ResourceHeap = nullptr;
    MTL::Buffer* m_SamplerHeap = nullptr;
    uint32_t m_ResourceCapacity = 0, m_SamplerCapacity = 0;
    uint32_t m_ResourceUsed = 0, m_SamplerUsed = 0;
    uint32_t m_SetCapacity = 0;
};

} // namespace nri
