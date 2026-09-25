// © 2026 NVIDIA Corporation

// Goal: wrapping native Metal objects into NRI objects

#pragma once

#define NRI_WRAPPER_METAL_H 1

#include "NRIDeviceCreation.h"

NriNamespaceBegin

// Native pointers use these exact metal-cpp types: MTL::Device*, MTL4::CommandQueue*, MTL::Buffer*, MTL::Texture*, and MTL::SharedEvent*.
// NRI retains every supplied native object until the corresponding NRI object is destroyed.
NriStruct(DeviceCreationMetalDesc) {
    void* mtlDevice;                 // MTL::Device*
    NriOptional void* mtl4Queues[3]; // MTL4::CommandQueue*, indexed by QueueType
    NriOptional Nri(CallbackInterface) callbackInterface;
    NriOptional Nri(AllocationCallbacks) allocationCallbacks;

    // Switches (disabled by default)
    bool enableNRIValidation;
};

NriStruct(BufferMetalDesc) {
    void* mtlBuffer;      // MTL::Buffer*
    Nri(BufferDesc) desc; // complete NRI metadata for the native buffer
};

NriStruct(TextureMetalDesc) {
    void* mtlTexture;      // MTL::Texture*
    Nri(TextureDesc) desc; // complete NRI metadata for the native texture
};

NriStruct(FenceMetalDesc) {
    void* mtlSharedEvent; // MTL::SharedEvent*
};

// Threadsafe: yes
NriStruct(WrapperMetalInterface) {
    Nri(Result)(NRI_CALL * CreateBufferMetal)(NriRef(Device) device, const NriRef(BufferMetalDesc) bufferMetalDesc, NriOut NriRef(Buffer*) buffer);
    Nri(Result)(NRI_CALL * CreateTextureMetal)(NriRef(Device) device, const NriRef(TextureMetalDesc) textureMetalDesc, NriOut NriRef(Texture*) texture);
    Nri(Result)(NRI_CALL * CreateFenceMetal)(NriRef(Device) device, const NriRef(FenceMetalDesc) fenceMetalDesc, NriOut NriRef(Fence*) fence);
};

NRI_API Nri(Result)
NRI_CALL nriCreateDeviceFromMetalDevice(const NriRef(DeviceCreationMetalDesc) deviceDesc, NriOut NriRef(Device*) device);

NriNamespaceEnd
