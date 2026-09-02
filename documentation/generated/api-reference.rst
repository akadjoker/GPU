GPU API Reference
==================

Generated from ``gpu/include/gpu/`` by Doxygen XML.

gpu::BlendComponent
-------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

One color or alpha blend equation.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``BlendFactor gpu::BlendComponent::sourceFactor = BlendFactor::One``
     - Source blend factor.
   * - ``BlendFactor gpu::BlendComponent::destinationFactor = BlendFactor::Zero``
     - Destination blend factor.
   * - ``BlendOperation gpu::BlendComponent::operation = BlendOperation::Add``
     - Blend arithmetic operation.

gpu::BufferDesc
---------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Parameters used to create a buffer.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``std::uint64_t gpu::BufferDesc::size = 0``
     - Requested buffer size in bytes.
   * - ``std::uint32_t gpu::BufferDesc::usage = 0``
     - Bitwise combination of BufferUsage values.
   * - ``DataView gpu::BufferDesc::initialData``
     - Optional initial bytes copied during creation.
   * - ``const char* gpu::BufferDesc::debugName = nullptr``
     - Optional non-owning debug label.

gpu::ColorTargetState
---------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Color-target format and blending state for a pipeline.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``Format gpu::ColorTargetState::format = Format::RGBA8Srgb``
     - Target storage format.
   * - ``BlendComponent gpu::ColorTargetState::colorBlend``
     - RGB blend equation.
   * - ``BlendComponent gpu::ColorTargetState::alphaBlend``
     - Alpha blend equation.
   * - ``std::uint8_t gpu::ColorTargetState::writeMask = ColorWriteAll``
     - Bitwise combination of ColorWrite values.
   * - ``bool gpu::ColorTargetState::blendEnabled = false``
     - Enable color and alpha blending.
   * - ``bool gpu::ColorTargetState::surface = false``
     - Use the presentation surface's native target format when needed.

gpu::DataView
-------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Non-owning immutable byte range supplied to a device call.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``const void* gpu::DataView::data = nullptr``
     - Start address of the byte range.
   * - ``std::uint64_t gpu::DataView::size = 0``
     - Number of bytes available at data.

gpu::DepthStencilState
----------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Depth and stencil state for a pipeline.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``Format gpu::DepthStencilState::format = Format::Depth24Stencil8``
     - Depth/stencil attachment format.
   * - ``CompareOp gpu::DepthStencilState::depthCompare = CompareOp::LessEqual``
     - Depth comparison operation.
   * - ``StencilFaceState gpu::DepthStencilState::stencilFront``
     - Front-face stencil state.
   * - ``StencilFaceState gpu::DepthStencilState::stencilBack``
     - Back-face stencil state.
   * - ``std::uint32_t gpu::DepthStencilState::stencilReadMask = 0xff``
     - Bits read by stencil comparisons.
   * - ``std::uint32_t gpu::DepthStencilState::stencilWriteMask = 0xff``
     - Bits written by stencil operations.
   * - ``bool gpu::DepthStencilState::depthWriteEnabled = true``
     - Enable depth writes.
   * - ``bool gpu::DepthStencilState::depthTestEnabled = false``
     - Enable depth testing.
   * - ``bool gpu::DepthStencilState::stencilEnabled = false``
     - Enable stencil testing.

gpu::Device
-----------
:Kind: class
:Header: ``gpu/include/gpu/GPU.h``

Backend-independent device and command interface.

Functions and methods
~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Signature
     - Description
   * - ``virtual gpu::Device::~Device()=default``
     - Destroy the device interface. Use destroyDevice for ownership.
   * - ``virtual const GPUCapabilities & gpu::Device::capabilities() const =0``
     - Return capabilities reported by this device.
   * - ``virtual std::uint64_t gpu::Device::totalErrorCount() const =0``
     - Return the total number of errors recorded by this device.
   * - ``virtual std::uint32_t gpu::Device::pendingErrorCount() const =0``
     - Return the number of pending errors available from this device.
   * - ``virtual bool gpu::Device::getError(::gpu::GPUError &error)=0``
     - Read the oldest pending device error.
   * - ``virtual void gpu::Device::clearErrors()=0``
     - Discard all pending device errors.
   * - ``virtual void gpu::Device::shutdown()=0``
     - Shut down backend resources owned by this device.
   * - ``virtual BufferHandle gpu::Device::createBuffer(const BufferDesc &desc)=0``
     - Create a buffer from a descriptor.
   * - ``virtual TextureHandle gpu::Device::createTexture(const TextureDesc &desc)=0``
     - Create a texture from a descriptor.
   * - ``virtual SamplerHandle gpu::Device::createSampler(const SamplerDesc &desc)=0``
     - Create a sampler from a descriptor.
   * - ``virtual PipelineHandle gpu::Device::createPipeline(const PipelineDesc &desc)=0``
     - Create a graphics or compute pipeline from a descriptor.
   * - ``virtual QueryHandle gpu::Device::createQuery(QueryType type)=0``
     - Create a query of the requested type.
   * - ``virtual void gpu::Device::destroy(BufferHandle handle)=0``
     - Destroy a buffer handle.
   * - ``virtual void gpu::Device::destroy(TextureHandle handle)=0``
     - Destroy a texture handle.
   * - ``virtual void gpu::Device::destroy(SamplerHandle handle)=0``
     - Destroy a sampler handle.
   * - ``virtual void gpu::Device::destroy(PipelineHandle handle)=0``
     - Destroy a pipeline handle.
   * - ``virtual void gpu::Device::destroy(QueryHandle handle)=0``
     - Destroy a query handle.
   * - ``virtual void gpu::Device::destroy(FenceHandle handle)=0``
     - Destroy a fence handle.
   * - ``virtual FenceHandle gpu::Device::insertFence()=0``
     - Insert a fence into the device command stream.
   * - ``virtual bool gpu::Device::isFenceSignaled(FenceHandle handle)=0``
     - Query whether a fence has signaled.
   * - ``virtual bool gpu::Device::beginRenderPass(const RenderPassDesc &desc)=0``
     - Begin a render pass.
   * - ``virtual void gpu::Device::endRenderPass()=0``
     - End the current render pass.
   * - ``virtual bool gpu::Device::setPipeline(PipelineHandle handle)=0``
     - Select a pipeline for subsequent commands.
   * - ``virtual bool gpu::Device::setViewport(const Viewport &viewport)=0``
     - Set the active viewport.
   * - ``virtual bool gpu::Device::setScissor(const Rect &rect)=0``
     - Set the active scissor rectangle.
   * - ``virtual bool gpu::Device::setStencilReference(std::uint32_t reference)=0``
     - Set the stencil reference value.
   * - ``virtual bool gpu::Device::bindVertexBuffer(std::uint32_t slot, BufferHandle handle, std::uint64_t offset=0)=0``
     - Bind a vertex buffer at a slot and byte offset.
   * - ``virtual bool gpu::Device::bindIndexBuffer(BufferHandle handle, IndexFormat format, std::uint64_t offset=0)=0``
     - Bind an index buffer with its index format and byte offset.
   * - ``virtual bool gpu::Device::bindUniformBuffer(std::uint32_t slot, BufferHandle handle, std::uint64_t offset, std::uint64_t size)=0``
     - Bind a uniform-buffer range.
   * - ``virtual bool gpu::Device::bindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler)=0``
     - Bind a sampled texture and sampler at a slot.
   * - ``virtual std::int32_t gpu::Device::registerBindlessTexture(TextureHandle texture, SamplerHandle sampler)=0``
     - Register a texture and sampler in the bindless descriptor table.
   * - ``virtual void gpu::Device::unregisterBindlessTexture(std::int32_t index)=0``
     - Release a bindless texture table index for reuse.
   * - ``virtual bool gpu::Device::bindStorageBuffer(std::uint32_t slot, BufferHandle handle, std::uint64_t offset, std::uint64_t size)=0``
     - Bind a storage-buffer range.
   * - ``virtual bool gpu::Device::bindStorageTexture(std::uint32_t slot, TextureHandle texture, std::uint32_t mipLevel)=0``
     - Bind a storage texture mip level.
   * - ``virtual bool gpu::Device::updateBuffer(BufferHandle handle, std::uint64_t offset, DataView data)=0``
     - Update a byte range in a buffer.
   * - ``virtual bool gpu::Device::updateTexture(TextureHandle handle, const TextureRegion &region, DataView data, const TextureDataLayout &layout={})=0``
     - Update a texture region from a data view and layout.
   * - ``virtual bool gpu::Device::reflectPipeline(PipelineHandle handle, PipelineReflection &reflection)=0``
     - Query reflection information for a linked pipeline.
   * - ``virtual bool gpu::Device::generateMipmaps(TextureHandle handle)=0``
     - Generate mip levels for a texture.
   * - ``virtual bool gpu::Device::copyTexture(TextureHandle destination, const TextureOrigin &destinationOrigin, TextureHandle source, const TextureRegion &sourceRegion)=0``
     - Copy a texture region to another texture.
   * - ``virtual bool gpu::Device::readTexture(TextureHandle handle, const TextureRegion &region, MutableDataView data, const TextureDataLayout &layout={})=0``
     - Read a texture region into caller-provided memory.
   * - ``virtual bool gpu::Device::copyBuffer(BufferHandle destination, std::uint64_t destinationOffset, BufferHandle source, std::uint64_t sourceOffset, std::uint64_t size)=0``
     - Copy bytes between two buffers.
   * - ``virtual void * gpu::Device::mapBuffer(BufferHandle handle, std::uint64_t offset, std::uint64_t size, MapMode mode)=0``
     - Map a buffer range for CPU access.
   * - ``virtual bool gpu::Device::unmapBuffer(BufferHandle handle)=0``
     - End a previous buffer mapping.
   * - ``virtual bool gpu::Device::draw(std::uint32_t vertexCount, std::uint32_t instanceCount=1, std::uint32_t firstVertex=0, std::uint32_t firstInstance=0)=0``
     - Issue a non-indexed draw.
   * - ``virtual bool gpu::Device::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount=1, std::uint32_t firstIndex=0, std::int32_t baseVertex=0, std::uint32_t firstInstance=0)=0``
     - Issue an indexed draw.
   * - ``virtual bool gpu::Device::memoryBarrier(std::uint32_t barriers)=0``
     - Insert a visibility barrier for the selected resource classes.
   * - ``virtual bool gpu::Device::dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ)=0``
     - Dispatch compute workgroups.
   * - ``virtual bool gpu::Device::drawIndirect(BufferHandle buffer, std::uint64_t offset)=0``
     - Issue an indirect non-indexed draw.
   * - ``virtual bool gpu::Device::drawIndexedIndirect(BufferHandle buffer, std::uint64_t offset)=0``
     - Issue an indirect indexed draw.
   * - ``virtual bool gpu::Device::drawIndirectCount(BufferHandle buffer, std::uint64_t offset, BufferHandle countBuffer, std::uint64_t countOffset, std::uint32_t maxDrawCount, std::uint32_t stride)=0``
     - Issue multiple indirect draws using a GPU count buffer.
   * - ``virtual bool gpu::Device::drawIndexedIndirectCount(BufferHandle buffer, std::uint64_t offset, BufferHandle countBuffer, std::uint64_t countOffset, std::uint32_t maxDrawCount, std::uint32_t stride)=0``
     - Issue multiple indexed indirect draws using a GPU count buffer.
   * - ``virtual bool gpu::Device::beginQuery(QueryHandle handle)=0``
     - Begin an occlusion or timestamp query.
   * - ``virtual void gpu::Device::endQuery(QueryHandle handle)=0``
     - End an active query.
   * - ``virtual bool gpu::Device::writeTimestamp(QueryHandle handle)=0``
     - Write a timestamp query value.
   * - ``virtual bool gpu::Device::isQueryResultAvailable(QueryHandle handle)=0``
     - Return whether a query result can be read.
   * - ``virtual bool gpu::Device::getQueryResult(QueryHandle handle, std::uint64_t &result)=0``
     - Read a query result into a caller-provided integer.
   * - ``virtual SurfaceState gpu::Device::surfaceState() const =0``
     - Return the current presentation-surface state.
   * - ``virtual bool gpu::Device::resizeSurface(std::uint32_t width, std::uint32_t height)=0``
     - Resize the presentation surface.
   * - ``virtual void gpu::Device::suspendSurface()=0``
     - Temporarily suspend presentation.
   * - ``virtual bool gpu::Device::resumeSurface()=0``
     - Resume presentation after suspension.
   * - ``virtual bool gpu::Device::present()=0``
     - Present the current surface image.

gpu::DeviceDesc
---------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUBackend.h``

Parameters used by createDevice.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``Backend gpu::DeviceDesc::backend = Backend::Null``
     - Backend to instantiate.
   * - ``RendererProfile gpu::DeviceDesc::profile = RendererProfile::Portable``
     - Portability profile requested from the backend.
   * - ``GPUCapabilities gpu::DeviceDesc::requiredCapabilities``
     - Minimum capabilities required by the application.
   * - ``SurfaceDesc gpu::DeviceDesc::surface``
     - Native surface information supplied to the selected backend.

gpu::DrawIndexedIndirectArgs
----------------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Layout of one indexed indirect draw command.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``std::uint32_t gpu::DrawIndexedIndirectArgs::indexCount = 0``
     - Number of indices.
   * - ``std::uint32_t gpu::DrawIndexedIndirectArgs::instanceCount = 1``
     - Number of instances.
   * - ``std::uint32_t gpu::DrawIndexedIndirectArgs::firstIndex = 0``
     - First index offset.
   * - ``std::int32_t gpu::DrawIndexedIndirectArgs::baseVertex = 0``
     - Signed vertex offset added to indices.
   * - ``std::uint32_t gpu::DrawIndexedIndirectArgs::firstInstance = 0``
     - First instance index.

gpu::DrawIndirectArgs
---------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Layout of one non-indexed indirect draw command.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``std::uint32_t gpu::DrawIndirectArgs::vertexCount = 0``
     - Number of vertices.
   * - ``std::uint32_t gpu::DrawIndirectArgs::instanceCount = 1``
     - Number of instances.
   * - ``std::uint32_t gpu::DrawIndirectArgs::firstVertex = 0``
     - First vertex index.
   * - ``std::uint32_t gpu::DrawIndirectArgs::firstInstance = 0``
     - First instance index.

gpu::GPUCapabilities
--------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUCapabilities.h``

Features and implementation limits reported by a device.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``bool gpu::GPUCapabilities::compute = false``
     - Compute dispatch is available.
   * - ``bool gpu::GPUCapabilities::storageBuffers = false``
     - Storage/uniform-style buffer resources are available.
   * - ``bool gpu::GPUCapabilities::storageTextures = false``
     - Storage texture resources are available.
   * - ``bool gpu::GPUCapabilities::memoryBarriers = false``
     - Explicit memory barriers are available.
   * - ``bool gpu::GPUCapabilities::indirectDraw = false``
     - Indirect draw commands are available.
   * - ``bool gpu::GPUCapabilities::indirectCount = false``
     - Indirect draw-count commands are available.
   * - ``bool gpu::GPUCapabilities::asyncReadback = false``
     - Asynchronous texture readback is available.
   * - ``bool gpu::GPUCapabilities::depthReadback = false``
     - Depth-texture readback is available.
   * - ``bool gpu::GPUCapabilities::timestampQueries = false``
     - Timestamp queries are available.
   * - ``bool gpu::GPUCapabilities::occlusionQueries = false``
     - Occlusion queries are available.
   * - ``bool gpu::GPUCapabilities::textureArrays = false``
     - Texture arrays are available.
   * - ``bool gpu::GPUCapabilities::textureCompressionBC = false``
     - BC texture compression family is available.
   * - ``bool gpu::GPUCapabilities::textureCompressionBC1 = false``
     - BC1 texture compression is available.
   * - ``bool gpu::GPUCapabilities::textureCompressionBC3 = false``
     - BC3 texture compression is available.
   * - ``bool gpu::GPUCapabilities::textureCompressionBC5 = false``
     - BC5 texture compression is available.
   * - ``bool gpu::GPUCapabilities::textureCompressionBC7 = false``
     - BC7 texture compression is available.
   * - ``bool gpu::GPUCapabilities::textureCompressionETC2 = false``
     - ETC2 texture compression is available.
   * - ``bool gpu::GPUCapabilities::textureCompressionASTC = false``
     - ASTC texture compression is available.
   * - ``bool gpu::GPUCapabilities::samplerBorderColor = false``
     - Sampler border colors are available.
   * - ``bool gpu::GPUCapabilities::wireframe = false``
     - Wireframe rasterization is available.
   * - ``bool gpu::GPUCapabilities::independentBlend = false``
     - Independent color-target blending is available.
   * - ``bool gpu::GPUCapabilities::baseInstance = false``
     - Non-zero base-instance draw parameters are available.
   * - ``bool gpu::GPUCapabilities::tessellationShader = false``
     - Tessellation shaders are available.
   * - ``bool gpu::GPUCapabilities::geometryShader = false``
     - Geometry shaders are available.
   * - ``bool gpu::GPUCapabilities::bindlessTextures = false``
     - Bindless texture registration is available.
   * - ``std::uint32_t gpu::GPUCapabilities::maxBindlessTextures = 0``
     - Maximum number of bindless textures.
   * - ``std::uint32_t gpu::GPUCapabilities::maxColorAttachments = 1``
     - Maximum number of color attachments.
   * - ``std::uint32_t gpu::GPUCapabilities::maxTextureDimension2D = 0``
     - Maximum 2D texture dimension.
   * - ``std::uint32_t gpu::GPUCapabilities::maxTextureDimension3D = 0``
     - Maximum 3D texture dimension.
   * - ``std::uint32_t gpu::GPUCapabilities::maxTextureArrayLayers = 0``
     - Maximum number of layers in a texture array.
   * - ``std::uint32_t gpu::GPUCapabilities::maxTextureBindings = 0``
     - Maximum number of texture bindings.
   * - ``std::uint32_t gpu::GPUCapabilities::maxSampleCount = 1``
     - Maximum supported sample count.
   * - ``std::uint32_t gpu::GPUCapabilities::maxUniformBufferBindings = 0``
     - Maximum number of uniform-buffer bindings.
   * - ``std::uint32_t gpu::GPUCapabilities::maxUniformBufferSize = 0``
     - Maximum uniform-buffer size in bytes.
   * - ``std::uint32_t gpu::GPUCapabilities::maxStorageBufferBindings = 0``
     - Maximum number of storage-buffer bindings.
   * - ``std::uint32_t gpu::GPUCapabilities::uniformBufferOffsetAlignment = 1``
     - Required offset alignment for uniform-buffer ranges.
   * - ``std::uint32_t gpu::GPUCapabilities::storageBufferOffsetAlignment = 1``
     - Required offset alignment for storage-buffer ranges.

gpu::GPUError
-------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUError.h``

One diagnostic record returned by a device or error queue.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``GPUErrorCode gpu::GPUError::code = GPUErrorCode::None``
     - Error classification.
   * - ``GPUErrorSeverity gpu::GPUError::severity = GPUErrorSeverity::Error``
     - Diagnostic severity.
   * - ``GPUOperation gpu::GPUError::operation = GPUOperation::None``
     - Operation that produced the error.
   * - ``std::uint64_t gpu::GPUError::resource = 0``
     - Opaque resource value associated with the error, if any.
   * - ``std::uint64_t gpu::GPUError::value0 = 0``
     - Operation-specific numeric diagnostic value.
   * - ``std::uint64_t gpu::GPUError::value1 = 0``
     - Operation-specific numeric diagnostic value.
   * - ``const char* gpu::GPUError::message = nullptr``
     - Diagnostic message pointer; ownership and lifetime are unspecified.

gpu::GPUErrorQueue
------------------
:Kind: class
:Header: ``gpu/include/gpu/GPUError.h``

Fixed-capacity FIFO queue for GPUError records.

Functions and methods
~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Signature
     - Description
   * - ``std::uint64_t gpu::GPUErrorQueue::totalErrorCount() const``
     - Return the total number of errors submitted to the queue.
   * - ``std::uint32_t gpu::GPUErrorQueue::pendingErrorCount() const``
     - Return the number of records currently available to consume.
   * - ``bool gpu::GPUErrorQueue::getError(GPUError &error)``
     - Pop the oldest available error.
   * - ``void gpu::GPUErrorQueue::clearErrors()``
     - Discard all pending records, including an overflow notice.
   * - ``void gpu::GPUErrorQueue::push(const GPUError &error)``
     - Append an error record to the queue.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``constexpr std::uint32_t gpu::GPUErrorQueue::Capacity = 256``
     - Maximum number of error records retained by the queue.

gpu::GPUProfiler
----------------
:Kind: class
:Header: ``gpu/include/gpu/GPUProfiler.h``

Optional timestamp-query based frame profiler.

Functions and methods
~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Signature
     - Description
   * - ``gpu::GPUProfiler::GPUProfiler(Device &device)``
     - Construct a profiler associated with a device.
   * - ``gpu::GPUProfiler::~GPUProfiler()``
     - Release profiler-owned query resources.
   * - ``gpu::GPUProfiler::GPUProfiler(const GPUProfiler &)=delete``
     - Copy construction is disabled.
   * - ``GPUProfiler & gpu::GPUProfiler::operator=(const GPUProfiler &)=delete``
     - Copy assignment is disabled.
   * - ``bool gpu::GPUProfiler::enabled() const``
     - Return whether timestamp profiling is enabled for the device.
   * - ``void gpu::GPUProfiler::beginFrame()``
     - Begin recording scopes for the next frame.
   * - ``void gpu::GPUProfiler::endFrame()``
     - End recording scopes for the current frame.
   * - ``bool gpu::GPUProfiler::beginScope(const char *name)``
     - Begin a named timestamp scope.
   * - ``void gpu::GPUProfiler::endScope()``
     - End the currently active timestamp scope.
   * - ``std::uint32_t gpu::GPUProfiler::collectFrame(ProfilerScopeResult *results, std::uint32_t maxResults)``
     - Collect results from a completed frame when queries are ready.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``constexpr std::uint32_t gpu::GPUProfiler::MaxScopesPerFrame = 16``
     - Maximum number of scopes recorded in one frame.
   * - ``constexpr std::uint32_t gpu::GPUProfiler::FrameLatency = 3``
     - Number of frame slots used to defer query collection.

gpu::Handle
-----------
:Kind: class
:Header: ``gpu/include/gpu/GPUHandles.h``

Strongly typed opaque resource handle.

Functions and methods
~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Signature
     - Description
   * - ``constexpr gpu::Handle< Tag >::Handle()=default``
     - Construct an invalid handle.
   * - ``constexpr gpu::Handle< Tag >::Handle(std::uint64_t value)``
     - Construct a handle from a backend-provided numeric value.
   * - ``constexpr bool gpu::Handle< Tag >::valid() const``
     - Return whether this handle contains a non-zero value.
   * - ``constexpr std::uint64_t gpu::Handle< Tag >::value() const``
     - Return the underlying opaque numeric value.
   * - ``constexpr gpu::Handle< Tag >::operator bool() const``
     - Convert to true when the handle is valid.
   * - ``constexpr bool operator==(Handle left, Handle right)``
     - Compare two handles of the same resource type.
   * - ``constexpr bool operator!=(Handle left, Handle right)``
     - Compare two handles of the same resource type.

gpu::MutableDataView
--------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Non-owning writable byte range supplied to a device call.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``void* gpu::MutableDataView::data = nullptr``
     - Start address of the writable byte range.
   * - ``std::uint64_t gpu::MutableDataView::size = 0``
     - Number of writable bytes available at data.

gpu::PipelineDesc
-----------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Graphics, compute, and fixed-function pipeline description.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``constexpr std::uint32_t gpu::PipelineDesc::MaxVertexBuffers = 8``
     - Maximum number of vertex-buffer layouts.
   * - ``constexpr std::uint32_t gpu::PipelineDesc::MaxColorTargets = 8``
     - Maximum number of color-target states.
   * - ``ShaderDesc gpu::PipelineDesc::vertex``
     - Vertex shader stage description.
   * - ``ShaderDesc gpu::PipelineDesc::fragment``
     - Fragment shader stage description.
   * - ``ShaderDesc gpu::PipelineDesc::compute``
     - Compute shader stage description.
   * - ``ShaderDesc gpu::PipelineDesc::tessControl``
     - Optional tessellation-control shader stage.
   * - ``ShaderDesc gpu::PipelineDesc::tessEvaluation``
     - Optional tessellation-evaluation shader stage.
   * - ``ShaderDesc gpu::PipelineDesc::geometry``
     - Optional geometry shader stage.
   * - ``VertexBufferLayout gpu::PipelineDesc::vertexBuffers[MaxVertexBuffers][MaxVertexBuffers]``
     - Vertex-buffer layout entries.
   * - ``ColorTargetState gpu::PipelineDesc::colorTargets[MaxColorTargets][MaxColorTargets]``
     - Color-target state entries.
   * - ``DepthStencilState gpu::PipelineDesc::depthStencil``
     - Depth/stencil state.
   * - ``RasterState gpu::PipelineDesc::raster``
     - Rasterization state.
   * - ``std::uint32_t gpu::PipelineDesc::vertexBufferCount = 0``
     - Number of active vertex-buffer layouts.
   * - ``std::uint32_t gpu::PipelineDesc::colorTargetCount = 1``
     - Number of active color targets.
   * - ``std::uint32_t gpu::PipelineDesc::patchControlPoints = 0``
     - Vertices per tessellation patch.
   * - ``Topology gpu::PipelineDesc::topology = Topology::Triangles``
     - Primitive topology.
   * - ``const char* gpu::PipelineDesc::debugName = nullptr``
     - Optional non-owning debug label.

gpu::PipelineReflection
-----------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Fixed-capacity reflection result for a linked pipeline.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``constexpr std::uint32_t gpu::PipelineReflection::MaxResources = 32``
     - Maximum number of resources returned.
   * - ``ShaderResource gpu::PipelineReflection::resources[MaxResources][MaxResources]``
     - Reflected resource entries.
   * - ``std::uint32_t gpu::PipelineReflection::resourceCount = 0``
     - Number of active entries in resources.
   * - ``bool gpu::PipelineReflection::truncated = false``
     - 

gpu::ProfilerScopeResult
------------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUProfiler.h``

Timing result for one named profiler scope.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``const char* gpu::ProfilerScopeResult::name = nullptr``
     - Scope name supplied to beginScope.
   * - ``double gpu::ProfilerScopeResult::milliseconds = 0.0``
     - Measured duration in milliseconds.

gpu::RasterState
----------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Rasterization state for a pipeline.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``CullMode gpu::RasterState::cullMode = CullMode::Back``
     - Face culling mode.
   * - ``FrontFace gpu::RasterState::frontFace = FrontFace::CounterClockwise``
     - Winding considered front-facing.
   * - ``float gpu::RasterState::depthBiasConstant = 0.0f``
     - Constant depth-bias term.
   * - ``float gpu::RasterState::depthBiasSlope = 0.0f``
     - Slope-scaled depth-bias term.
   * - ``bool gpu::RasterState::scissorEnabled = false``
     - Enable scissor testing for the pipeline.

gpu::Rect
---------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Integer scissor rectangle.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``std::int32_t gpu::Rect::x = 0``
     - Rectangle origin on the X axis.
   * - ``std::int32_t gpu::Rect::y = 0``
     - Rectangle origin on the Y axis.
   * - ``std::uint32_t gpu::Rect::width = 0``
     - Rectangle width.
   * - ``std::uint32_t gpu::Rect::height = 0``
     - Rectangle height.

gpu::RenderPassColorAttachment
------------------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Color attachment load, store, and clear state.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``TargetAttachment gpu::RenderPassColorAttachment::target``
     - Texture target for this attachment.
   * - ``bool gpu::RenderPassColorAttachment::surface = false``
     - Select the presentation surface instead of target.texture.
   * - ``LoadOp gpu::RenderPassColorAttachment::loadOp = LoadOp::Load``
     - Operation performed before rendering.
   * - ``StoreOp gpu::RenderPassColorAttachment::storeOp = StoreOp::Store``
     - Operation performed after rendering.
   * - ``float gpu::RenderPassColorAttachment::clearColor[4][4] = {0.0f, 0.0f, 0.0f, 0.0f}``
     - RGBA clear value used when loadOp is Clear.

gpu::RenderPassDepthStencilAttachment
-------------------------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Depth/stencil attachment load, store, and clear state.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``TargetAttachment gpu::RenderPassDepthStencilAttachment::target``
     - Texture target for the depth/stencil attachment.
   * - ``LoadOp gpu::RenderPassDepthStencilAttachment::depthLoadOp = LoadOp::Load``
     - Depth load operation.
   * - ``StoreOp gpu::RenderPassDepthStencilAttachment::depthStoreOp = StoreOp::Store``
     - Depth store operation.
   * - ``LoadOp gpu::RenderPassDepthStencilAttachment::stencilLoadOp = LoadOp::Load``
     - Stencil load operation.
   * - ``StoreOp gpu::RenderPassDepthStencilAttachment::stencilStoreOp = StoreOp::Store``
     - Stencil store operation.
   * - ``float gpu::RenderPassDepthStencilAttachment::clearDepth = 1.0f``
     - Depth clear value used when depthLoadOp is Clear.
   * - ``std::uint32_t gpu::RenderPassDepthStencilAttachment::clearStencil = 0``
     - Stencil clear value used when stencilLoadOp is Clear.

gpu::RenderPassDesc
-------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Color and depth/stencil attachments for beginRenderPass.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``constexpr std::uint32_t gpu::RenderPassDesc::MaxColorAttachments = 8``
     - Maximum number of color attachments.
   * - ``RenderPassColorAttachment gpu::RenderPassDesc::colors[MaxColorAttachments][MaxColorAttachments]``
     - Color attachment entries.
   * - ``RenderPassDepthStencilAttachment gpu::RenderPassDesc::depthStencil``
     - Depth/stencil attachment state.
   * - ``std::uint32_t gpu::RenderPassDesc::colorCount = 0``
     - Number of active color attachments.
   * - ``bool gpu::RenderPassDesc::hasDepthStencil = false``
     - Whether depthStencil is active.

gpu::RenderPassScope
--------------------
:Kind: class
:Header: ``gpu/include/gpu/GPU.h``

RAII helper that ends a render pass when leaving scope.

Functions and methods
~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Signature
     - Description
   * - ``gpu::RenderPassScope::RenderPassScope(Device &device, const RenderPassDesc &desc)``
     - Begin a render pass and remember whether it became active.
   * - ``gpu::RenderPassScope::~RenderPassScope()``
     - End the pass if beginRenderPass succeeded.
   * - ``gpu::RenderPassScope::RenderPassScope(const RenderPassScope &)=delete``
     - Copy construction is disabled.
   * - ``RenderPassScope & gpu::RenderPassScope::operator=(const RenderPassScope &)=delete``
     - Copy assignment is disabled.
   * - ``gpu::RenderPassScope::RenderPassScope(RenderPassScope &&)=delete``
     - Move construction is disabled.
   * - ``RenderPassScope & gpu::RenderPassScope::operator=(RenderPassScope &&)=delete``
     - Move assignment is disabled.
   * - ``gpu::RenderPassScope::operator bool() const``
     - Return whether the render pass was successfully started.

gpu::SamplerDesc
----------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Parameters used to create a sampler.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``Filter gpu::SamplerDesc::minFilter = Filter::Linear``
     - Minification filter.
   * - ``Filter gpu::SamplerDesc::magFilter = Filter::Linear``
     - Magnification filter.
   * - ``Filter gpu::SamplerDesc::mipFilter = Filter::Linear``
     - Mip-level filter.
   * - ``AddressMode gpu::SamplerDesc::addressU = AddressMode::Repeat``
     - Address mode for the U coordinate.
   * - ``AddressMode gpu::SamplerDesc::addressV = AddressMode::Repeat``
     - Address mode for the V coordinate.
   * - ``AddressMode gpu::SamplerDesc::addressW = AddressMode::Repeat``
     - Address mode for the W coordinate.
   * - ``float gpu::SamplerDesc::maxAnisotropy = 1.0f``
     - Maximum anisotropy requested by the sampler.
   * - ``float gpu::SamplerDesc::lodMin = 0.0f``
     - Minimum level-of-detail clamp.
   * - ``float gpu::SamplerDesc::lodMax = 32.0f``
     - Maximum level-of-detail clamp.
   * - ``CompareOp gpu::SamplerDesc::compare = CompareOp::LessEqual``
     - Depth comparison operation.
   * - ``bool gpu::SamplerDesc::compareEnabled = false``
     - Enable depth comparison.
   * - ``const char* gpu::SamplerDesc::debugName = nullptr``
     - Optional non-owning debug label.

gpu::ShaderDesc
---------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Source and entry point used to create a shader stage.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``DataView gpu::ShaderDesc::source``
     - Non-owning shader source byte range.
   * - ``const char* gpu::ShaderDesc::entryPoint = "main"``
     - Null-terminated entry-point name.
   * - ``const char* gpu::ShaderDesc::debugName = nullptr``
     - Optional non-owning debug label.

gpu::ShaderResource
-------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

One active resource reported by pipeline reflection.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``constexpr std::uint32_t gpu::ShaderResource::MaxNameLength = 64``
     - Maximum number of characters stored in name.
   * - ``char gpu::ShaderResource::name[MaxNameLength][MaxNameLength] = {}``
     - Null-terminated resource name when it fits the fixed buffer.
   * - ``ShaderResourceType gpu::ShaderResource::type = ShaderResourceType::Sampler``
     - Reflected resource category.
   * - ``std::uint32_t gpu::ShaderResource::slot = 0``
     - Texture unit for Sampler, or binding point for other types.
   * - ``std::uint32_t gpu::ShaderResource::elementCount = 1``
     - Element count for arrays, or 1 for a non-array resource.
   * - ``std::uint32_t gpu::ShaderResource::blockSize = 0``
     - Uniform-block size in bytes, or 0 for other resource types.

gpu::StencilFaceState
---------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Stencil comparison and operations for one face.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``CompareOp gpu::StencilFaceState::compare = CompareOp::Always``
     - Stencil comparison operation.
   * - ``StencilOperation gpu::StencilFaceState::failOperation = StencilOperation::Keep``
     - Operation when the stencil test fails.
   * - ``StencilOperation gpu::StencilFaceState::depthFailOperation = StencilOperation::Keep``
     - Operation when stencil passes but depth fails.
   * - ``StencilOperation gpu::StencilFaceState::passOperation = StencilOperation::Keep``
     - Operation when both stencil and depth pass.

gpu::SurfaceDesc
----------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUSurface.h``

Native surface information passed to a device backend.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``void* gpu::SurfaceDesc::nativeHandle = nullptr``
     - Backend-specific native window or surface handle.
   * - ``std::uint32_t gpu::SurfaceDesc::width = 0``
     - Requested surface width in pixels.
   * - ``std::uint32_t gpu::SurfaceDesc::height = 0``
     - Requested surface height in pixels.
   * - ``Format gpu::SurfaceDesc::format = Format::RGBA8Srgb``
     - Requested surface color format.

gpu::TargetAttachment
---------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Texture subresource used as a render-pass attachment.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``TextureHandle gpu::TargetAttachment::texture``
     - Target texture handle.
   * - ``std::uint32_t gpu::TargetAttachment::mipLevel = 0``
     - Mip level used as the target.
   * - ``std::uint32_t gpu::TargetAttachment::layer = 0``
     - Array layer used as the target.

gpu::TextureDataLayout
----------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Byte layout used for texture upload and readback data.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``std::uint64_t gpu::TextureDataLayout::offset = 0``
     - Byte offset of the first texel in the data view.
   * - ``std::uint32_t gpu::TextureDataLayout::bytesPerRow = 0``
     - Byte distance between consecutive rows.
   * - ``std::uint32_t gpu::TextureDataLayout::rowsPerImage = 0``
     - Number of rows between consecutive images or layers.

gpu::TextureDesc
----------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Parameters used to create a texture.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``TextureDimension gpu::TextureDesc::dimension = TextureDimension::Texture2D``
     - Texture dimensionality and array interpretation.
   * - ``Format gpu::TextureDesc::format = Format::RGBA8``
     - Texture storage format.
   * - ``std::uint32_t gpu::TextureDesc::width = 0``
     - Width in texels.
   * - ``std::uint32_t gpu::TextureDesc::height = 0``
     - Height in texels.
   * - ``std::uint32_t gpu::TextureDesc::depthOrLayers = 1``
     - Depth in texels or array-layer count.
   * - ``std::uint32_t gpu::TextureDesc::mipCount = 1``
     - Number of mip levels.
   * - ``std::uint32_t gpu::TextureDesc::sampleCount = 1``
     - Requested multisample count.
   * - ``std::uint32_t gpu::TextureDesc::usage = TextureUsageSampled``
     - Bitwise combination of TextureUsage values.
   * - ``DataView gpu::TextureDesc::initialData``
     - Optional initial texture bytes.
   * - ``const char* gpu::TextureDesc::debugName = nullptr``
     - Optional non-owning debug label.

gpu::TextureOrigin
------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Destination origin for a texture copy operation.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``std::uint32_t gpu::TextureOrigin::mipLevel = 0``
     - Destination mip level.
   * - ``std::uint32_t gpu::TextureOrigin::x = 0``
     - Destination X coordinate.
   * - ``std::uint32_t gpu::TextureOrigin::y = 0``
     - Destination Y coordinate.
   * - ``std::uint32_t gpu::TextureOrigin::z = 0``
     - Destination Z coordinate or layer.

gpu::TextureRegion
------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

A mip-level region expressed in texels or layers.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``std::uint32_t gpu::TextureRegion::mipLevel = 0``
     - Mip level containing the region.
   * - ``std::uint32_t gpu::TextureRegion::x = 0``
     - Region origin on the X axis.
   * - ``std::uint32_t gpu::TextureRegion::y = 0``
     - Region origin on the Y axis.
   * - ``std::uint32_t gpu::TextureRegion::z = 0``
     - Region origin on the Z axis or layer index.
   * - ``std::uint32_t gpu::TextureRegion::width = 0``
     - Region width.
   * - ``std::uint32_t gpu::TextureRegion::height = 0``
     - Region height.
   * - ``std::uint32_t gpu::TextureRegion::depthOrLayers = 1``
     - Region depth or layer count.

gpu::VertexAttribute
--------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

One vertex input attribute declaration.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``VertexFormat gpu::VertexAttribute::format = VertexFormat::Float32``
     - Attribute element format.
   * - ``std::uint32_t gpu::VertexAttribute::offset = 0``
     - Byte offset from the start of the vertex element.
   * - ``std::uint32_t gpu::VertexAttribute::shaderLocation = 0``
     - Shader input location.

gpu::VertexBufferLayout
-----------------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Vertex-buffer stride, step mode, and attributes.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``constexpr std::uint32_t gpu::VertexBufferLayout::MaxAttributes = 16``
     - Maximum number of attributes stored in attributes.
   * - ``std::uint32_t gpu::VertexBufferLayout::stride = 0``
     - Byte stride between vertex or instance elements.
   * - ``VertexStepMode gpu::VertexBufferLayout::stepMode = VertexStepMode::Vertex``
     - Whether the layout advances per vertex or per instance.
   * - ``VertexAttribute gpu::VertexBufferLayout::attributes[MaxAttributes][MaxAttributes]``
     - Attribute declarations, up to attributeCount entries.
   * - ``std::uint32_t gpu::VertexBufferLayout::attributeCount = 0``
     - Number of active entries in attributes.

gpu::Viewport
-------------
:Kind: struct
:Header: ``gpu/include/gpu/GPUDescriptors.h``

Viewport transform and depth range.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``float gpu::Viewport::x = 0.0f``
     - Viewport origin on the X axis.
   * - ``float gpu::Viewport::y = 0.0f``
     - Viewport origin on the Y axis.
   * - ``float gpu::Viewport::width = 0.0f``
     - Viewport width.
   * - ``float gpu::Viewport::height = 0.0f``
     - Viewport height.
   * - ``float gpu::Viewport::minDepth = 0.0f``
     - Minimum depth mapped by the viewport.
   * - ``float gpu::Viewport::maxDepth = 1.0f``
     - Maximum depth mapped by the viewport.

gpu
---
:Kind: namespace
:Header: ``gpu/include/gpu/GPU.h``

Functions and methods
~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Signature
     - Description
   * - ``void gpu::reportDeviceCreationFailure(GPUError *error, GPUErrorCode code, const char *message)``
     - Populate an error with a device-creation failure.
   * - ``Device * gpu::createDevice(const DeviceDesc &desc, GPUError *error=nullptr)``
     - Create a device for the requested backend.
   * - ``void gpu::destroyDevice(Device *device)``
     - Shut down and release a device returned by createDevice.
   * - ``bool gpu::requirementsMet(const GPUCapabilities &required, const GPUCapabilities &available)``
     - Check whether an available capability set satisfies requirements.
   * - ``const char * gpu::toString(GPUErrorCode code)``
     - Return a stable label for an error code.
   * - ``const char * gpu::toString(GPUErrorSeverity severity)``
     - Return a stable label for an error severity.
   * - ``const char * gpu::toString(GPUOperation operation)``
     - Return a stable label for an operation.

Fields and aliases
~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``using gpu::BufferHandle = typedef Handle<struct BufferTag>``
     - Opaque handle for a buffer resource.
   * - ``using gpu::TextureHandle = typedef Handle<struct TextureTag>``
     - Opaque handle for a texture resource.
   * - ``using gpu::SamplerHandle = typedef Handle<struct SamplerTag>``
     - Opaque handle for a sampler resource.
   * - ``using gpu::PipelineHandle = typedef Handle<struct PipelineTag>``
     - Opaque handle for a pipeline resource.
   * - ``using gpu::QueryHandle = typedef Handle<struct QueryTag>``
     - Opaque handle for a query resource.
   * - ``using gpu::FenceHandle = typedef Handle<struct FenceTag>``
     - Opaque handle for a fence resource.

Enumerations
~~~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Declaration
     - Description
   * - ``Backend``
     - Backend implementation selected when creating a device.
   * - ``Filter``
     - Minification, magnification, or mip filtering mode.
   * - ``AddressMode``
     - Addressing mode used outside a sampler coordinate range.
   * - ``ShaderResourceType``
     - Resource category reported by pipeline reflection.
   * - ``GPUErrorCode``
     - Error codes reported by device operations and the error queue.
   * - ``GPUErrorSeverity``
     - Severity associated with a GPUError.
   * - ``GPUOperation``
     - Operation associated with a GPUError.
   * - ``SurfaceState``
     - Current presentation-surface state.
   * - ``RendererProfile``
     - Portability profile requested during device creation.
   * - ``Format``
     - Texture and render-target formats understood by the API.
   * - ``TextureDimension``
     - Texture dimensionality and array shape.
   * - ``Topology``
     - Primitive topology used by a graphics pipeline.
   * - ``LoadOp``
     - Operation applied to an attachment before rendering.
   * - ``StoreOp``
     - Operation applied to an attachment after rendering.
   * - ``CompareOp``
     - Comparison function used by depth, stencil, or sampler state.
   * - ``IndexFormat``
     - Element type of an index buffer.
   * - ``MapMode``
     - CPU access mode requested by mapBuffer.
   * - ``QueryType``
     - Query category created by createQuery.
   * - ``VertexFormat``
     - Vertex attribute element format.
   * - ``VertexStepMode``
     - Frequency at which a vertex buffer layout advances.
   * - ``BlendFactor``
     - Multiplicative factor used by a blend equation.
   * - ``BlendOperation``
     - Arithmetic operation used by a blend equation.
   * - ``CullMode``
     - Face-culling mode used by rasterization.
   * - ``FrontFace``
     - Winding that identifies front-facing primitives.
   * - ``StencilOperation``
     - Operation applied to a stencil value.
   * - ``ColorWrite``
     - Bit flags selecting writable color channels.
   * - ``BufferUsage``
     - Bit flags describing permitted buffer uses.
   * - ``TextureUsage``
     - Bit flags describing permitted texture uses.
   * - ``Barrier``
     - Bit flags selecting resource classes for memoryBarrier.

Enum values
~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Value
     - Description
   * - ``Null``
     - 
   * - ``OpenGL``
     - 
   * - ``OpenGLES``
     - 
   * - ``WebGPU``
     - 
   * - ``Vulkan``
     - 
   * - ``Metal``
     - 
   * - ``Nearest``
     - 
   * - ``Linear``
     - 
   * - ``Repeat``
     - 
   * - ``MirrorRepeat``
     - 
   * - ``ClampToEdge``
     - 
   * - ``ClampToBorder``
     - 
   * - ``Sampler``
     - 
   * - ``UniformBuffer``
     - 
   * - ``StorageBuffer``
     - 
   * - ``StorageTexture``
     - 
   * - ``None``
     - 
   * - ``InvalidArgument``
     - 
   * - ``InvalidHandle``
     - 
   * - ``UnsupportedFeature``
     - 
   * - ``UnsupportedFormat``
     - 
   * - ``OutOfBounds``
     - 
   * - ``OutOfMemory``
     - 
   * - ``ShaderCompilationFailed``
     - 
   * - ``PipelineCreationFailed``
     - 
   * - ``DeviceCreationFailed``
     - 
   * - ``DeviceLost``
     - 
   * - ``SurfaceLost``
     - 
   * - ``BackendFailure``
     - 
   * - ``ErrorQueueOverflow``
     - 
   * - ``Info``
     - 
   * - ``Warning``
     - 
   * - ``Error``
     - 
   * - ``Fatal``
     - 
   * - ``None``
     - 
   * - ``CreateBuffer``
     - 
   * - ``CreateTexture``
     - 
   * - ``CreateSampler``
     - 
   * - ``CreatePipeline``
     - 
   * - ``ReflectPipeline``
     - 
   * - ``CreateQuery``
     - 
   * - ``CreateFence``
     - 
   * - ``CreateDevice``
     - 
   * - ``UpdateBuffer``
     - 
   * - ``UpdateTexture``
     - 
   * - ``GenerateMipmaps``
     - 
   * - ``MapBuffer``
     - 
   * - ``ReadBuffer``
     - 
   * - ``ReadTexture``
     - 
   * - ``BeginRenderPass``
     - 
   * - ``SetPipeline``
     - 
   * - ``BindResource``
     - 
   * - ``Draw``
     - 
   * - ``DrawIndirect``
     - 
   * - ``Dispatch``
     - 
   * - ``Copy``
     - 
   * - ``Present``
     - 
   * - ``Destroy``
     - 
   * - ``Ready``
     - 
   * - ``Suspended``
     - 
   * - ``Lost``
     - 
   * - ``Portable``
     - 
   * - ``Modern``
     - 
   * - ``Unknown``
     - 
   * - ``R8``
     - 
   * - ``RG8``
     - 
   * - ``RGBA8``
     - 
   * - ``RGBA8Srgb``
     - 
   * - ``R16Float``
     - 
   * - ``RG16Float``
     - 
   * - ``RGBA16Float``
     - 
   * - ``R32Float``
     - 
   * - ``RG32Float``
     - 
   * - ``RGB32Float``
     - 
   * - ``RGBA32Float``
     - 
   * - ``R11G11B10Float``
     - 
   * - ``RGB10A2``
     - 
   * - ``R16Uint``
     - 
   * - ``RG16Uint``
     - 
   * - ``RGBA16Uint``
     - 
   * - ``R32Uint``
     - 
   * - ``RG32Uint``
     - 
   * - ``RGBA32Uint``
     - 
   * - ``BC1RGBA``
     - 
   * - ``BC1RGBASrgb``
     - 
   * - ``BC3RGBA``
     - 
   * - ``BC3RGBASrgb``
     - 
   * - ``BC5RG``
     - 
   * - ``BC7RGBA``
     - 
   * - ``BC7RGBASrgb``
     - 
   * - ``ETC2RGBA8``
     - 
   * - ``ETC2RGBA8Srgb``
     - 
   * - ``ASTC4x4RGBA``
     - 
   * - ``ASTC4x4RGBASrgb``
     - 
   * - ``Depth16``
     - 
   * - ``Depth24``
     - 
   * - ``Depth32Float``
     - 
   * - ``Depth24Stencil8``
     - 
   * - ``Texture2D``
     - 
   * - ``Texture2DArray``
     - 
   * - ``Texture3D``
     - 
   * - ``TextureCube``
     - 
   * - ``Triangles``
     - 
   * - ``TriangleStrip``
     - 
   * - ``Lines``
     - 
   * - ``LineStrip``
     - 
   * - ``Points``
     - 
   * - ``Patches``
     - 
   * - ``Load``
     - 
   * - ``Clear``
     - 
   * - ``DontCare``
     - 
   * - ``Store``
     - 
   * - ``Discard``
     - 
   * - ``Never``
     - 
   * - ``Less``
     - 
   * - ``Equal``
     - 
   * - ``LessEqual``
     - 
   * - ``Greater``
     - 
   * - ``NotEqual``
     - 
   * - ``GreaterEqual``
     - 
   * - ``Always``
     - 
   * - ``Uint16``
     - 
   * - ``Uint32``
     - 
   * - ``Read``
     - 
   * - ``Write``
     - 
   * - ``Occlusion``
     - 
   * - ``Timestamp``
     - 
   * - ``Float32``
     - 
   * - ``Float32x2``
     - 
   * - ``Float32x3``
     - 
   * - ``Float32x4``
     - 
   * - ``Uint32``
     - 
   * - ``Uint32x2``
     - 
   * - ``Uint32x3``
     - 
   * - ``Uint32x4``
     - 
   * - ``Unorm8x4``
     - 
   * - ``Snorm8x4``
     - 
   * - ``Vertex``
     - 
   * - ``Instance``
     - 
   * - ``Zero``
     - 
   * - ``One``
     - 
   * - ``SourceColor``
     - 
   * - ``OneMinusSourceColor``
     - 
   * - ``SourceAlpha``
     - 
   * - ``OneMinusSourceAlpha``
     - 
   * - ``DestinationColor``
     - 
   * - ``OneMinusDestinationColor``
     - 
   * - ``DestinationAlpha``
     - 
   * - ``OneMinusDestinationAlpha``
     - 
   * - ``Add``
     - 
   * - ``Subtract``
     - 
   * - ``ReverseSubtract``
     - 
   * - ``Minimum``
     - 
   * - ``Maximum``
     - 
   * - ``None``
     - 
   * - ``Front``
     - 
   * - ``Back``
     - 
   * - ``CounterClockwise``
     - 
   * - ``Clockwise``
     - 
   * - ``Keep``
     - 
   * - ``Zero``
     - 
   * - ``Replace``
     - 
   * - ``IncrementClamp``
     - 
   * - ``DecrementClamp``
     - 
   * - ``Invert``
     - 
   * - ``IncrementWrap``
     - 
   * - ``DecrementWrap``
     - 
   * - ``ColorWriteRed = 1u << 0``
     - 
   * - ``ColorWriteGreen = 1u << 1``
     - 
   * - ``ColorWriteBlue = 1u << 2``
     - 
   * - ``ColorWriteAlpha = 1u << 3``
     - 
   * - ``ColorWriteAll = ColorWriteRed | ColorWriteGreen | ColorWriteBlue | ColorWriteAlpha``
     - 
   * - ``BufferUsageVertex = 1u << 0``
     - 
   * - ``BufferUsageIndex = 1u << 1``
     - 
   * - ``BufferUsageUniform = 1u << 2``
     - 
   * - ``BufferUsageStorage = 1u << 3``
     - 
   * - ``BufferUsageIndirect = 1u << 4``
     - 
   * - ``BufferUsageStaging = 1u << 5``
     - 
   * - ``BufferUsageReadback = 1u << 6``
     - 
   * - ``TextureUsageSampled = 1u << 0``
     - 
   * - ``TextureUsageStorage = 1u << 1``
     - 
   * - ``TextureUsageRenderTarget = 1u << 2``
     - 
   * - ``TextureUsageCopySource = 1u << 3``
     - 
   * - ``TextureUsageCopyDestination = 1u << 4``
     - 
   * - ``BarrierVertex = 1u << 0``
     - 
   * - ``BarrierIndex = 1u << 1``
     - 
   * - ``BarrierUniform = 1u << 2``
     - 
   * - ``BarrierStorage = 1u << 3``
     - 
   * - ``BarrierTexture = 1u << 4``
     - 
   * - ``BarrierIndirect = 1u << 5``
     - 
   * - ``BarrierAll = 0xffffffffu``
     - 

backends.md
-----------
:Header: ``/media/projectos/projects/cpp/GPU/docs/api/backends.md``

commands.md
-----------
:Header: ``/media/projectos/projects/cpp/GPU/docs/api/commands.md``

device.md
---------
:Header: ``/media/projectos/projects/cpp/GPU/docs/api/device.md``

diagnostics.md
--------------
:Header: ``/media/projectos/projects/cpp/GPU/docs/api/diagnostics.md``

overview.md
-----------
:Header: ``/media/projectos/projects/cpp/GPU/docs/api/overview.md``

resources.md
------------
:Header: ``/media/projectos/projects/cpp/GPU/docs/api/resources.md``

types-and-capabilities.md
-------------------------
:Header: ``/media/projectos/projects/cpp/GPU/docs/api/types-and-capabilities.md``

GPU.h
-----
:Header: ``gpu/include/gpu/GPU.h``

GPUBackend.h
------------
:Header: ``gpu/include/gpu/GPUBackend.h``

GPUCapabilities.h
-----------------
:Header: ``gpu/include/gpu/GPUCapabilities.h``

GPUDescriptors.h
----------------
:Header: ``gpu/include/gpu/GPUDescriptors.h``

GPUDiagnostics.h
----------------
:Header: ``gpu/include/gpu/GPUDiagnostics.h``

GPUError.h
----------
:Header: ``gpu/include/gpu/GPUError.h``

GPUHandles.h
------------
:Header: ``gpu/include/gpu/GPUHandles.h``

GPUProfiler.h
-------------
:Header: ``gpu/include/gpu/GPUProfiler.h``

GPUSurface.h
------------
:Header: ``gpu/include/gpu/GPUSurface.h``

GPUTypes.h
----------
:Header: ``gpu/include/gpu/GPUTypes.h``
