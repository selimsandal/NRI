// © 2026 NVIDIA Corporation

SwapChainMetal::~SwapChainMetal() {
    if (m_DrawableResidency) {
        m_Queue->GetNativeObject()->removeResidencySet(m_DrawableResidency);
        m_DrawableResidency->release();
    }

    if (m_Drawable)
        m_Drawable->release();
    for (TextureMetal* texture : m_Textures)
        Destroy(m_Device.GetAllocationCallbacks(), texture);
    if (m_Layer)
        m_Layer->release();
}

Result SwapChainMetal::Create(const SwapChainDesc& desc) {
    m_Layer = (CA::MetalLayer*)desc.window.metal.caMetalLayer;
    if (!m_Layer || !desc.queue)
        return Result::INVALID_ARGUMENT;
    m_Layer->retain();
    m_Queue = (QueueMetal*)desc.queue;
    m_Layer->setDevice(m_Device.GetNativeObject());
    // The layer's set also covers resources used by presentation, not just drawable textures.
    m_DrawableResidency = m_Layer->residencySet();
    m_DrawableResidency->retain();
    m_Queue->GetNativeObject()->addResidencySet(m_DrawableResidency);
    // G22 swapchains receive display-encoded shader output, as on D3D/Vulkan.
    // An sRGB attachment would encode it again and wash out colors.
    MTL::PixelFormat format = desc.format == SwapChainFormat::BT709_G10_16BIT ? MTL::PixelFormatRGBA16Float : (desc.format == SwapChainFormat::BT709_G22_10BIT || desc.format == SwapChainFormat::BT2020_G2084_10BIT ? MTL::PixelFormatRGB10A2Unorm : MTL::PixelFormatBGRA8Unorm);
    m_Layer->setPixelFormat(format);
    m_Layer->setFramebufferOnly(false);
    m_Layer->setDrawableSize(CGSizeMake(desc.width, desc.height));
    m_Layer->setDisplaySyncEnabled(bool(desc.flags & SwapChainBits::VSYNC));
    uint32_t textureNum = std::max<uint32_t>(2, desc.textureNum);
    textureNum = std::min<uint32_t>(3, textureNum);
    m_Layer->setMaximumDrawableCount(textureNum);
    m_TextureDesc.type = TextureType::TEXTURE_2D;
    m_TextureDesc.usage = TextureUsageBits::COLOR_ATTACHMENT;
    m_TextureDesc.format = format == MTL::PixelFormatRGBA16Float ? Format::RGBA16_SFLOAT : (format == MTL::PixelFormatBGRA8Unorm ? Format::BGRA8_UNORM : Format::R10_G10_B10_A2_UNORM);
    m_TextureDesc.width = desc.width;
    m_TextureDesc.height = desc.height;
    m_TextureDesc.depth = 1;
    m_TextureDesc.mipNum = 1;
    m_TextureDesc.layerNum = 1;
    m_TextureDesc.sampleNum = 1;
    for (uint32_t i = 0; i < textureNum; i++) {
        TextureMetal* texture = Allocate<TextureMetal>(m_Device.GetAllocationCallbacks(), m_Device);
        if (!texture)
            return Result::OUT_OF_MEMORY;
        texture->Create(m_TextureDesc);
        m_Textures.push_back(texture);
    }

    return Result::SUCCESS;
}

Texture* const* SwapChainMetal::GetTextures(uint32_t& textureNum) const {
    textureNum = (uint32_t)m_Textures.size();

    return (Texture* const*)m_Textures.data();
}

Result SwapChainMetal::AcquireNextTexture(FenceMetal& fence, uint32_t& textureIndex) {
    if (!fence.IsSwapChainSemaphore() || m_Drawable)
        return Result::INVALID_ARGUMENT;
    CGSize size = m_Layer->drawableSize();
    if ((uint32_t)size.width != m_TextureDesc.width || (uint32_t)size.height != m_TextureDesc.height)
        return Result::OUT_OF_DATE;
    m_Drawable = m_Layer->nextDrawable();
    if (!m_Drawable)
        return Result::FAILURE;
    m_Drawable->retain();
    m_Current = (m_Current + 1) % (uint32_t)m_Textures.size();
    textureIndex = m_Current;
    m_Textures[m_Current]->SetDrawableTexture(m_Drawable->texture(), m_TextureDesc);
    m_Queue->GetNativeObject()->wait(m_Drawable);
    m_Queue->GetNativeObject()->signalEvent(fence.GetNativeObject(), fence.NextSignalValue());

    return Result::SUCCESS;
}

Result SwapChainMetal::Present(FenceMetal& fence, uint64_t presentId) {
    if (!m_Drawable || !fence.IsSwapChainSemaphore())
        return Result::INVALID_ARGUMENT;
    m_Queue->GetNativeObject()->wait(fence.GetNativeObject(), fence.GetScheduledValue());
    m_Queue->GetNativeObject()->signalDrawable(m_Drawable);
    m_Drawable->present();
    m_Drawable->release();
    m_Drawable = nullptr;
    m_LastPresentId = presentId;

    return Result::SUCCESS;
}

Result SwapChainMetal::WaitForPresent(uint64_t presentId) {
    MaybeUnused(presentId);

    return Result::UNSUPPORTED;
}

Result SwapChainMetal::GetDisplayDesc(DisplayDesc& desc) {
    MaybeUnused(desc);

    return Result::UNSUPPORTED;
}
