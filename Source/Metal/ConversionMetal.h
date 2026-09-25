// © 2026 NVIDIA Corporation

#pragma once

namespace nri {

MTL::PixelFormat GetPixelFormatMetal(Format format);
MTL::VertexFormat GetVertexFormatMetal(Format format);
FormatSupportBits GetFormatSupportMetal(const MTL::Device& device, Format format);
MTL::CompareFunction GetCompareMetal(CompareOp compareOp);

} // namespace nri
