// © 2026 NVIDIA Corporation
#pragma once

#if NRI_ENABLE_METAL_SHADER_CONVERTER
struct IRRootSignature;
#endif

namespace nri {

struct PipelineLayoutMetal final : public DebugNameBase {
    PipelineLayoutMetal(DeviceMetal& device);
    ~PipelineLayoutMetal();
    Result Create(const PipelineLayoutDesc& desc);
    DeviceMetal& GetDevice() const;
    uint32_t GetRootDataSize() const;
    uint32_t GetRootConstantOffset(uint32_t index) const;
    uint32_t GetRootDescriptorOffset(uint32_t index) const;
    uint32_t GetDrawParametersOffset() const;
    uint32_t GetDrawIndexOffset() const;
    bool IsDrawParametersEmulationEnabled() const;
    bool IsDrawIndexEmulationEnabled() const;
    void GetSetRootOffsets(uint32_t index, uint32_t& resource, uint32_t& sampler) const;
    const DescriptorSetMappingMetal& GetDescriptorSetMapping(uint32_t index) const;
    void InitRootData(void* data) const;
    void WriteSetPointers(void* data, uint32_t setIndex, const DescriptorSetMetal& set) const;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRRootSignature* GetRootSignature() const;
#endif
    void SetDebugName(const char*) NRI_DEBUG_NAME_OVERRIDE {
    }

private:
    DeviceMetal& m_Device;
    Vector<DescriptorSetMappingMetal> m_Sets;
    Vector<uint32_t> m_ConstantOffsets;
    Vector<uint32_t> m_DescriptorOffsets;
    Vector<uint32_t> m_SetOffsets;
    Vector<DescriptorMetal*> m_RootSamplers;
    MTL::Buffer* m_RootSamplerBuffer = nullptr;
    uint32_t m_RootSamplerOffset = UINT32_MAX;
    uint32_t m_DrawParametersOffset = UINT32_MAX;
    uint32_t m_DrawIndexOffset = UINT32_MAX;
    uint32_t m_RootDataSize = 0;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRRootSignature* m_RootSignature = nullptr;
#endif
};

} // namespace nri
