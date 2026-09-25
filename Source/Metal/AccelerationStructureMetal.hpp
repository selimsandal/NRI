// © 2026 NVIDIA Corporation

namespace nri {

static MTL::AccelerationStructureUsage GetAccelerationStructureUsageMetal(AccelerationStructureBits flags) {
    MTL::AccelerationStructureUsage usage = MTL::AccelerationStructureUsageNone;
    if (flags & AccelerationStructureBits::ALLOW_UPDATE)
        usage |= MTL::AccelerationStructureUsageRefit;
    if (flags & AccelerationStructureBits::PREFER_FAST_BUILD)
        usage |= MTL::AccelerationStructureUsagePreferFastBuild;
    if (flags & AccelerationStructureBits::PREFER_FAST_TRACE)
        usage |= MTL::AccelerationStructureUsagePreferFastIntersection;
    if (flags & AccelerationStructureBits::MINIMIZE_MEMORY)
        usage |= MTL::AccelerationStructureUsageMinimizeMemory;

    return usage;
}

static MTL::AttributeFormat GetAccelerationStructureVertexFormatMetal(Format format) {
    switch (format) {
        case Format::RG16_SFLOAT:
            return MTL::AttributeFormatHalf2;
        case Format::RGBA16_SFLOAT:
            return MTL::AttributeFormatHalf4;
        case Format::RG32_SFLOAT:
            return MTL::AttributeFormatFloat2;
        case Format::RGB32_SFLOAT:
            return MTL::AttributeFormatFloat3;
        case Format::RGBA32_SFLOAT:
            return MTL::AttributeFormatFloat4;
        default:
            return MTL::AttributeFormatInvalid;
    }
}

AccelerationStructureMetal::~AccelerationStructureMetal() {
    Release();
    if (m_ShaderBindingHeader) {
        m_Device.RemoveResidency(m_ShaderBindingHeader);
        m_ShaderBindingHeader->release();
    }
}

void AccelerationStructureMetal::Release() {
    if (m_AccelerationStructure) {
        if (m_IsResident)
            m_Device.RemoveResidency(m_AccelerationStructure);
        m_AccelerationStructure->release();
    }
    m_AccelerationStructure = nullptr;
    m_IsResident = false;
}

MTL4::AccelerationStructureDescriptor* AccelerationStructureMetal::CreateBottomLevelDescriptor(const BottomLevelGeometryDesc* geometries, uint32_t geometryNum, bool sizing) const {
    auto* descriptor = MTL4::PrimitiveAccelerationStructureDescriptor::alloc()->init();
    descriptor->setUsage(GetAccelerationStructureUsageMetal(m_Flags));

    Scratch<NS::Object*> objects = NRI_ALLOCATE_SCRATCH(m_Device, NS::Object*, geometryNum);
    for (uint32_t i = 0; i < geometryNum; i++) {
        const BottomLevelGeometryDesc& geometry = geometries[i];
        if (geometry.type == BottomLevelGeometryType::TRIANGLES) {
            const BottomLevelTrianglesDesc& triangles = geometry.triangles;
            auto* metalGeometry = MTL4::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
            const auto address = [sizing](const Buffer* buffer, uint64_t offset) -> MTL::GPUAddress {
                return sizing ? (buffer ? 1 : 0) : (buffer ? ((const BufferMetal*)buffer)->GetGpuAddress() + offset : 0);
            };
            metalGeometry->setVertexBuffer(MTL4::BufferRange(address(triangles.vertexBuffer, triangles.vertexOffset)));
            metalGeometry->setVertexStride(triangles.vertexStride);
            metalGeometry->setVertexFormat(GetAccelerationStructureVertexFormatMetal(triangles.vertexFormat));
            metalGeometry->setTriangleCount(triangles.indexBuffer ? triangles.indexNum / 3 : triangles.vertexNum / 3);
            if (triangles.indexBuffer) {
                metalGeometry->setIndexBuffer(MTL4::BufferRange(address(triangles.indexBuffer, triangles.indexOffset)));
                metalGeometry->setIndexType(triangles.indexType == IndexType::UINT16 ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32);
            }
            if (triangles.transformBuffer) {
                metalGeometry->setTransformationMatrixBuffer(MTL4::BufferRange(address(triangles.transformBuffer, triangles.transformOffset)));
                metalGeometry->setTransformationMatrixLayout(MTL::MatrixLayoutRowMajor);
            }
            metalGeometry->setOpaque((geometry.flags & BottomLevelGeometryBits::OPAQUE_GEOMETRY) != 0);
            metalGeometry->setAllowDuplicateIntersectionFunctionInvocation((geometry.flags & BottomLevelGeometryBits::NO_DUPLICATE_ANY_HIT_INVOCATION) == 0);
            objects[i] = metalGeometry;
        } else {
            const BottomLevelAabbsDesc& aabbs = geometry.aabbs;
            auto* metalGeometry = MTL4::AccelerationStructureBoundingBoxGeometryDescriptor::alloc()->init();
            const MTL::GPUAddress address = sizing ? 1 : ((const BufferMetal*)aabbs.buffer)->GetGpuAddress() + aabbs.offset;
            metalGeometry->setBoundingBoxBuffer(MTL4::BufferRange(address));
            metalGeometry->setBoundingBoxCount(aabbs.num);
            metalGeometry->setBoundingBoxStride(aabbs.stride);
            metalGeometry->setIntersectionFunctionTableOffset(1);
            metalGeometry->setOpaque((geometry.flags & BottomLevelGeometryBits::OPAQUE_GEOMETRY) != 0);
            metalGeometry->setAllowDuplicateIntersectionFunctionInvocation((geometry.flags & BottomLevelGeometryBits::NO_DUPLICATE_ANY_HIT_INVOCATION) == 0);
            objects[i] = metalGeometry;
        }
    }

    NS::Array* array = NS::Array::array(objects, geometryNum);
    descriptor->setGeometryDescriptors(array);
    for (uint32_t i = 0; i < geometryNum; i++)
        objects[i]->release();

    return descriptor;
}

MTL4::AccelerationStructureDescriptor* AccelerationStructureMetal::CreateTopLevelDescriptor(MTL::GPUAddress convertedInstanceAddress, uint32_t instanceNum) const {
    auto* descriptor = MTL4::InstanceAccelerationStructureDescriptor::alloc()->init();
    descriptor->setUsage(GetAccelerationStructureUsageMetal(m_Flags));
    descriptor->setInstanceCount(instanceNum);
    descriptor->setInstanceDescriptorBuffer(MTL4::BufferRange(convertedInstanceAddress));
    descriptor->setInstanceDescriptorStride(sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor));
    descriptor->setInstanceDescriptorType(MTL::AccelerationStructureInstanceDescriptorTypeIndirect);
    descriptor->setInstanceTransformationMatrixLayout(MTL::MatrixLayoutRowMajor);

    return descriptor;
}

Result AccelerationStructureMetal::CreateShaderBindingHeader(uint32_t instanceNum) {
    // Converter's header is two addresses, four reserved uint64s and three dispatch uint32s, padded to 8-byte alignment.
    constexpr uint64_t headerSize = 64;
    m_InstanceContributionOffset = (headerSize + 15) & ~15ull;
    const uint64_t size = m_InstanceContributionOffset + uint64_t(instanceNum) * sizeof(uint32_t);
    m_ShaderBindingHeader = m_Device.GetNativeObject()->newBuffer((NS::UInteger)size, MTL::ResourceStorageModeShared | MTL::ResourceHazardTrackingModeUntracked);
    if (!m_ShaderBindingHeader)
        return Result::OUT_OF_MEMORY;

    memset(m_ShaderBindingHeader->contents(), 0, size);
    uint64_t* header = (uint64_t*)m_ShaderBindingHeader->contents();
    header[1] = m_ShaderBindingHeader->gpuAddress() + m_InstanceContributionOffset;
    m_Device.AddResidency(m_ShaderBindingHeader);

    return Result::SUCCESS;
}

Result AccelerationStructureMetal::Create(const AccelerationStructureDesc& desc) {
    constexpr AccelerationStructureBits micromapFlags = AccelerationStructureBits::ALLOW_MICROMAP_UPDATE | AccelerationStructureBits::ALLOW_DISABLE_MICROMAPS;
    if (desc.flags & micromapFlags)
        return Result::UNSUPPORTED;

    if (desc.type == AccelerationStructureType::BOTTOM_LEVEL) {
        for (uint32_t i = 0; i < desc.geometryOrInstanceNum; i++) {
            if (desc.geometries[i].type == BottomLevelGeometryType::TRIANGLES && desc.geometries[i].triangles.micromap)
                return Result::UNSUPPORTED;
        }
    }

    m_Flags = desc.flags;
    m_Type = desc.type;
    MTL4::AccelerationStructureDescriptor* descriptor = desc.type == AccelerationStructureType::BOTTOM_LEVEL ? CreateBottomLevelDescriptor(desc.geometries, desc.geometryOrInstanceNum, true) : CreateTopLevelDescriptor(1, desc.geometryOrInstanceNum);
    m_Sizes = m_Device.GetNativeObject()->accelerationStructureSizes(descriptor);
    descriptor->release();
    m_Size = desc.optimizedSize ? std::min<uint64_t>(m_Sizes.accelerationStructureSize, desc.optimizedSize) : m_Sizes.accelerationStructureSize;
    BufferDesc barrierDesc = {};
    barrierDesc.size = m_Size;
    m_BarrierBuffer.Create(barrierDesc);

    return desc.type == AccelerationStructureType::TOP_LEVEL ? CreateShaderBindingHeader(desc.geometryOrInstanceNum) : Result::SUCCESS;
}

Result AccelerationStructureMetal::Allocate(MemoryLocation memoryLocation, float, bool) {
    if (memoryLocation != MemoryLocation::DEVICE)
        return Result::UNSUPPORTED;

    Release();
    m_AccelerationStructure = m_Device.GetNativeObject()->newAccelerationStructure((NS::UInteger)m_Size);
    if (!m_AccelerationStructure)
        return Result::OUT_OF_MEMORY;

    m_Device.AddResidency(m_AccelerationStructure);
    m_IsResident = true;
    if (m_ShaderBindingHeader)
        ((uint64_t*)m_ShaderBindingHeader->contents())[0] = m_AccelerationStructure->gpuResourceID()._impl;

    return Result::SUCCESS;
}

Result AccelerationStructureMetal::Bind(MemoryMetal& memory, uint64_t offset) {
    if (memory.GetLocation() != MemoryLocation::DEVICE)
        return Result::UNSUPPORTED;

    Release();
    m_AccelerationStructure = memory.GetNativeObject()->newAccelerationStructure((NS::UInteger)m_Size, (NS::UInteger)offset);
    if (!m_AccelerationStructure)
        return Result::FAILURE;

    if (m_ShaderBindingHeader)
        ((uint64_t*)m_ShaderBindingHeader->contents())[0] = m_AccelerationStructure->gpuResourceID()._impl;

    return Result::SUCCESS;
}

void AccelerationStructureMetal::GetMemoryDesc(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const {
    const MTL::SizeAndAlign requirements = m_Device.GetNativeObject()->heapAccelerationStructureSizeAndAlign((NS::UInteger)m_Size);
    memoryDesc.size = requirements.size;
    memoryDesc.alignment = (uint32_t)requirements.align;
    memoryDesc.type = (MemoryType)memoryLocation;
    memoryDesc.mustBeDedicated = false;
}

void AccelerationStructureMetal::SetDebugName(const char* name) {
    NS::String* label = NS::String::string(name, NS::UTF8StringEncoding);
    if (m_AccelerationStructure)
        m_AccelerationStructure->setLabel(label);
    if (m_ShaderBindingHeader)
        m_ShaderBindingHeader->setLabel(label);
}

MTL4::AccelerationStructureDescriptor* AccelerationStructureMetal::CreateBuildDescriptor(const BottomLevelGeometryDesc* geometries, uint32_t geometryNum, MTL::GPUAddress convertedInstanceAddress, uint32_t instanceNum) const {
    return m_Type == AccelerationStructureType::BOTTOM_LEVEL ? CreateBottomLevelDescriptor(geometries, geometryNum, false) : CreateTopLevelDescriptor(convertedInstanceAddress, instanceNum);
}

} // namespace nri
