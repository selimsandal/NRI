// © 2026 NVIDIA Corporation
#pragma once

#if NRI_ENABLE_METAL_SHADER_CONVERTER
#    define IR_RUNTIME_METALCPP
#    define IR_RUNTIME_METAL4
#    include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#endif

namespace nri {

struct PipelineMetal final : public DebugNameBase {
    PipelineMetal(DeviceMetal& device);
    ~PipelineMetal();
    Result Create(const GraphicsPipelineDesc& desc);
    Result Create(const ComputePipelineDesc& desc);
    Result Create(const RayTracingPipelineDesc& desc);
    Result WriteShaderGroupIdentifiers(uint32_t baseShaderGroupIndex, uint32_t shaderGroupNum, uint32_t dstStride, void* dst) const;
    DeviceMetal& GetDevice() const;
    MTL::RenderPipelineState* GetRenderPipeline() const;
    MTL::ComputePipelineState* GetComputePipeline() const;
    MTL::VisibleFunctionTable* GetVisibleFunctionTable() const;
    MTL::IntersectionFunctionTable* GetIntersectionFunctionTable() const;
    MTL::ResourceID GetVisibleFunctionTableResourceID() const;
    MTL::ResourceID GetIntersectionFunctionTableResourceID() const;
    MTL::DepthStencilState* GetDepthStencilState() const;
    MTL::PrimitiveType GetPrimitiveType() const;
    MTL::CullMode GetCullMode() const;
    MTL::Winding GetWinding() const;
    MTL::TriangleFillMode GetFillMode() const;
    MTL::DepthClipMode GetDepthClipMode() const;
    bool IsDepthBoundsEnabled() const;
    bool HasSampleLocations() const;
    const DepthBiasDesc& GetDepthBias() const;
    MTL::Size GetThreadGroupSize() const;
    MTL::Size GetMeshThreadGroupSize() const;
    MTL::Size GetTaskThreadGroupSize() const;
    bool IsConverted() const;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    bool IsGeometryEmulation() const;
    bool IsTessellationEmulation() const;
    const IRRuntimeGeometryPipelineConfig& GetGeometryConfig() const;
    const IRRuntimeTessellationPipelineConfig& GetTessellationConfig() const;
    IRRuntimePrimitiveType GetEmulationPrimitive() const;
    uint32_t GetVertexStride(uint32_t bindingSlot) const;
#endif
    const PipelineLayoutMetal& GetLayout() const;
    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

private:
    Result LoadFunction(const ShaderDesc& shader, MTL::Library*& library, MTL::Function*& function, const VertexInputDesc* vertexInput = nullptr, MTL::Library** stageInLibrary = nullptr, bool emulation = false, uint32_t sampleMask = ALL);
    DeviceMetal& m_Device;
    const PipelineLayoutMetal* m_Layout = nullptr;
    MTL::RenderPipelineState* m_Render = nullptr;
    MTL::ComputePipelineState* m_Compute = nullptr;
    MTL::VisibleFunctionTable* m_VisibleFunctionTable = nullptr;
    MTL::IntersectionFunctionTable* m_IntersectionFunctionTable = nullptr;
    Vector<uint8_t> m_ShaderGroupIdentifiers;
    MTL::DepthStencilState* m_DepthStencil = nullptr;
    MTL::PrimitiveType m_Primitive = MTL::PrimitiveTypeTriangle;
    MTL::CullMode m_Cull = MTL::CullModeNone;
    MTL::Winding m_Winding = MTL::WindingClockwise;
    MTL::TriangleFillMode m_Fill = MTL::TriangleFillModeFill;
    MTL::DepthClipMode m_DepthClip = MTL::DepthClipModeClip;
    bool m_DepthBounds = false;
    bool m_SampleLocations = false;
    DepthBiasDesc m_DepthBias = {};
    MTL::Size m_ThreadGroup = MTL::Size(1, 1, 1), m_MeshGroup = MTL::Size(1, 1, 1), m_TaskGroup = MTL::Size(1, 1, 1);
    uint32_t m_MeshPayloadSize = 0;
    bool m_Converted = false;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    IRRuntimeGeometryPipelineConfig m_GeometryConfig = {};
    IRRuntimeTessellationPipelineConfig m_TessellationConfig = {};
    IRRuntimePrimitiveType m_EmulationPrimitive = IRRuntimePrimitiveTypeTriangle;
    uint16_t m_VertexStrides[31] = {};
    bool m_GeometryEmulation = false;
    bool m_TessellationEmulation = false;
#endif
};

} // namespace nri
