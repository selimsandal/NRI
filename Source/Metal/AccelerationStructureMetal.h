// © 2026 NVIDIA Corporation

#pragma once

#include <Metal/MTL4AccelerationStructure.hpp>
#include "BufferMetal.h"

namespace nri {

struct AccelerationStructureMetal final : public DebugNameBase {
    inline AccelerationStructureMetal(DeviceMetal& device) : m_Device(device), m_BarrierBuffer(device) {
    }

    ~AccelerationStructureMetal();

    inline DeviceMetal& GetDevice() const {
        return m_Device;
    }

    inline MTL::AccelerationStructure* GetNativeObject() const {
        return m_AccelerationStructure;
    }

    inline AccelerationStructureBits GetFlags() const {
        return m_Flags;
    }

    inline uint64_t GetBuildScratchBufferSize() const {
        return m_Sizes.buildScratchBufferSize;
    }

    inline uint64_t GetUpdateScratchBufferSize() const {
        return m_Sizes.refitScratchBufferSize;
    }

    inline uint64_t GetSize() const {
        return m_Size;
    }

    inline Buffer* GetBuffer() const {
        return (Buffer*)&m_BarrierBuffer;
    }

    inline uint64_t GetHandle() const {
        return m_AccelerationStructure ? m_AccelerationStructure->gpuResourceID()._impl : 0;
    }

    inline MTL::Buffer* GetShaderBindingHeaderBuffer() const {
        return m_ShaderBindingHeader;
    }

    inline MTL::GPUAddress GetShaderBindingHeaderAddress() const {
        return m_ShaderBindingHeader ? m_ShaderBindingHeader->gpuAddress() : 0;
    }

    inline uint64_t GetInstanceContributionAddress() const {
        return GetShaderBindingHeaderAddress() + m_InstanceContributionOffset;
    }

    Result Create(const AccelerationStructureDesc& desc);
    Result Allocate(MemoryLocation memoryLocation, float priority, bool committed);
    Result Bind(MemoryMetal& memory, uint64_t offset);
    void GetMemoryDesc(MemoryLocation memoryLocation, MemoryDesc& memoryDesc) const;
    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

    // The returned descriptor is owned by the caller and must be released after command encoding.
    MTL4::AccelerationStructureDescriptor* CreateBuildDescriptor(const BottomLevelGeometryDesc* geometries, uint32_t geometryNum, MTL::GPUAddress convertedInstanceAddress = 0, uint32_t instanceNum = 0) const;

private:
    MTL4::AccelerationStructureDescriptor* CreateBottomLevelDescriptor(const BottomLevelGeometryDesc* geometries, uint32_t geometryNum, bool sizing) const;
    MTL4::AccelerationStructureDescriptor* CreateTopLevelDescriptor(MTL::GPUAddress convertedInstanceAddress, uint32_t instanceNum) const;
    Result CreateShaderBindingHeader(uint32_t instanceNum);
    void Release();

    DeviceMetal& m_Device;
    // NRI exposes this only as a barrier identity. Metal AS storage is not an MTLBuffer.
    BufferMetal m_BarrierBuffer;
    MTL::AccelerationStructure* m_AccelerationStructure = nullptr;
    MTL::Buffer* m_ShaderBindingHeader = nullptr;
    MTL::AccelerationStructureSizes m_Sizes = {};
    AccelerationStructureBits m_Flags = AccelerationStructureBits::NONE;
    AccelerationStructureType m_Type = AccelerationStructureType::BOTTOM_LEVEL;
    uint64_t m_Size = 0;
    uint64_t m_InstanceContributionOffset = 0;
    bool m_IsResident = false;
};

} // namespace nri
