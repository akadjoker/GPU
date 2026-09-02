# Backend support

The same public API can be built with separate backend components:

| Backend | CMake target | Typical prerequisite |
| --- | --- | --- |
| Null | `gpu` (legacy in-tree target) | None |
| OpenGL | `GPU::gl_static` or `GPU::gl_shared` | A current desktop GL context |
| OpenGL ES | `GPU::gles_static` or `GPU::gles_shared` | GLESv2 library and context |
| Vulkan | `GPU::vk_static` or `GPU::vk_shared` | Vulkan SDK and a platform surface |

Choose exactly one backend component for each executable because each
component provides `gpu::createDevice`. Feature availability is represented by
`GPUCapabilities`; do not infer support from the backend enum alone.

The OpenGL and OpenGL ES implementations use a caller-provided current
context. Vulkan owns its device-side objects after initialization, while the
application remains responsible for creating a compatible native window
surface. **REVIEW:** the callback structures used to pass these contexts and
surfaces currently live under `gpu/backends/`, not in the public include tree;
their exact callback signatures must be documented before presenting them as a
stable integration API.
