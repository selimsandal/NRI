// © 2026 NVIDIA Corporation

CommandBufferMetal::~CommandBufferMetal() {
    if (m_RenderPass)
        m_RenderPass->release();
    if (m_CounterFence)
        m_CounterFence->release();
    for (ClearPipelineMetal& clear : m_ClearPipelines) {
        clear.pipeline->release();
        clear.depthStencil->release();
    }
    for (ClearStoragePipelineMetal& clear : m_ClearStoragePipelines)
        clear.pipeline->release();
    if (m_ClearLibrary)
        m_ClearLibrary->release();
    if (m_ClearStorageLibrary)
        m_ClearStorageLibrary->release();
    if (m_ConvertInstances)
        m_ConvertInstances->release();
    if (m_CopyRayArguments)
        m_CopyRayArguments->release();
    if (m_FilterDrawArguments)
        m_FilterDrawArguments->release();
    if (m_EmulateDrawArguments)
        m_EmulateDrawArguments->release();
    if (m_PrepareDrawRoots)
        m_PrepareDrawRoots->release();
    if (m_Arguments)
        m_Arguments->release();
    if (m_ClearStorageArguments)
        m_ClearStorageArguments->release();
    if (m_CommandBuffer)
        m_CommandBuffer->release();
}

Result CommandBufferMetal::Create(const CommandAllocator& allocator) {
    m_Allocator = (CommandAllocatorMetal*)&allocator;
    m_CommandBuffer = m_Device.GetNativeObject()->newCommandBuffer();
    MTL4::ArgumentTableDescriptor* desc = MTL4::ArgumentTableDescriptor::alloc()->init();
    desc->setMaxBufferBindCount(31);
    desc->setMaxTextureBindCount(1);
    desc->setInitializeBindings(true);
    desc->setSupportAttributeStrides(true);
    m_Arguments = m_Device.GetNativeObject()->newArgumentTable(desc, nullptr);
    m_ClearStorageArguments = m_Device.GetNativeObject()->newArgumentTable(desc, nullptr);
    desc->release();

    return m_CommandBuffer && m_Arguments && m_ClearStorageArguments ? Result::SUCCESS : Result::OUT_OF_MEMORY;
}

Result CommandBufferMetal::Begin(const DescriptorPool* pool) {
    m_Result = Result::SUCCESS;
    m_DrawRootAddress = 0;
    m_Pipeline = nullptr;
    m_RenderEncoder = nullptr;
    m_ComputeEncoder = nullptr;
    m_DescriptorPool = (DescriptorPoolMetal*)pool;
    m_Graphics.layout = nullptr;
    m_Compute.layout = nullptr;
    m_BindPoint = BindPoint::GRAPHICS;
    m_ViewportNum = m_ScissorNum = 0;
    m_HasDepthBias = false;
    m_DepthMin = 0.0f;
    m_DepthMax = 1.0f;
    m_SamplePositionNum = 0;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    memset(m_EmulationVertexBuffers, 0, sizeof(m_EmulationVertexBuffers));
#endif
    m_CommandBuffer->beginCommandBuffer(m_Allocator->GetNativeObject());
    m_CommandBuffer->useResidencySet(m_Device.GetResidencySet());

    return Result::SUCCESS;
}

Result CommandBufferMetal::End() {
    if (m_ComputeEncoder) {
        m_ComputeEncoder->endEncoding();
        m_ComputeEncoder = nullptr;
    }

    m_CommandBuffer->endCommandBuffer();
    return m_Result;
}

void CommandBufferMetal::RecordFailure(Result result) {
    if (m_Result == Result::SUCCESS)
        m_Result = result;
}

void CommandBufferMetal::CmdSetDescriptorPool(const DescriptorPool& pool) {
    m_DescriptorPool = (DescriptorPoolMetal*)&pool;
}

void CommandBufferMetal::CmdSetPipelineLayout(BindPoint bindPoint, const PipelineLayout& layout) {
    m_BindPoint = bindPoint;
    State& state = bindPoint == BindPoint::GRAPHICS ? m_Graphics : m_Compute;
    state.layout = (const PipelineLayoutMetal*)&layout;
    state.root.resize(state.layout->GetRootDataSize());
    state.layout->InitRootData(state.root.data());
}

void CommandBufferMetal::CmdSetDescriptorSet(const SetDescriptorSetDesc& desc) {
    State& s = GetState(desc.bindPoint);
    s.layout->WriteSetPointers(s.root.data(), desc.setIndex, *(DescriptorSetMetal*)desc.descriptorSet);
}

void CommandBufferMetal::CmdSetRootConstants(const SetRootConstantsDesc& desc) {
    State& s = GetState(desc.bindPoint);
    memcpy(s.root.data() + s.layout->GetRootConstantOffset(desc.rootConstantIndex) + desc.offset, desc.data, desc.size);
}

void CommandBufferMetal::CmdSetRootDescriptor(const SetRootDescriptorDesc& desc) {
    State& s = GetState(desc.bindPoint);
    uint64_t address = ((DescriptorMetal*)desc.descriptor)->GetBuffer()->gpuAddress() + ((DescriptorMetal*)desc.descriptor)->GetBufferOffset() + desc.offset;
    memcpy(s.root.data() + s.layout->GetRootDescriptorOffset(desc.rootDescriptorIndex), &address, sizeof(address));
}

CommandBufferMetal::State& CommandBufferMetal::GetState(BindPoint bindPoint) {
    if (bindPoint == BindPoint::INHERIT)
        bindPoint = m_BindPoint;

    return bindPoint == BindPoint::GRAPHICS ? m_Graphics : m_Compute;
}

void CommandBufferMetal::CmdSetPipeline(const Pipeline& pipeline) {
    const bool hadSampleLocations = m_Pipeline && m_Pipeline->HasSampleLocations();
    m_Pipeline = (const PipelineMetal*)&pipeline;
    m_RenderPipelineDirty = true;

    if (m_RenderEncoder && hadSampleLocations != m_Pipeline->HasSampleLocations()) {
        SuspendRendering();
        m_RenderPass->setSamplePositions(m_SamplePositions, m_Pipeline->HasSampleLocations() ? m_SamplePositionNum : 0);
        ResumeRendering();
    }
}

static inline MTL::Stages GetBarrierStagesMetal(StageBits stages) {
    if (stages == StageBits::ALL)
        return MTL::StageAll;

    if (stages == StageBits::NONE)
        return 0;

    MTL::Stages result = 0;

    if (stages & (StageBits::INDEX_INPUT | StageBits::VERTEX_SHADER | StageBits::TESSELLATION_SHADERS | StageBits::GEOMETRY_SHADER))
        result |= MTL::StageVertex | MTL::StageObject | MTL::StageMesh;

    if (stages & StageBits::TASK_SHADER)
        result |= MTL::StageObject;

    if (stages & StageBits::MESH_SHADER)
        result |= MTL::StageMesh;

    if (stages & (StageBits::FRAGMENT_SHADER | StageBits::DEPTH_STENCIL_ATTACHMENT | StageBits::COLOR_ATTACHMENT | StageBits::SHADING_RATE_ATTACHMENT | StageBits::RESOLVE))
        result |= MTL::StageFragment;

    if (stages & (StageBits::COMPUTE_SHADER | StageBits::RAY_TRACING_SHADERS | StageBits::CLEAR_STORAGE))
        result |= MTL::StageDispatch;

    if (stages & StageBits::ACCELERATION_STRUCTURE)
        result |= MTL::StageAccelerationStructure;

    if (stages & StageBits::COPY)
        result |= MTL::StageBlit;

    if (stages & StageBits::INDIRECT)
        result |= MTL::StageVertex | MTL::StageObject | MTL::StageMesh | MTL::StageDispatch;

    return result;
}

void CommandBufferMetal::CmdBarrier(const BarrierDesc& desc) {
    MTL::Stages before = 0, after = 0;
    auto accumulate = [&](StageBits source, StageBits destination) {
        before |= GetBarrierStagesMetal(source);
        after |= GetBarrierStagesMetal(destination);
    };

    for (uint32_t i = 0; i < desc.globalNum; i++)
        accumulate(desc.globals[i].before.stages, desc.globals[i].after.stages);

    for (uint32_t i = 0; i < desc.bufferNum; i++)
        accumulate(desc.buffers[i].before.stages, desc.buffers[i].after.stages);

    for (uint32_t i = 0; i < desc.textureNum; i++)
        accumulate(desc.textures[i].before.stages, desc.textures[i].after.stages);

    if (!before || !after)
        return;

    MTL4::CommandEncoder* encoder = m_RenderEncoder ? (MTL4::CommandEncoder*)m_RenderEncoder : (MTL4::CommandEncoder*)BeginCompute();
    const MTL::Stages supported = m_RenderEncoder ? MTL::StageVertex | MTL::StageFragment | MTL::StageTile | MTL::StageObject | MTL::StageMesh : MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure;
    // Queue barriers exclude commands in this encoder. Cover that scope with
    // an encoder barrier, and publish writes to subsequent encoders separately.
    encoder->barrierAfterQueueStages(before, after, MTL4::VisibilityOptionDevice);

    if ((before & supported) && (after & supported))
        encoder->barrierAfterEncoderStages(before & supported, after & supported, MTL4::VisibilityOptionDevice);

    encoder->barrierAfterStages(before, after, MTL4::VisibilityOptionDevice);
}

void CommandBufferMetal::CmdSetIndexBuffer(const Buffer& buffer, uint64_t offset, IndexType type) {
    const BufferMetal& b = (const BufferMetal&)buffer;
    m_IndexAddress = b.GetGpuAddress() + offset;
    m_IndexLength = b.GetDesc().size - offset;
    m_IndexType = type == IndexType::UINT16 ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32;
}

void CommandBufferMetal::CmdSetVertexBuffers(uint32_t base, const VertexBufferDesc* descs, uint32_t num) {
    for (uint32_t i = 0; i < num; i++) {
        const BufferMetal& buffer = *(BufferMetal*)descs[i].buffer;
        m_Arguments->setAddress(buffer.GetGpuAddress() + descs[i].offset, descs[i].stride, 6 + base + i);
#if NRI_ENABLE_METAL_SHADER_CONVERTER
        m_EmulationVertexBuffers[base + i] = {buffer.GetGpuAddress() + descs[i].offset, (uint32_t)std::min<uint64_t>(buffer.GetDesc().size - descs[i].offset, UINT32_MAX), descs[i].stride};
#endif
    }
}

void CommandBufferMetal::CmdSetViewports(const Viewport* v, uint32_t n) {
    m_ViewportNum = n;
    for (uint32_t i = 0; i < n; i++)
        m_Viewports[i] = {v[i].x, v[i].y, v[i].width, v[i].height, v[i].depthMin, v[i].depthMax};

    if (m_RenderEncoder)
        m_RenderEncoder->setViewports(m_Viewports, n);
}

static inline MTL::ScissorRect GetScissorRectMetal(const nri::Rect& rect) {
    const int32_t x = std::max<int32_t>(0, rect.x);
    const int32_t y = std::max<int32_t>(0, rect.y);

    return {(NS::UInteger)x, (NS::UInteger)y, (NS::UInteger)std::max<int32_t>(0, int32_t(rect.x) + rect.width - x), (NS::UInteger)std::max<int32_t>(0, int32_t(rect.y) + rect.height - y)};
}

void CommandBufferMetal::CmdSetScissors(const Rect* r, uint32_t n) {
    m_ScissorNum = n;
    for (uint32_t i = 0; i < n; i++)
        m_Scissors[i] = GetScissorRectMetal(r[i]);

    if (m_RenderEncoder)
        m_RenderEncoder->setScissorRects(m_Scissors, n);
}

void CommandBufferMetal::CmdSetStencilReference(uint8_t f, uint8_t b) {
    m_FrontStencil = f;
    m_BackStencil = b;

    if (m_RenderEncoder)
        m_RenderEncoder->setStencilReferenceValues(f, b);
}

void CommandBufferMetal::CmdSetDepthBounds(float min, float max) {
    m_DepthMin = min;
    m_DepthMax = max;

    if (m_RenderEncoder && m_Pipeline && m_Pipeline->IsDepthBoundsEnabled())
        m_RenderEncoder->setDepthTestBounds(min, max);
}

void CommandBufferMetal::CmdSetBlendConstants(const Color32f& c) {
    m_BlendColor = c;

    if (m_RenderEncoder)
        m_RenderEncoder->setBlendColor(c.x, c.y, c.z, c.w);
}

void CommandBufferMetal::CmdSetSampleLocations(const SampleLocation* locations, Sample_t locationNum, Sample_t sampleNum) {
    NRI_CHECK(locationNum == sampleNum && locationNum <= 16, "Metal supports one sample-location pattern per pixel");
    m_SamplePositionNum = locationNum;

    for (uint32_t i = 0; i < locationNum; i++)
        m_SamplePositions[i] = {(locations[i].x + 8) / 16.0f, (locations[i].y + 8) / 16.0f};

    if (m_RenderEncoder && m_Pipeline && m_Pipeline->HasSampleLocations()) {
        SuspendRendering();
        m_RenderPass->setSamplePositions(m_SamplePositions, m_SamplePositionNum);
        ResumeRendering();
    }
}

void CommandBufferMetal::CmdSetShadingRate(const ShadingRateDesc&) {
    RecordFailure(Result::UNSUPPORTED);
}

void CommandBufferMetal::CmdSetDepthBias(const DepthBiasDesc& d) {
    m_DepthBias = d;
    m_HasDepthBias = true;

    if (m_RenderEncoder)
        m_RenderEncoder->setDepthBias(d.constant, d.slope, d.clamp);
}

void CommandBufferMetal::CmdBeginRendering(const RenderingDesc& desc) {
    if (m_ComputeEncoder) {
        m_ComputeEncoder->endEncoding();
        m_ComputeEncoder = nullptr;
    }

    MTL4::RenderPassDescriptor* pass = MTL4::RenderPassDescriptor::alloc()->init();
    m_RenderColorNum = (uint8_t)desc.colorNum;
    m_ViewMask = desc.viewMask;
    uint32_t layerNum = UINT32_MAX;
    m_RenderDepth = m_RenderStencil = MTL::PixelFormatInvalid;
    m_RenderWidth = m_RenderHeight = UINT32_MAX;
    m_RenderSampleNum = 1;
    for (uint32_t i = 0; i < desc.colorNum; i++) {
        const AttachmentDesc& a = desc.colors[i];
        const DescriptorMetal& d = *(DescriptorMetal*)a.descriptor;
        auto* n = pass->colorAttachments()->object(i);
        n->setTexture(d.GetTexture());
        m_RenderColors[i] = d.GetTexture()->pixelFormat();
        m_RenderWidth = std::min(m_RenderWidth, std::max(1u, (uint32_t)d.GetTexture()->width() >> d.GetTextureViewDesc().mipOffset));
        m_RenderHeight = std::min(m_RenderHeight, std::max(1u, (uint32_t)d.GetTexture()->height() >> d.GetTextureViewDesc().mipOffset));
        m_RenderSampleNum = (uint8_t)d.GetTexture()->sampleCount();
        n->setLevel(d.GetTextureViewDesc().mipOffset);
        n->setSlice(d.GetTextureViewDesc().layerOffset);
        n->setDepthPlane(d.GetTextureViewDesc().sliceOffset);
        layerNum = std::min(layerNum, d.GetTextureViewDesc().layerNum == REMAINING ? uint32_t(d.GetTexture()->arrayLength()) - d.GetTextureViewDesc().layerOffset : uint32_t(d.GetTextureViewDesc().layerNum));
        n->setLoadAction(a.loadOp == LoadOp::CLEAR ? MTL::LoadActionClear : MTL::LoadActionLoad);
        n->setStoreAction(a.storeOp == StoreOp::STORE ? MTL::StoreActionStore : MTL::StoreActionDontCare);
        const FormatProps& props = GetFormatProps(d.GetTextureViewDesc().format);

        if (props.isInteger && props.isSigned)
            n->setClearColor(MTL::ClearColor(a.clearValue.color.i.x, a.clearValue.color.i.y, a.clearValue.color.i.z, a.clearValue.color.i.w));
        else if (props.isInteger)
            n->setClearColor(MTL::ClearColor(a.clearValue.color.ui.x, a.clearValue.color.ui.y, a.clearValue.color.ui.z, a.clearValue.color.ui.w));
        else
            n->setClearColor(MTL::ClearColor(a.clearValue.color.f.x, a.clearValue.color.f.y, a.clearValue.color.f.z, a.clearValue.color.f.w));

        if (a.resolveDst) {
            const DescriptorMetal& resolve = *(DescriptorMetal*)a.resolveDst;
            n->setResolveTexture(resolve.GetTexture());
            n->setResolveLevel(resolve.GetTextureViewDesc().mipOffset);
            n->setResolveSlice(resolve.GetTextureViewDesc().layerOffset);
            n->setStoreAction(MTL::StoreActionStoreAndMultisampleResolve);
        }
    }
    auto setDepthStencil = [&](const AttachmentDesc& a, bool depth) {
        if (!a.descriptor)
            return;
        const DescriptorMetal& d = *(DescriptorMetal*)a.descriptor;
        MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = pass->depthAttachment();
        MTL::RenderPassStencilAttachmentDescriptor* stencilAttachment = pass->stencilAttachment();
        auto* n = depth ? (MTL::RenderPassAttachmentDescriptor*)depthAttachment : (MTL::RenderPassAttachmentDescriptor*)stencilAttachment;
        n->setTexture(d.GetTexture());
        n->setLevel(d.GetTextureViewDesc().mipOffset);
        n->setSlice(d.GetTextureViewDesc().layerOffset);
        n->setLoadAction(a.loadOp == LoadOp::CLEAR ? MTL::LoadActionClear : MTL::LoadActionLoad);
        layerNum = std::min(layerNum, d.GetTextureViewDesc().layerNum == REMAINING ? uint32_t(d.GetTexture()->arrayLength()) - d.GetTextureViewDesc().layerOffset : uint32_t(d.GetTextureViewDesc().layerNum));
        n->setStoreAction(a.storeOp == StoreOp::STORE ? MTL::StoreActionStore : MTL::StoreActionDontCare);
        if (depth)
            depthAttachment->setClearDepth(a.clearValue.depthStencil.depth);
        else
            stencilAttachment->setClearStencil(a.clearValue.depthStencil.stencil);
        if (a.resolveDst) {
            // Sample-zero stencil resolves do not implement any NRI resolve operation.
            if (!depth || a.resolveOp == ResolveOp::AVERAGE) {
                RecordFailure(Result::UNSUPPORTED);

                return;
            }

            const DescriptorMetal& resolve = *(DescriptorMetal*)a.resolveDst;
            n->setResolveTexture(resolve.GetTexture());
            n->setResolveLevel(resolve.GetTextureViewDesc().mipOffset);
            n->setResolveSlice(resolve.GetTextureViewDesc().layerOffset);
            n->setStoreAction(MTL::StoreActionStoreAndMultisampleResolve);
            if (depth)
                depthAttachment->setDepthResolveFilter(a.resolveOp == ResolveOp::MIN ? MTL::MultisampleDepthResolveFilterMin : (a.resolveOp == ResolveOp::MAX ? MTL::MultisampleDepthResolveFilterMax : MTL::MultisampleDepthResolveFilterSample0));
            else
                stencilAttachment->setStencilResolveFilter(MTL::MultisampleStencilResolveFilterSample0);
        }
        if (depth)
            m_RenderDepth = d.GetTexture()->pixelFormat();
        else
            m_RenderStencil = d.GetTexture()->pixelFormat();
        m_RenderWidth = std::min(m_RenderWidth, std::max(1u, (uint32_t)d.GetTexture()->width() >> d.GetTextureViewDesc().mipOffset));
        m_RenderHeight = std::min(m_RenderHeight, std::max(1u, (uint32_t)d.GetTexture()->height() >> d.GetTextureViewDesc().mipOffset));
        m_RenderSampleNum = (uint8_t)d.GetTexture()->sampleCount();
    };
    if (desc.depth.descriptor) {
        const MTL::PixelFormat format = ((DescriptorMetal*)desc.depth.descriptor)->GetTexture()->pixelFormat();
        if (format != MTL::PixelFormatStencil8)
            setDepthStencil(desc.depth, true);
        if (!desc.stencil.descriptor && (format == MTL::PixelFormatDepth24Unorm_Stencil8 || format == MTL::PixelFormatDepth32Float_Stencil8))
            setDepthStencil(desc.depth, false);
    }
    if (desc.stencil.descriptor)
        setDepthStencil(desc.stencil, false);
    pass->setRenderTargetArrayLength(layerNum == UINT32_MAX ? 1 : layerNum);
    for (uint32_t i = 0; i < m_RenderColorNum; i++) {
        auto* attachment = pass->colorAttachments()->object(i);
        m_RenderStore[i] = attachment->storeAction();
        attachment->setStoreAction(MTL::StoreActionUnknown);
    }
    m_DepthStore = pass->depthAttachment()->storeAction();
    m_StencilStore = pass->stencilAttachment()->storeAction();
    if (m_RenderDepth != MTL::PixelFormatInvalid)
        pass->depthAttachment()->setStoreAction(MTL::StoreActionUnknown);
    if (m_RenderStencil != MTL::PixelFormatInvalid)
        pass->stencilAttachment()->setStoreAction(MTL::StoreActionUnknown);
    m_RenderPass = pass;
    m_VisibilityMode = MTL::VisibilityResultModeDisabled;
    m_VisibilityOffset = 0;
    pass->setSamplePositions(m_SamplePositions, m_Pipeline && m_Pipeline->HasSampleLocations() ? m_SamplePositionNum : 0);
    m_RenderEncoder = m_CommandBuffer->renderCommandEncoder(pass);
    m_ComputeEncoder = nullptr;
    m_RenderPipelineDirty = true;

    ApplyRasterState();
}

void CommandBufferMetal::ApplyRasterState() {
    if (m_ViewportNum)
        m_RenderEncoder->setViewports(m_Viewports, m_ViewportNum);

    if (m_ScissorNum)
        m_RenderEncoder->setScissorRects(m_Scissors, m_ScissorNum);

    m_RenderEncoder->setStencilReferenceValues(m_FrontStencil, m_BackStencil);
    m_RenderEncoder->setBlendColor(m_BlendColor.x, m_BlendColor.y, m_BlendColor.z, m_BlendColor.w);
}

void CommandBufferMetal::BindArguments(BindPoint point) {
    if (point == BindPoint::GRAPHICS && m_RenderPipelineDirty && m_Pipeline) {
        MTL::VertexAmplificationViewMapping mappings[32] = {};
        uint32_t viewIndices[32] = {};
        uint32_t count = 0;
        const Multiview multiview = m_Pipeline->GetMultiview();
        uint32_t mask = multiview == Multiview::LAYER_BASED ? m_Pipeline->GetViewMask() : m_ViewMask;
        for (uint32_t view = 0; mask; view++, mask >>= 1) {
            if (mask & 1) {
                viewIndices[count] = view;
                mappings[count].renderTargetArrayIndexOffset = multiview == Multiview::LAYER_BASED ? view : 0;
                mappings[count].viewportArrayIndexOffset = multiview == Multiview::VIEWPORT_BASED ? view : 0;
                count++;
            }
        }
        m_RenderEncoder->setVertexAmplificationCount(std::max(1u, count), mappings);
        if (count && !m_Pipeline->IsConverted())
            m_Arguments->setAddress(m_Allocator->Upload(viewIndices, sizeof(viewIndices)), 3);
    }
    // A previous pass's pipeline may not match the new attachments. Bind only
    // when drawing, after the caller has selected the pipeline for this pass.
    if (point == BindPoint::GRAPHICS && m_RenderPipelineDirty && m_Pipeline) {
        m_RenderEncoder->setRenderPipelineState(m_Pipeline->GetRenderPipeline());
        m_RenderEncoder->setDepthStencilState(m_Pipeline->GetDepthStencilState());
        m_RenderEncoder->setCullMode(m_Pipeline->GetCullMode());
        m_RenderEncoder->setFrontFacingWinding(m_Pipeline->GetWinding());
        m_RenderEncoder->setTriangleFillMode(m_Pipeline->GetFillMode());
        m_RenderEncoder->setDepthClipMode(m_Pipeline->GetDepthClipMode());

        if (m_Device.GetDesc().features.depthBoundsTest)
            m_RenderEncoder->setDepthTestBounds(m_Pipeline->IsDepthBoundsEnabled() ? m_DepthMin : 0.0f, m_Pipeline->IsDepthBoundsEnabled() ? m_DepthMax : 1.0f);

        const auto& bias = m_HasDepthBias ? m_DepthBias : m_Pipeline->GetDepthBias();
        m_RenderEncoder->setDepthBias(bias.constant, bias.slope, bias.clamp);
        m_RenderPipelineDirty = false;
    }

    State& s = point == BindPoint::GRAPHICS ? m_Graphics : m_Compute;
    MTL::GPUAddress root = m_DrawRootAddress;
    m_DrawRootAddress = 0;
    if (s.layout && !s.root.empty())
        root = root ? root : m_Allocator->Upload(s.root.data(), s.root.size());
    if (m_DescriptorPool) {
        m_Arguments->setAddress(m_DescriptorPool->GetResourceHeapAddress(), 0);
        m_Arguments->setAddress(m_DescriptorPool->GetSamplerHeapAddress(), 1);
    }
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (point == BindPoint::GRAPHICS && m_Pipeline && m_Pipeline->IsTessellationEmulation()) {
        const uint64_t empty = 0;

        if (!root)
            root = m_Allocator->Upload(&empty, sizeof(empty));

        m_Arguments->setAddress(root, kIRArgumentBufferHullDomainBindPoint);

        if (!m_DescriptorPool || !m_DescriptorPool->GetResourceHeapAddress())
            m_Arguments->setAddress(root, kIRDescriptorHeapBindPoint);

        if (!m_DescriptorPool || !m_DescriptorPool->GetSamplerHeapAddress())
            m_Arguments->setAddress(root, kIRSamplerHeapBindPoint);
    }
#endif
    if (root)
        m_Arguments->setAddress(root, 2);
    if (point == BindPoint::GRAPHICS)
        m_RenderEncoder->setArgumentTable(m_Arguments, MTL::RenderStageVertex | MTL::RenderStageObject | MTL::RenderStageMesh | MTL::RenderStageFragment);
    else
        BeginCompute()->setArgumentTable(m_Arguments);
}

ClearPipelineMetal* CommandBufferMetal::GetClearPipeline(uint32_t colorIndex, PlaneBits planes, bool isInteger, bool isSigned) {
    for (ClearPipelineMetal& clear : m_ClearPipelines) {
        bool equal = clear.depth == m_RenderDepth && clear.stencil == m_RenderStencil && clear.colorNum == m_RenderColorNum && clear.colorIndex == colorIndex && clear.sampleNum == m_RenderSampleNum && clear.planes == planes && clear.isInteger == isInteger && clear.isSigned == isSigned;
        for (uint32_t i = 0; equal && i < m_RenderColorNum; i++)
            equal = clear.colors[i] == m_RenderColors[i];
        if (equal)
            return &clear;
    }
    if (!m_ClearLibrary) {
        std::string source = "#include <metal_stdlib>\nusing namespace metal;\nstruct C { float4 f; float depth; };\nstruct V { float4 position [[position]]; uint layer [[render_target_array_index]]; };\nvertex V clear_vs(uint i [[vertex_id]], uint layer [[instance_id]], constant C& c [[buffer(3)]]) { float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)}; return {float4(p[i], c.depth, 1), layer}; }\n";
        const char* types[] = {"float4", "uint4", "int4"};
        const char* fields[] = {"c.f", "as_type<uint4>(c.f)", "as_type<int4>(c.f)"};
        for (uint32_t type = 0; type < 3; type++) {
            for (uint32_t index = 0; index < 8; index++) {
                char function[256] = {};
                snprintf(function, sizeof(function), "struct O_%u_%u { %s value [[color(%u)]]; }; fragment O_%u_%u clear_%u_%u(constant C& c [[buffer(3)]]) { return {%s}; }\n", type, index, types[type], index, type, index, type, index, fields[type]);
                source += function;
            }
        }
        NS::Error* error = nullptr;
        m_ClearLibrary = m_Device.GetNativeObject()->newLibrary(NS::String::string(source.c_str(), NS::UTF8StringEncoding), nullptr, &error);

        if (!m_ClearLibrary) {
            m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal clear shader compilation failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");

            return nullptr;
        }
    }
    MTL::RenderPipelineDescriptor* pd = MTL::RenderPipelineDescriptor::alloc()->init();
    MTL::Function* vertex = m_ClearLibrary->newFunction(NS::String::string("clear_vs", NS::UTF8StringEncoding));
    char name[32] = {};
    snprintf(name, sizeof(name), "clear_%u_%u", isInteger ? (isSigned ? 2 : 1) : 0, colorIndex);
    MTL::Function* fragment = m_ClearLibrary->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
    pd->setVertexFunction(vertex);
    pd->setInputPrimitiveTopology(MTL::PrimitiveTopologyClassTriangle);
    if (planes & PlaneBits::COLOR)
        pd->setFragmentFunction(fragment);
    pd->setSampleCount(m_RenderSampleNum);
    for (uint32_t i = 0; i < m_RenderColorNum; i++) {
        auto* attachment = pd->colorAttachments()->object(i);
        attachment->setPixelFormat(m_RenderColors[i]);
        attachment->setWriteMask(i == colorIndex && (planes & PlaneBits::COLOR) ? MTL::ColorWriteMaskAll : MTL::ColorWriteMaskNone);
    }
    pd->setDepthAttachmentPixelFormat(m_RenderDepth);
    pd->setStencilAttachmentPixelFormat(m_RenderStencil);
    NS::Error* error = nullptr;
    ClearPipelineMetal clear = {};
    clear.pipeline = m_Device.GetNativeObject()->newRenderPipelineState(pd, &error);
    MTL::DepthStencilDescriptor* dd = MTL::DepthStencilDescriptor::alloc()->init();
    dd->setDepthCompareFunction(MTL::CompareFunctionAlways);
    dd->setDepthWriteEnabled(planes & PlaneBits::DEPTH);
    MTL::StencilDescriptor* stencil = MTL::StencilDescriptor::alloc()->init();
    stencil->setStencilCompareFunction(MTL::CompareFunctionAlways);
    stencil->setDepthStencilPassOperation(planes & PlaneBits::STENCIL ? MTL::StencilOperationReplace : MTL::StencilOperationKeep);
    stencil->setWriteMask(planes & PlaneBits::STENCIL ? 0xFF : 0);
    dd->setFrontFaceStencil(stencil);
    dd->setBackFaceStencil(stencil);
    clear.depthStencil = m_Device.GetNativeObject()->newDepthStencilState(dd);
    stencil->release();
    dd->release();
    fragment->release();
    vertex->release();
    pd->release();
    if (!clear.pipeline || !clear.depthStencil) {
        if (clear.pipeline)
            clear.pipeline->release();
        if (clear.depthStencil)
            clear.depthStencil->release();
        m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal clear pipeline creation failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");
        return nullptr;
    }
    memcpy(clear.colors, m_RenderColors, sizeof(clear.colors));
    clear.depth = m_RenderDepth;
    clear.stencil = m_RenderStencil;
    clear.colorNum = m_RenderColorNum;
    clear.colorIndex = (uint8_t)colorIndex;
    clear.sampleNum = m_RenderSampleNum;
    clear.planes = planes;
    clear.isInteger = isInteger;
    clear.isSigned = isSigned;
    m_ClearPipelines.push_back(clear);
    return &m_ClearPipelines.back();
}

void CommandBufferMetal::CmdClearAttachments(const ClearAttachmentDesc* clears, uint32_t clearNum, const Rect* rects, uint32_t rectNum) {
    for (uint32_t i = 0; i < clearNum; i++) {
        const ClearAttachmentDesc& desc = clears[i];
        PlaneBits planes = desc.planes;
        bool integer = false, signedInteger = false;
        if (planes == PlaneBits::ALL) {
            if (desc.colorAttachmentIndex < m_RenderColorNum)
                planes = PlaneBits::COLOR;
            else
                planes = (PlaneBits)((m_RenderDepth != MTL::PixelFormatInvalid ? (uint8_t)PlaneBits::DEPTH : 0) | (m_RenderStencil != MTL::PixelFormatInvalid ? (uint8_t)PlaneBits::STENCIL : 0));
        }
        if (planes & PlaneBits::COLOR) {
            MTL::PixelFormat pixel = m_RenderColors[desc.colorAttachmentIndex];
            integer = pixel == MTL::PixelFormatR8Uint || pixel == MTL::PixelFormatRG8Uint || pixel == MTL::PixelFormatRGBA8Uint || pixel == MTL::PixelFormatR16Uint || pixel == MTL::PixelFormatRG16Uint || pixel == MTL::PixelFormatRGBA16Uint || pixel == MTL::PixelFormatR32Uint || pixel == MTL::PixelFormatRG32Uint || pixel == MTL::PixelFormatRGBA32Uint || pixel == MTL::PixelFormatRGB10A2Uint;
            signedInteger = pixel == MTL::PixelFormatR8Sint || pixel == MTL::PixelFormatRG8Sint || pixel == MTL::PixelFormatRGBA8Sint || pixel == MTL::PixelFormatR16Sint || pixel == MTL::PixelFormatRG16Sint || pixel == MTL::PixelFormatRGBA16Sint || pixel == MTL::PixelFormatR32Sint || pixel == MTL::PixelFormatRG32Sint || pixel == MTL::PixelFormatRGBA32Sint;
            integer |= signedInteger;
        }
        ClearPipelineMetal* clear = GetClearPipeline(desc.colorAttachmentIndex, planes, integer, signedInteger);
        if (!clear) {
            RecordFailure(Result::FAILURE);
            continue;
        }

        struct ClearConstants {
            Color32f color;
            float depth;
        } constants = {};

        memcpy(&constants.color, &desc.value.color, sizeof(constants.color));
        constants.depth = planes & PlaneBits::DEPTH ? desc.value.depthStencil.depth : 0.0f;
        m_Arguments->setAddress(m_Allocator->Upload(&constants, sizeof(constants)), 3);
        m_RenderEncoder->setArgumentTable(m_Arguments, MTL::RenderStageVertex | MTL::RenderStageFragment);
        m_RenderEncoder->setRenderPipelineState(clear->pipeline);
        m_RenderEncoder->setVertexAmplificationCount(1, nullptr);
        m_RenderEncoder->setDepthStencilState(clear->depthStencil);
        m_RenderEncoder->setCullMode(MTL::CullModeNone);
        m_RenderEncoder->setTriangleFillMode(MTL::TriangleFillModeFill);
        m_RenderEncoder->setDepthBias(0.0f, 0.0f, 0.0f);
        m_RenderEncoder->setDepthClipMode(MTL::DepthClipModeClamp);

        if (m_Device.GetDesc().features.depthBoundsTest)
            m_RenderEncoder->setDepthTestBounds(0.0f, 1.0f);

        m_RenderEncoder->setStencilReferenceValue(desc.value.depthStencil.stencil);
        MTL::Viewport viewport = {0.0, 0.0, (double)m_RenderWidth, (double)m_RenderHeight, 0.0, 1.0};
        m_RenderEncoder->setViewport(viewport);
        if (rectNum) {
            for (uint32_t j = 0; j < rectNum; j++) {
                MTL::ScissorRect scissor = GetScissorRectMetal(rects[j]);
                m_RenderEncoder->setScissorRect(scissor);
                m_RenderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, 0, 3, m_RenderPass->renderTargetArrayLength());
            }
        } else {
            MTL::ScissorRect scissor = {0, 0, m_RenderWidth, m_RenderHeight};
            m_RenderEncoder->setScissorRect(scissor);
            m_RenderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, 0, 3, m_RenderPass->renderTargetArrayLength());
        }
    }
    m_RenderPipelineDirty = true;
    ApplyRasterState();
}

void CommandBufferMetal::CmdDraw(const DrawDesc& d) {
    SetDrawArguments(&d, sizeof(d), false);
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (m_Pipeline->IsGeometryEmulation() || m_Pipeline->IsTessellationEmulation()) {
        DrawEmulated(&d, sizeof(d), false, d.vertexNum, d.instanceNum);

        return;
    }
#endif
    BindArguments(BindPoint::GRAPHICS);
    m_RenderEncoder->drawPrimitives(m_Pipeline->GetPrimitiveType(), d.baseVertex, d.vertexNum, d.instanceNum, d.baseInstance);
}

void CommandBufferMetal::CmdDrawIndexed(const DrawIndexedDesc& d) {
    SetDrawArguments(&d, sizeof(d), true);
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (m_Pipeline->IsGeometryEmulation() || m_Pipeline->IsTessellationEmulation()) {
        DrawEmulated(&d, sizeof(d), true, d.indexNum, d.instanceNum);

        return;
    }
#endif
    BindArguments(BindPoint::GRAPHICS);
    uint64_t o = uint64_t(d.baseIndex) * (m_IndexType == MTL::IndexTypeUInt16 ? 2 : 4);
    m_RenderEncoder->drawIndexedPrimitives(m_Pipeline->GetPrimitiveType(), d.indexNum, m_IndexType, m_IndexAddress + o, m_IndexLength - o, d.instanceNum, d.baseVertex, d.baseInstance);
}

void CommandBufferMetal::CmdDrawIndirect(const Buffer& b, uint64_t o, uint32_t n, uint32_t s, const Buffer* c, uint64_t co) {
    const bool emulatedParameters = m_Graphics.layout->IsDrawParametersEmulationEnabled();
    const uint32_t argumentSize = emulatedParameters ? sizeof(DrawBaseDesc) : sizeof(DrawDesc);
    MTL::GPUAddress address = PrepareIndirectArguments(b, o, n, s, argumentSize, c, co);

    if (!address)
        return;

    const MTL::GPUAddress roots = PrepareIndirectDrawRoots(address, n, s);
    if (m_Result != Result::SUCCESS)
        return;

    address += emulatedParameters ? 8 : 0;

#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (m_Pipeline->IsGeometryEmulation() || m_Pipeline->IsTessellationEmulation()) {
        DrawEmulatedIndirect(address, roots, n, s, false);

        return;
    }
#endif
    const uint16_t type = 0;

    if (m_Pipeline->IsConverted())
        m_Arguments->setAddress(m_Allocator->Upload(&type, sizeof(type)), 5);

    for (uint32_t i = 0; i < n; i++) {
        const MTL::GPUAddress arguments = address + uint64_t(i) * s;

        if (roots)
            m_DrawRootAddress = roots + uint64_t(i) * m_Graphics.layout->GetRootDataSize();

        if (m_Pipeline->IsConverted())
            m_Arguments->setAddress(arguments, 4);

        BindArguments(BindPoint::GRAPHICS);
        m_RenderEncoder->drawPrimitives(m_Pipeline->GetPrimitiveType(), arguments);
    }
}

void CommandBufferMetal::CmdDrawIndexedIndirect(const Buffer& b, uint64_t o, uint32_t n, uint32_t s, const Buffer* c, uint64_t co) {
    const bool emulatedParameters = m_Graphics.layout->IsDrawParametersEmulationEnabled();
    const uint32_t argumentSize = emulatedParameters ? sizeof(DrawIndexedBaseDesc) : sizeof(DrawIndexedDesc);
    MTL::GPUAddress address = PrepareIndirectArguments(b, o, n, s, argumentSize, c, co);

    if (!address)
        return;

    const MTL::GPUAddress roots = PrepareIndirectDrawRoots(address, n, s);
    if (m_Result != Result::SUCCESS)
        return;

    address += emulatedParameters ? 8 : 0;

#if NRI_ENABLE_METAL_SHADER_CONVERTER
    if (m_Pipeline->IsGeometryEmulation() || m_Pipeline->IsTessellationEmulation()) {
        DrawEmulatedIndirect(address, roots, n, s, true);

        return;
    }
#endif
    const uint16_t type = m_IndexType == MTL::IndexTypeUInt16 ? 1 : 2;

    if (m_Pipeline->IsConverted())
        m_Arguments->setAddress(m_Allocator->Upload(&type, sizeof(type)), 5);

    for (uint32_t i = 0; i < n; i++) {
        const MTL::GPUAddress arguments = address + uint64_t(i) * s;

        if (roots)
            m_DrawRootAddress = roots + uint64_t(i) * m_Graphics.layout->GetRootDataSize();

        if (m_Pipeline->IsConverted())
            m_Arguments->setAddress(arguments, 4);

        BindArguments(BindPoint::GRAPHICS);
        m_RenderEncoder->drawIndexedPrimitives(m_Pipeline->GetPrimitiveType(), m_IndexType, m_IndexAddress, m_IndexLength, arguments);
    }
}

void CommandBufferMetal::CmdDrawMeshTasks(const DrawMeshTasksDesc& d) {
    BindArguments(BindPoint::GRAPHICS);
    m_RenderEncoder->drawMeshThreadgroups(MTL::Size(d.x, d.y, d.z), m_Pipeline->GetTaskThreadGroupSize(), m_Pipeline->GetMeshThreadGroupSize());
}

void CommandBufferMetal::CmdDrawMeshTasksIndirect(const Buffer& b, uint64_t offset, uint32_t drawNum, uint32_t stride, const Buffer* countBuffer, uint64_t countOffset) {
    const MTL::GPUAddress address = PrepareIndirectArguments(b, offset, drawNum, stride, sizeof(DrawMeshTasksDesc), countBuffer, countOffset);

    if (!address)
        return;

    BindArguments(BindPoint::GRAPHICS);
    for (uint32_t i = 0; i < drawNum; i++)
        m_RenderEncoder->drawMeshThreadgroups(address + uint64_t(i) * stride, m_Pipeline->GetTaskThreadGroupSize(), m_Pipeline->GetMeshThreadGroupSize());
}

MTL::GPUAddress CommandBufferMetal::PrepareIndirectArguments(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t& stride, uint32_t argumentSize, const Buffer* countBuffer, uint64_t countOffset) {
    const MTL::GPUAddress source = ((const BufferMetal&)buffer).GetGpuAddress() + offset;

    if (!drawNum || !countBuffer)
        return drawNum ? source : 0;

    if (!m_FilterDrawArguments) {
        const char* sourceCode = R"(
            #include <metal_stdlib>
            using namespace metal;
            struct Args { const device uint* source; const device uint* count; device uint* destination; uint drawNum; uint stride; uint words; };
            kernel void filter_draws(constant Args& a [[buffer(0)]], uint i [[thread_position_in_grid]]) {
                if (i >= a.drawNum) return;
                for (uint j = 0; j < a.words; j++)
                    a.destination[i * a.words + j] = i < *a.count ? a.source[i * a.stride + j] : 0;
            })";
        NS::Error* error = nullptr;
        MTL::Library* library = m_Device.GetNativeObject()->newLibrary(NS::String::string(sourceCode, NS::UTF8StringEncoding), nullptr, &error);
        MTL::Function* function = library ? library->newFunction(NS::String::string("filter_draws", NS::UTF8StringEncoding)) : nullptr;

        if (function) {
            m_FilterDrawArguments = m_Device.GetNativeObject()->newComputePipelineState(function, &error);
            function->release();
        }

        if (library)
            library->release();

        if (!m_FilterDrawArguments) {
            RecordFailure(Result::FAILURE);

            return 0;
        }
    }

    const MTL::GPUAddress destination = m_Allocator->Upload(nullptr, uint64_t(drawNum) * argumentSize);

    struct Arguments {
        MTL::GPUAddress source, count, destination;
        uint32_t drawNum, stride, words;
    } arguments = {source, ((const BufferMetal*)countBuffer)->GetGpuAddress() + countOffset, destination, drawNum, stride / 4, argumentSize / 4};

    const MTL::GPUAddress constants = m_Allocator->Upload(&arguments, sizeof(arguments));

    if (!destination || !constants) {
        RecordFailure(Result::OUT_OF_MEMORY);

        return 0;
    }

    SuspendRendering();
    auto* encoder = BeginCompute();
    encoder->barrierAfterQueueStages(MTL::StageAll, MTL::StageDispatch, MTL4::VisibilityOptionDevice);
    m_ClearStorageArguments->setAddress(constants, 0);
    encoder->setArgumentTable(m_ClearStorageArguments);
    encoder->setComputePipelineState(m_FilterDrawArguments);
    encoder->dispatchThreads(MTL::Size(drawNum, 1, 1), MTL::Size(64, 1, 1));
    ResumeRendering();
    stride = argumentSize;

    return destination;
}

void CommandBufferMetal::CmdEndRendering() {
    for (uint32_t i = 0; i < m_RenderColorNum; i++)
        m_RenderEncoder->setColorStoreAction(m_RenderStore[i], i);
    if (m_RenderDepth != MTL::PixelFormatInvalid)
        m_RenderEncoder->setDepthStoreAction(m_DepthStore);
    if (m_RenderStencil != MTL::PixelFormatInvalid)
        m_RenderEncoder->setStencilStoreAction(m_StencilStore);
    m_RenderEncoder->endEncoding();
    m_RenderEncoder = nullptr;
    m_RenderPass->release();
    m_RenderPass = nullptr;
}

void CommandBufferMetal::SetDrawArguments(const void* data, uint64_t size, bool indexed) {
    if (m_Graphics.layout->IsDrawParametersEmulationEnabled()) {
        const uint32_t offset = m_Graphics.layout->GetDrawParametersOffset();
        const uint32_t baseVertexOffset = indexed ? 12 : 8;
        memcpy(m_Graphics.root.data() + offset, (const uint8_t*)data + baseVertexOffset, 8);
    }

    if (m_Graphics.layout->IsDrawIndexEmulationEnabled()) {
        const uint32_t drawIndex = 0;
        memcpy(m_Graphics.root.data() + m_Graphics.layout->GetDrawIndexOffset(), &drawIndex, sizeof(drawIndex));
    }

    if (!m_Pipeline->IsConverted())
        return;

    uint16_t type = indexed ? (m_IndexType == MTL::IndexTypeUInt16 ? 1 : 2) : 0;
    m_Arguments->setAddress(m_Allocator->Upload(data, size), 4);
    m_Arguments->setAddress(m_Allocator->Upload(&type, sizeof(type)), 5);
}

MTL::GPUAddress CommandBufferMetal::PrepareIndirectDrawRoots(MTL::GPUAddress arguments, uint32_t drawNum, uint32_t stride) {
    const bool parameters = m_Graphics.layout->IsDrawParametersEmulationEnabled();
    const bool index = m_Graphics.layout->IsDrawIndexEmulationEnabled();

    if (!parameters && !index)
        return 0;

    if (!m_PrepareDrawRoots) {
        const char* source = R"(
            #include <metal_stdlib>
            using namespace metal;
            struct Args { const device uint* source; const device uint* root; device uint* roots; uint drawNum; uint stride; uint rootWords; uint parameterOffset; uint indexOffset; };
            kernel void prepare_draw_roots(constant Args& a [[buffer(0)]], uint i [[thread_position_in_grid]]) {
                if (i >= a.drawNum) return;
                for (uint j = 0; j < a.rootWords; j++) a.roots[i * a.rootWords + j] = a.root[j];
                if (a.parameterOffset != ~0u) {
                    a.roots[i * a.rootWords + a.parameterOffset] = a.source[i * a.stride];
                    a.roots[i * a.rootWords + a.parameterOffset + 1] = a.source[i * a.stride + 1];
                }
                if (a.indexOffset != ~0u) a.roots[i * a.rootWords + a.indexOffset] = i;
            })";
        NS::Error* error = nullptr;
        MTL::Library* library = m_Device.GetNativeObject()->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);
        MTL::Function* function = library ? library->newFunction(NS::String::string("prepare_draw_roots", NS::UTF8StringEncoding)) : nullptr;

        if (function) {
            m_PrepareDrawRoots = m_Device.GetNativeObject()->newComputePipelineState(function, &error);
            function->release();
        }

        if (library)
            library->release();

        if (!m_PrepareDrawRoots) {
            RecordFailure(Result::FAILURE);

            return 0;
        }
    }

    const uint32_t rootSize = m_Graphics.layout->GetRootDataSize();
    const MTL::GPUAddress root = m_Allocator->Upload(m_Graphics.root.data(), rootSize);
    const MTL::GPUAddress roots = m_Allocator->Upload(nullptr, uint64_t(drawNum) * rootSize);

    struct Arguments {
        MTL::GPUAddress source, root, roots;
        uint32_t drawNum, stride, rootWords, parameterOffset, indexOffset;
    } constants = {arguments, root, roots, drawNum, stride / 4, rootSize / 4, parameters ? m_Graphics.layout->GetDrawParametersOffset() / 4 : UINT32_MAX, index ? m_Graphics.layout->GetDrawIndexOffset() / 4 : UINT32_MAX};

    const MTL::GPUAddress constantAddress = m_Allocator->Upload(&constants, sizeof(constants));

    if (!root || !roots || !constantAddress) {
        RecordFailure(Result::OUT_OF_MEMORY);

        return 0;
    }

    SuspendRendering();
    auto* encoder = BeginCompute();
    encoder->barrierAfterQueueStages(MTL::StageAll, MTL::StageDispatch, MTL4::VisibilityOptionDevice);
    m_ClearStorageArguments->setAddress(constantAddress, 0);
    encoder->setArgumentTable(m_ClearStorageArguments);
    encoder->setComputePipelineState(m_PrepareDrawRoots);
    encoder->dispatchThreads(MTL::Size(drawNum, 1, 1), MTL::Size(64, 1, 1));
    ResumeRendering();

    return roots;
}

#if NRI_ENABLE_METAL_SHADER_CONVERTER
IRRuntimeDrawInfo CommandBufferMetal::PrepareEmulationDraw(bool indexed, MTL::Size& objectGroup, MTL::Size& meshGroup) {
    const IRRuntimePrimitiveType primitive = m_Pipeline->GetEmulationPrimitive();
    IRRuntimeDrawInfo info = {};
    uint32_t objectThreads = 0, meshThreads = 0;

    if (m_Pipeline->IsTessellationEmulation()) {
        const auto& config = m_Pipeline->GetTessellationConfig();
        info = IRRuntimeCalculateDrawInfoForGSTSEmulation(primitive, m_IndexType, config.outputPrimitiveType, config.gsMaxInputPrimitivesPerMeshThreadgroup, config.hsMaxPatchesPerObjectThreadgroup, config.hsInputControlPointCount, config.hsMaxObjectThreadsPerThreadgroup, config.gsInstanceCount);
        IRRuntimeCalculateThreadgroupSizeForTessellationAndGeometry(config.hsMaxPatchesPerObjectThreadgroup, config.hsMaxObjectThreadsPerThreadgroup, config.gsMaxInputPrimitivesPerMeshThreadgroup, &objectThreads, &meshThreads);
        const MTL::GPUAddress tables = m_Device.GetTessellatorTables();

        if (!tables) {
            RecordFailure(Result::OUT_OF_MEMORY);

            return {};
        }

        m_Arguments->setAddress(tables, kIRRuntimeTessellatorTablesBindPoint);
        m_RenderEncoder->setObjectThreadgroupMemoryLength(15360, 0);
    } else {
        const auto& config = m_Pipeline->GetGeometryConfig();
        info = IRRuntimeCalculateDrawInfoForGSEmulation(primitive, m_IndexType, config.gsVertexSizeInBytes, config.gsMaxInputPrimitivesPerMeshThreadgroup, m_Pipeline->GetTessellationConfig().gsInstanceCount);
        IRRuntimeCalculateThreadgroupSizeForGeometry(primitive, config.gsMaxInputPrimitivesPerMeshThreadgroup, info.objectThreadgroupVertexStride, &objectThreads, &meshThreads);
    }
    info.indexType = indexed ? uint16_t(m_IndexType + 1) : kIRNonIndexedDraw;
    info.indexBuffer = indexed ? m_IndexAddress : 0;
    objectGroup = MTL::Size(objectThreads, 1, 1);
    meshGroup = MTL::Size(meshThreads, 1, 1);
    m_Arguments->setAddress(m_Allocator->Upload(&info, sizeof(info)), kIRArgumentBufferUniformsBindPoint);

    m_Arguments->setAddress(m_Allocator->Upload(m_EmulationVertexBuffers, sizeof(m_EmulationVertexBuffers)), kIRVertexBufferBindPoint);

    return info;
}

void CommandBufferMetal::DrawEmulated(const void* arguments, uint64_t size, bool indexed, uint32_t vertexNum, uint32_t instanceNum) {
    const IRRuntimePrimitiveType primitive = m_Pipeline->GetEmulationPrimitive();

    if (!instanceNum || vertexNum <= IRRuntimePrimitiveTypeVertexOverlap(primitive))
        return;

    MTL::Size objectThreads, meshThreads;
    const IRRuntimeDrawInfo info = PrepareEmulationDraw(indexed, objectThreads, meshThreads);

    if (!info.objectThreadgroupVertexStride)
        return;

    const MTL::Size groups = IRRuntimeCalculateObjectTgCountForTessellationAndGeometryEmulation(vertexNum, info.objectThreadgroupVertexStride, primitive, instanceNum);
    m_Arguments->setAddress(m_Allocator->Upload(arguments, size), kIRArgumentBufferDrawArgumentsBindPoint);
    BindArguments(BindPoint::GRAPHICS);
    m_RenderEncoder->drawMeshThreadgroups(groups, objectThreads, meshThreads);
}

void CommandBufferMetal::DrawEmulatedIndirect(MTL::GPUAddress arguments, MTL::GPUAddress roots, uint32_t drawNum, uint32_t stride, bool indexed) {
    if (!m_EmulateDrawArguments) {
        const char* source = R"(
            #include <metal_stdlib>
            using namespace metal;
            struct Args { const device uint* source; device packed_uint3* grids; uint drawNum; uint stride; uint verticesPerGroup; uint overlap; };
            kernel void emulate_draws(constant Args& a [[buffer(0)]], uint i [[thread_position_in_grid]]) {
                if (i >= a.drawNum) return;
                uint vertices = a.source[i * a.stride];
                uint instances = a.source[i * a.stride + 1];
                uint groups = vertices > a.overlap ? 1 + (vertices - a.overlap - 1) / a.verticesPerGroup : 0;
                a.grids[i] = packed_uint3(groups, instances, 1);
            })";
        NS::Error* error = nullptr;
        MTL::Library* library = m_Device.GetNativeObject()->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);
        MTL::Function* function = library ? library->newFunction(NS::String::string("emulate_draws", NS::UTF8StringEncoding)) : nullptr;

        if (function) {
            m_EmulateDrawArguments = m_Device.GetNativeObject()->newComputePipelineState(function, &error);
            function->release();
        }

        if (library)
            library->release();

        if (!m_EmulateDrawArguments) {
            RecordFailure(Result::FAILURE);

            return;
        }
    }

    MTL::Size objectThreads, meshThreads;
    const IRRuntimeDrawInfo info = PrepareEmulationDraw(indexed, objectThreads, meshThreads);

    if (!info.objectThreadgroupVertexStride)
        return;

    const MTL::GPUAddress grids = m_Allocator->Upload(nullptr, uint64_t(drawNum) * sizeof(DrawMeshTasksDesc));

    struct Arguments {
        MTL::GPUAddress source, grids;
        uint32_t drawNum, stride, verticesPerGroup, overlap;
    } constants = {arguments, grids, drawNum, stride / 4, info.objectThreadgroupVertexStride, IRRuntimePrimitiveTypeVertexOverlap(m_Pipeline->GetEmulationPrimitive())};

    const MTL::GPUAddress constantAddress = m_Allocator->Upload(&constants, sizeof(constants));

    if (!grids || !constantAddress) {
        RecordFailure(Result::OUT_OF_MEMORY);

        return;
    }

    SuspendRendering();
    auto* encoder = BeginCompute();
    encoder->barrierAfterQueueStages(MTL::StageAll, MTL::StageDispatch, MTL4::VisibilityOptionDevice);
    m_ClearStorageArguments->setAddress(constantAddress, 0);
    encoder->setArgumentTable(m_ClearStorageArguments);
    encoder->setComputePipelineState(m_EmulateDrawArguments);
    encoder->dispatchThreads(MTL::Size(drawNum, 1, 1), MTL::Size(64, 1, 1));
    ResumeRendering();

    if (m_Pipeline->IsTessellationEmulation())
        m_RenderEncoder->setObjectThreadgroupMemoryLength(15360, 0);

    for (uint32_t i = 0; i < drawNum; i++) {
        if (roots)
            m_DrawRootAddress = roots + uint64_t(i) * m_Graphics.layout->GetRootDataSize();

        m_Arguments->setAddress(arguments + uint64_t(i) * stride, kIRArgumentBufferDrawArgumentsBindPoint);
        BindArguments(BindPoint::GRAPHICS);
        m_RenderEncoder->drawMeshThreadgroups(grids + uint64_t(i) * sizeof(DrawMeshTasksDesc), objectThreads, meshThreads);
    }
}
#endif

MTL4::ComputeCommandEncoder* CommandBufferMetal::BeginCompute() {
    if (!m_ComputeEncoder) {
        m_ComputeEncoder = m_CommandBuffer->computeCommandEncoder();
        m_RenderEncoder = nullptr;
    }
    return m_ComputeEncoder;
}

void CommandBufferMetal::CmdDispatch(const DispatchDesc& d) {
    BindArguments(BindPoint::COMPUTE);
    BeginCompute()->setComputePipelineState(m_Pipeline->GetComputePipeline());
    BeginCompute()->dispatchThreadgroups(MTL::Size(d.workGroupNumX, d.workGroupNumY, d.workGroupNumZ), m_Pipeline->GetThreadGroupSize());
}

void CommandBufferMetal::CmdDispatchIndirect(const Buffer& b, uint64_t o) {
    BindArguments(BindPoint::COMPUTE);
    BeginCompute()->setComputePipelineState(m_Pipeline->GetComputePipeline());
    BeginCompute()->dispatchThreadgroups(((BufferMetal&)b).GetGpuAddress() + o, m_Pipeline->GetThreadGroupSize());
}

void CommandBufferMetal::CmdCopyBuffer(Buffer& d, uint64_t dof, const Buffer& s, uint64_t sof, uint64_t z) {
    if (z == WHOLE_SIZE)
        z = ((const BufferMetal&)s).GetDesc().size;

    BeginCompute()->copyFromBuffer(((BufferMetal&)s).GetNativeObject(), sof, ((BufferMetal&)d).GetNativeObject(), dof, z);
}

MTL::Size CommandBufferMetal::GetRegionSize(const TextureMetal& t, const TextureRegionDesc& r) {
    const TextureDesc& d = t.GetDesc();
    return MTL::Size(r.width ? r.width : std::max(1u, uint32_t(d.width) >> r.mipOffset), r.height ? r.height : std::max(1u, uint32_t(d.height) >> r.mipOffset), r.depth ? r.depth : std::max(1u, uint32_t(d.depth) >> r.mipOffset));
}

void CommandBufferMetal::CmdCopyTexture(Texture& d, const TextureRegionDesc* dr, const Texture& s, const TextureRegionDesc* sr) {
    if (!dr && !sr) {
        BeginCompute()->copyFromTexture(((TextureMetal&)s).GetNativeObject(), ((TextureMetal&)d).GetNativeObject());
        return;
    }
    TextureRegionDesc a = sr ? *sr : TextureRegionDesc{}, b = dr ? *dr : TextureRegionDesc{};
    MTL::Size size = GetRegionSize((TextureMetal&)s, a);
    const FormatProps& props = GetFormatProps(((TextureMetal&)s).GetDesc().format);
    size.width = (size.width + props.blockWidth - 1) / props.blockWidth * props.blockWidth;
    size.height = (size.height + props.blockHeight - 1) / props.blockHeight * props.blockHeight;
    BeginCompute()->copyFromTexture(((TextureMetal&)s).GetNativeObject(), a.layerOffset, a.mipOffset, MTL::Origin(a.x, a.y, a.z), size, ((TextureMetal&)d).GetNativeObject(), b.layerOffset, b.mipOffset, MTL::Origin(b.x, b.y, b.z));
}

static MTL::BlitOption GetTextureCopyOptionsMetal(const TextureMetal& texture, PlaneBits planes) {
    const FormatProps& props = GetFormatProps(texture.GetDesc().format);

    if (props.isDepth && props.isStencil) {
        if (planes == PlaneBits::DEPTH)
            return MTL::BlitOptionDepthFromDepthStencil;

        if (planes == PlaneBits::STENCIL)
            return MTL::BlitOptionStencilFromDepthStencil;
    }

    return MTL::BlitOptionNone;
}

void CommandBufferMetal::CmdUploadBufferToTexture(Texture& d, const TextureRegionDesc& r, const Buffer& s, const TextureDataLayoutDesc& l) {
    BeginCompute()->copyFromBuffer(((BufferMetal&)s).GetNativeObject(), l.offset, l.rowPitch, l.slicePitch, GetRegionSize((TextureMetal&)d, r), ((TextureMetal&)d).GetNativeObject(), r.layerOffset, r.mipOffset, MTL::Origin(r.x, r.y, r.z), GetTextureCopyOptionsMetal((TextureMetal&)d, r.planes));
}

void CommandBufferMetal::CmdReadbackTextureToBuffer(Buffer& d, const TextureDataLayoutDesc& l, const Texture& s, const TextureRegionDesc& r) {
    BeginCompute()->copyFromTexture(((TextureMetal&)s).GetNativeObject(), r.layerOffset, r.mipOffset, MTL::Origin(r.x, r.y, r.z), GetRegionSize((TextureMetal&)s, r), ((BufferMetal&)d).GetNativeObject(), l.offset, l.rowPitch, l.slicePitch, GetTextureCopyOptionsMetal((TextureMetal&)s, r.planes));
}

void CommandBufferMetal::CmdZeroBuffer(Buffer& b, uint64_t o, uint64_t z) {
    if (z == WHOLE_SIZE)
        z = ((BufferMetal&)b).GetDesc().size - o;

    BeginCompute()->fillBuffer(((BufferMetal&)b).GetNativeObject(), NS::Range(o, z), 0);
}

void CommandBufferMetal::CmdResolveTexture(Texture& dst, const TextureRegionDesc* dstRegion, const Texture& src, const TextureRegionDesc* srcRegion, ResolveOp op) {
    // Region resolves and color min/max filtering are not exposed by this backend.
    const TextureMetal& source = (const TextureMetal&)src;
    const TextureMetal& destination = (const TextureMetal&)dst;
    const FormatProps& props = GetFormatProps(source.GetDesc().format);

    if (dstRegion || srcRegion || props.isStencil || (!props.isDepth && op != ResolveOp::AVERAGE) || (props.isDepth && op == ResolveOp::AVERAGE)) {
        RecordFailure(Result::UNSUPPORTED);

        return;
    }

    if (m_ComputeEncoder) {
        m_ComputeEncoder->endEncoding();
        m_ComputeEncoder = nullptr;
    }

    MTL4::RenderPassDescriptor* pass = MTL4::RenderPassDescriptor::alloc()->init();

    for (uint32_t layer = 0; layer < source.GetDesc().layerNum; layer++) {
        auto setAttachment = [&](MTL::RenderPassAttachmentDescriptor* attachment) {
            attachment->setTexture(source.GetNativeObject());
            attachment->setSlice(layer);
            attachment->setLoadAction(MTL::LoadActionLoad);
            attachment->setStoreAction(MTL::StoreActionStoreAndMultisampleResolve);
            attachment->setResolveTexture(destination.GetNativeObject());
            attachment->setResolveSlice(layer);
        };

        if (props.isDepth) {
            setAttachment(pass->depthAttachment());
            pass->depthAttachment()->setDepthResolveFilter(op == ResolveOp::MIN ? MTL::MultisampleDepthResolveFilterMin : (op == ResolveOp::MAX ? MTL::MultisampleDepthResolveFilterMax : MTL::MultisampleDepthResolveFilterSample0));
        }

        if (props.isStencil) {
            setAttachment(pass->stencilAttachment());
            pass->stencilAttachment()->setStencilResolveFilter(MTL::MultisampleStencilResolveFilterSample0);
        }

        if (!props.isDepth && !props.isStencil)
            setAttachment(pass->colorAttachments()->object(0));

        MTL4::RenderCommandEncoder* encoder = m_CommandBuffer->renderCommandEncoder(pass);
        encoder->endEncoding();
    }

    pass->release();
}

MTL::ComputePipelineState* CommandBufferMetal::GetClearStoragePipeline(MTL::TextureType textureType, uint8_t valueType) {
    for (const ClearStoragePipelineMetal& clear : m_ClearStoragePipelines) {
        if (clear.textureType == textureType && clear.valueType == valueType)
            return clear.pipeline;
    }

    if (!m_ClearStorageLibrary) {
        std::string source =
            "#include <metal_stdlib>\nusing namespace metal;\n"
            "kernel void clear_storage_buffer(device uint* d [[buffer(3)]], constant uint4& v [[buffer(4)]], uint i [[thread_position_in_grid]]) { d[i] = v.x; }\n";
        const char* channelTypes[] = {"float", "uint", "int"};
        const char* textureTypes[] = {"texture_buffer", "texture1d", "texture1d_array", "texture2d", "texture2d_array", "texture3d"};
        const char* coordinates[] = {"p.x", "p.x", "p.x", "p.xy", "p.xy", "uint3(p.xy, p.z + z)"};
        const char* arraySlices[] = {"", "", ", p.y", "", ", p.z", ""};
        for (uint32_t dimension = 0; dimension < 6; dimension++) {
            for (uint32_t type = 0; type < 3; type++) {
                char function[512] = {};
                snprintf(function, sizeof(function), "kernel void clear_storage_%u_%u(%s<%s, access::write> d [[texture(0)]], constant uint4& v [[buffer(4)]], constant uint& z [[buffer(5)]], uint3 p [[thread_position_in_grid]]) { d.write(%s(v), %s%s); }\n", dimension, type, textureTypes[dimension], channelTypes[type], type == 0 ? "as_type<float4>" : (type == 1 ? "uint4" : "as_type<int4>"), coordinates[dimension], arraySlices[dimension]);
                source += function;
            }
        }
        NS::Error* error = nullptr;
        m_ClearStorageLibrary = m_Device.GetNativeObject()->newLibrary(NS::String::string(source.c_str(), NS::UTF8StringEncoding), nullptr, &error);

        if (!m_ClearStorageLibrary) {
            m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal storage clear shader compilation failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");

            return nullptr;
        }
    }

    uint32_t dimension = 0;
    switch (textureType) {
        case MTL::TextureType1D:
            dimension = 1;
            break;
        case MTL::TextureType1DArray:
            dimension = 2;
            break;
        case MTL::TextureType2D:
            dimension = 3;
            break;
        case MTL::TextureType2DArray:
            dimension = 4;
            break;
        case MTL::TextureType3D:
            dimension = 5;
            break;
        default:
            break;
    }
    char name[48] = {};
    if (valueType == 3)
        snprintf(name, sizeof(name), "clear_storage_buffer");
    else
        snprintf(name, sizeof(name), "clear_storage_%u_%u", dimension, valueType);
    MTL::Function* function = m_ClearStorageLibrary->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
    NS::Error* error = nullptr;
    MTL::ComputePipelineState* pipeline = function ? m_Device.GetNativeObject()->newComputePipelineState(function, &error) : nullptr;
    if (function)
        function->release();
    if (!pipeline) {
        m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal storage clear pipeline creation failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");
        return nullptr;
    }
    m_ClearStoragePipelines.push_back({pipeline, textureType, valueType});

    return pipeline;
}

void CommandBufferMetal::CmdClearStorage(const ClearStorageDesc& desc) {
    const DescriptorMetal& descriptor = *(const DescriptorMetal*)desc.descriptor;
    MTL::Texture* texture = descriptor.GetTexture();
    MTL::TextureType textureType = texture ? texture->textureType() : MTL::TextureTypeTextureBuffer;
    uint8_t valueType = 0;
    if (texture) {
        const MTL::PixelFormat format = texture->pixelFormat();
        const bool signedInteger = format == MTL::PixelFormatR8Sint || format == MTL::PixelFormatRG8Sint || format == MTL::PixelFormatRGBA8Sint || format == MTL::PixelFormatR16Sint || format == MTL::PixelFormatRG16Sint || format == MTL::PixelFormatRGBA16Sint || format == MTL::PixelFormatR32Sint || format == MTL::PixelFormatRG32Sint || format == MTL::PixelFormatRGBA32Sint;
        const bool unsignedInteger = format == MTL::PixelFormatR8Uint || format == MTL::PixelFormatRG8Uint || format == MTL::PixelFormatRGBA8Uint || format == MTL::PixelFormatR16Uint || format == MTL::PixelFormatRG16Uint || format == MTL::PixelFormatRGBA16Uint || format == MTL::PixelFormatR32Uint || format == MTL::PixelFormatRG32Uint || format == MTL::PixelFormatRGBA32Uint || format == MTL::PixelFormatRGB10A2Uint;
        valueType = signedInteger ? 2 : (unsignedInteger ? 1 : 0);
    }
    MTL::ComputePipelineState* pipeline = GetClearStoragePipeline(textureType, texture ? valueType : 3);
    if (!pipeline) {
        RecordFailure(Result::FAILURE);
        return;
    }

    MTL::Size grid = {};
    if (texture) {
        grid = MTL::Size(texture->width(), texture->height(), texture->depth());
        if (textureType == MTL::TextureType1DArray)
            grid.height = texture->arrayLength();
        else if (textureType == MTL::TextureType2DArray)
            grid.depth = texture->arrayLength();
        const TextureViewDesc& view = descriptor.GetTextureViewDesc();
        uint32_t sliceOffset = textureType == MTL::TextureType3D ? view.sliceOffset : 0;

        if (textureType == MTL::TextureType3D)
            grid.depth = view.sliceNum == REMAINING ? grid.depth - sliceOffset : view.sliceNum;

        m_ClearStorageArguments->setTexture(texture->gpuResourceID(), 0);
        m_ClearStorageArguments->setAddress(m_Allocator->Upload(&sliceOffset, sizeof(sliceOffset)), 5);
    } else {
        grid = MTL::Size(descriptor.GetBufferSize() / sizeof(uint32_t), 1, 1);
        m_ClearStorageArguments->setAddress(descriptor.GetBuffer()->gpuAddress() + descriptor.GetBufferOffset(), 3);
    }
    if (!grid.width || !grid.height || !grid.depth)
        return;

    m_ClearStorageArguments->setAddress(m_Allocator->Upload(&desc.value, sizeof(desc.value)), 4);
    MTL4::ComputeCommandEncoder* encoder = BeginCompute();
    encoder->setArgumentTable(m_ClearStorageArguments);
    encoder->setComputePipelineState(pipeline);
    encoder->dispatchThreads(grid, MTL::Size(8, std::min<NS::UInteger>(grid.height, 8), 1));

    encoder->setArgumentTable(m_Arguments);
    if (m_Pipeline && m_Pipeline->GetComputePipeline())
        encoder->setComputePipelineState(m_Pipeline->GetComputePipeline());
}

void CommandBufferMetal::CmdResetQueries(QueryPool& p, uint32_t o, uint32_t n) {
    QueryPoolMetal& q = (QueryPoolMetal&)p;
    // Timestamps are overwritten by GPU writes. CPU invalidation here would race
    // earlier submissions still using the same heap.
    if (!q.GetCounterHeap())
        BeginCompute()->fillBuffer(q.GetVisibilityBuffer(), NS::Range(uint64_t(o) * 8, uint64_t(n) * 8), 0);
}

void CommandBufferMetal::SuspendRendering() {
    for (uint32_t i = 0; i < m_RenderColorNum; i++) {
        m_RenderEncoder->setColorStoreAction(MTL::StoreActionStore, i);
        m_RenderPass->colorAttachments()->object(i)->setLoadAction(MTL::LoadActionLoad);
    }

    if (m_RenderDepth != MTL::PixelFormatInvalid) {
        m_RenderEncoder->setDepthStoreAction(MTL::StoreActionStore);
        m_RenderPass->depthAttachment()->setLoadAction(MTL::LoadActionLoad);
    }

    if (m_RenderStencil != MTL::PixelFormatInvalid) {
        m_RenderEncoder->setStencilStoreAction(MTL::StoreActionStore);
        m_RenderPass->stencilAttachment()->setLoadAction(MTL::LoadActionLoad);
    }

    m_RenderEncoder->endEncoding();
    m_RenderEncoder = nullptr;
}

void CommandBufferMetal::ResumeRendering() {
    if (m_ComputeEncoder) {
        m_ComputeEncoder->endEncoding();
        m_ComputeEncoder = nullptr;
    }

    m_RenderEncoder = m_CommandBuffer->renderCommandEncoder(m_RenderPass);
    m_RenderEncoder->barrierAfterQueueStages(MTL::StageFragment | MTL::StageBlit | MTL::StageDispatch, MTL::StageVertex | MTL::StageObject | MTL::StageMesh | MTL::StageFragment, MTL4::VisibilityOptionDevice);
    m_RenderPipelineDirty = true;

    ApplyRasterState();
    m_RenderEncoder->setVisibilityResultMode(m_VisibilityMode, m_VisibilityOffset);
}

void CommandBufferMetal::CmdBeginQuery(QueryPool& p, uint32_t o) {
    QueryPoolMetal& q = (QueryPoolMetal&)p;

    if (m_RenderPass->visibilityResultBuffer() != q.GetVisibilityBuffer()) {
        SuspendRendering();
        m_RenderPass->setVisibilityResultBuffer(q.GetVisibilityBuffer());
        m_RenderPass->setVisibilityResultType(MTL::VisibilityResultTypeAccumulate);
        ResumeRendering();
    }

    m_VisibilityMode = MTL::VisibilityResultModeCounting;
    m_VisibilityOffset = uint64_t(o) * 8;
    m_RenderEncoder->setVisibilityResultMode(m_VisibilityMode, m_VisibilityOffset);
}

void CommandBufferMetal::CmdEndQuery(QueryPool& p, uint32_t o) {
    QueryPoolMetal& q = (QueryPoolMetal&)p;
    if (q.GetCounterHeap()) {
        if (m_RenderEncoder)
            m_RenderEncoder->writeTimestamp(MTL4::TimestampGranularityPrecise, MTL::RenderStageFragment, q.GetCounterHeap(), o);
        else
            BeginCompute()->writeTimestamp(MTL4::TimestampGranularityPrecise, q.GetCounterHeap(), o);
    } else {
        m_VisibilityMode = MTL::VisibilityResultModeDisabled;
        m_VisibilityOffset = 0;
        m_RenderEncoder->setVisibilityResultMode(MTL::VisibilityResultModeDisabled, 0);
    }
}

void CommandBufferMetal::CmdCopyQueries(const QueryPool& p, uint32_t o, uint32_t n, Buffer& d, uint64_t x) {
    const QueryPoolMetal& q = (const QueryPoolMetal&)p;
    auto* encoder = BeginCompute();
    encoder->barrierAfterQueueStages(MTL::StageFragment | MTL::StageDispatch | MTL::StageAccelerationStructure, MTL::StageBlit, MTL4::VisibilityOptionDevice);
    encoder->barrierAfterEncoderStages(MTL::StageDispatch | MTL::StageAccelerationStructure, MTL::StageBlit, MTL4::VisibilityOptionDevice);
    if (q.GetCounterHeap()) {
        if (!m_CounterFence)
            m_CounterFence = m_Device.GetNativeObject()->newFence();
        if (!m_CounterFence) {
            RecordFailure(Result::OUT_OF_MEMORY);
            return;
        }
        encoder->updateFence(m_CounterFence, MTL::StageDispatch | MTL::StageBlit);
        encoder->endEncoding();
        m_ComputeEncoder = nullptr;
        m_CommandBuffer->resolveCounterHeap(q.GetCounterHeap(), NS::Range(o, n), MTL4::BufferRange(((BufferMetal&)d).GetGpuAddress() + x, uint64_t(n) * q.GetQuerySize()), m_CounterFence, nullptr);
    } else
        encoder->copyFromBuffer(q.GetVisibilityBuffer(), uint64_t(o) * 8, ((BufferMetal&)d).GetNativeObject(), x, uint64_t(n) * 8);
}

void CommandBufferMetal::CmdBeginAnnotation(const char* n, uint32_t) {
    m_CommandBuffer->pushDebugGroup(NS::String::string(n, NS::UTF8StringEncoding));
}

void CommandBufferMetal::CmdEndAnnotation() {
    m_CommandBuffer->popDebugGroup();
}

void CommandBufferMetal::CmdAnnotation(const char* n, uint32_t c) {
    MaybeUnused(c);
    CmdBeginAnnotation(n, 0);
    CmdEndAnnotation();
}

void CommandBufferMetal::SetDebugName(const char* n) {
    m_CommandBuffer->setLabel(NS::String::string(n, NS::UTF8StringEncoding));
}

bool CommandBufferMetal::CreateRayTracingKernels() {
    if (m_ConvertInstances && m_CopyRayArguments)
        return true;

    const char* source = R"metal(
#include <metal_stdlib>
using namespace metal;
struct NriInstance { float transform[12]; uint idMask; uint offsetFlags; ulong resource; };
struct MetalInstance { float transform[12]; uint options; uint mask; uint tableOffset; uint userID; ulong resource; };
kernel void convert_instances(device const NriInstance* src [[buffer(3)]], device MetalInstance* dst [[buffer(4)]], device uint* contributions [[buffer(5)]], uint i [[thread_position_in_grid]]) {
    for (uint j = 0; j < 12; j++) dst[i].transform[j] = src[i].transform[j];
    dst[i].options = src[i].offsetFlags >> 24;
    dst[i].mask = src[i].idMask >> 24;
    dst[i].tableOffset = 0;
    dst[i].userID = src[i].idMask & 0xffffff;
    dst[i].resource = src[i].resource;
    contributions[i] = src[i].offsetFlags & 0xffffff;
}
kernel void copy_words(device const uint* src [[buffer(3)]], device uint* dst [[buffer(4)]], uint i [[thread_position_in_grid]]) { dst[i] = src[i]; }
)metal";
    NS::Error* error = nullptr;
    MTL::Library* library = m_Device.GetNativeObject()->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);

    if (library) {
        MTL::Function* instances = library->newFunction(NS::String::string("convert_instances", NS::UTF8StringEncoding));
        MTL::Function* copy = library->newFunction(NS::String::string("copy_words", NS::UTF8StringEncoding));
        m_ConvertInstances = m_Device.GetNativeObject()->newComputePipelineState(instances, &error);
        m_CopyRayArguments = m_Device.GetNativeObject()->newComputePipelineState(copy, &error);
        instances->release();
        copy->release();
        library->release();
    }

    if (!m_ConvertInstances || !m_CopyRayArguments) {
        m_Device.ReportMessage(Message::ERROR, Result::FAILURE, __FILE__, __LINE__, "Metal ray-tracing helper compilation failed: %s", error ? error->localizedDescription()->utf8String() : "unknown error");
        RecordFailure(Result::FAILURE);

        return false;
    }

    return true;
}

void CommandBufferMetal::CmdBuildBottomLevelAccelerationStructures(const BuildBottomLevelAccelerationStructureDesc* descs, uint32_t num) {
    auto* encoder = BeginCompute();

    for (uint32_t i = 0; i < num; i++) {
        const auto& desc = descs[i];
        auto& dst = *(AccelerationStructureMetal*)desc.dst;
        auto* descriptor = dst.CreateBuildDescriptor(desc.geometries, desc.geometryNum);
        MTL4::BufferRange scratch(((BufferMetal*)desc.scratchBuffer)->GetGpuAddress() + desc.scratchOffset, desc.src ? dst.GetUpdateScratchBufferSize() : dst.GetBuildScratchBufferSize());

        if (desc.src)
            encoder->refitAccelerationStructure(((const AccelerationStructureMetal*)desc.src)->GetNativeObject(), descriptor, dst.GetNativeObject(), scratch);
        else
            encoder->buildAccelerationStructure(dst.GetNativeObject(), descriptor, scratch);
        descriptor->release();
    }
}

void CommandBufferMetal::CmdBuildTopLevelAccelerationStructures(const BuildTopLevelAccelerationStructureDesc* descs, uint32_t num) {
    if (!CreateRayTracingKernels())
        return;

    auto* encoder = BeginCompute();

    for (uint32_t i = 0; i < num; i++) {
        const auto& desc = descs[i];
        auto& dst = *(AccelerationStructureMetal*)desc.dst;
        uint64_t instances = m_Allocator->Upload(nullptr, std::max<uint64_t>(1, desc.instanceNum) * sizeof(MTL::IndirectAccelerationStructureInstanceDescriptor));

        if (desc.instanceNum) {
            m_ClearStorageArguments->setAddress(((const BufferMetal*)desc.instanceBuffer)->GetGpuAddress() + desc.instanceOffset, 3);
            m_ClearStorageArguments->setAddress(instances, 4);
            m_ClearStorageArguments->setAddress(dst.GetInstanceContributionAddress(), 5);
            encoder->setArgumentTable(m_ClearStorageArguments);
            encoder->setComputePipelineState(m_ConvertInstances);
            encoder->dispatchThreads(MTL::Size(desc.instanceNum, 1, 1), MTL::Size(64, 1, 1));
            encoder->barrierAfterEncoderStages(MTL::StageDispatch, MTL::StageAccelerationStructure, MTL4::VisibilityOptionDevice);
        }
        auto* descriptor = dst.CreateBuildDescriptor(nullptr, 0, instances, desc.instanceNum);
        MTL4::BufferRange scratch(((BufferMetal*)desc.scratchBuffer)->GetGpuAddress() + desc.scratchOffset, desc.src ? dst.GetUpdateScratchBufferSize() : dst.GetBuildScratchBufferSize());

        if (desc.src)
            encoder->refitAccelerationStructure(((const AccelerationStructureMetal*)desc.src)->GetNativeObject(), descriptor, dst.GetNativeObject(), scratch);
        else
            encoder->buildAccelerationStructure(dst.GetNativeObject(), descriptor, scratch);
        descriptor->release();
    }
    encoder->setArgumentTable(m_Arguments);
}

void CommandBufferMetal::CmdCopyAccelerationStructure(AccelerationStructure& dst, const AccelerationStructure& src, CopyMode mode) {
    auto& destination = (AccelerationStructureMetal&)dst;
    const auto& source = (const AccelerationStructureMetal&)src;
    auto* encoder = BeginCompute();

    if (mode == CopyMode::COMPACT)
        encoder->copyAndCompactAccelerationStructure(source.GetNativeObject(), destination.GetNativeObject());
    else
        encoder->copyAccelerationStructure(source.GetNativeObject(), destination.GetNativeObject());

    if (source.GetShaderBindingHeaderBuffer())
        encoder->copyFromBuffer(source.GetShaderBindingHeaderBuffer(), 64, destination.GetShaderBindingHeaderBuffer(), 64, source.GetShaderBindingHeaderBuffer()->length() - 64);
}

void CommandBufferMetal::CmdWriteAccelerationStructureSizes(const AccelerationStructure* const* structures, uint32_t num, QueryPool& pool, uint32_t offset) {
    auto& queries = (QueryPoolMetal&)pool;
    auto* encoder = BeginCompute();

    for (uint32_t i = 0; i < num; i++) {
        const auto& structure = *(const AccelerationStructureMetal*)structures[i];
        uint64_t address = queries.GetVisibilityBuffer()->gpuAddress() + uint64_t(offset + i) * 8;

        if (queries.GetType() == QueryType::ACCELERATION_STRUCTURE_COMPACTED_SIZE)
            encoder->writeCompactedAccelerationStructureSize(structure.GetNativeObject(), MTL4::BufferRange(address, 8));
        else {
            if (!CreateRayTracingKernels())
                return;
            const uint64_t size = structure.GetSize();
            m_ClearStorageArguments->setAddress(m_Allocator->Upload(&size, sizeof(size)), 3);
            m_ClearStorageArguments->setAddress(address, 4);
            encoder->setArgumentTable(m_ClearStorageArguments);
            encoder->setComputePipelineState(m_CopyRayArguments);
            encoder->dispatchThreads(MTL::Size(2, 1, 1), MTL::Size(2, 1, 1));
        }
    }
    encoder->setArgumentTable(m_Arguments);
}

MTL::GPUAddress CommandBufferMetal::SetRayDispatchArguments(const DispatchRaysIndirectDesc& desc) {
    // IRDispatchRaysArgument ABI, shared by converted shaders and native ray stages.
    struct Arguments {
        DispatchRaysIndirectDesc desc;
        uint64_t root, resources, samplers, visibleFunctions, intersectionFunctions, intersectionTables;
    } args = {};

    static_assert(sizeof(DispatchRaysIndirectDesc) == 104, "Native ray dispatch ABI mismatch");
    static_assert(offsetof(DispatchRaysIndirectDesc, width) == 88, "Native ray dimensions ABI mismatch");
    static_assert(sizeof(Arguments) == 152, "Native ray arguments ABI mismatch");
    static_assert(offsetof(Arguments, root) == 104, "Native ray root ABI mismatch");
    static_assert(offsetof(Arguments, resources) == 112, "Native resource heap ABI mismatch");
    static_assert(offsetof(Arguments, samplers) == 120, "Native sampler heap ABI mismatch");
    static_assert(offsetof(Arguments, visibleFunctions) == 128, "Native visible table ABI mismatch");
    static_assert(offsetof(Arguments, intersectionFunctions) == 136, "Native intersection table ABI mismatch");
    static_assert(offsetof(Arguments, intersectionTables) == 144, "Native multiple-table ABI mismatch");
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    static_assert(sizeof(DispatchRaysIndirectDesc) == sizeof(IRDispatchRaysDescriptor), "Ray dispatch descriptor ABI mismatch");
    static_assert(offsetof(DispatchRaysIndirectDesc, width) == offsetof(IRDispatchRaysDescriptor, Width), "Ray dispatch dimensions ABI mismatch");
    static_assert(sizeof(Arguments) == sizeof(IRDispatchRaysArgument), "Ray dispatch arguments ABI mismatch");
    static_assert(offsetof(Arguments, root) == offsetof(IRDispatchRaysArgument, GRS), "Ray dispatch root ABI mismatch");
    static_assert(offsetof(Arguments, visibleFunctions) == offsetof(IRDispatchRaysArgument, VisibleFunctionTable), "Ray dispatch function table ABI mismatch");
#endif

    args.desc = desc;
    args.root = m_Allocator->Upload(m_Compute.root.data(), m_Compute.root.size());
    args.resources = m_DescriptorPool ? m_DescriptorPool->GetResourceHeapAddress() : 0;
    args.samplers = m_DescriptorPool ? m_DescriptorPool->GetSamplerHeapAddress() : 0;
    args.visibleFunctions = m_Pipeline->GetVisibleFunctionTableResourceID()._impl;
    args.intersectionFunctions = m_Pipeline->GetIntersectionFunctionTableResourceID()._impl;
    const uint64_t address = m_Allocator->Upload(&args, sizeof(args));
    m_Arguments->setAddress(address, 3);

    return address;
}

void CommandBufferMetal::CmdDispatchRays(const DispatchRaysDesc& desc) {
    auto regionAddress = [](const StridedBufferRegion& region) -> uint64_t {
        return region.buffer ? ((const BufferMetal*)region.buffer)->GetGpuAddress() + region.offset : 0;
    };
    DispatchRaysIndirectDesc args = {};
    args.raygenShaderRecordAddress = regionAddress(desc.raygenShaderRecord);
    args.raygenShaderRecordSize = desc.raygenShaderRecord.size;
    args.missShaderBindingTableAddress = regionAddress(desc.missShaderBindingTable);
    args.missShaderBindingTableSize = desc.missShaderBindingTable.size;
    args.missShaderBindingTableStride = desc.missShaderBindingTable.stride;
    args.hitShaderBindingTableAddress = regionAddress(desc.hitShaderBindingTable);
    args.hitShaderBindingTableSize = desc.hitShaderBindingTable.size;
    args.hitShaderBindingTableStride = desc.hitShaderBindingTable.stride;
    args.callableShaderBindingTableAddress = regionAddress(desc.callableShaderBindingTable);
    args.callableShaderBindingTableSize = desc.callableShaderBindingTable.size;
    args.callableShaderBindingTableStride = desc.callableShaderBindingTable.stride;
    args.width = desc.width;
    args.height = desc.height;
    args.depth = desc.depth;
    SetRayDispatchArguments(args);
    BindArguments(BindPoint::COMPUTE);
    auto* encoder = BeginCompute();
    encoder->setComputePipelineState(m_Pipeline->GetComputePipeline());
    encoder->dispatchThreads(MTL::Size(desc.width, desc.height, desc.depth), MTL::Size(8, 8, 1));
}

void CommandBufferMetal::CmdDispatchRaysIndirect(const Buffer& buffer, uint64_t offset) {
    if (!CreateRayTracingKernels())
        return;

    uint64_t arguments = SetRayDispatchArguments({});
    uint64_t source = ((const BufferMetal&)buffer).GetGpuAddress() + offset;
    auto* encoder = BeginCompute();
    m_ClearStorageArguments->setAddress(source, 3);
    m_ClearStorageArguments->setAddress(arguments, 4);
    encoder->setArgumentTable(m_ClearStorageArguments);
    encoder->setComputePipelineState(m_CopyRayArguments);
    encoder->dispatchThreads(MTL::Size(25, 1, 1), MTL::Size(32, 1, 1));
    encoder->barrierAfterEncoderStages(MTL::StageDispatch, MTL::StageDispatch, MTL4::VisibilityOptionDevice);
    BindArguments(BindPoint::COMPUTE);
    encoder->setComputePipelineState(m_Pipeline->GetComputePipeline());
    encoder->dispatchThreadgroups(source + offsetof(DispatchRaysIndirectDesc, width), MTL::Size(1, 1, 1));
}
