// © 2026 NVIDIA Corporation

#pragma once

#include <atomic>
#include <memory>

namespace nri {

struct QueueFeedbackMetal {
    ~QueueFeedbackMetal();
    std::atomic<NS::Error*> error{nullptr};
    dispatch_group_t pending = dispatch_group_create();
};

struct QueueMetal final : public DebugNameBase {
    inline QueueMetal(DeviceMetal& device) : m_Device(device), m_Feedback(std::allocate_shared<QueueFeedbackMetal>(device.GetStdAllocator())) {
    }

    ~QueueMetal();

    inline DeviceMetal& GetDevice() const {
        return m_Device;
    }

    inline MTL4::CommandQueue* GetNativeObject() const {
        return m_Queue;
    }

    inline QueueType GetType() const {
        return m_Type;
    }

    Result Create(QueueType type);
    Result Create(QueueType type, MTL4::CommandQueue* queue);
    Result Commit(const MTL4::CommandBuffer* const* commandBuffers, uint32_t commandBufferNum);
    Result WaitIdle();
    Result UploadHostMemoryToTexture(const UploadHostMemoryToTextureDesc* copyDescs, uint32_t copyDescNum);
    Result ReadbackTextureToHostMemory(const ReadbackTextureToHostMemoryDesc* copyDescs, uint32_t copyDescNum);
    void SetDebugName(const char* name) NRI_DEBUG_NAME_OVERRIDE;

private:
    DeviceMetal& m_Device;
    std::shared_ptr<QueueFeedbackMetal> m_Feedback;
    MTL4::CommandQueue* m_Queue = nullptr;
    QueueType m_Type = QueueType::MAX_NUM;
};

} // namespace nri
