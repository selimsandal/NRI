// © 2026 NVIDIA Corporation

namespace nri {

PipelineLayoutMetal::PipelineLayoutMetal(DeviceMetal& device)
    : m_Device(device), m_Sets(device.GetStdAllocator()), m_ConstantOffsets(device.GetStdAllocator()), m_DescriptorOffsets(device.GetStdAllocator()), m_SetOffsets(device.GetStdAllocator()), m_RootSamplers(device.GetStdAllocator()) {
}

PipelineLayoutMetal::~PipelineLayoutMetal() {
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (m_RootSignature)
        IRRootSignatureDestroy(m_RootSignature);
#endif
    if (m_RootSamplerBuffer) {
        m_Device.RemoveResidency(m_RootSamplerBuffer);
        m_RootSamplerBuffer->release();
    }

    for (auto* sampler : m_RootSamplers)
        Destroy(sampler);
}

Result PipelineLayoutMetal::Create(const PipelineLayoutDesc& desc) {
    uint32_t offset = 0;

#if NRI_ENABLE_METAL_SHADER_CONVERTER
    Vector<IRRootParameter1> parameters(m_Device.GetStdAllocator());
    Vector<IRDescriptorRange1> ranges(m_Device.GetStdAllocator());
    uint32_t rangeNum = desc.rootSamplerNum;

    for (uint32_t i = 0; i < desc.descriptorSetNum; i++)
        rangeNum += desc.descriptorSets[i].rangeNum;

    ranges.reserve(rangeNum);

    const bool vertexStage = (desc.shaderStages & StageBits::VERTEX_SHADER) != 0;
    if ((desc.flags & PipelineLayoutBits::ENABLE_DRAW_PARAMETERS_EMULATION) && vertexStage) {
        m_DrawParametersOffset = offset;
        offset += 8;
        IRRootParameter1 parameter = {};
        parameter.ParameterType = IRRootParameterType32BitConstants;
        parameter.Constants = {0, 999, 2};
        parameter.ShaderVisibility = IRShaderVisibilityVertex;
        parameters.push_back(parameter);
    }

    if ((desc.flags & PipelineLayoutBits::ENABLE_DRAW_INDEX_EMULATION) && vertexStage) {
        m_DrawIndexOffset = offset;
        offset += 4;
        IRRootParameter1 parameter = {};
        parameter.ParameterType = IRRootParameterType32BitConstants;
        parameter.Constants = {1, 999, 1};
        parameter.ShaderVisibility = IRShaderVisibilityVertex;
        parameters.push_back(parameter);
    }
#endif

    for (uint32_t i = 0; i < desc.rootConstantNum; i++) {
        m_ConstantOffsets.push_back(offset);
        offset += desc.rootConstants[i].size;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        IRRootParameter1 parameter = {};
        parameter.ParameterType = IRRootParameterType32BitConstants;
        parameter.Constants = {desc.rootConstants[i].registerIndex, desc.rootRegisterSpace, desc.rootConstants[i].size / 4};
        parameter.ShaderVisibility = IRShaderVisibilityAll;
        parameters.push_back(parameter);
#endif
    }

    offset = (offset + 7) & ~7u;

    for (uint32_t i = 0; i < desc.rootDescriptorNum; i++) {
        m_DescriptorOffsets.push_back(offset);
        offset += 8;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        const auto& root = desc.rootDescriptors[i];
        IRRootParameter1 parameter = {};
        parameter.ParameterType = root.descriptorType == DescriptorType::CONSTANT_BUFFER ? IRRootParameterTypeCBV : (root.descriptorType == DescriptorType::STORAGE_STRUCTURED_BUFFER ? IRRootParameterTypeUAV : IRRootParameterTypeSRV);
        parameter.Descriptor = {root.registerIndex, desc.rootRegisterSpace, IRRootDescriptorFlagDataVolatile};
        parameter.ShaderVisibility = IRShaderVisibilityAll;
        parameters.push_back(parameter);
#endif
    }

    for (uint32_t i = 0; i < desc.descriptorSetNum; i++) {
        m_Sets.emplace_back(m_Device.GetStdAllocator());
        auto& mapping = m_Sets.back();
        const auto& set = desc.descriptorSets[i];

        for (uint32_t j = 0; j < set.rangeNum; j++) {
            const auto& range = set.ranges[j];
            DescriptorRangeMappingMetal m = {};
            m.sampler = range.descriptorType == DescriptorType::SAMPLER;
            m.offset = m.sampler ? mapping.samplerNum : mapping.resourceNum;
            m.descriptorNum = range.descriptorNum;
            m.type = range.descriptorType;

            if (m.sampler)
                mapping.samplerNum += range.descriptorNum;
            else
                mapping.resourceNum += range.descriptorNum;

            mapping.ranges.push_back(m);

            if (range.flags & DescriptorRangeBits::VARIABLE_SIZED_ARRAY)
                mapping.variableRange = j;
        }

        if (mapping.variableRange != UINT32_MAX) {
            auto& variable = mapping.ranges[mapping.variableRange];

            for (uint32_t j = mapping.variableRange + 1; j < set.rangeNum; j++) {
                if (mapping.ranges[j].sampler == variable.sampler)
                    mapping.ranges[j].offset -= variable.descriptorNum;
            }

            variable.offset = (variable.sampler ? mapping.samplerNum : mapping.resourceNum) - variable.descriptorNum;
        }

        // D3D root tables cannot mix resource and sampler descriptors. Each
        // nonempty group gets one pointer, in exactly the same order in the ABI.
        for (uint32_t sampler = 0; sampler < 2; sampler++) {
            bool present = false;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
            const size_t first = ranges.size();
#endif
            for (uint32_t j = 0; j < set.rangeNum; j++) {
                const auto& source = set.ranges[j];
                const auto& mappingRange = mapping.ranges[j];

                if (mappingRange.sampler != bool(sampler) || source.descriptorType == DescriptorType::MUTABLE)
                    continue;

                present = true;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
                IRDescriptorRange1 range = {};
                range.RangeType = sampler ? IRDescriptorRangeTypeSampler : (source.descriptorType == DescriptorType::CONSTANT_BUFFER ? IRDescriptorRangeTypeCBV : ((source.descriptorType == DescriptorType::STORAGE_BUFFER || source.descriptorType == DescriptorType::STORAGE_STRUCTURED_BUFFER || source.descriptorType == DescriptorType::STORAGE_TEXTURE) ? IRDescriptorRangeTypeUAV : IRDescriptorRangeTypeSRV));
                range.NumDescriptors = source.descriptorNum;
                range.BaseShaderRegister = source.baseRegisterIndex;
                range.RegisterSpace = set.registerSpace;
                range.OffsetInDescriptorsFromTableStart = mappingRange.offset;
                ranges.push_back(range);
#endif
            }

            m_SetOffsets.push_back(present ? offset : UINT32_MAX);

            if (present) {
                offset += 8;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
                IRRootParameter1 parameter = {};
                parameter.ParameterType = IRRootParameterTypeDescriptorTable;
                parameter.DescriptorTable = {(uint32_t)(ranges.size() - first), ranges.data() + first};
                parameter.ShaderVisibility = IRShaderVisibilityAll;
                parameters.push_back(parameter);
#endif
            }
        }
    }

    if (desc.rootSamplerNum) {
        m_RootSamplerOffset = offset;
        offset += 8;
        m_RootSamplerBuffer = m_Device.GetNativeObject()->newBuffer(uint64_t(desc.rootSamplerNum) * 24, MTL::ResourceStorageModeShared);

        if (!m_RootSamplerBuffer)
            return Result::OUT_OF_MEMORY;

        m_Device.AddResidency(m_RootSamplerBuffer);
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        const size_t first = ranges.size();
#endif
        for (uint32_t i = 0; i < desc.rootSamplerNum; i++) {
            DescriptorMetal* sampler = nullptr;
            Result result = m_Device.CreateImplementation<DescriptorMetal>(sampler, desc.rootSamplers[i].desc);

            if (result != Result::SUCCESS)
                return result;

            m_RootSamplers.push_back(sampler);
            sampler->WriteEntry((uint8_t*)m_RootSamplerBuffer->contents() + uint64_t(i) * 24);
#if NRI_ENABLE_METAL_SHADER_CONVERTER
            ranges.push_back({IRDescriptorRangeTypeSampler, 1, desc.rootSamplers[i].registerIndex, desc.rootRegisterSpace, IRDescriptorRangeFlagNone, i});
#endif
        }
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        IRRootParameter1 parameter = {};
        parameter.ParameterType = IRRootParameterTypeDescriptorTable;
        parameter.DescriptorTable = {desc.rootSamplerNum, ranges.data() + first};
        parameter.ShaderVisibility = IRShaderVisibilityAll;
        parameters.push_back(parameter);
#endif
    }

    m_RootDataSize = (offset + 15) & ~15u;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRVersionedRootSignatureDescriptor root = {};
    root.version = IRRootSignatureVersion_1_1;
    root.desc_1_1.NumParameters = (uint32_t)parameters.size();
    root.desc_1_1.pParameters = parameters.data();
    uint32_t flags = IRRootSignatureFlagAllowInputAssemblerInputLayout;

    if (desc.flags & PipelineLayoutBits::RESOURCE_HEAP_DIRECTLY_INDEXED)
        flags |= IRRootSignatureFlagCBVSRVUAVHeapDirectlyIndexed;

    if (desc.flags & PipelineLayoutBits::SAMPLER_HEAP_DIRECTLY_INDEXED)
        flags |= IRRootSignatureFlagSamplerHeapDirectlyIndexed;

    root.desc_1_1.Flags = (IRRootSignatureFlags)flags;
    IRError* error = nullptr;
    m_RootSignature = IRRootSignatureCreateFromDescriptor(&root, &error);

    if (error) {
        m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal root signature creation failed (converter error %u)", IRErrorGetCode(error));
        IRErrorDestroy(error);
    }

    if (!m_RootSignature)
        return Result::FAILURE;
#endif

    return Result::SUCCESS;
}

DeviceMetal& PipelineLayoutMetal::GetDevice() const {
    return m_Device;
}

uint32_t PipelineLayoutMetal::GetRootDataSize() const {
    return m_RootDataSize;
}

uint32_t PipelineLayoutMetal::GetRootConstantOffset(uint32_t index) const {
    return m_ConstantOffsets[index];
}

uint32_t PipelineLayoutMetal::GetRootDescriptorOffset(uint32_t index) const {
    return m_DescriptorOffsets[index];
}

uint32_t PipelineLayoutMetal::GetDrawParametersOffset() const {
    return m_DrawParametersOffset;
}

uint32_t PipelineLayoutMetal::GetDrawIndexOffset() const {
    return m_DrawIndexOffset;
}

bool PipelineLayoutMetal::IsDrawParametersEmulationEnabled() const {
    return m_DrawParametersOffset != UINT32_MAX;
}

bool PipelineLayoutMetal::IsDrawIndexEmulationEnabled() const {
    return m_DrawIndexOffset != UINT32_MAX;
}

void PipelineLayoutMetal::GetSetRootOffsets(uint32_t index, uint32_t& resource, uint32_t& sampler) const {
    resource = m_SetOffsets[2 * index];
    sampler = m_SetOffsets[2 * index + 1];
}

const DescriptorSetMappingMetal& PipelineLayoutMetal::GetDescriptorSetMapping(uint32_t index) const {
    return m_Sets[index];
}

void PipelineLayoutMetal::InitRootData(void* data) const {
    if (m_RootDataSize)
        memset(data, 0, m_RootDataSize);

    if (m_RootSamplerBuffer) {
        uint64_t address = m_RootSamplerBuffer->gpuAddress();
        memcpy((uint8_t*)data + m_RootSamplerOffset, &address, sizeof(address));
    }
}

void PipelineLayoutMetal::WriteSetPointers(void* data, uint32_t index, const DescriptorSetMetal& set) const {
    const uint64_t addresses[] = {set.GetResourceAddress(), set.GetSamplerAddress()};

    for (uint32_t i = 0; i < 2; i++) {
        uint32_t offset = m_SetOffsets[index * 2 + i];

        if (offset != UINT32_MAX)
            memcpy((uint8_t*)data + offset, &addresses[i], sizeof(uint64_t));
    }
}

#if NRI_ENABLE_METAL_SHADER_CONVERTER
IRRootSignature* PipelineLayoutMetal::GetRootSignature() const {
    return m_RootSignature;
}
#endif

} // namespace nri
