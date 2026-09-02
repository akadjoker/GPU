#include "gpu/GPUDiagnostics.h"

namespace gpu
{

  const char *toString(GPUErrorCode code)
  {
    switch (code)
    {
    case GPUErrorCode::None:
      return "None";
    case GPUErrorCode::InvalidArgument:
      return "InvalidArgument";
    case GPUErrorCode::InvalidHandle:
      return "InvalidHandle";
    case GPUErrorCode::UnsupportedFeature:
      return "UnsupportedFeature";
    case GPUErrorCode::UnsupportedFormat:
      return "UnsupportedFormat";
    case GPUErrorCode::OutOfBounds:
      return "OutOfBounds";
    case GPUErrorCode::OutOfMemory:
      return "OutOfMemory";
    case GPUErrorCode::ShaderCompilationFailed:
      return "ShaderCompilationFailed";
    case GPUErrorCode::PipelineCreationFailed:
      return "PipelineCreationFailed";
    case GPUErrorCode::DeviceCreationFailed:
      return "DeviceCreationFailed";
    case GPUErrorCode::DeviceLost:
      return "DeviceLost";
    case GPUErrorCode::SurfaceLost:
      return "SurfaceLost";
    case GPUErrorCode::BackendFailure:
      return "BackendFailure";
    case GPUErrorCode::ErrorQueueOverflow:
      return "ErrorQueueOverflow";
    }
    return "Unknown";
  }

  const char *toString(GPUErrorSeverity severity)
  {
    switch (severity)
    {
    case GPUErrorSeverity::Info:
      return "Info";
    case GPUErrorSeverity::Warning:
      return "Warning";
    case GPUErrorSeverity::Error:
      return "Error";
    case GPUErrorSeverity::Fatal:
      return "Fatal";
    }
    return "Unknown";
  }

  const char *toString(GPUOperation operation)
  {
    switch (operation)
    {
    case GPUOperation::None:
      return "None";
    case GPUOperation::CreateBuffer:
      return "CreateBuffer";
    case GPUOperation::CreateTexture:
      return "CreateTexture";
    case GPUOperation::CreateSampler:
      return "CreateSampler";
    case GPUOperation::CreatePipeline:
      return "CreatePipeline";
    case GPUOperation::ReflectPipeline:
      return "ReflectPipeline";
    case GPUOperation::CreateQuery:
      return "CreateQuery";
    case GPUOperation::CreateFence:
      return "CreateFence";
    case GPUOperation::CreateDevice:
      return "CreateDevice";
    case GPUOperation::UpdateBuffer:
      return "UpdateBuffer";
    case GPUOperation::UpdateTexture:
      return "UpdateTexture";
    case GPUOperation::GenerateMipmaps:
      return "GenerateMipmaps";
    case GPUOperation::MapBuffer:
      return "MapBuffer";
    case GPUOperation::ReadBuffer:
      return "ReadBuffer";
    case GPUOperation::ReadTexture:
      return "ReadTexture";
    case GPUOperation::BeginRenderPass:
      return "BeginRenderPass";
    case GPUOperation::SetPipeline:
      return "SetPipeline";
    case GPUOperation::BindResource:
      return "BindResource";
    case GPUOperation::Draw:
      return "Draw";
    case GPUOperation::DrawIndirect:
      return "DrawIndirect";
    case GPUOperation::Dispatch:
      return "Dispatch";
    case GPUOperation::Copy:
      return "Copy";
    case GPUOperation::Present:
      return "Present";
    case GPUOperation::Destroy:
      return "Destroy";
    }
    return "Unknown";
  }

} // namespace gpu
