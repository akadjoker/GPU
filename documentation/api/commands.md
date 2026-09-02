# Commands and ordering

Render commands are submitted through `gpu::Device`. A typical frame follows
this order:

1. Begin a render pass with `beginRenderPass`.
2. Set the pipeline, viewport, scissor, and resource bindings.
3. Issue `draw` or `drawIndexed` calls.
4. End the pass with `endRenderPass`.
5. Submit compute work, copies, or barriers as required.
6. Call `present` when the surface is ready.

`gpu::RenderPassScope` is a convenient RAII wrapper for balanced begin/end
calls. Commands that return `bool` report validation or capability failures;
the device error queue contains the detailed error.

Buffer and texture offsets must satisfy the limits reported by
`GPUCapabilities`, including uniform/storage alignment. Use
`memoryBarrier` between writes and subsequent reads when the backend requires a
visibility transition. Query and indirect operations are optional features and
must be checked before use.
