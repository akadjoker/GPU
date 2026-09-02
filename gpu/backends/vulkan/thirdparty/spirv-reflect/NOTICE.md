# Vendored: SPIRV-Reflect

Used by `VulkanDevice::reflectPipeline()` to read the resource bindings
(uniform buffers, samplers, storage buffers/images) out of a pipeline's
SPIR-V modules.

- `spirv_reflect.h` / `spirv_reflect.c` — from
  https://github.com/KhronosGroup/SPIRV-Reflect, `main` branch, fetched
  2026-08-29. Copyright 2017-2022 Google Inc., Apache License 2.0 (see the
  header comment in each file for the full notice).
- `include/spirv/unified1/spirv.h` — from
  https://github.com/KhronosGroup/SPIRV-Headers, `main` branch, fetched
  2026-08-29. Copyright 2014-2024 The Khronos Group Inc., MIT License (see
  the header comment for the full notice).

Neither file is modified from upstream. Do not hand-edit them; re-fetch
from the URLs above to update. Warnings are disabled for this directory in
`gpu/CMakeLists.txt` since it's third-party code we don't maintain.
