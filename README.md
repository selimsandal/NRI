# NVIDIA RENDER INTERFACE (NRI)

[![Status](https://github.com/NVIDIA-RTX/NRI/actions/workflows/build.yml/badge.svg)](https://github.com/NVIDIA-RTX/NRI/actions/workflows/build.yml)

*NRI* is a modular, extensible, low-level abstract rendering interface that was designed to support all low-level features of *D3D12* and *Vulkan* GAPIs. At the same time, it aims to simplify usage and reduce the amount of code needed (especially compared with VK).

Goals:
- Generalization and unification of *D3D12* ([spec](https://microsoft.github.io/DirectX-Specs/)) and VK ([spec](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html))
- Explicitness (providing access to low-level features of modern GAPIs)
- Quality-of-life and high-level extensions (e.g., streaming and upscaling)
- Low overhead
- Cross-platform and platform independence (AMD/INTEL friendly)
- *D3D11* support ([spec](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)) (as much as possible)

Non-goals:
- Exposing entities not existing in GAPIs
- High-level (*D3D11*-like) abstraction
- Hidden management of any kind (except for some high-level extensions where it's desired)
- Automatic barriers (better handled in a higher-level abstraction)

Supported GAPIs:
- *Vulkan 1.4+, 1.3++ or 1.2+++* - advertises modern Vulkan compatibility, with a *Vulkan 1.2* core + _VK_KHR_synchronization2_ baseline
- *D3D12*
- *D3D11*
- *WebGPU* (through [wgpu-native](https://github.com/gfx-rs/wgpu-native))
- *Metal 4* (native backend through [metal-cpp](https://developer.apple.com/metal/cpp/))
- None / dummy (everything is supported but does nothing)

## WHY NRI?

There is [NVRHI](https://github.com/NVIDIA-RTX/NVRHI), which offers a *D3D11*-like abstraction layer, implying some overhead. There is [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton), which was designed for *D3D12* emulation using *Vulkan*. There is [nvpro-samples](https://github.com/nvpro-samples), which explores all "dark corners" of *Vulkan* usage, but does not offer any cross-API support. There are some other good, unmentioned projects, but *NRI* was designed to offer a reasonably simple, low-overhead, and high-performance render interface suitable for game development, professional rendering and hobby projects. Additionally, *NRI* can serve as a middleware for integrations. For instance, the [NRD integration layer](https://github.com/NVIDIA-RTX/NRD/tree/master/Integration) is based on *NRI*, unifying multiple GAPI support within a single codebase.

## KEY FEATURES
 - *Multi-API support* - generalized common denominator for *D3D12*, *Vulkan*, and *D3D11* (default *D3D11* behavior is changed to match *D3D12*/VK using *NVAPI* or **AMD AGS** libraries, where applicable)
 - *C/C++ compatible* - native interface, compatible with both languages (can be used as a *shared* or *static* library)
 - *High performance* - low overhead with zero memory allocations at runtime, honored user-provided memory allocator
 - *Modern hardware tech* - support for Ray Tracing, Mesh Shaders, descriptor indexing (including directly indexed descriptor heaps) and more
 - *Mobile-ready* - optimized for Tile-Based Rendering (TBR/TBDR) architectures and friendly to mobile-oriented API designs
 - *Portable API shape* - maps cleanly to constrained GAPIs, including WebGPU and consoles
 - *D3D12 Ultimate* - full support, including Enhanced Barriers
 - *Platform Flexibility* - Windows, Linux, MacOS, and Android support
 - *Developer Experience* - integrated GAPI- and NRI- validation, VK [printf](https://github.com/KhronosGroup/*Vulkan*-ValidationLayers/blob/main/docs/debug_printf.md), and timeline annotations (GAPI, [NVTX](https://github.com/NVIDIA/NVTX) and [PIX](https://devblogs.microsoft.com/pix/winpixeventruntime/), if "WinPixEventRuntime.dll" is nearby)
 - *Memory Management* - deep integration with *AMD Virtual Memory Allocator* to ease memory management and memory reuse

Available interfaces:
 - `NRI.h` - core functionality
 - `NRIDeviceCreation.h` - device creation and related functionality
 - `NRIHelper.h` - a collection of various helpers to ease use of the core interface
 - `NRIImgui.h` - a light-weight *ImGui* renderer (no *ImGui* dependency)
 - `NRILowLatency.h` - low latency support (aka *NVIDIA REFLEX*)
 - `NRIMeshShader.h` - mesh shaders
 - `NRIRayTracing.h` - ray tracing
 - `NRIStreamer.h` - a convenient way to stream data into resources
 - `NRISwapChain.h` - swap chain and related functionality
 - `NRIUpscaler.h` - a configurable collection of common upscalers (*NIS*, *FSR*, *DLSS-SR*, *DLSS-RR*)

Repository organization:
- there is only `main` branch used for development
- stable versions are in `Releases` section

## BUILD INSTRUCTIONS

- Install [*Cmake*](https://cmake.org/download/) 3.30+
- Build (variant 1) - using *Git* and *CMake* explicitly
    - Clone project and init submodules
    - Generate and build the project using *CMake*
    - To build the binary with static MSVC runtime, add `-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"` parameter when deploying the project
- Build (variant 2) - by running scripts:
    - Run `1-Deploy`
    - Run `2-Build`

Notes:
- *Xlib* and *Wayland* can be both enabled
- Minimal supported client is Windows 8.1+
- The native Metal 4 backend requires macOS 26, Xcode 26+, and [metal-cpp](https://developer.apple.com/metal/cpp/) headers. Native `.metallib` shaders need no converter; HLSL/DXIL requires Metal Shader Converter 4.0.1 and `NRI_ENABLE_METAL_SHADER_CONVERTER=ON`.
- Install the Metal compiler with `xcodebuild -downloadComponent MetalToolchain`. CMake downloads pinned metal-cpp headers from Apple when the Metal backend is enabled. Compile native shaders with `xcrun -sdk macosx metal -c shader.metal -o shader.air` and `xcrun -sdk macosx metallib shader.air -o shader.metallib`; [NRI.metal](Include/NRI.metal) describes the native binding ABI.
- Metal Shader Converter installs headers and `libmetalirconverter.dylib` under `/usr/local` by default. Override their paths with the CMake options below. SDK packaging does not redistribute Converter; applications using DXIL must install it separately under Apple's license.
- Query `DeviceDesc` and `GetFormatSupport` for optional Metal features. Metal 4 acceleration structures require Apple9 (M3/A17 Pro) or newer; depth bounds require Apple10. Ray-tracing execution and depth bounds have not been verified on supported hardware.
- Metal supports two-view layer-based and viewport-based multiview. Converter-free builds also expose flexible multiview through the native view-index binding in [NRI.metal](Include/NRI.metal). Metal Shader Converter does not support `SV_ViewID`. Geometry and tessellation stages require Converter's mesh-shader emulation; Metal 4 has no native patch-draw commands. Native shaders can use mesh/object stages directly.
- Metal swapchains support G22, linear-float and PQ color spaces, and waitable presentation through drawable presentation callbacks. `GetDisplayDesc`, LowLatency and Video interfaces are unsupported: the public Apple APIs do not expose the complete NRI contracts. Variable-rate rasterization maps do not implement NRI shading-rate semantics. Pipeline-statistics queries, micromaps and region resolves are not exposed.
- NIS requires `NRI_ENABLE_NIS_SDK=ON` and Metal Shader Converter. Other upscalers are unsupported on Metal; MetalFX is not an NRI upscaler type. Native fragment shaders implement sample masks with `[[sample_mask]]`; pipeline sample masks require Converter.
- Set `MTL_DEBUG_LAYER=1` before launching to enable Apple API validation. `MTL_SHADER_VALIDATION=1` additionally instruments shaders, but is incompatible with binary archives and can exceed hardware resource limits for large kernels. Run pipeline-cache tests separately without shader instrumentation.

## CMAKE OPTIONS

- `NRI_AGILITY_SDK_DIR` - Directory where *Agility SDK* will be copied relative to the directory with binaries
- `NRI_AGILITY_SDK_VERSION_MAJOR`- *Agility SDK* major version
- `NRI_AGILITY_SDK_VERSION_MINOR` - *Agility SDK* minor version
- `NRI_SHADERS_PATH` - Shader output path override
- `NRI_NVAPI_CUSTOM_PATH` - Path to a custom NVAPI library directory
- `NRI_METAL_SHADER_CONVERTER_INCLUDE_PATH` - Optional Metal Shader Converter include root
- `NRI_METAL_SHADER_CONVERTER_LIBRARY` - Optional Metal Shader Converter library
- `NRI_STATIC_LIBRARY` - Build static library
- `NRI_ENABLE_NVTX_SUPPORT` - Annotations for NVIDIA Nsight Systems
- `NRI_ENABLE_DEBUG_NAMES_AND_ANNOTATIONS` - Enable debug names, host and device annotations
- `NRI_ENABLE_NONE_SUPPORT` - Enable NONE backend
- `NRI_ENABLE_VK_SUPPORT` - Enable *Vulkan* backend
- `NRI_ENABLE_WGPU_SUPPORT` - Enable *WebGPU* backend through *wgpu-native*
- `NRI_ENABLE_METAL_SUPPORT` - Enable the native Metal 4 backend on Apple platforms
- `NRI_ENABLE_METAL_SHADER_CONVERTER` - Enable DXIL conversion in the Metal backend; native `.metallib` shaders do not need it
- `NRI_ENABLE_VALIDATION_SUPPORT` - Enable Validation backend (otherwise `enableNRIValidation` is ignored)
- `NRI_ENABLE_NIS_SDK` - Enable NVIDIA Image Sharpening SDK
- `NRI_ENABLE_IMGUI_EXTENSION` - Enable `NRIImgui` extension
- `NRI_STREAMER_THREAD_SAFE` - `NRIStreamer` thread safety (`OFF` is faster)
- `NRI_ENABLE_D3D11_SUPPORT` - Enable *D3D11* backend
- `NRI_ENABLE_D3D12_SUPPORT` - Enable *D3D12* backend
- `NRI_ENABLE_AMDAGS`- Enable *AMD AGS* library for D3D
- `NRI_ENABLE_NVAPI` - Enable NVAPI library for D3D
- `NRI_ENABLE_AGILITY_SDK_SUPPORT` - Enable *Agility SDK* support to unlock access to latest *D3D12* features
- `NRI_ENABLE_XLIB_SUPPORT` - Enable X11 support
- `NRI_ENABLE_WAYLAND_SUPPORT` - Enable Wayland support
- `NRI_ENABLE_NGX_SDK` - Enable *NVIDIA NGX (DLSS)* SDK
- `NRI_ENABLE_FFX_SDK` - Enable *AMD FidelityFX* SDK
- `NRI_ENABLE_XESS_SDK` - Enable *INTEL XeSS* SDK

## AGILITY SDK

The bare minimum requirement for *D3D12* backend is *D3D12 Ultimate* (*Windows SDK 10.0.20348*, last pre-*Agility SDK* release). *NRI* can be compiled for this *Windows SDK* using `1-Deploy.bat -DNRI_ENABLE_AGILITY_SDK_SUPPORT=OFF -A "x64,version=10.0.20348.0"` command line. But using the latest *Agility SDK* is highly recommended to get access to most recent *D3D12* features and improved validation. See *Overview* and *Download* sections [*here*](https://devblogs.microsoft.com/directx/directx12agility/) for more details about *Agility SDK*.

Steps (already enabled by default):
- modify `NRI_AGILITY_SDK_VERSION_MAJOR` and `NRI_AGILITY_SDK_VERSION_MINOR` to the desired value
- enable or disable `NRI_ENABLE_AGILITY_SDK_SUPPORT`
- re-deploy project
- (optional) include auto-generated `NRIAgilitySDK.h` header in the code of your executable using *NRI*

It's safe to compile *NRI* using the default (most likely latest) *Agility SDK* and export some specific `D3D12SDKVersion` in the executable using *NRI*. In this case, *NRI* queries latest "available" interfaces according to the actually loaded `D3D12Core.dll`.

## KNOWN VULKAN EXTENSIONS

Required:
 - _VK_KHR_synchronization2_
 - (APPLE) _VK_KHR_portability_enumeration_
 - (APPLE) _VK_KHR_get_physical_device_properties2_
 - (APPLE) _VK_KHR_portability_subset_

Supported:
 - (Instance) _VK_KHR_get_surface_capabilities2_
 - (Instance) _VK_KHR_surface_
 - (Instance) _VK_KHR_win32_surface_ (_VK_KHR_xlib_surface_, _VK_KHR_wayland_surface_,  _VK_EXT_metal_surface_)
 - (Instance) _VK_EXT_swapchain_colorspace_
 - (Instance) _VK_EXT_debug_utils_
 - (Instance) _VK_EXT_surface_maintenance1_
 - _VK_KHR_copy_commands2_
 - _VK_KHR_dynamic_rendering_
 - _VK_KHR_swapchain_
 - _VK_KHR_present_id_
 - _VK_KHR_present_wait_
 - _VK_KHR_swapchain_mutable_format_
 - _VK_KHR_push_descriptor_
 - _VK_KHR_maintenance4_
 - _VK_KHR_maintenance5_
 - _VK_KHR_maintenance6_
 - _VK_KHR_maintenance7_
 - _VK_KHR_maintenance8_
 - _VK_KHR_maintenance9_
 - _VK_KHR_line_rasterization_
 - _VK_KHR_fragment_shading_rate_
 - _VK_KHR_pipeline_library_
 - _VK_KHR_ray_tracing_pipeline_
 - _VK_KHR_acceleration_structure_ (depends on _VK_KHR_deferred_host_operations_)
 - _VK_KHR_ray_query_
 - _VK_KHR_ray_tracing_position_fetch_
 - _VK_KHR_ray_tracing_maintenance1_
 - _VK_KHR_fragment_shader_barycentric_
 - _VK_KHR_shader_clock_
 - _VK_KHR_compute_shader_derivatives_
 - _VK_KHR_unified_image_layouts_
 - _VK_KHR_shader_integer_dot_product_
 - _VK_KHR_dynamic_rendering_local_read_
 - _VK_EXT_extended_dynamic_state_
 - _VK_EXT_calibrated_timestamps_
 - _VK_EXT_pipeline_creation_cache_control_
 - _VK_EXT_pipeline_robustness_
 - _VK_EXT_image_robustness_
 - _VK_EXT_subgroup_size_control_
 - _VK_EXT_mutable_descriptor_type_
 - _VK_EXT_swapchain_maintenance1_
 - _VK_EXT_present_mode_fifo_latest_ready_
 - _VK_EXT_opacity_micromap_
 - _VK_EXT_sample_locations_
 - _VK_EXT_conservative_rasterization_
 - _VK_EXT_mesh_shader_
 - _VK_EXT_shader_atomic_float_
 - _VK_EXT_shader_atomic_float2_
 - _VK_EXT_memory_budget_
 - _VK_EXT_memory_priority_
 - _VK_EXT_image_sliced_view_of_3d_
 - _VK_EXT_custom_border_color_
 - _VK_EXT_robustness2_
 - _VK_EXT_fragment_shader_interlock_
 - _VK_EXT_zero_initialize_device_memory_
 - _VK_NV_low_latency2_
 - _VK_NVX_binary_import_
 - _VK_NVX_image_view_handle_

## SAMPLES OVERVIEW

[*NRD sample*](https://github.com/NVIDIA-RTX/NRD-Sample):
- main sample demonstrating path tracing best practices

[*NRI samples*](https://github.com/NVIDIA-RTX/NRISamples):
- many samples demonstrating NRI usage

## C/C++ INTERFACE DIFFERENCES

| C++                   | C                     |
|-----------------------|-----------------------|
| `nri::Interface`      | `NriInterface`        |
| `nri::Enum::MEMBER`   | `NriEnum_MEMBER`      |
| `nri::CONST`          | `NRI_CONST`           |
| `nri::nriFunction`    | `nriFunction`         |
| `nri::Function`       | `nriFunction`         |
| Reference `&`         | Pointer `*`           |

## ENTITIES

| NRI                     | *D3D11*                                 | *D3D12*                         | VK                           | WGPU                                      |
|-------------------------|-----------------------------------------|---------------------------------|------------------------------|-------------------------------------------|
| `Device`                | `ID3D11Device`                          | `ID3D12Device`                  | `VkDevice`                   | `WGPUDevice`                              |
| `CommandBuffer`         | `ID3D11DeviceContext` (deferred)        | `ID3D12CommandList`             | `VkCommandBuffer`            | `WGPUCommandBuffer`                       |
| `CommandQueue`          | `ID3D11DeviceContext` (immediate)       | `ID3D12CommandQueue`            | `VkQueue`                    | `WGPUQueue`                               |
| `Fence`                 | `ID3D11Fence`                           | `ID3D12Fence`                   | `VkSemaphore` (timeline)     | N/A                                       |
| `CommandAllocator`      | N/A                                     | `ID3D12CommandAllocator`        | `VkCommandPool`              | N/A                                       |
| `Buffer`                | `ID3D11Buffer`                          | `ID3D12Resource`                | `VkBuffer`                   | `WGPUBuffer`                              |
| `Texture`               | `ID3D11Texture`                         | `ID3D12Resource`                | `VkImage`                    | `WGPUTexture`                             |
| `Memory`                | N/A                                     | `ID3D12Heap`                    | `VkDeviceMemory`             | N/A                                       |
| `Descriptor`            | `ID3D11*View` or `ID3D11SamplerState`   | `D3D12_CPU_DESCRIPTOR_HANDLE`   | `Vk*View` or `VkSampler`     | `WGPUTextureView`, `WGPUBuffer`, `WGPUSampler` |
| `DescriptorSet`         | N/A                                     | N/A                             | `VkDescriptorSet`            | N/A                                       |
| `DescriptorPool`        | N/A                                     | `ID3D12DescriptorHeap`          | `VkDescriptorPool`           | N/A                                       |
| `PipelineLayout`        | N/A                                     | `ID3D12RootSignature`           | `VkPipelineLayout`           | N/A                                       |
| `Pipeline`              | `ID3D11*Shader` and `ID3D11*State`      | `ID3D12StateObject`             | `VkPipeline`                 | N/A                                       |
| `AccelerationStructure` | N/A                                     | `ID3D12Resource`                | `VkAccelerationStructure`    | N/A                                       |

## LICENSE

*NRI* is licensed under the MIT License.
