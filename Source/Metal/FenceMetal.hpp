// © 2026 NVIDIA Corporation

FenceMetal::~FenceMetal() {
    if (m_Event)
        m_Event->release();
}

Result FenceMetal::Create(uint64_t initialValue) {
    m_IsSwapChainSemaphore = initialValue == SWAPCHAIN_SEMAPHORE;
    m_NextValue = m_IsSwapChainSemaphore ? 0 : initialValue;
    m_Event = m_Device.GetNativeObject()->newSharedEvent();
    if (!m_Event)
        return Result::OUT_OF_MEMORY;
    m_Event->setSignaledValue(m_NextValue);

    return Result::SUCCESS;
}

Result FenceMetal::Create(const FenceMetalDesc& desc) {
    m_Event = (MTL::SharedEvent*)desc.mtlSharedEvent;
    m_Event->retain();
    m_NextValue = m_Event->signaledValue();

    return Result::SUCCESS;
}

uint64_t FenceMetal::GetValue() const {
    return m_Event ? m_Event->signaledValue() : 0;
}

uint64_t FenceMetal::NextSignalValue() {
    return ++m_NextValue;
}

void FenceMetal::Wait(uint64_t value) {
    if (!m_Event || m_IsSwapChainSemaphore)
        return;

    // The public NRI wait has no result. Bound the wait so a lost device cannot deadlock the caller forever.
    m_Event->waitUntilSignaledValue(value, NRI_TIMEOUT_FENCE);
}

void FenceMetal::SetDebugName(const char* name) {
    if (m_Event)
        m_Event->setLabel(NS::String::string(name, NS::UTF8StringEncoding));
}
