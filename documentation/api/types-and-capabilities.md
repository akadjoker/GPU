# Types and capabilities

Descriptors intentionally contain plain value types so an application can
construct them without backend headers. `Format`, `TextureDimension`,
`VertexFormat`, `Topology`, and the usage/barrier flags describe intent; the
selected backend maps them to native API values.

Before using an optional operation, inspect `device->capabilities()`. Relevant
flags include `compute`, `storageBuffers`, `storageTextures`, `indirectDraw`,
`indirectCount`, `timestampQueries`, `textureArrays`, and
`bindlessTextures`. Limits such as `maxTextureDimension2D`,
`maxUniformBufferSize`, and alignment fields are backend-specific and must not
be assumed to be portable.

Surface state is available through `surfaceState()`. A suspended or lost
surface may require `resumeSurface`, `resizeSurface`, or application-level
recreation before presentation can continue.
