// © 2026 NVIDIA Corporation

#pragma once

namespace nri {

MTL::TextureDescriptor* MakeTextureDescriptorMetal(const TextureDesc& desc, MemoryLocation location);

struct TextureMetal final : public DebugNameBase {
    inline TextureMetal(DeviceMetal& device) : m_Device(device) {
    }

    ~TextureMetal();

    inline DeviceMetal& GetDevice() const {
        return m_Device;
    }

    inline MTL::Texture* GetNativeObject() const {
        return m_Texture;
    }

    inline const TextureDesc& GetDesc() const {
        return m_Desc;
    }

    Result Create(const TextureDesc& desc);
    Result Create(const TextureDesc& desc, MemoryLocation location);
    Result Create(const TextureMetalDesc& desc);
    Result Bind(MemoryMetal& memory, uint64_t offset);
    void GetMemoryDesc(MemoryLocation location, MemoryDesc& memoryDesc) const;
    void SetDrawableTexture(MTL::Texture* texture, const TextureDesc& desc);
    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

private:
    void Release();

    DeviceMetal& m_Device;
    MTL::Texture* m_Texture = nullptr;
    TextureDesc m_Desc = {};
    bool m_IsResident = false;
};

} // namespace nri
