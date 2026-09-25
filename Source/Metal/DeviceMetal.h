// © 2026 NVIDIA Corporation

#pragma once

namespace nri {

struct DeviceMetal final : public DeviceBase {
    DeviceMetal(const CallbackInterface& callbacks, const AllocationCallbacks& allocationCallbacks);
    ~DeviceMetal();

    inline MTL::Device* GetNativeObject() const {
        return m_Device;
    }

    inline const DeviceDesc& GetDesc() const override {
        return m_Desc;
    }

    Result Create(const DeviceCreationDesc& desc);
    Result Create(const DeviceCreationDesc& desc, const DeviceCreationMetalDesc& metalDesc);
    void Destruct() override;
    void AddResidency(MTL::Allocation* allocation);
    void RemoveResidency(MTL::Allocation* allocation);
    void CommitResidency();
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    MTL::GPUAddress GetTessellatorTables();
#endif

    MTL::ResidencySet* GetResidencySet() const {
        return m_Residency;
    }

    Result GetQueue(QueueType type, uint32_t index, Queue*& queue);
    Result WaitIdle();

    const CoreInterface& GetCoreInterface() const {
        return m_Core;
    }

    Result FillFunctionTable(CoreInterface& table) const override;
    Result FillFunctionTable(HelperInterface& table) const override;
    Result FillFunctionTable(StreamerInterface& table) const override;
    Result FillFunctionTable(SwapChainInterface& table) const override;
    Result FillFunctionTable(MeshShaderInterface& table) const override;
    Result FillFunctionTable(RayTracingInterface& table) const override;
    Result FillFunctionTable(DescriptorHeapInterface& table) const override;
    Result FillFunctionTable(UpscalerInterface& table) const override;
    Result FillFunctionTable(WrapperMetalInterface& table) const override;
#if NRI_ENABLE_IMGUI_EXTENSION
    Result FillFunctionTable(ImguiInterface& table) const override;
#endif

    template <typename Implementation, typename Interface, typename... Args>
    Result CreateImplementation(Interface*& entity, const Args&... args) {
        auto* impl = Allocate<Implementation>(GetAllocationCallbacks(), *this);
        entity = nullptr;

        if (!impl)
            return Result::OUT_OF_MEMORY;

        Result result = impl->Create(args...);

        if (result != Result::SUCCESS)
            Destroy(GetAllocationCallbacks(), impl);
        else
            entity = (Interface*)impl;

        return result;
    }

private:
    Result Create(const DeviceCreationDesc& desc, MTL::Device* device, void* const* queues);
    void FillDesc(const AdapterDesc& adapterDesc);

    DeviceDesc m_Desc = {};
    MTL::Device* m_Device = nullptr;
    MTL::ResidencySet* m_Residency = nullptr;
    QueueMetal* m_Queues[3] = {};
    std::mutex m_ResidencyLock;
    UnorderedMap<MTL::Allocation*, uint32_t> m_ResidencyReferences;
#if NRI_ENABLE_METAL_SHADER_CONVERTER
    MTL::Buffer* m_TessellatorTables = nullptr;
#endif
    CoreInterface m_Core = {};
};

} // namespace nri
