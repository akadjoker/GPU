# Resources and ownership

Resources are created from descriptors and identified by typed opaque handles:
`BufferHandle`, `TextureHandle`, `SamplerHandle`, `PipelineHandle`, and
`QueryHandle`. A default-constructed handle is invalid and must not be passed
to commands.

Creation and update operations return a handle or `bool`. On failure, query the
device error queue with `getError`; backend implementations may report
`UnsupportedFormat`, `OutOfMemory`, or `InvalidArgument` depending on the
descriptor.

The application owns the lifetime of each resource. Destroy resources before
destroying their device, and destroy each handle at most once:

```cpp
// `device` is a valid device from the lifecycle example.
const std::uint32_t value = 0;
gpu::BufferDesc bufferDesc;
bufferDesc.size = sizeof(value);
bufferDesc.usage = gpu::BufferUsageVertex;
bufferDesc.initialData = {&value, sizeof(value)};

gpu::BufferHandle buffer = device->createBuffer(bufferDesc);
if (buffer) {
    device->updateBuffer(buffer, 0, {&value, sizeof(value)});
    device->destroy(buffer);
}
```

`DataView` and `MutableDataView` are non-owning views. Their memory must remain
valid for the duration of the call. `TextureDataLayout` contains `offset`,
`bytesPerRow`, and `rowsPerImage`; the exact validation and packing behavior is
`REVIEW` because it is not specified in the public header comments.
