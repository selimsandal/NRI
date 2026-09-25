// © 2026 NVIDIA Corporation

#pragma once

namespace nri {

struct UploadChunkMetal {
    MTL::Buffer* buffer = nullptr;
    uint64_t offset = 0;
    uint64_t size = 0;
};

struct CommandAllocatorMetal final : public DebugNameBase {
    inline CommandAllocatorMetal(DeviceMetal& device) : m_Device(device), m_UploadChunks(device.GetStdAllocator()) {
    }

    ~CommandAllocatorMetal();

    inline DeviceMetal& GetDevice() const {
        return m_Device;
    }

    inline MTL4::CommandAllocator* GetNativeObject() const {
        return m_Allocator;
    }

    inline QueueMetal& GetQueue() const {
        return *m_Queue;
    }

    Result Create(const Queue& queue);
    void Reset();
    MTL::GPUAddress Upload(const void* data, uint64_t size, uint64_t alignment = 16);
    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

private:
    DeviceMetal& m_Device;
    QueueMetal* m_Queue = nullptr;
    MTL4::CommandAllocator* m_Allocator = nullptr;
    Vector<UploadChunkMetal> m_UploadChunks;
};

} // namespace nri
