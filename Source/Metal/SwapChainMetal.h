// © 2026 NVIDIA Corporation

#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>

namespace nri {

struct SwapChainMetal final {
    inline SwapChainMetal(DeviceMetal& device) : m_Device(device), m_Textures(device.GetStdAllocator()) {
    }

    ~SwapChainMetal();

    Result Create(const SwapChainDesc& desc);

    inline DeviceMetal& GetDevice() const {
        return m_Device;
    }

    Texture* const* GetTextures(uint32_t& textureNum) const;
    Result AcquireNextTexture(FenceMetal& fence, uint32_t& textureIndex);
    Result Present(FenceMetal& fence, uint64_t presentId);
    Result WaitForPresent(uint64_t presentId);
    Result GetDisplayDesc(DisplayDesc& desc);

private:
    struct PresentationState {
        std::mutex lock;
        std::condition_variable conditionVariable;
        uint64_t completedPresentId = 0;
        bool isShuttingDown = false;
    };

    DeviceMetal& m_Device;
    QueueMetal* m_Queue = nullptr;
    CA::MetalLayer* m_Layer = nullptr;
    CA::MetalDrawable* m_Drawable = nullptr;
    MTL::ResidencySet* m_DrawableResidency = nullptr;
    Vector<TextureMetal*> m_Textures;
    TextureDesc m_TextureDesc = {};
    uint32_t m_Current = 0;
    bool m_IsWaitable = false;
    std::shared_ptr<PresentationState> m_PresentationState = std::make_shared<PresentationState>();
};

} // namespace nri
