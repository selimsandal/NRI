// © 2026 NVIDIA Corporation

MTL::PixelFormat nri::GetPixelFormatMetal(Format format) {
    switch (format) {
        case Format::R8_UNORM:
            return MTL::PixelFormatR8Unorm;
        case Format::R8_SNORM:
            return MTL::PixelFormatR8Snorm;
        case Format::R8_UINT:
            return MTL::PixelFormatR8Uint;
        case Format::R8_SINT:
            return MTL::PixelFormatR8Sint;
        case Format::RG8_UNORM:
            return MTL::PixelFormatRG8Unorm;
        case Format::RG8_SNORM:
            return MTL::PixelFormatRG8Snorm;
        case Format::RG8_UINT:
            return MTL::PixelFormatRG8Uint;
        case Format::RG8_SINT:
            return MTL::PixelFormatRG8Sint;
        case Format::BGRA8_UNORM:
            return MTL::PixelFormatBGRA8Unorm;
        case Format::BGRA8_SRGB:
            return MTL::PixelFormatBGRA8Unorm_sRGB;
        case Format::RGBA8_UNORM:
            return MTL::PixelFormatRGBA8Unorm;
        case Format::RGBA8_SRGB:
            return MTL::PixelFormatRGBA8Unorm_sRGB;
        case Format::RGBA8_SNORM:
            return MTL::PixelFormatRGBA8Snorm;
        case Format::RGBA8_UINT:
            return MTL::PixelFormatRGBA8Uint;
        case Format::RGBA8_SINT:
            return MTL::PixelFormatRGBA8Sint;
#define NRI_METAL_PLAIN_FORMAT(nriName, metalName) \
    case Format::nriName: \
        return MTL::PixelFormat##metalName
            NRI_METAL_PLAIN_FORMAT(R16_UNORM, R16Unorm);
            NRI_METAL_PLAIN_FORMAT(R16_SNORM, R16Snorm);
            NRI_METAL_PLAIN_FORMAT(R16_UINT, R16Uint);
            NRI_METAL_PLAIN_FORMAT(R16_SINT, R16Sint);
            NRI_METAL_PLAIN_FORMAT(R16_SFLOAT, R16Float);
            NRI_METAL_PLAIN_FORMAT(RG16_UNORM, RG16Unorm);
            NRI_METAL_PLAIN_FORMAT(RG16_SNORM, RG16Snorm);
            NRI_METAL_PLAIN_FORMAT(RG16_UINT, RG16Uint);
            NRI_METAL_PLAIN_FORMAT(RG16_SINT, RG16Sint);
            NRI_METAL_PLAIN_FORMAT(RG16_SFLOAT, RG16Float);
            NRI_METAL_PLAIN_FORMAT(RGBA16_UNORM, RGBA16Unorm);
            NRI_METAL_PLAIN_FORMAT(RGBA16_SNORM, RGBA16Snorm);
            NRI_METAL_PLAIN_FORMAT(RGBA16_UINT, RGBA16Uint);
            NRI_METAL_PLAIN_FORMAT(RGBA16_SINT, RGBA16Sint);
            NRI_METAL_PLAIN_FORMAT(RGBA16_SFLOAT, RGBA16Float);
            NRI_METAL_PLAIN_FORMAT(R32_UINT, R32Uint);
            NRI_METAL_PLAIN_FORMAT(R32_SINT, R32Sint);
            NRI_METAL_PLAIN_FORMAT(R32_SFLOAT, R32Float);
            NRI_METAL_PLAIN_FORMAT(RG32_UINT, RG32Uint);
            NRI_METAL_PLAIN_FORMAT(RG32_SINT, RG32Sint);
            NRI_METAL_PLAIN_FORMAT(RG32_SFLOAT, RG32Float);
            NRI_METAL_PLAIN_FORMAT(RGBA32_UINT, RGBA32Uint);
            NRI_METAL_PLAIN_FORMAT(RGBA32_SINT, RGBA32Sint);
            NRI_METAL_PLAIN_FORMAT(RGBA32_SFLOAT, RGBA32Float);
#undef NRI_METAL_PLAIN_FORMAT
        case Format::B5_G6_R5_UNORM:
            return MTL::PixelFormatB5G6R5Unorm;
        case Format::B5_G5_R5_A1_UNORM:
            return MTL::PixelFormatBGR5A1Unorm;
        case Format::B4_G4_R4_A4_UNORM:
            return MTL::PixelFormatABGR4Unorm;
        case Format::R10_G10_B10_A2_UNORM:
            return MTL::PixelFormatRGB10A2Unorm;
        case Format::R10_G10_B10_A2_UINT:
            return MTL::PixelFormatRGB10A2Uint;
        case Format::R11_G11_B10_UFLOAT:
            return MTL::PixelFormatRG11B10Float;
        case Format::R9_G9_B9_E5_UFLOAT:
            return MTL::PixelFormatRGB9E5Float;
        case Format::BC1_RGBA_UNORM:
            return MTL::PixelFormatBC1_RGBA;
        case Format::BC1_RGBA_SRGB:
            return MTL::PixelFormatBC1_RGBA_sRGB;
        case Format::BC2_RGBA_UNORM:
            return MTL::PixelFormatBC2_RGBA;
        case Format::BC2_RGBA_SRGB:
            return MTL::PixelFormatBC2_RGBA_sRGB;
        case Format::BC3_RGBA_UNORM:
            return MTL::PixelFormatBC3_RGBA;
        case Format::BC3_RGBA_SRGB:
            return MTL::PixelFormatBC3_RGBA_sRGB;
        case Format::BC4_R_UNORM:
            return MTL::PixelFormatBC4_RUnorm;
        case Format::BC4_R_SNORM:
            return MTL::PixelFormatBC4_RSnorm;
        case Format::BC5_RG_UNORM:
            return MTL::PixelFormatBC5_RGUnorm;
        case Format::BC5_RG_SNORM:
            return MTL::PixelFormatBC5_RGSnorm;
        case Format::BC6H_RGB_UFLOAT:
            return MTL::PixelFormatBC6H_RGBUfloat;
        case Format::BC6H_RGB_SFLOAT:
            return MTL::PixelFormatBC6H_RGBFloat;
        case Format::BC7_RGBA_UNORM:
            return MTL::PixelFormatBC7_RGBAUnorm;
        case Format::BC7_RGBA_SRGB:
            return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
        case Format::ETC2_RGB8_UNORM:
            return MTL::PixelFormatETC2_RGB8;
        case Format::ETC2_RGB8_SRGB:
            return MTL::PixelFormatETC2_RGB8_sRGB;
        case Format::ETC2_RGB8_A1_UNORM:
            return MTL::PixelFormatETC2_RGB8A1;
        case Format::ETC2_RGB8_A1_SRGB:
            return MTL::PixelFormatETC2_RGB8A1_sRGB;
        case Format::ETC2_RGB8_A8_UNORM:
            return MTL::PixelFormatEAC_RGBA8;
        case Format::ETC2_RGB8_A8_SRGB:
            return MTL::PixelFormatEAC_RGBA8_sRGB;
        case Format::ETC2_R11_UNORM:
            return MTL::PixelFormatEAC_R11Unorm;
        case Format::ETC2_R11_SNORM:
            return MTL::PixelFormatEAC_R11Snorm;
        case Format::ETC2_R11_G11_UNORM:
            return MTL::PixelFormatEAC_RG11Unorm;
        case Format::ETC2_R11_G11_SNORM:
            return MTL::PixelFormatEAC_RG11Snorm;
#define NRI_METAL_ASTC_FORMAT(nriSize, metalSize) \
    case Format::ASTC_##nriSize##_UNORM: \
        return MTL::PixelFormatASTC_##metalSize##_LDR; \
    case Format::ASTC_##nriSize##_SRGB: \
        return MTL::PixelFormatASTC_##metalSize##_sRGB
            NRI_METAL_ASTC_FORMAT(4X4, 4x4);
            NRI_METAL_ASTC_FORMAT(5X4, 5x4);
            NRI_METAL_ASTC_FORMAT(5X5, 5x5);
            NRI_METAL_ASTC_FORMAT(6X5, 6x5);
            NRI_METAL_ASTC_FORMAT(6X6, 6x6);
            NRI_METAL_ASTC_FORMAT(8X5, 8x5);
            NRI_METAL_ASTC_FORMAT(8X6, 8x6);
            NRI_METAL_ASTC_FORMAT(8X8, 8x8);
            NRI_METAL_ASTC_FORMAT(10X5, 10x5);
            NRI_METAL_ASTC_FORMAT(10X6, 10x6);
            NRI_METAL_ASTC_FORMAT(10X8, 10x8);
            NRI_METAL_ASTC_FORMAT(10X10, 10x10);
            NRI_METAL_ASTC_FORMAT(12X10, 12x10);
            NRI_METAL_ASTC_FORMAT(12X12, 12x12);
#undef NRI_METAL_ASTC_FORMAT
        case Format::D16_UNORM:
            return MTL::PixelFormatDepth16Unorm;
        case Format::D32_SFLOAT:
            return MTL::PixelFormatDepth32Float;
        case Format::D24_UNORM_S8_UINT:
            // Depth24Unorm_Stencil8 is unavailable on Apple GPUs.
            return MTL::PixelFormatInvalid;
        case Format::D32_SFLOAT_S8_UINT:
            return MTL::PixelFormatDepth32Float_Stencil8;
        default:
            return MTL::PixelFormatInvalid;
    }
}

MTL::VertexFormat nri::GetVertexFormatMetal(Format format) {
    switch (format) {
        case Format::R8_UNORM:
            return MTL::VertexFormatUCharNormalized;
        case Format::R8_SNORM:
            return MTL::VertexFormatCharNormalized;
        case Format::R8_UINT:
            return MTL::VertexFormatUChar;
        case Format::R8_SINT:
            return MTL::VertexFormatChar;
        case Format::RG8_UNORM:
            return MTL::VertexFormatUChar2Normalized;
        case Format::RG8_SNORM:
            return MTL::VertexFormatChar2Normalized;
        case Format::RG8_UINT:
            return MTL::VertexFormatUChar2;
        case Format::RG8_SINT:
            return MTL::VertexFormatChar2;
        case Format::RGBA8_UNORM:
            return MTL::VertexFormatUChar4Normalized;
        case Format::RGBA8_SNORM:
            return MTL::VertexFormatChar4Normalized;
        case Format::RGBA8_UINT:
            return MTL::VertexFormatUChar4;
        case Format::RGBA8_SINT:
            return MTL::VertexFormatChar4;
        case Format::R10_G10_B10_A2_UNORM:
            return MTL::VertexFormatUInt1010102Normalized;
        case Format::R16_SFLOAT:
            return MTL::VertexFormatHalf;
        case Format::RG16_SFLOAT:
            return MTL::VertexFormatHalf2;
        case Format::RGBA16_SFLOAT:
            return MTL::VertexFormatHalf4;
        case Format::R32_UINT:
            return MTL::VertexFormatUInt;
        case Format::R32_SINT:
            return MTL::VertexFormatInt;
        case Format::R32_SFLOAT:
            return MTL::VertexFormatFloat;
        case Format::RG32_UINT:
            return MTL::VertexFormatUInt2;
        case Format::RG32_SINT:
            return MTL::VertexFormatInt2;
        case Format::RG32_SFLOAT:
            return MTL::VertexFormatFloat2;
        case Format::RGB32_UINT:
            return MTL::VertexFormatUInt3;
        case Format::RGB32_SINT:
            return MTL::VertexFormatInt3;
        case Format::RGB32_SFLOAT:
            return MTL::VertexFormatFloat3;
        case Format::RGBA32_UINT:
            return MTL::VertexFormatUInt4;
        case Format::RGBA32_SINT:
            return MTL::VertexFormatInt4;
        case Format::RGBA32_SFLOAT:
            return MTL::VertexFormatFloat4;
        default:
            return MTL::VertexFormatInvalid;
    }
}

nri::FormatSupportBits nri::GetFormatSupportMetal(MTL::Device& device, Format format) {
    MTL::PixelFormat pixelFormat = GetPixelFormatMetal(format);
    MTL::VertexFormat vertexFormat = GetVertexFormatMetal(format);

    if (pixelFormat == MTL::PixelFormatInvalid && vertexFormat == MTL::VertexFormatInvalid)
        return FormatSupportBits::UNSUPPORTED;

    FormatSupportBits support = FormatSupportBits::UNSUPPORTED;
    const bool isBc = format >= Format::BC1_RGBA_UNORM && format <= Format::BC7_RGBA_SRGB;
    if (pixelFormat != MTL::PixelFormatInvalid && (!isBc || device.supportsBCTextureCompression()))
        support |= FormatSupportBits::TEXTURE;
    if ((isBc && device.supportsBCTextureCompression()) || (format >= Format::ETC2_RGB8_UNORM && format <= Format::ASTC_12X12_SRGB))
        support |= FormatSupportBits::HOST_COPY;
    if (vertexFormat != MTL::VertexFormatInvalid)
        support |= FormatSupportBits::VERTEX_BUFFER;

    // Metal Feature Set Tables, "Texture buffer pixel formats" (May 21, 2026).
    const bool rgb32 = format == Format::RGB32_UINT || format == Format::RGB32_SINT || format == Format::RGB32_SFLOAT;
    const bool typedBuffer = format >= Format::R8_UNORM && format <= Format::R11_G11_B10_UFLOAT && !rgb32 && format != Format::BGRA8_UNORM && format != Format::BGRA8_SRGB && format != Format::RGBA8_SRGB && format != Format::B5_G6_R5_UNORM && format != Format::B5_G5_R5_A1_UNORM && format != Format::B4_G4_R4_A4_UNORM;
    if (typedBuffer)
        support |= FormatSupportBits::BUFFER | FormatSupportBits::STORAGE_BUFFER;

    const bool depth = format == Format::D16_UNORM || format == Format::D32_SFLOAT || format == Format::D32_SFLOAT_S8_UINT;
    FormatSupportBits multisample = FormatSupportBits::UNSUPPORTED;
    if (device.supportsTextureSampleCount(2))
        multisample |= FormatSupportBits::MULTISAMPLE_2X;
    if (device.supportsTextureSampleCount(4))
        multisample |= FormatSupportBits::MULTISAMPLE_4X;
    if (device.supportsTextureSampleCount(8))
        multisample |= FormatSupportBits::MULTISAMPLE_8X;
    if (depth) {
        support |= FormatSupportBits::DEPTH_STENCIL_ATTACHMENT | multisample;

        return support;
    }

    const bool color = pixelFormat != MTL::PixelFormatInvalid && format < Format::BC1_RGBA_UNORM;
    if (!color)
        return support;

    support |= FormatSupportBits::COLOR_ATTACHMENT | multisample | FormatSupportBits::HOST_COPY;

    const bool integer = format == Format::R8_UINT || format == Format::R8_SINT || format == Format::RG8_UINT || format == Format::RG8_SINT || format == Format::RGBA8_UINT || format == Format::RGBA8_SINT || format == Format::R16_UINT || format == Format::R16_SINT || format == Format::RG16_UINT || format == Format::RG16_SINT || format == Format::RGBA16_UINT || format == Format::RGBA16_SINT || format == Format::R32_UINT || format == Format::R32_SINT || format == Format::RG32_UINT || format == Format::RG32_SINT || format == Format::RGBA32_UINT || format == Format::RGBA32_SINT || format == Format::R10_G10_B10_A2_UINT;
    const bool float32 = format == Format::R32_SFLOAT || format == Format::RG32_SFLOAT || format == Format::RGBA32_SFLOAT;
    const bool packed16 = format == Format::B5_G6_R5_UNORM || format == Format::B5_G5_R5_A1_UNORM || format == Format::B4_G4_R4_A4_UNORM;
    if (!integer)
        support |= FormatSupportBits::BLEND;
    if (!integer && (!float32 || device.supports32BitFloatFiltering()))
        support |= FormatSupportBits::MULTISAMPLE_RESOLVE;

    // Every ordinary format with "Write" in Apple's table can back Metal texture views.
    if (!packed16 && format != Format::R9_G9_B9_E5_UFLOAT)
        support |= FormatSupportBits::STORAGE_TEXTURE;
    if (format == Format::R32_UINT || format == Format::R32_SINT)
        support |= FormatSupportBits::STORAGE_TEXTURE_ATOMICS | FormatSupportBits::STORAGE_BUFFER_ATOMICS;

    return support;
}

MTL::CompareFunction nri::GetCompareMetal(CompareOp compareOp) {
    constexpr std::array<MTL::CompareFunction, (size_t)CompareOp::MAX_NUM> g_CompareFunctions = {
        MTL::CompareFunctionAlways, MTL::CompareFunctionAlways, MTL::CompareFunctionNever, MTL::CompareFunctionEqual, MTL::CompareFunctionNotEqual,
        MTL::CompareFunctionLess, MTL::CompareFunctionLessEqual, MTL::CompareFunctionGreater, MTL::CompareFunctionGreaterEqual};
    NRI_VALIDATE_ARRAY(g_CompareFunctions);

    return g_CompareFunctions[(uint32_t)compareOp];
}
