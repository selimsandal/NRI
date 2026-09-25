// © 2026 NVIDIA Corporation
#pragma once

#include <limits.h>

namespace nri {

struct PipelineCacheMetal final : public DebugNameBase {
    PipelineCacheMetal(DeviceMetal& device);
    ~PipelineCacheMetal();
    Result Create(const PipelineCacheDesc& desc);
    Result GetData(void* dst, uint64_t& size) const;
    DeviceMetal& GetDevice() const;
    MTL::BinaryArchive* GetNativeObject() const;

    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

private:
    DeviceMetal& m_Device;
    MTL::BinaryArchive* m_Archive = nullptr;
    char m_Path[PATH_MAX] = {};
};

} // namespace nri
