#ifndef GPU_DIAGNOSTICS_H
#define GPU_DIAGNOSTICS_H

#include "GPUError.h"

namespace gpu
{

  /** @brief Return a stable label for an error code. */
  const char *toString(GPUErrorCode code);
  /** @brief Return a stable label for an error severity. */
  const char *toString(GPUErrorSeverity severity);
  /** @brief Return a stable label for an operation. */
  const char *toString(GPUOperation operation);

} // namespace gpu

#endif
