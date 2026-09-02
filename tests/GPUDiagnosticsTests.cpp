#include "gpu/GPUDiagnostics.h"

#include <cassert>
#include <cstring>

using namespace gpu;

namespace {

void testErrorCodeStrings() {
  assert(std::strcmp(toString(GPUErrorCode::None), "None") == 0);
  assert(std::strcmp(toString(GPUErrorCode::InvalidArgument),
                     "InvalidArgument") == 0);
  assert(std::strcmp(toString(GPUErrorCode::ErrorQueueOverflow),
                     "ErrorQueueOverflow") == 0);
  for (std::uint16_t value = 0;
      value <= static_cast<std::uint16_t>(GPUErrorCode::ErrorQueueOverflow);
      ++value) {
    const char *text = toString(static_cast<GPUErrorCode>(value));
    assert(text != nullptr);
    assert(std::strcmp(text, "Unknown") != 0);
  }
}

void testSeverityStrings() {
  assert(std::strcmp(toString(GPUErrorSeverity::Info), "Info") == 0);
  assert(std::strcmp(toString(GPUErrorSeverity::Fatal), "Fatal") == 0);
}

void testOperationStrings() {
  assert(std::strcmp(toString(GPUOperation::CreateBuffer), "CreateBuffer") ==
        0);
  assert(std::strcmp(toString(GPUOperation::Destroy), "Destroy") == 0);
  for (std::uint16_t value = 0;
      value <= static_cast<std::uint16_t>(GPUOperation::Destroy); ++value) {
    const char *text = toString(static_cast<GPUOperation>(value));
    assert(text != nullptr);
    assert(std::strcmp(text, "Unknown") != 0);
  }
}

} // namespace

int main() {
  testErrorCodeStrings();
  testSeverityStrings();
  testOperationStrings();
  return 0;
}
