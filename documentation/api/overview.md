# API overview

GPU exposes a small, backend-independent C++14 interface. Applications create
one `gpu::Device`, create resources through descriptors, submit commands, and
destroy the device when finished.

The public API is grouped into these headers:

- `GPUBackend.h`: backend selection and device creation.
- `GPU.h`: the `gpu::Device` command and resource interface.
- `GPUDescriptors.h`, `GPUTypes.h`, and `GPUHandles.h`: value types,
  descriptors, and opaque resource handles.
- `GPUCapabilities.h` and `GPUSurface.h`: feature discovery and presentation.
- `GPUError.h`, `GPUDiagnostics.h`, and `GPUProfiler.h`: errors, diagnostics,
  and optional GPU timing.

Read the guide pages for behavior and invariants, then use the generated API
reference in `build-docs/docs/generated/` for the complete declaration list.
