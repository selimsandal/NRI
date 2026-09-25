// © 2026 NVIDIA Corporation

MTL::TextureDescriptor* nri::MakeTextureDescriptorMetal(const TextureDesc& desc, MemoryLocation location) {
    MTL::TextureDescriptor* nativeDesc = MTL::TextureDescriptor::texture2DDescriptor(GetPixelFormatMetal(desc.format), desc.width, desc.height ? desc.height : 1, false);
    nativeDesc->setWidth(desc.width);
    nativeDesc->setHeight(desc.height ? desc.height : 1);
    nativeDesc->setDepth(desc.depth ? desc.depth : 1);
    nativeDesc->setMipmapLevelCount(desc.mipNum ? desc.mipNum : 1);
    nativeDesc->setArrayLength(desc.layerNum ? desc.layerNum : 1);
    nativeDesc->setSampleCount(desc.sampleNum ? desc.sampleNum : 1);
    nativeDesc->setStorageMode(location == MemoryLocation::DEVICE ? MTL::StorageModePrivate : MTL::StorageModeShared);
    nativeDesc->setCpuCacheMode(location == MemoryLocation::DEVICE || location == MemoryLocation::HOST_READBACK ? MTL::CPUCacheModeDefaultCache : MTL::CPUCacheModeWriteCombined);
    nativeDesc->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);

    // Converter maps 1D textures to height-one 2D textures. This also preserves
    // NRI mipmaps, which Metal's native 1D textures cannot represent.
    MTL::TextureType type = MTL::TextureType2D;
    if (desc.type == TextureType::TEXTURE_3D)
        type = MTL::TextureType3D;
    else if ((desc.sampleNum ? desc.sampleNum : 1) > 1)
        type = (desc.layerNum ? desc.layerNum : 1) > 1 ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DMultisample;
    else if ((desc.layerNum ? desc.layerNum : 1) > 1)
        type = MTL::TextureType2DArray;
    nativeDesc->setTextureType(type);

    MTL::TextureUsage usage = MTL::TextureUsageUnknown;
    if (desc.usage & TextureUsageBits::SHADER_RESOURCE)
        usage |= MTL::TextureUsageShaderRead;
    if (desc.usage & TextureUsageBits::SHADER_RESOURCE_STORAGE)
        usage |= MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite;
    if (desc.usage & (TextureUsageBits::COLOR_ATTACHMENT | TextureUsageBits::DEPTH_STENCIL_ATTACHMENT | TextureUsageBits::INPUT_ATTACHMENT))
        usage |= MTL::TextureUsageRenderTarget;
    if (usage != MTL::TextureUsageUnknown)
        usage |= MTL::TextureUsagePixelFormatView;
    nativeDesc->setUsage(usage);

    return nativeDesc;
}

TextureMetal::~TextureMetal() {
    Release();
}

void TextureMetal::Release() {
    if (m_Texture) {
        if (m_IsResident)
            m_Device.RemoveResidency(m_Texture);
        m_Texture->release();
    }
    m_Texture = nullptr;
    m_IsResident = false;
}

Result TextureMetal::Create(const TextureDesc& desc) {
    Release();
    m_Desc = FixTextureDesc(desc);

    return Result::SUCCESS;
}

Result TextureMetal::Create(const TextureDesc& desc, MemoryLocation location) {
    Create(desc);
    MTL::TextureDescriptor* nativeDesc = MakeTextureDescriptorMetal(m_Desc, location);
    m_Texture = m_Device.GetNativeObject()->newTexture(nativeDesc);
    if (!m_Texture)
        return Result::OUT_OF_MEMORY;
    m_Device.AddResidency(m_Texture);
    m_IsResident = true;

    return Result::SUCCESS;
}

Result TextureMetal::Create(const TextureMetalDesc& desc) {
    Release();
    m_Desc = FixTextureDesc(desc.desc);
    m_Texture = (MTL::Texture*)desc.mtlTexture;
    m_Texture->retain();
    m_Device.AddResidency(m_Texture);
    m_IsResident = true;

    return Result::SUCCESS;
}

Result TextureMetal::Bind(MemoryMetal& memory, uint64_t offset) {
    Release();
    MTL::TextureDescriptor* desc = MakeTextureDescriptorMetal(m_Desc, memory.GetLocation());
    m_Texture = memory.GetNativeObject()->newTexture(desc, (NS::UInteger)offset);

    return m_Texture ? Result::SUCCESS : Result::FAILURE;
}

void TextureMetal::GetMemoryDesc(MemoryLocation location, MemoryDesc& memoryDesc) const {
    MTL::TextureDescriptor* desc = MakeTextureDescriptorMetal(m_Desc, location);
    MTL::SizeAndAlign requirements = m_Device.GetNativeObject()->heapTextureSizeAndAlign(desc);
    memoryDesc = {requirements.size, (uint32_t)requirements.align, (MemoryType)location, false};
}

void TextureMetal::SetDrawableTexture(MTL::Texture* texture, const TextureDesc& desc) {
    Release();
    m_Desc = FixTextureDesc(desc);
    m_Texture = texture;
    if (m_Texture)
        m_Texture->retain();
}

void TextureMetal::SetDebugName(const char* name) {
    if (m_Texture)
        m_Texture->setLabel(NS::String::string(name, NS::UTF8StringEncoding));
}
