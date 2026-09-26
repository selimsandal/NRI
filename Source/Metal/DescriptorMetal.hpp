// © 2026 NVIDIA Corporation

namespace nri {

static MTL::TextureType GetTextureViewTypeMetal(TextureView type, const TextureDesc& textureDesc) {
    const bool multisampled = textureDesc.sampleNum > 1;

    switch (type) {
        case TextureView::TEXTURE_ARRAY:
        case TextureView::STORAGE_TEXTURE_ARRAY:
            return multisampled ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DArray;
        case TextureView::TEXTURE_CUBE:
            return MTL::TextureTypeCube;
        case TextureView::TEXTURE_CUBE_ARRAY:
            return MTL::TextureTypeCubeArray;
        default:
            if (textureDesc.type == TextureType::TEXTURE_3D)
                return MTL::TextureType3D;

            return multisampled ? MTL::TextureType2DMultisample : MTL::TextureType2D;
    }
}

static MTL::TextureSwizzle GetTextureSwizzleMetal(ComponentSwizzle swizzle, MTL::TextureSwizzle identity) {
    const MTL::TextureSwizzle swizzles[] = {identity, MTL::TextureSwizzleZero, MTL::TextureSwizzleOne, MTL::TextureSwizzleRed, MTL::TextureSwizzleGreen, MTL::TextureSwizzleBlue, MTL::TextureSwizzleAlpha};

    return swizzles[(uint32_t)swizzle];
}

struct MetalDescriptorEntry {
    uint64_t buffer;
    uint64_t texture;
    uint64_t metadata;
};

static_assert(sizeof(MetalDescriptorEntry) == 24, "Metal shader-converter descriptor ABI changed");

DescriptorMetal::DescriptorMetal(DeviceMetal& device)
    : m_Device(device) {
}

DescriptorMetal::~DescriptorMetal() {
    if (m_TextureView)
        m_TextureView->release();
    if (m_Sampler)
        m_Sampler->release();
}

Result DescriptorMetal::Create(const BufferViewDesc& desc) {
    m_Buffer = (BufferMetal*)desc.buffer;
    m_BufferOffset = desc.offset;
    m_BufferSize = desc.size == WHOLE_SIZE ? m_Buffer->GetDesc().size - desc.offset : desc.size;
    m_TypedBuffer = desc.type == BufferView::BUFFER || desc.type == BufferView::STORAGE_BUFFER;
    if (m_TypedBuffer) {
        const uint32_t stride = GetFormatProps(desc.format).stride;
        if (!stride)
            return Result::INVALID_ARGUMENT;
        MTL::Buffer* buffer = m_Buffer->GetNativeObject();
        MTL::TextureUsage usage = MTL::TextureUsageShaderRead;
        if (desc.type == BufferView::STORAGE_BUFFER)
            usage |= MTL::TextureUsageShaderWrite;
        MTL::TextureDescriptor* textureDesc = MTL::TextureDescriptor::textureBufferDescriptor(GetPixelFormatMetal(desc.format), m_BufferSize / stride, buffer->resourceOptions(), usage);
        m_TextureView = buffer->newTexture(textureDesc, m_BufferOffset, m_BufferSize);
        if (!m_TextureView)
            return Result::FAILURE;
    }

    return Result::SUCCESS;
}

Result DescriptorMetal::Create(const TextureViewDesc& desc) {
    m_Texture = (TextureMetal*)desc.texture;
    m_TextureViewDesc = desc;
    m_TextureViewDesc.format = desc.format == Format::UNKNOWN ? m_Texture->GetDesc().format : desc.format;
    MTL::Texture* texture = m_Texture->GetNativeObject();
    if (!texture)
        return Result::SUCCESS; // Swapchain drawable is deliberately resolved at use time.

    // Render pass descriptors address levels and slices in the parent texture. Returning a
    // subresource view here would make CommandBufferMetal apply these offsets twice.
    const bool attachment = desc.type == TextureView::COLOR_ATTACHMENT || desc.type == TextureView::DEPTH_STENCIL_ATTACHMENT || desc.type == TextureView::SHADING_RATE_ATTACHMENT;
    MTL::PixelFormat format = desc.format == Format::UNKNOWN ? texture->pixelFormat() : GetPixelFormatMetal(desc.format);

    if (attachment) {
        if (format != texture->pixelFormat())
            m_TextureView = texture->newTextureView(format);

        return format == texture->pixelFormat() || m_TextureView ? Result::SUCCESS : Result::FAILURE;
    }

    if (format == MTL::PixelFormatDepth32Float_Stencil8 && desc.planes == PlaneBits::STENCIL)
        format = MTL::PixelFormatX32_Stencil8;

    const NS::UInteger mipNum = desc.mipNum == REMAINING ? texture->mipmapLevelCount() - desc.mipOffset : desc.mipNum;
    const NS::UInteger layerNum = desc.layerNum == REMAINING ? texture->arrayLength() - desc.layerOffset : desc.layerNum;
    const MTL::TextureType type = GetTextureViewTypeMetal(desc.type, m_Texture->GetDesc());
    const MTL::TextureSwizzleChannels swizzle = MTL::TextureSwizzleChannels::Make(
        GetTextureSwizzleMetal(desc.components.r, MTL::TextureSwizzleRed),
        GetTextureSwizzleMetal(desc.components.g, MTL::TextureSwizzleGreen),
        GetTextureSwizzleMetal(desc.components.b, MTL::TextureSwizzleBlue),
        GetTextureSwizzleMetal(desc.components.a, MTL::TextureSwizzleAlpha));
    m_TextureView = texture->newTextureView(format, type, NS::Range(desc.mipOffset, mipNum), NS::Range(desc.layerOffset, layerNum), swizzle);

    return m_TextureView ? Result::SUCCESS : Result::FAILURE;
}

Result DescriptorMetal::Create(const SamplerDesc& desc) {
    MTL::SamplerDescriptor* d = MTL::SamplerDescriptor::alloc()->init();
    const MTL::SamplerAddressMode address[] = {MTL::SamplerAddressModeRepeat, MTL::SamplerAddressModeMirrorRepeat, MTL::SamplerAddressModeClampToEdge, MTL::SamplerAddressModeClampToBorderColor, MTL::SamplerAddressModeMirrorClampToEdge};
    d->setMinFilter(desc.filters.min == Filter::LINEAR ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
    d->setMagFilter(desc.filters.mag == Filter::LINEAR ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
    d->setMipFilter(desc.filters.mip == Filter::LINEAR ? MTL::SamplerMipFilterLinear : MTL::SamplerMipFilterNearest);
    d->setSAddressMode(address[(uint32_t)desc.addressModes.u]);
    d->setTAddressMode(address[(uint32_t)desc.addressModes.v]);
    d->setRAddressMode(address[(uint32_t)desc.addressModes.w]);
    d->setLodMinClamp(desc.mipMin);
    d->setLodMaxClamp(desc.mipMax);
    d->setMaxAnisotropy(desc.anisotropy ? desc.anisotropy : 1);
    d->setCompareFunction(GetCompareMetal(desc.compareOp));
    d->setNormalizedCoordinates(!desc.unnormalizedCoordinates);
    d->setSupportArgumentBuffers(true);
    const bool usesBorder = desc.addressModes.u == AddressMode::CLAMP_TO_BORDER || desc.addressModes.v == AddressMode::CLAMP_TO_BORDER || desc.addressModes.w == AddressMode::CLAMP_TO_BORDER;
    if (usesBorder) {
        const bool transparentBlack = desc.isInteger ? desc.borderColor.ui.x == 0 && desc.borderColor.ui.y == 0 && desc.borderColor.ui.z == 0 && desc.borderColor.ui.w == 0 : desc.borderColor.f.x == 0.0f && desc.borderColor.f.y == 0.0f && desc.borderColor.f.z == 0.0f && desc.borderColor.f.w == 0.0f;
        const bool opaqueBlack = desc.isInteger ? desc.borderColor.ui.x == 0 && desc.borderColor.ui.y == 0 && desc.borderColor.ui.z == 0 && desc.borderColor.ui.w == 1 : desc.borderColor.f.x == 0.0f && desc.borderColor.f.y == 0.0f && desc.borderColor.f.z == 0.0f && desc.borderColor.f.w == 1.0f;
        const bool opaqueWhite = desc.isInteger ? desc.borderColor.ui.x == 1 && desc.borderColor.ui.y == 1 && desc.borderColor.ui.z == 1 && desc.borderColor.ui.w == 1 : desc.borderColor.f.x == 1.0f && desc.borderColor.f.y == 1.0f && desc.borderColor.f.z == 1.0f && desc.borderColor.f.w == 1.0f;

        if (opaqueWhite)
            d->setBorderColor(MTL::SamplerBorderColorOpaqueWhite);
        else if (opaqueBlack)
            d->setBorderColor(MTL::SamplerBorderColorOpaqueBlack);
        else if (transparentBlack)
            d->setBorderColor(MTL::SamplerBorderColorTransparentBlack);
        else {
            d->release();

            return Result::UNSUPPORTED;
        }
    }

    const bool nativeLodBias = m_Device.GetNativeObject()->supportsFamily(MTL::GPUFamilyApple10);
    if (nativeLodBias)
        d->setLodBias(desc.mipBias);
    m_SamplerBias = nativeLodBias ? 0.0f : desc.mipBias;
    m_Sampler = m_Device.GetNativeObject()->newSamplerState(d);
    d->release();

    return m_Sampler ? Result::SUCCESS : Result::FAILURE;
}

DeviceMetal& DescriptorMetal::GetDevice() const {
    return m_Device;
}

Result DescriptorMetal::Create(const AccelerationStructureMetal& accelerationStructure) {
    m_AccelerationStructure = &accelerationStructure;

    return Result::SUCCESS;
}

MTL::Buffer* DescriptorMetal::GetBuffer() const {
    return m_AccelerationStructure ? m_AccelerationStructure->GetShaderBindingHeaderBuffer() : (m_Buffer ? m_Buffer->GetNativeObject() : nullptr);
}

MTL::Texture* DescriptorMetal::GetTexture() const {
    return m_TextureView ? m_TextureView : (m_Texture ? m_Texture->GetNativeObject() : nullptr);
}

MTL::SamplerState* DescriptorMetal::GetSampler() const {
    return m_Sampler;
}

uint64_t DescriptorMetal::GetBufferOffset() const {
    return m_BufferOffset;
}

uint64_t DescriptorMetal::GetBufferSize() const {
    return m_BufferSize;
}

const TextureViewDesc& DescriptorMetal::GetTextureViewDesc() const {
    return m_TextureViewDesc;
}

void DescriptorMetal::WriteEntry(void* dst) const {
    MetalDescriptorEntry e = {};
    MTL::Buffer* buffer = GetBuffer();
    MTL::Texture* texture = GetTexture();
    if (buffer) {
        e.buffer = buffer->gpuAddress() + m_BufferOffset;
        e.metadata = (m_BufferSize & 0xFFFFFFFFull) | (uint64_t(m_TypedBuffer) << 63);
    }
    if (texture)
        e.texture = texture->gpuResourceID()._impl;
    if (m_Sampler) {
        e.buffer = m_Sampler->gpuResourceID()._impl;
        e.texture = 0;
        memcpy(&e.metadata, &m_SamplerBias, sizeof(m_SamplerBias));
    }
    memcpy(dst, &e, sizeof(e));
}

void DescriptorMetal::SetDebugName(const char* name) {
    NS::String* label = NS::String::string(name, NS::UTF8StringEncoding);
    if (m_TextureView)
        m_TextureView->setLabel(label);
    // Sampler labels are immutable after creation.
}

} // namespace nri
