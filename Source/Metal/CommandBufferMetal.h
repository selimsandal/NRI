// © 2026 NVIDIA Corporation

#pragma once

#if NRI_ENABLE_METAL_SHADER_CONVERTER
#    define IR_RUNTIME_METALCPP
#    define IR_RUNTIME_METAL4
#    define IR_PRIVATE_IMPLEMENTATION
#    include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#endif

namespace nri {

struct ClearPipelineMetal {
    MTL::RenderPipelineState* pipeline = nullptr;
    MTL::DepthStencilState* depthStencil = nullptr;
    MTL::PixelFormat colors[8] = {};
    MTL::PixelFormat depth = MTL::PixelFormatInvalid;
    MTL::PixelFormat stencil = MTL::PixelFormatInvalid;
    uint8_t colorNum = 0;
    uint8_t colorIndex = 0;
    uint8_t sampleNum = 1;
    PlaneBits planes = PlaneBits::NONE;
    bool isSigned = false;
    bool isInteger = false;
};

struct ClearStoragePipelineMetal {
    MTL::ComputePipelineState* pipeline = nullptr;
    MTL::TextureType textureType = MTL::TextureTypeTextureBuffer;
    uint8_t valueType = 0;
};

struct CommandBufferMetal final : public DebugNameBase {
    inline CommandBufferMetal(DeviceMetal& device)
        : m_Device(device)
        , m_ClearPipelines(device.GetStdAllocator()) {
    }

    ~CommandBufferMetal();
    Result Create(const CommandAllocator& allocator);
    Result Begin(const DescriptorPool* descriptorPool);
    Result End();

    inline DeviceMetal& GetDevice() const {
        return m_Device;
    }

    inline MTL4::CommandBuffer* GetNativeObject() const {
        return m_CommandBuffer;
    }

    inline MTL4::ComputeCommandEncoder* GetComputeEncoder() {
        return BeginCompute();
    }

    inline MTL4::RenderCommandEncoder* GetRenderEncoder() const {
        return m_RenderEncoder;
    }

    void RecordFailure(Result result);
    void CmdSetDescriptorPool(const DescriptorPool& descriptorPool);
    void CmdSetPipelineLayout(BindPoint bindPoint, const PipelineLayout& pipelineLayout);
    void CmdSetDescriptorSet(const SetDescriptorSetDesc& desc);
    void CmdSetRootConstants(const SetRootConstantsDesc& desc);
    void CmdSetRootDescriptor(const SetRootDescriptorDesc& desc);
    void CmdSetPipeline(const Pipeline& pipeline);
    void CmdBarrier(const BarrierDesc& desc);
    void CmdSetIndexBuffer(const Buffer& buffer, uint64_t offset, IndexType indexType);
    void CmdSetVertexBuffers(uint32_t baseSlot, const VertexBufferDesc* descs, uint32_t num);
    void CmdSetViewports(const Viewport* viewports, uint32_t num);
    void CmdSetScissors(const Rect* rects, uint32_t num);
    void CmdSetStencilReference(uint8_t front, uint8_t back);
    void CmdSetDepthBounds(float min, float max);
    void CmdSetBlendConstants(const Color32f& color);
    void CmdSetSampleLocations(const SampleLocation*, Sample_t, Sample_t);
    void CmdSetShadingRate(const ShadingRateDesc&);
    void CmdSetDepthBias(const DepthBiasDesc& desc);
    void CmdBeginRendering(const RenderingDesc& desc);
    void CmdClearAttachments(const ClearAttachmentDesc*, uint32_t, const Rect*, uint32_t);
    void CmdDraw(const DrawDesc& desc);
    void CmdDrawIndexed(const DrawIndexedDesc& desc);
    void CmdDrawIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, const Buffer* countBuffer, uint64_t countOffset);
    void CmdDrawIndexedIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, const Buffer* countBuffer, uint64_t countOffset);
    void CmdDrawMeshTasks(const DrawMeshTasksDesc& desc);
    void CmdDrawMeshTasksIndirect(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t stride, const Buffer* countBuffer, uint64_t countOffset);
    void CmdBuildTopLevelAccelerationStructures(const BuildTopLevelAccelerationStructureDesc* descs, uint32_t num);
    void CmdBuildBottomLevelAccelerationStructures(const BuildBottomLevelAccelerationStructureDesc* descs, uint32_t num);
    void CmdCopyAccelerationStructure(AccelerationStructure& dst, const AccelerationStructure& src, CopyMode mode);
    void CmdWriteAccelerationStructureSizes(const AccelerationStructure* const* structures, uint32_t num, QueryPool& pool, uint32_t offset);
    void CmdDispatchRays(const DispatchRaysDesc& desc);
    void CmdDispatchRaysIndirect(const Buffer& buffer, uint64_t offset);
    void CmdEndRendering();
    void CmdDispatch(const DispatchDesc& desc);
    void CmdDispatchIndirect(const Buffer& buffer, uint64_t offset);
    void CmdCopyBuffer(Buffer& dst, uint64_t dstOffset, const Buffer& src, uint64_t srcOffset, uint64_t size);
    void CmdCopyTexture(Texture& dst, const TextureRegionDesc* dstRegion, const Texture& src, const TextureRegionDesc* srcRegion);
    void CmdUploadBufferToTexture(Texture& dst, const TextureRegionDesc& region, const Buffer& src, const TextureDataLayoutDesc& layout);
    void CmdReadbackTextureToBuffer(Buffer& dst, const TextureDataLayoutDesc& layout, const Texture& src, const TextureRegionDesc& region);
    void CmdZeroBuffer(Buffer& buffer, uint64_t offset, uint64_t size);
    void CmdResolveTexture(Texture&, const TextureRegionDesc*, const Texture&, const TextureRegionDesc*, ResolveOp);
    void CmdClearStorage(const ClearStorageDesc&);
    void CmdResetQueries(QueryPool& pool, uint32_t offset, uint32_t num);
    void CmdBeginQuery(QueryPool& pool, uint32_t offset);
    void CmdEndQuery(QueryPool& pool, uint32_t offset);
    void CmdCopyQueries(const QueryPool& pool, uint32_t offset, uint32_t num, Buffer& dst, uint64_t dstOffset);
    void CmdBeginAnnotation(const char* name, uint32_t);
    void CmdEndAnnotation();
    void CmdAnnotation(const char* name, uint32_t);
    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

private:
    struct State {
        const PipelineLayoutMetal* layout = nullptr;
        Vector<uint8_t> root;

        State(StdAllocator<uint8_t>& a) : root(a) {
        }
    };

    MTL4::ComputeCommandEncoder* BeginCompute();
    void SuspendRendering();
    void ResumeRendering();
    MTL::GPUAddress PrepareIndirectArguments(const Buffer& buffer, uint64_t offset, uint32_t drawNum, uint32_t& stride, uint32_t argumentSize, const Buffer* countBuffer, uint64_t countOffset);
    void BindArguments(BindPoint bindPoint);
    State& GetState(BindPoint bindPoint);
    void ApplyRasterState();
    void SetDrawArguments(const void* data, uint64_t size, bool indexed);
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRRuntimeDrawInfo PrepareEmulationDraw(bool indexed, MTL::Size& objectThreads, MTL::Size& meshThreads);
    void DrawEmulated(const void* arguments, uint64_t size, bool indexed, uint32_t vertexNum, uint32_t instanceNum);
    void DrawEmulatedIndirect(MTL::GPUAddress arguments, uint32_t drawNum, uint32_t stride, bool indexed);
#endif
    bool CreateRayTracingKernels();
    MTL::GPUAddress SetRayDispatchArguments(const DispatchRaysIndirectDesc& desc);
    ClearPipelineMetal* GetClearPipeline(uint32_t colorIndex, PlaneBits planes, bool isInteger, bool isSigned);
    MTL::ComputePipelineState* GetClearStoragePipeline(MTL::TextureType textureType, uint8_t valueType);
    static MTL::Size GetRegionSize(const TextureMetal& texture, const TextureRegionDesc& region);
    DeviceMetal& m_Device;
    CommandAllocatorMetal* m_Allocator = nullptr;
    MTL4::CommandBuffer* m_CommandBuffer = nullptr;
    MTL4::ArgumentTable* m_Arguments = nullptr;
    MTL4::ArgumentTable* m_ClearStorageArguments = nullptr;
    MTL4::ComputeCommandEncoder* m_ComputeEncoder = nullptr;
    MTL4::RenderCommandEncoder* m_RenderEncoder = nullptr;
    MTL4::RenderPassDescriptor* m_RenderPass = nullptr;
    MTL::Fence* m_CounterFence = nullptr;
    MTL::Library* m_ClearLibrary = nullptr;
    MTL::Library* m_ClearStorageLibrary = nullptr;
    MTL::ComputePipelineState* m_ConvertInstances = nullptr;
    MTL::ComputePipelineState* m_CopyRayArguments = nullptr;
    MTL::ComputePipelineState* m_FilterDrawArguments = nullptr;
    MTL::ComputePipelineState* m_EmulateDrawArguments = nullptr;
    DescriptorPoolMetal* m_DescriptorPool = nullptr;
    const PipelineMetal* m_Pipeline = nullptr;
    bool m_RenderPipelineDirty = true;
    State m_Graphics{m_Device.GetStdAllocator()}, m_Compute{m_Device.GetStdAllocator()};
    BindPoint m_BindPoint = BindPoint::GRAPHICS;
    MTL::Viewport m_Viewports[16] = {};
    MTL::ScissorRect m_Scissors[16] = {};
    uint32_t m_ViewportNum = 0, m_ScissorNum = 0;
    uint8_t m_FrontStencil = 0, m_BackStencil = 0;
    Color32f m_BlendColor = {};
    DepthBiasDesc m_DepthBias = {};
    bool m_HasDepthBias = false;
    float m_DepthMin = 0.0f, m_DepthMax = 1.0f;
    MTL::SamplePosition m_SamplePositions[16] = {};
    uint8_t m_SamplePositionNum = 0;
    MTL::GPUAddress m_IndexAddress = 0;
    uint64_t m_IndexLength = 0;
    MTL::IndexType m_IndexType = MTL::IndexTypeUInt16;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRRuntimeVertexBuffers m_EmulationVertexBuffers = {};
#endif
    Vector<ClearPipelineMetal> m_ClearPipelines;
    Vector<ClearStoragePipelineMetal> m_ClearStoragePipelines{m_Device.GetStdAllocator()};
    MTL::PixelFormat m_RenderColors[8] = {};
    MTL::StoreAction m_RenderStore[8] = {};
    MTL::StoreAction m_DepthStore = MTL::StoreActionDontCare, m_StencilStore = MTL::StoreActionDontCare;
    MTL::PixelFormat m_RenderDepth = MTL::PixelFormatInvalid, m_RenderStencil = MTL::PixelFormatInvalid;
    uint32_t m_RenderWidth = 0, m_RenderHeight = 0;
    uint32_t m_ViewMask = 0;
    uint8_t m_RenderColorNum = 0, m_RenderSampleNum = 1;
    MTL::VisibilityResultMode m_VisibilityMode = MTL::VisibilityResultModeDisabled;
    uint64_t m_VisibilityOffset = 0;
    Result m_Result = Result::SUCCESS;
};

} // namespace nri
