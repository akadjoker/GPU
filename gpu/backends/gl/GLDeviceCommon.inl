// Implementacoes de metodos de Device que sao byte-a-byte identicas entre
// PortableGLDevice (backend OpenGL desktop) e GLESDevice (backend OpenGL
// ES). Movidas para aqui tal como estavam, sem reescrever nada.
//
// Incluido por PortableGLDevice.cpp e GLESDevice.cpp com
// GPU_GL_DEVICE_CLASS definido para o nome da classe concreta:
//
//   #define GPU_GL_DEVICE_CLASS PortableGLDevice
//   #include "GLDeviceCommon.inl"
//   #undef GPU_GL_DEVICE_CLASS
//
// (GLESDevice.cpp inclui "../gl/GLDeviceCommon.inl", por estar noutro
// diretorio.)
//
// Nao inclui createTexture, initialize, drawIndirectCount,
// drawIndexedIndirectCount, setPipeline, draw, drawIndexed nem
// compressedTextureFormat: essas divergem a serio entre GL e GLES (ver
// doc/HANDOFF_DEDUP.md).

GPU_GL_DEVICE_CLASS::GPU_GL_DEVICE_CLASS(const DeviceDesc &desc)
    : mSurface(static_cast<const GLSurface *>(desc.surface.nativeHandle)),
      mSurfaceWidth(desc.surface.width), mSurfaceHeight(desc.surface.height) {}

GPU_GL_DEVICE_CLASS::~GPU_GL_DEVICE_CLASS() { shutdown(); }

const GPUCapabilities &GPU_GL_DEVICE_CLASS::capabilities() const {
  return mCapabilities;
}

std::uint64_t GPU_GL_DEVICE_CLASS::totalErrorCount() const {
  return mErrors.totalErrorCount();
}

std::uint32_t GPU_GL_DEVICE_CLASS::pendingErrorCount() const {
  return mErrors.pendingErrorCount();
}

bool GPU_GL_DEVICE_CLASS::getError(::gpu::GPUError &error) {
  return mErrors.getError(error);
}

void GPU_GL_DEVICE_CLASS::clearErrors() { mErrors.clearErrors(); }

void GPU_GL_DEVICE_CLASS::push(::gpu::GPUErrorCode code,
                            ::gpu::GPUOperation operation,
                            std::uint64_t resource, std::uint64_t value0,
                            std::uint64_t value1, const char *message) {
  ::gpu::GPUError error;
  error.code = code;
  error.operation = operation;
  error.resource = resource;
  error.value0 = value0;
  error.value1 = value1;
  error.message = message;
  mErrors.push(error);
}

template <typename HandleType, typename Object>
HandleType GPU_GL_DEVICE_CLASS::insert(ResourcePool<Object, HandleType> &pool,
                                    const Object &object,
                                    ::gpu::GPUOperation operation) {
  const HandleType handle = pool.insert(object);
  if (handle.valid())
    return handle;
  push(::gpu::GPUErrorCode::OutOfMemory, operation, 0, pool.size(), 0,
       "resource pool allocation failed");
  return {};
}

template <typename HandleType, typename Object>
Object *GPU_GL_DEVICE_CLASS::find(HandleType handle,
                               ResourcePool<Object, HandleType> &pool,
                               ::gpu::GPUOperation operation) {
  const std::uint64_t value = handle.value();
  Object *object = pool.find(handle);
  if (!object) {
    push(::gpu::GPUErrorCode::InvalidHandle, operation, value, 0, 0,
         "invalid resource handle");
    return nullptr;
  }
  return object;
}

template <typename HandleType, typename Object>
const Object *GPU_GL_DEVICE_CLASS::find(
    HandleType handle, const ResourcePool<Object, HandleType> &pool,
    ::gpu::GPUOperation operation) {
  return find(handle, const_cast<ResourcePool<Object, HandleType> &>(pool),
              operation);
}

template <typename HandleType, typename Object>
void GPU_GL_DEVICE_CLASS::release(HandleType handle,
                               ResourcePool<Object, HandleType> &pool,
                               ::gpu::GPUOperation operation) {
  if (!find(handle, pool, operation))
    return;
  pool.erase(handle);
}

void GPU_GL_DEVICE_CLASS::shutdown() {
  if (!mAlive)
    return;
  if (!mSurface->makeCurrent(mSurface->userData)) {
    const std::uint64_t leakedResources =
        static_cast<std::uint64_t>(mBuffers.size()) + mTextures.size() +
        mSamplers.size() + mPipelines.size() + mQueries.size() +
        mFences.size();
    push(GPUErrorCode::DeviceLost, GPUOperation::Destroy, 0, leakedResources,
         0, "context unavailable during shutdown, native resources leaked");
    mFramebufferCacheCount = 0;
    mFences.clear();
    mQueries.clear();
    mPipelines.clear();
    mSamplers.clear();
    mTextures.clear();
    mBuffers.clear();
    mCopyReadFramebuffer = 0;
    mCopyDrawFramebuffer = 0;
    mAlive = false;
    mInRenderPass = false;
    mPipeline = PipelineHandle();
    mIndexBuffer = BufferHandle();
    mActiveOcclusionQuery = QueryHandle();
    mSurfaceState = SurfaceState::Lost;
    return;
  }
  mFences.forEach([](FenceObject &fence) { glDeleteSync(fence.sync); });
  mQueries.forEach([](QueryObject &query) { glDeleteQueries(1, &query.id); });
  mPipelines.forEach([](PipelineObject &pipeline) {
    glDeleteVertexArrays(1, &pipeline.vertexArray);
    glDeleteProgram(pipeline.program);
  });
  mSamplers.forEach(
      [](SamplerObject &sampler) { glDeleteSamplers(1, &sampler.id); });
  mTextures.forEach(
      [](TextureObject &texture) { glDeleteTextures(1, &texture.id); });
  mBuffers.forEach(
      [](BufferObject &buffer) { glDeleteBuffers(1, &buffer.id); });
  destroyFramebufferCache();
  mFences.clear();
  mQueries.clear();
  mPipelines.clear();
  mSamplers.clear();
  mTextures.clear();
  mBuffers.clear();
  if (mCopyReadFramebuffer)
    glDeleteFramebuffers(1, &mCopyReadFramebuffer);
  if (mCopyDrawFramebuffer)
    glDeleteFramebuffers(1, &mCopyDrawFramebuffer);
  mCopyReadFramebuffer = 0;
  mCopyDrawFramebuffer = 0;
  mAlive = false;
  mInRenderPass = false;
  mPipeline = PipelineHandle();
  mIndexBuffer = BufferHandle();
  mActiveOcclusionQuery = QueryHandle();
  mSurfaceState = SurfaceState::Lost;
}

BufferHandle GPU_GL_DEVICE_CLASS::createBuffer(const BufferDesc &desc) {
  if (!mAlive || desc.size == 0 || desc.usage == 0 ||
      desc.size > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      (desc.initialData.data && desc.initialData.size > desc.size)) {
    push(mAlive ? ::gpu::GPUErrorCode::InvalidArgument
                : ::gpu::GPUErrorCode::DeviceLost,
         ::gpu::GPUOperation::CreateBuffer, 0, desc.size, desc.initialData.size,
         mAlive ? "invalid buffer descriptor" : "device is shut down");
    return BufferHandle();
  }
  if ((desc.usage & BufferUsageStorage) && !mCapabilities.storageBuffers) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreateBuffer, 0, desc.usage, 0,
         "storage buffers are unavailable on this device");
    return BufferHandle();
  }
  if ((desc.usage & BufferUsageIndirect) && !mCapabilities.indirectDraw) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreateBuffer, 0, desc.usage, 0,
         "indirect draw buffers are unavailable on this device");
    return BufferHandle();
  }
  BufferObject object;
  object.size = desc.size;
  object.usage = desc.usage;
  object.target = bufferTarget(desc.usage);
  drainDriverErrors();
  glGenBuffers(1, &object.id);
  if (!object.id) {
    push(::gpu::GPUErrorCode::OutOfMemory, ::gpu::GPUOperation::CreateBuffer, 0,
         desc.size, 0, "OpenGL buffer creation failed");
    return BufferHandle();
  }
  glBindBuffer(object.target, object.id);
  glBufferData(object.target, static_cast<GLsizeiptr>(desc.size),
               desc.initialData.data, GL_STATIC_DRAW);
  const GLenum bufferError = glGetError();
  if (bufferError != GL_NO_ERROR) {
    glDeleteBuffers(1, &object.id);
    push(::gpu::GPUErrorCode::OutOfMemory, ::gpu::GPUOperation::CreateBuffer, 0,
         bufferError, desc.size, "OpenGL rejected the buffer allocation");
    return BufferHandle();
  }
  setObjectLabel(GL_BUFFER, object.id, desc.debugName);
  const BufferHandle handle =
      insert<BufferHandle>(mBuffers, object, ::gpu::GPUOperation::CreateBuffer);
  if (!handle.valid())
    glDeleteBuffers(1, &object.id);
  return handle;
}

void GPU_GL_DEVICE_CLASS::evictFramebuffers(GLuint texture) {
  std::uint32_t index = 0;
  while (index < mFramebufferCacheCount) {
    FramebufferCacheEntry &entry = mFramebufferCache[index];
    bool referenced =
        entry.key.hasDepthStencil && entry.key.depthStencil.texture == texture;
    for (std::uint32_t color = 0;
         !referenced && color < entry.key.colorCount; ++color)
      referenced = entry.key.colors[color].texture == texture;
    if (!referenced) {
      ++index;
      continue;
    }
    glDeleteFramebuffers(1, &entry.framebuffer);
    mFramebufferCache[index] = mFramebufferCache[mFramebufferCacheCount - 1];
    --mFramebufferCacheCount;
  }
}

void GPU_GL_DEVICE_CLASS::destroyFramebufferCache() {
  for (std::uint32_t index = 0; index < mFramebufferCacheCount; ++index)
    glDeleteFramebuffers(1, &mFramebufferCache[index].framebuffer);
  mFramebufferCacheCount = 0;
}

void GPU_GL_DEVICE_CLASS::drainDriverErrors() {
  while (glGetError() != GL_NO_ERROR) {
  }
}

char *GPU_GL_DEVICE_CLASS::nextDiagnostic() {
  char *slot = mShaderDiagnostic[mShaderDiagnosticSlot];
  mShaderDiagnosticSlot =
      (mShaderDiagnosticSlot + 1) % ShaderDiagnosticSlots;
  slot[0] = '\0';
  return slot;
}

void GPU_GL_DEVICE_CLASS::assignUniformBlockBindings(GLuint program) {
  GLint blockCount = 0;
  glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &blockCount);
  if (blockCount <= 0)
    return;
  bool explicitBindings = false;
  for (GLint index = 0; index < blockCount; ++index) {
    GLint binding = 0;
    glGetActiveUniformBlockiv(program, static_cast<GLuint>(index),
                              GL_UNIFORM_BLOCK_BINDING, &binding);
    if (binding != 0)
      explicitBindings = true;
  }
  if (explicitBindings)
    return;
  if (static_cast<std::uint32_t>(blockCount) >
      mCapabilities.maxUniformBufferBindings) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreatePipeline, 0,
         static_cast<std::uint64_t>(blockCount),
         mCapabilities.maxUniformBufferBindings,
         "pipeline declares more uniform blocks than the device supports");
    return;
  }
  for (GLint index = 0; index < blockCount; ++index)
    glUniformBlockBinding(program, static_cast<GLuint>(index),
                          static_cast<GLuint>(index));
}

void GPU_GL_DEVICE_CLASS::assignSamplerUnits(GLuint program) {
  GLint uniformCount = 0;
  glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniformCount);
  if (uniformCount <= 0)
    return;
  GLint previousProgram = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glUseProgram(program);

  struct SamplerUniform {
    GLint location;
    GLint elements;
  };
  SamplerUniform samplers[64];
  std::uint32_t samplerCount = 0;
  bool explicitBindings = false;
  for (GLint index = 0; index < uniformCount; ++index) {
    GLint elements = 0;
    GLenum type = 0;
    GLchar name[256] = {};
    glGetActiveUniform(program, static_cast<GLuint>(index), sizeof(name),
                       nullptr, &elements, &type, name);
    if (!samplerUniformType(type) || elements <= 0)
      continue;
    const GLint location = glGetUniformLocation(program, name);
    if (location < 0)
      continue;
    GLint current = 0;
    glGetUniformiv(program, location, &current);
    if (current != 0)
      explicitBindings = true;
    if (samplerCount < 64) {
      samplers[samplerCount].location = location;
      samplers[samplerCount].elements = elements;
      ++samplerCount;
    }
  }

  if (!explicitBindings && samplerCount != 0) {
    GLint units[16];
    std::uint32_t nextUnit = 0;
    for (std::uint32_t index = 0; index < samplerCount; ++index) {
      GLint remaining = samplers[index].elements;
      GLint written = 0;
      while (remaining > 0) {
        const GLint batch = remaining < 16 ? remaining : 16;
        for (GLint element = 0; element < batch; ++element)
          units[element] = static_cast<GLint>(nextUnit + element);
        if (nextUnit + static_cast<std::uint32_t>(batch) >
            mCapabilities.maxTextureBindings) {
          push(::gpu::GPUErrorCode::UnsupportedFeature,
               ::gpu::GPUOperation::CreatePipeline, 0, samplerCount,
               mCapabilities.maxTextureBindings,
               "pipeline declares more samplers than the device has texture "
               "units");
          break;
        }
        glUniform1iv(samplers[index].location + written, batch, units);
        nextUnit += static_cast<std::uint32_t>(batch);
        written += batch;
        remaining -= batch;
      }
    }
  }

  glUseProgram(static_cast<GLuint>(previousProgram));
}

GLuint GPU_GL_DEVICE_CLASS::compileShader(GLenum stage, const ShaderDesc &desc) {
  if (!desc.source.data || desc.source.size == 0 ||
      desc.source.size >
          static_cast<std::uint64_t>(std::numeric_limits<GLint>::max()))
    return 0;
  if (desc.entryPoint && std::strcmp(desc.entryPoint, "main") != 0) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreatePipeline, 0, stage, 0,
         "GLSL shaders must use \"main\" as the entry point");
    return 0;
  }
  const GLuint shader = glCreateShader(stage);
  const GLchar *source = static_cast<const GLchar *>(desc.source.data);
  const GLint length = static_cast<GLint>(desc.source.size);
  glShaderSource(shader, 1, &source, &length);
  glCompileShader(shader);
  setObjectLabel(GL_SHADER, shader, desc.debugName);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE)
    return shader;
  GLsizei written = 0;
  char *diagnostic = nextDiagnostic();
  glGetShaderInfoLog(shader, static_cast<GLsizei>(ShaderDiagnosticSize - 1),
                    &written, diagnostic);
  diagnostic[std::max<GLsizei>(written, 0)] = '\0';
  glDeleteShader(shader);
  push(::gpu::GPUErrorCode::ShaderCompilationFailed,
       ::gpu::GPUOperation::CreatePipeline, 0, stage, 0, diagnostic);
  return 0;
}

PipelineHandle GPU_GL_DEVICE_CLASS::createComputePipeline(
    const PipelineDesc &desc) {
  const std::uint64_t maxShaderSize =
      static_cast<std::uint64_t>(std::numeric_limits<GLint>::max());
  if (!mCapabilities.compute || desc.compute.source.size == 0 ||
      desc.compute.source.size > maxShaderSize) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
         "compute pipelines are unavailable on this device");
    return PipelineHandle();
  }
  const GLuint compute = compileShader(GL_COMPUTE_SHADER, desc.compute);
  if (!compute)
    return PipelineHandle();
  PipelineObject object;
  object.isCompute = true;
  object.program = glCreateProgram();
  glAttachShader(object.program, compute);
  glLinkProgram(object.program);
  glDeleteShader(compute);
  GLint linked = GL_FALSE;
  glGetProgramiv(object.program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLsizei written = 0;
    char *diagnostic = nextDiagnostic();
    glGetProgramInfoLog(object.program,
                       static_cast<GLsizei>(ShaderDiagnosticSize - 1),
                       &written, diagnostic);
    diagnostic[std::max<GLsizei>(written, 0)] = '\0';
    glDeleteProgram(object.program);
    push(::gpu::GPUErrorCode::PipelineCreationFailed,
         ::gpu::GPUOperation::CreatePipeline, 0, 0, 0, diagnostic);
    return PipelineHandle();
  }
  setObjectLabel(GL_PROGRAM, object.program, desc.debugName);
  assignSamplerUnits(object.program);
  assignUniformBlockBindings(object.program);
  const PipelineHandle handle = insert<PipelineHandle>(
      mPipelines, object, ::gpu::GPUOperation::CreatePipeline);
  if (!handle.valid())
    glDeleteProgram(object.program);
  return handle;
}

PipelineHandle GPU_GL_DEVICE_CLASS::createPipeline(const PipelineDesc &desc) {
  const std::uint64_t maxShaderSize =
      static_cast<std::uint64_t>(std::numeric_limits<GLint>::max());
  const bool hasCompute = desc.compute.source.data != nullptr;
  const bool hasGraphics =
      desc.vertex.source.data != nullptr || desc.fragment.source.data != nullptr;
  if (!mAlive || hasCompute == hasGraphics) {
    push(mAlive ? ::gpu::GPUErrorCode::InvalidArgument
                : ::gpu::GPUErrorCode::DeviceLost,
         ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
         mAlive ? "pipeline must be either compute or graphics"
                : "device is shut down");
    return PipelineHandle();
  }
  if (hasCompute)
    return createComputePipeline(desc);
  if (!desc.vertex.source.data || !desc.fragment.source.data ||
      desc.vertex.source.size == 0 || desc.fragment.source.size == 0 ||
      desc.vertex.source.size > maxShaderSize ||
      desc.fragment.source.size > maxShaderSize) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
         "graphics pipeline requires vertex and fragment shaders");
    return PipelineHandle();
  }
  // This backend never compiles/attaches tessellation control/evaluation
  // or geometry stages (mCapabilities.tessellationShader/geometryShader
  // are always false here) - reject a request for them instead of
  // silently dropping the extra stages and falling back to a
  // vertex+fragment-only pipeline, matching the Vulkan backend's
  // validation for the same descriptor fields.
  if (desc.tessControl.source.data || desc.tessEvaluation.source.data ||
      desc.geometry.source.data || desc.topology == Topology::Patches) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreatePipeline, 0, 0, 0,
         "tessellation and geometry shaders are unavailable on this device");
    return PipelineHandle();
  }
  if (desc.vertexBufferCount > PipelineDesc::MaxVertexBuffers ||
      desc.colorTargetCount > mCapabilities.maxColorAttachments) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline, 0,
         desc.vertexBufferCount, desc.colorTargetCount,
         "invalid pipeline layout");
    return PipelineHandle();
  }
  for (std::uint32_t index = 0; index < desc.colorTargetCount; ++index) {
    GLint internalFormat = 0;
    GLenum layout = 0;
    GLenum type = 0;
    if (depthFormat(desc.colorTargets[index].format) ||
        !textureFormat(desc.colorTargets[index].format, internalFormat, layout,
                       type) ||
        (!mCapabilities.independentBlend && index != 0 &&
         !blendStateEqual(desc.colorTargets[0], desc.colorTargets[index]))) {
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline, 0,
           index, static_cast<std::uint64_t>(desc.colorTargets[index].format),
           "invalid or unsupported pipeline color target state");
      return PipelineHandle();
    }
  }
  for (std::uint32_t slot = 0; slot < desc.vertexBufferCount; ++slot)
    if (desc.vertexBuffers[slot].stride == 0 ||
        desc.vertexBuffers[slot].attributeCount >
            VertexBufferLayout::MaxAttributes) {
      push(GPUErrorCode::InvalidArgument, GPUOperation::CreatePipeline, 0,
           slot, desc.vertexBuffers[slot].attributeCount,
           "invalid vertex buffer layout");
      return PipelineHandle();
    }
  const GLuint vertex = compileShader(GL_VERTEX_SHADER, desc.vertex);
  if (!vertex)
    return PipelineHandle();
  const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, desc.fragment);
  if (!fragment) {
    glDeleteShader(vertex);
    return PipelineHandle();
  }
  PipelineObject object;
  object.vertexBufferCount = desc.vertexBufferCount;
  object.topology = topologyValue(desc.topology);
  object.raster = desc.raster;
  object.depthStencil = desc.depthStencil;
  object.colorTargetCount = desc.colorTargetCount;
  for (std::uint32_t index = 0; index < desc.colorTargetCount; ++index)
    object.colorTargets[index] = desc.colorTargets[index];
  for (std::uint32_t slot = 0; slot < desc.vertexBufferCount; ++slot)
    object.vertexBuffers[slot] = desc.vertexBuffers[slot];
  object.program = glCreateProgram();
  glAttachShader(object.program, vertex);
  glAttachShader(object.program, fragment);
  glLinkProgram(object.program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(object.program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLsizei written = 0;
    char *diagnostic = nextDiagnostic();
    glGetProgramInfoLog(object.program,
                       static_cast<GLsizei>(ShaderDiagnosticSize - 1),
                       &written, diagnostic);
    diagnostic[std::max<GLsizei>(written, 0)] = '\0';
    glDeleteProgram(object.program);
    push(::gpu::GPUErrorCode::PipelineCreationFailed,
         ::gpu::GPUOperation::CreatePipeline, 0, 0, 0, diagnostic);
    return PipelineHandle();
  }
  glGenVertexArrays(1, &object.vertexArray);
  setObjectLabel(GL_PROGRAM, object.program, desc.debugName);
  assignSamplerUnits(object.program);
  assignUniformBlockBindings(object.program);
  const PipelineHandle handle = insert<PipelineHandle>(
      mPipelines, object, ::gpu::GPUOperation::CreatePipeline);
  if (!handle.valid()) {
    glDeleteVertexArrays(1, &object.vertexArray);
    glDeleteProgram(object.program);
  }
  return handle;
}

QueryHandle GPU_GL_DEVICE_CLASS::createQuery(QueryType type) {
  if (!mAlive ||
      (type == QueryType::Timestamp && !mCapabilities.timestampQueries) ||
      (type == QueryType::Occlusion && !mCapabilities.occlusionQueries)) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreateQuery, 0,
         static_cast<std::uint64_t>(type), 0,
         mAlive ? "query type is unavailable on this device"
                : "device is shut down");
    return QueryHandle();
  }
  QueryObject object;
  object.type = type;
  glGenQueries(1, &object.id);
  if (!object.id) {
    push(::gpu::GPUErrorCode::OutOfMemory, ::gpu::GPUOperation::CreateQuery, 0,
         0, 0, "OpenGL query creation failed");
    return QueryHandle();
  }
  const QueryHandle handle =
      insert<QueryHandle>(mQueries, object, ::gpu::GPUOperation::CreateQuery);
  if (!handle.valid())
    glDeleteQueries(1, &object.id);
  return handle;
}

void GPU_GL_DEVICE_CLASS::destroy(BufferHandle handle) {
  BufferObject *slot =
      find(handle, mBuffers, ::gpu::GPUOperation::Destroy);
  if (!slot)
    return;
  glDeleteBuffers(1, &slot->id);
  release(handle, mBuffers, ::gpu::GPUOperation::Destroy);
  if (mIndexBuffer == handle)
    mIndexBuffer = BufferHandle();
}

void GPU_GL_DEVICE_CLASS::destroy(TextureHandle handle) {
  TextureObject *slot =
      find(handle, mTextures, ::gpu::GPUOperation::Destroy);
  if (!slot)
    return;
  evictFramebuffers(slot->id);
  glDeleteTextures(1, &slot->id);
  release(handle, mTextures, ::gpu::GPUOperation::Destroy);
}

void GPU_GL_DEVICE_CLASS::destroy(SamplerHandle handle) {
  SamplerObject *slot =
      find(handle, mSamplers, ::gpu::GPUOperation::Destroy);
  if (!slot)
    return;
  glDeleteSamplers(1, &slot->id);
  release(handle, mSamplers, ::gpu::GPUOperation::Destroy);
}

void GPU_GL_DEVICE_CLASS::destroy(PipelineHandle handle) {
  PipelineObject *slot =
      find(handle, mPipelines, ::gpu::GPUOperation::Destroy);
  if (!slot)
    return;
  glDeleteVertexArrays(1, &slot->vertexArray);
  glDeleteProgram(slot->program);
  release(handle, mPipelines, ::gpu::GPUOperation::Destroy);
  if (mPipeline == handle)
    mPipeline = PipelineHandle();
}

void GPU_GL_DEVICE_CLASS::destroy(QueryHandle handle) {
  QueryObject *slot = find(handle, mQueries, ::gpu::GPUOperation::Destroy);
  if (!slot)
    return;
  glDeleteQueries(1, &slot->id);
  release(handle, mQueries, ::gpu::GPUOperation::Destroy);
  if (mActiveOcclusionQuery == handle)
    mActiveOcclusionQuery = QueryHandle();
}

void GPU_GL_DEVICE_CLASS::destroy(FenceHandle handle) {
  FenceObject *slot = find(handle, mFences, ::gpu::GPUOperation::Destroy);
  if (!slot)
    return;
  glDeleteSync(slot->sync);
  release(handle, mFences, ::gpu::GPUOperation::Destroy);
}

bool GPU_GL_DEVICE_CLASS::setScissor(const Rect &rect) {
  if (!mInRenderPass || rect.width == 0 || rect.height == 0 || rect.x < 0 ||
      rect.y < 0 ||
      static_cast<std::uint64_t>(rect.x) + rect.width > mRenderWidth ||
      static_cast<std::uint64_t>(rect.y) + rect.height > mRenderHeight) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource, 0,
         rect.width, rect.height, "invalid scissor rectangle");
    return false;
  }
  glScissor(rect.x, rect.y, static_cast<GLsizei>(rect.width),
            static_cast<GLsizei>(rect.height));
  return true;
}

bool GPU_GL_DEVICE_CLASS::setStencilReference(std::uint32_t reference) {
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::BindResource);
  if (!mInRenderPass || !pipeline ||
      !pipeline->depthStencil.stencilEnabled) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         mPipeline.value(), reference, 0,
         "stencil reference requires an active stencil pipeline");
    return false;
  }
  const DepthStencilState &state = pipeline->depthStencil;
  glStencilFuncSeparate(GL_FRONT, compareValue(state.stencilFront.compare),
                        static_cast<GLint>(reference), state.stencilReadMask);
  glStencilFuncSeparate(GL_BACK, compareValue(state.stencilBack.compare),
                        static_cast<GLint>(reference), state.stencilReadMask);
  return true;
}

bool GPU_GL_DEVICE_CLASS::bindVertexBuffer(std::uint32_t slot,
                                        BufferHandle handle,
                                        std::uint64_t offset) {
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::BindResource);
  BufferObject *buffer =
      find(handle, mBuffers, GPUOperation::BindResource);
  if (!mInRenderPass || !pipeline || !buffer ||
      slot >= pipeline->vertexBufferCount ||
      !(buffer->usage & BufferUsageVertex) ||
      offset >= buffer->size) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         handle.value(), slot, offset, "invalid vertex buffer binding");
    return false;
  }
  const VertexBufferLayout &layout = pipeline->vertexBuffers[slot];
  glBindVertexArray(pipeline->vertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, buffer->id);
  for (std::uint32_t index = 0; index < layout.attributeCount; ++index) {
    const VertexAttribute &attribute = layout.attributes[index];
    const VertexFormatInfo info = vertexFormatInfo(attribute.format);
    const std::uintptr_t pointer =
        static_cast<std::uintptr_t>(offset + attribute.offset);
    glEnableVertexAttribArray(attribute.shaderLocation);
    if (info.integer)
      glVertexAttribIPointer(attribute.shaderLocation, info.components, info.type,
                             static_cast<GLsizei>(layout.stride),
                             reinterpret_cast<const void *>(pointer));
    else
      glVertexAttribPointer(attribute.shaderLocation, info.components, info.type,
                            info.normalized,
                            static_cast<GLsizei>(layout.stride),
                            reinterpret_cast<const void *>(pointer));
    glVertexAttribDivisor(
        attribute.shaderLocation,
        layout.stepMode == VertexStepMode::Instance ? 1u : 0u);
  }
  return true;
}

bool GPU_GL_DEVICE_CLASS::bindIndexBuffer(BufferHandle handle, IndexFormat format,
                                       std::uint64_t offset) {
  BufferObject *buffer =
      find(handle, mBuffers, GPUOperation::BindResource);
  if (!mInRenderPass || !mPipeline.valid() || !buffer ||
      !(buffer->usage & BufferUsageIndex) ||
      offset >= buffer->size) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         handle.value(), offset, buffer ? buffer->size : 0,
         "invalid index buffer binding");
    return false;
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->id);
  mIndexBuffer = handle;
  mIndexType = format == IndexFormat::Uint16 ? GL_UNSIGNED_SHORT
                                             : GL_UNSIGNED_INT;
  mIndexOffset = offset;
  return true;
}

bool GPU_GL_DEVICE_CLASS::bindUniformBuffer(std::uint32_t slot,
                                         BufferHandle handle,
                                         std::uint64_t offset,
                                         std::uint64_t size) {
  BufferObject *buffer =
      find(handle, mBuffers, GPUOperation::BindResource);
  if (!mInRenderPass || !buffer ||
      !(buffer->usage & BufferUsageUniform) || size == 0 ||
      slot >= mCapabilities.maxUniformBufferBindings ||
      (offset % mCapabilities.uniformBufferOffsetAlignment) != 0 ||
      offset > buffer->size || size > buffer->size - offset ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      size > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::BindResource, handle.value(),
         offset, size, "invalid uniform buffer range");
    return false;
  }
  glBindBufferRange(GL_UNIFORM_BUFFER, slot, buffer->id,
                    static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(size));
  return true;
}

std::int32_t GPU_GL_DEVICE_CLASS::uniformBlockSlot(PipelineHandle handle,
                                        const char *name) {
  PipelineObject *pipeline =
      find(handle, mPipelines, GPUOperation::BindResource);
  if (!pipeline || !name || !pipeline->program) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         handle.value(), 0, 0, "uniform block lookup needs a valid pipeline");
    return -1;
  }
  const GLuint index = glGetUniformBlockIndex(pipeline->program, name);
  if (index == GL_INVALID_INDEX)
    return -1;
  GLint binding = 0;
  glGetActiveUniformBlockiv(pipeline->program, index, GL_UNIFORM_BLOCK_BINDING,
                            &binding);
  return static_cast<std::int32_t>(binding);
}

std::int32_t GPU_GL_DEVICE_CLASS::textureSlot(PipelineHandle handle, const char *name) {
  PipelineObject *pipeline =
      find(handle, mPipelines, GPUOperation::BindResource);
  if (!pipeline || !name || !pipeline->program) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         handle.value(), 0, 0, "texture slot lookup needs a valid pipeline");
    return -1;
  }
  const GLint location = glGetUniformLocation(pipeline->program, name);
  if (location < 0)
    return -1;
  GLint unit = 0;
  GLint previousProgram = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glUseProgram(pipeline->program);
  glGetUniformiv(pipeline->program, location, &unit);
  glUseProgram(static_cast<GLuint>(previousProgram));
  return static_cast<std::int32_t>(unit);
}

bool GPU_GL_DEVICE_CLASS::bindTexture(std::uint32_t slot, TextureHandle texture,
                                   SamplerHandle sampler) {
  TextureObject *textureObject =
      find(texture, mTextures, GPUOperation::BindResource);
  SamplerObject *samplerObject =
      find(sampler, mSamplers, GPUOperation::BindResource);
  if (!mInRenderPass || slot >= mCapabilities.maxTextureBindings) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         texture.value(), slot, mCapabilities.maxTextureBindings,
         "texture binding requires a render pass slot");
    return false;
  }
  if (!textureObject || !samplerObject)
    return false;
  for (std::uint32_t index = 0; index < mPassAttachmentCount; ++index) {
    if (mPassAttachments[index] != texture)
      continue;
    // GL/GLES: ler de uma textura que e attachment da passada atual e um
    // feedback loop. O resultado e indefinido, por isso e recusado aqui.
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         texture.value(), slot, 0,
         "texture is an attachment of the current render pass (feedback loop)");
    return false;
  }
  glActiveTexture(GL_TEXTURE0 + slot);
  glBindTexture(textureObject->target, textureObject->id);
  glBindSampler(slot, samplerObject->id);
  return true;
}

std::int32_t GPU_GL_DEVICE_CLASS::registerBindlessTexture(TextureHandle texture,
                                                       SamplerHandle sampler) {
  (void)texture;
  (void)sampler;
  push(GPUErrorCode::UnsupportedFeature, GPUOperation::BindResource, 0, 0, 0,
       "bindless textures are unavailable on this GL/GLES backend");
  return -1;
}

void GPU_GL_DEVICE_CLASS::unregisterBindlessTexture(std::int32_t index) {
  (void)index;
}

bool GPU_GL_DEVICE_CLASS::bindStorageBuffer(std::uint32_t slot,
                                         BufferHandle handle,
                                         std::uint64_t offset,
                                         std::uint64_t size) {
  BufferObject *buffer = find(handle, mBuffers, GPUOperation::BindResource);
  if (!buffer || !(buffer->usage & BufferUsageStorage) || size == 0 ||
      slot >= mCapabilities.maxStorageBufferBindings ||
      (offset % mCapabilities.storageBufferOffsetAlignment) != 0 ||
      offset > buffer->size || size > buffer->size - offset ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      size > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::BindResource, handle.value(),
         offset, size, "invalid storage buffer range");
    return false;
  }
  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, slot, buffer->id,
                    static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(size));
  return true;
}

bool GPU_GL_DEVICE_CLASS::updateBuffer(BufferHandle handle, std::uint64_t offset,
                                    DataView data) {
  BufferObject *buffer =
      find(handle, mBuffers, GPUOperation::UpdateBuffer);
  if (mInRenderPass || !buffer || !data.data || data.size == 0 ||
      offset > buffer->size || data.size > buffer->size - offset ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      data.size > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::UpdateBuffer, handle.value(),
         offset, data.size, "buffer update is out of bounds");
    return false;
  }
  glBindBuffer(buffer->target, buffer->id);
  glBufferSubData(buffer->target, static_cast<GLintptr>(offset),
                  static_cast<GLsizeiptr>(data.size), data.data);
  return true;
}

bool GPU_GL_DEVICE_CLASS::updateTexture(TextureHandle handle,
                                     const TextureRegion &region, DataView data,
                                     const TextureDataLayout &layout) {
  TextureObject *texture =
      find(handle, mTextures, GPUOperation::UpdateTexture);
  if (!texture)
    return false;
  if (mInRenderPass || !(texture->usage & TextureUsageCopyDestination) ||
      texture->sampleCount != 1 || !data.data || data.size == 0 ||
      region.width == 0 || region.height == 0 ||
      region.depthOrLayers == 0 || region.mipLevel >= texture->mipCount) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::UpdateTexture,
         handle.value(), region.mipLevel, data.size,
         "invalid texture update state or descriptor");
    return false;
  }
  TextureDimension dimension = TextureDimension::Texture2D;
  if (texture->target == GL_TEXTURE_CUBE_MAP)
    dimension = TextureDimension::TextureCube;
  else if (texture->target == GL_TEXTURE_2D_ARRAY)
    dimension = TextureDimension::Texture2DArray;
  else if (texture->target == GL_TEXTURE_3D)
    dimension = TextureDimension::Texture3D;
  const std::uint32_t mipWidth =
      mipDimension(texture->width, region.mipLevel);
  const std::uint32_t mipHeight =
      mipDimension(texture->height, region.mipLevel);
  std::uint32_t mipDepth = 1;
  if (dimension == TextureDimension::TextureCube)
    mipDepth = 6;
  else if (dimension == TextureDimension::Texture2DArray)
    mipDepth = texture->depthOrLayers;
  else if (dimension == TextureDimension::Texture3D)
    mipDepth = mipDimension(texture->depthOrLayers, region.mipLevel);
  if (region.x > mipWidth || region.width > mipWidth - region.x ||
      region.y > mipHeight || region.height > mipHeight - region.y ||
      region.z > mipDepth || region.depthOrLayers > mipDepth - region.z ||
      (dimension == TextureDimension::Texture2D &&
       (region.z != 0 || region.depthOrLayers != 1))) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::UpdateTexture, handle.value(),
         region.width, region.height,
         "texture update region is out of bounds");
    return false;
  }
  CompressedFormatInfo compressedInfo;
  const bool compressed = compressedTextureFormat(texture->format, compressedInfo);
  const std::uint32_t texelBytes = bytesPerTexel(texture->format);
  const std::uint64_t copiedRows =
      compressed ? (static_cast<std::uint64_t>(region.height) + 3) / 4
                 : region.height;
  const std::uint64_t tightRowBytes =
      compressed
          ? ((static_cast<std::uint64_t>(region.width) + 3) / 4) *
                compressedInfo.blockBytes
          : static_cast<std::uint64_t>(region.width) * texelBytes;
  const std::uint64_t bytesPerRow =
      layout.bytesPerRow == 0 ? tightRowBytes : layout.bytesPerRow;
  const std::uint64_t rowsPerImage =
      layout.rowsPerImage == 0 ? copiedRows : layout.rowsPerImage;
  const bool invalidBlockRegion =
      compressed &&
      ((region.x & 3u) != 0 || (region.y & 3u) != 0 ||
       ((region.width & 3u) != 0 && region.x + region.width != mipWidth) ||
       ((region.height & 3u) != 0 && region.y + region.height != mipHeight));
  std::uint64_t required = 0;
  if (invalidBlockRegion || bytesPerRow < tightRowBytes ||
      rowsPerImage < copiedRows ||
      (!compressed && (texelBytes == 0 || bytesPerRow % texelBytes != 0)) ||
      (compressed &&
       (bytesPerRow != tightRowBytes || rowsPerImage != copiedRows)) ||
      bytesPerRow >
          static_cast<std::uint64_t>((std::numeric_limits<GLint>::max)()) ||
      rowsPerImage >
          static_cast<std::uint64_t>((std::numeric_limits<GLint>::max)()) ||
      !requiredUploadSize(layout.offset, bytesPerRow, rowsPerImage, copiedRows,
                          tightRowBytes, region.depthOrLayers, required) ||
      required > data.size ||
      required >
          static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::UpdateTexture, handle.value(),
         required, data.size, "texture upload layout is invalid or too small");
    return false;
  }
  const std::uint8_t *bytes = static_cast<const std::uint8_t *>(data.data) +
                              static_cast<std::size_t>(layout.offset);
  drainDriverErrors();
  const std::uint64_t imageStride = bytesPerRow * rowsPerImage;
  glBindTexture(texture->target, texture->id);
  if (compressed) {
    const std::uint64_t imageSize = tightRowBytes * copiedRows;
    const std::uint64_t maxImageSize = static_cast<std::uint64_t>(
        (std::numeric_limits<GLsizei>::max)());
    if (imageSize > maxImageSize / region.depthOrLayers) {
      push(GPUErrorCode::OutOfBounds, GPUOperation::UpdateTexture,
           handle.value(), imageSize, region.depthOrLayers,
           "compressed texture upload is too large");
      return false;
    }
    if (dimension == TextureDimension::Texture2D) {
      glCompressedTexSubImage2D(
          texture->target, static_cast<GLint>(region.mipLevel),
          static_cast<GLint>(region.x), static_cast<GLint>(region.y),
          static_cast<GLsizei>(region.width),
          static_cast<GLsizei>(region.height), compressedInfo.internalFormat,
          static_cast<GLsizei>(imageSize), bytes);
    } else if (dimension == TextureDimension::TextureCube) {
      for (std::uint32_t image = 0; image < region.depthOrLayers; ++image)
        glCompressedTexSubImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.z + image,
            static_cast<GLint>(region.mipLevel), static_cast<GLint>(region.x),
            static_cast<GLint>(region.y), static_cast<GLsizei>(region.width),
            static_cast<GLsizei>(region.height), compressedInfo.internalFormat,
            static_cast<GLsizei>(imageSize), bytes + image * imageStride);
    } else {
      glCompressedTexSubImage3D(
          texture->target, static_cast<GLint>(region.mipLevel),
          static_cast<GLint>(region.x), static_cast<GLint>(region.y),
          static_cast<GLint>(region.z), static_cast<GLsizei>(region.width),
          static_cast<GLsizei>(region.height),
          static_cast<GLsizei>(region.depthOrLayers),
          compressedInfo.internalFormat,
          static_cast<GLsizei>(imageSize * region.depthOrLayers), bytes);
    }
  } else {
    GLint internalFormat = 0;
    GLenum format = 0;
    GLenum type = 0;
    textureFormat(texture->format, internalFormat, format, type);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,
                  static_cast<GLint>(bytesPerRow / texelBytes));
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,
                  static_cast<GLint>(rowsPerImage));
    if (dimension == TextureDimension::Texture2D) {
      glTexSubImage2D(texture->target, static_cast<GLint>(region.mipLevel),
                      static_cast<GLint>(region.x),
                      static_cast<GLint>(region.y),
                      static_cast<GLsizei>(region.width),
                      static_cast<GLsizei>(region.height), format, type, bytes);
    } else if (dimension == TextureDimension::TextureCube) {
      for (std::uint32_t image = 0; image < region.depthOrLayers; ++image)
        glTexSubImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.z + image,
            static_cast<GLint>(region.mipLevel), static_cast<GLint>(region.x),
            static_cast<GLint>(region.y), static_cast<GLsizei>(region.width),
            static_cast<GLsizei>(region.height), format, type,
            bytes + image * imageStride);
    } else {
      glTexSubImage3D(
          texture->target, static_cast<GLint>(region.mipLevel),
          static_cast<GLint>(region.x), static_cast<GLint>(region.y),
          static_cast<GLint>(region.z), static_cast<GLsizei>(region.width),
          static_cast<GLsizei>(region.height),
          static_cast<GLsizei>(region.depthOrLayers), format, type, bytes);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  }
  const GLenum uploadError = glGetError();
  if (uploadError != GL_NO_ERROR) {
    push(GPUErrorCode::BackendFailure, GPUOperation::UpdateTexture,
         handle.value(), uploadError, region.mipLevel,
         "OpenGL rejected texture update");
    return false;
  }
  return true;
}

bool GPU_GL_DEVICE_CLASS::generateMipmaps(TextureHandle handle) {
  TextureObject *texture =
      find(handle, mTextures, GPUOperation::GenerateMipmaps);
  if (mInRenderPass || !texture) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::GenerateMipmaps,
         handle.value(), 0, 0,
         "mipmap generation requires a valid texture outside a render pass");
    return false;
  }
  if (texture->mipCount <= 1) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::GenerateMipmaps,
         handle.value(), texture->mipCount, 0,
         "texture was created with a single mip level");
    return false;
  }
  CompressedFormatInfo compressedInfo;
  if (compressedTextureFormat(texture->format, compressedInfo)) {
    push(GPUErrorCode::UnsupportedFormat, GPUOperation::GenerateMipmaps,
         handle.value(), static_cast<std::uint64_t>(texture->format), 0,
         "compressed textures cannot generate mipmaps");
    return false;
  }
  if (depthFormat(texture->format)) {
    // glGenerateMipmap exige um formato color-renderable e filtravel.
    push(GPUErrorCode::UnsupportedFormat, GPUOperation::GenerateMipmaps,
         handle.value(), static_cast<std::uint64_t>(texture->format), 0,
         "depth and stencil textures cannot generate mipmaps");
    return false;
  }
  if (texture->sampleCount != 1) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::GenerateMipmaps,
         handle.value(), texture->sampleCount, 0,
         "multisample textures cannot generate mipmaps");
    return false;
  }
  drainDriverErrors();
  // glActiveTexture(GL_TEXTURE0)/glBindTexture() below would otherwise
  // permanently clobber whatever the caller had bound to texture unit 0
  // (e.g. via bindTexture(0, ...) for an upcoming draw) - save and restore
  // both around the mipmap generation so this call has no side effect on
  // unrelated texture-binding state.
  GLint previousActiveTexture = GL_TEXTURE0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  GLenum bindingPname = GL_TEXTURE_BINDING_2D;
  switch (texture->target) {
  case GL_TEXTURE_CUBE_MAP:
    bindingPname = GL_TEXTURE_BINDING_CUBE_MAP;
    break;
  case GL_TEXTURE_2D_ARRAY:
    bindingPname = GL_TEXTURE_BINDING_2D_ARRAY;
    break;
  case GL_TEXTURE_3D:
    bindingPname = GL_TEXTURE_BINDING_3D;
    break;
  default:
    break;
  }
  GLint previousBinding = 0;
  glGetIntegerv(bindingPname, &previousBinding);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(texture->target, texture->id);
  glGenerateMipmap(texture->target);
  const GLenum error = glGetError();
  glBindTexture(texture->target, static_cast<GLuint>(previousBinding));
  glActiveTexture(static_cast<GLenum>(previousActiveTexture));
  if (error != GL_NO_ERROR) {
    push(GPUErrorCode::BackendFailure, GPUOperation::GenerateMipmaps,
         handle.value(), error, 0, "OpenGL rejected mipmap generation");
    return false;
  }
  return true;
}

bool GPU_GL_DEVICE_CLASS::copyTexture(TextureHandle destination,
                                   const TextureOrigin &destinationOrigin,
                                   TextureHandle source,
                                   const TextureRegion &sourceRegion) {
  TextureObject *destinationTexture =
      find(destination, mTextures, GPUOperation::Copy);
  TextureObject *sourceTexture = find(source, mTextures, GPUOperation::Copy);
  if (mInRenderPass || !destinationTexture || !sourceTexture ||
      sourceRegion.width == 0 || sourceRegion.height == 0 ||
      sourceRegion.depthOrLayers == 0 ||
      sourceRegion.mipLevel >= sourceTexture->mipCount ||
      destinationOrigin.mipLevel >= destinationTexture->mipCount ||
      destinationTexture->format != sourceTexture->format ||
      !(sourceTexture->usage & TextureUsageCopySource) ||
      !(destinationTexture->usage & TextureUsageCopyDestination)) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::Copy, destination.value(),
         sourceRegion.mipLevel, destinationOrigin.mipLevel,
         "invalid texture copy state or descriptor");
    return false;
  }
  CompressedFormatInfo compressedInfo;
  if (compressedTextureFormat(sourceTexture->format, compressedInfo)) {
    push(GPUErrorCode::UnsupportedFormat, GPUOperation::Copy,
         destination.value(),
         static_cast<std::uint64_t>(sourceTexture->format), 0,
         "compressed texture copy is not supported");
    return false;
  }
  if (sourceTexture->sampleCount != 1 || destinationTexture->sampleCount != 1) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::Copy,
         destination.value(), sourceTexture->sampleCount,
         destinationTexture->sampleCount,
         "multisample texture copy is not supported");
    return false;
  }
  const TextureDimension sourceDimension =
      dimensionOfTarget(sourceTexture->target);
  const TextureDimension destinationDimension =
      dimensionOfTarget(destinationTexture->target);
  const std::uint32_t sourceMipWidth =
      mipDimension(sourceTexture->width, sourceRegion.mipLevel);
  const std::uint32_t sourceMipHeight =
      mipDimension(sourceTexture->height, sourceRegion.mipLevel);
  const std::uint32_t destinationMipWidth =
      mipDimension(destinationTexture->width, destinationOrigin.mipLevel);
  const std::uint32_t destinationMipHeight =
      mipDimension(destinationTexture->height, destinationOrigin.mipLevel);
  std::uint32_t sourceMipDepth = 1;
  if (sourceDimension == TextureDimension::TextureCube)
    sourceMipDepth = 6;
  else if (sourceDimension == TextureDimension::Texture2DArray)
    sourceMipDepth = sourceTexture->depthOrLayers;
  else if (sourceDimension == TextureDimension::Texture3D)
    sourceMipDepth =
        mipDimension(sourceTexture->depthOrLayers, sourceRegion.mipLevel);
  std::uint32_t destinationMipDepth = 1;
  if (destinationDimension == TextureDimension::TextureCube)
    destinationMipDepth = 6;
  else if (destinationDimension == TextureDimension::Texture2DArray)
    destinationMipDepth = destinationTexture->depthOrLayers;
  else if (destinationDimension == TextureDimension::Texture3D)
    destinationMipDepth = mipDimension(destinationTexture->depthOrLayers,
                                       destinationOrigin.mipLevel);
  if (sourceRegion.x > sourceMipWidth ||
      sourceRegion.width > sourceMipWidth - sourceRegion.x ||
      sourceRegion.y > sourceMipHeight ||
      sourceRegion.height > sourceMipHeight - sourceRegion.y ||
      sourceRegion.z > sourceMipDepth ||
      sourceRegion.depthOrLayers > sourceMipDepth - sourceRegion.z ||
      destinationOrigin.x > destinationMipWidth ||
      sourceRegion.width > destinationMipWidth - destinationOrigin.x ||
      destinationOrigin.y > destinationMipHeight ||
      sourceRegion.height > destinationMipHeight - destinationOrigin.y ||
      destinationOrigin.z > destinationMipDepth ||
      sourceRegion.depthOrLayers > destinationMipDepth - destinationOrigin.z ||
      (sourceDimension == TextureDimension::Texture2D &&
       (sourceRegion.z != 0 || sourceRegion.depthOrLayers != 1)) ||
      (destinationDimension == TextureDimension::Texture2D &&
       destinationOrigin.z != 0)) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::Copy, destination.value(),
         sourceRegion.width, sourceRegion.height,
         "texture copy region is out of bounds");
    return false;
  }
  drainDriverErrors();
  ensureFramebuffer(mCopyReadFramebuffer);
  ensureFramebuffer(mCopyDrawFramebuffer);
  GLenum mask = 0;
  const GLenum attachment = textureAttachmentPoint(sourceTexture->format, mask);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, mCopyReadFramebuffer);
  detachAllAttachments(GL_READ_FRAMEBUFFER);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mCopyDrawFramebuffer);
  detachAllAttachments(GL_DRAW_FRAMEBUFFER);
  const GLenum copyBuffer =
      attachment == GL_COLOR_ATTACHMENT0 ? GL_COLOR_ATTACHMENT0 : GL_NONE;
  glReadBuffer(copyBuffer);
  glDrawBuffers(1, &copyBuffer);
  for (std::uint32_t layer = 0; layer < sourceRegion.depthOrLayers; ++layer) {
    attachTexture(GL_READ_FRAMEBUFFER, attachment, sourceTexture->id,
                 sourceTexture->target, sourceRegion.mipLevel,
                 sourceRegion.z + layer);
    attachTexture(GL_DRAW_FRAMEBUFFER, attachment, destinationTexture->id,
                 destinationTexture->target, destinationOrigin.mipLevel,
                 destinationOrigin.z + layer);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE ||
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      push(GPUErrorCode::BackendFailure, GPUOperation::Copy,
           destination.value(), layer, 0,
           "texture copy framebuffer is incomplete");
      return false;
    }
    glBlitFramebuffer(
        static_cast<GLint>(sourceRegion.x), static_cast<GLint>(sourceRegion.y),
        static_cast<GLint>(sourceRegion.x + sourceRegion.width),
        static_cast<GLint>(sourceRegion.y + sourceRegion.height),
        static_cast<GLint>(destinationOrigin.x),
        static_cast<GLint>(destinationOrigin.y),
        static_cast<GLint>(destinationOrigin.x + sourceRegion.width),
        static_cast<GLint>(destinationOrigin.y + sourceRegion.height), mask,
        GL_NEAREST);
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  const GLenum copyError = glGetError();
  if (copyError != GL_NO_ERROR) {
    push(GPUErrorCode::BackendFailure, GPUOperation::Copy, destination.value(),
         copyError, 0, "OpenGL rejected texture copy");
    return false;
  }
  return true;
}

bool GPU_GL_DEVICE_CLASS::copyBuffer(BufferHandle destination,
                                  std::uint64_t destinationOffset,
                                  BufferHandle source,
                                  std::uint64_t sourceOffset,
                                  std::uint64_t size) {
  BufferObject *destinationBuffer =
      find(destination, mBuffers, GPUOperation::Copy);
  BufferObject *sourceBuffer = find(source, mBuffers, GPUOperation::Copy);
  if (mInRenderPass || !destinationBuffer || !sourceBuffer || size == 0 ||
      destinationOffset > destinationBuffer->size ||
      size > destinationBuffer->size - destinationOffset ||
      sourceOffset > sourceBuffer->size ||
      size > sourceBuffer->size - sourceOffset ||
      destinationOffset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      sourceOffset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      size > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::Copy, destination.value(),
         destinationOffset, size, "buffer copy is out of bounds");
    return false;
  }
  glBindBuffer(GL_COPY_READ_BUFFER, sourceBuffer->id);
  glBindBuffer(GL_COPY_WRITE_BUFFER, destinationBuffer->id);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      static_cast<GLintptr>(sourceOffset),
                      static_cast<GLintptr>(destinationOffset),
                      static_cast<GLsizeiptr>(size));
  return true;
}

bool GPU_GL_DEVICE_CLASS::isQueryResultAvailable(QueryHandle handle) {
  QueryObject *query = find(handle, mQueries, GPUOperation::CreateQuery);
  if (!query || !query->written || query->active) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::CreateQuery,
         handle.value(), 0, 0, "query has no pending result");
    return false;
  }
  GLuint available = GL_FALSE;
  glGetQueryObjectuiv(query->id, GL_QUERY_RESULT_AVAILABLE, &available);
  return available != GL_FALSE;
}

bool GPU_GL_DEVICE_CLASS::isFenceSignaled(FenceHandle handle) {
  FenceObject *fence = find(handle, mFences, GPUOperation::CreateFence);
  if (!fence)
    return false;
  const GLenum result =
      glClientWaitSync(fence->sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
  return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

SurfaceState GPU_GL_DEVICE_CLASS::surfaceState() const { return mSurfaceState; }

bool GPU_GL_DEVICE_CLASS::resizeSurface(std::uint32_t width,
                                     std::uint32_t height) {
  if (!mAlive || width == 0 || height == 0) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::Present, 0, width, height,
         "invalid surface size");
    return false;
  }
  mSurfaceWidth = width;
  mSurfaceHeight = height;
  mSurfaceState = SurfaceState::Ready;
  return true;
}

void GPU_GL_DEVICE_CLASS::suspendSurface() {
  if (!mAlive) {
    push(GPUErrorCode::DeviceLost, GPUOperation::Present, 0, 0, 0,
         "device is shut down");
    return;
  }
  mSurfaceState = SurfaceState::Suspended;
}

bool GPU_GL_DEVICE_CLASS::resumeSurface() {
  if (!mAlive || !mSurface->makeCurrent(mSurface->userData)) {
    push(mAlive ? GPUErrorCode::SurfaceLost : GPUErrorCode::DeviceLost,
         GPUOperation::Present, 0, static_cast<std::uint64_t>(mSurfaceState), 0,
         mAlive ? "surface could not be made current"
                : "device is shut down");
    mSurfaceState = SurfaceState::Lost;
    return false;
  }
  mSurfaceState = SurfaceState::Ready;
  return true;
}

bool GPU_GL_DEVICE_CLASS::present() {
  if (!mAlive || mInRenderPass || mSurfaceState != SurfaceState::Ready) {
    push(mAlive ? ::gpu::GPUErrorCode::InvalidArgument
                : ::gpu::GPUErrorCode::DeviceLost,
         ::gpu::GPUOperation::Present, 0, 0, 0,
         mAlive ? "presentation requires an idle device"
                : "device is shut down");
    return false;
  }
  mSurface->present(mSurface->userData);
  return true;
}

FenceHandle GPU_GL_DEVICE_CLASS::insertFence() {
  if (!mAlive || !mCapabilities.asyncReadback) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::CreateFence, 0, 0, 0,
         mAlive ? "fences are unavailable on this device"
                : "device is shut down");
    return FenceHandle();
  }
  FenceObject object;
  object.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (!object.sync) {
    push(GPUErrorCode::OutOfMemory, GPUOperation::CreateFence, 0, 0, 0,
#if GPU_GL_DESKTOP
         "OpenGL fence creation failed");
#else
         "OpenGL ES fence creation failed");
#endif
    return FenceHandle();
  }
  const FenceHandle handle =
      insert<FenceHandle>(mFences, object, GPUOperation::CreateFence);
  if (!handle.valid())
    glDeleteSync(object.sync);
  return handle;
}

GLuint GPU_GL_DEVICE_CLASS::acquireFramebuffer(const FramebufferKey &key) {
  const auto sameAttachment = [](const FramebufferAttachment &left,
                                 const FramebufferAttachment &right) {
    return left.texture == right.texture && left.target == right.target &&
           left.mipLevel == right.mipLevel && left.layer == right.layer &&
           left.stencil == right.stencil;
  };
  ++mFramebufferCacheClock;
  for (std::uint32_t index = 0; index < mFramebufferCacheCount; ++index) {
    FramebufferCacheEntry &entry = mFramebufferCache[index];
    if (entry.key.colorCount != key.colorCount ||
        entry.key.hasDepthStencil != key.hasDepthStencil)
      continue;
    bool matches = true;
    for (std::uint32_t color = 0; color < key.colorCount && matches; ++color)
      matches = sameAttachment(entry.key.colors[color], key.colors[color]);
    if (matches && key.hasDepthStencil)
      matches = sameAttachment(entry.key.depthStencil, key.depthStencil);
    if (!matches)
      continue;
    entry.lastUse = mFramebufferCacheClock;
    return entry.framebuffer;
  }

  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  if (framebuffer == 0) {
    push(::gpu::GPUErrorCode::OutOfMemory,
         ::gpu::GPUOperation::BeginRenderPass, 0, 0, 0,
         "OpenGL framebuffer creation failed");
    return 0;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  GLenum drawBuffers[RenderPassDesc::MaxColorAttachments] = {};
  for (std::uint32_t index = 0; index < key.colorCount; ++index) {
    drawBuffers[index] = GL_COLOR_ATTACHMENT0 + index;
    attachTexture(drawBuffers[index], key.colors[index].texture,
                  key.colors[index].target, key.colors[index].mipLevel,
                  key.colors[index].layer);
  }
  if (key.colorCount != 0) {
    glDrawBuffers(static_cast<GLsizei>(key.colorCount), drawBuffers);
  } else {
#if GPU_GL_DESKTOP
    glDrawBuffer(GL_NONE);
#else
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
#endif
    glReadBuffer(GL_NONE);
  }
  if (key.hasDepthStencil)
    attachTexture(key.depthStencil.stencil ? GL_DEPTH_STENCIL_ATTACHMENT
                                           : GL_DEPTH_ATTACHMENT,
                  key.depthStencil.texture, key.depthStencil.target,
                  key.depthStencil.mipLevel, key.depthStencil.layer);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    push(::gpu::GPUErrorCode::BackendFailure,
         ::gpu::GPUOperation::BeginRenderPass, 0, 0, 0,
         "render pass framebuffer is incomplete");
    return 0;
  }

  std::uint32_t slot = mFramebufferCacheCount;
  if (slot == FramebufferCacheSize) {
    slot = 0;
    for (std::uint32_t index = 1; index < FramebufferCacheSize; ++index)
      if (mFramebufferCache[index].lastUse < mFramebufferCache[slot].lastUse)
        slot = index;
    glDeleteFramebuffers(1, &mFramebufferCache[slot].framebuffer);
  } else {
    ++mFramebufferCacheCount;
  }
  mFramebufferCache[slot] = FramebufferCacheEntry{};
  mFramebufferCache[slot].key = key;
  mFramebufferCache[slot].framebuffer = framebuffer;
  mFramebufferCache[slot].lastUse = mFramebufferCacheClock;
  return framebuffer;
}

SamplerHandle GPU_GL_DEVICE_CLASS::createSampler(const SamplerDesc &desc) {
  if (!mAlive) {
    push(::gpu::GPUErrorCode::DeviceLost, ::gpu::GPUOperation::CreateSampler, 0,
         0, 0, "device is shut down");
    return SamplerHandle();
  }
  const bool wantsBorder = desc.addressU == AddressMode::ClampToBorder ||
                           desc.addressV == AddressMode::ClampToBorder ||
                           desc.addressW == AddressMode::ClampToBorder;
  if (wantsBorder && !mCapabilities.samplerBorderColor) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreateSampler, 0, 0, 0,
         "border address mode is unavailable on this device");
    return SamplerHandle();
  }
  if (desc.maxAnisotropy < 1.0f || desc.lodMin < 0.0f ||
      desc.lodMax < desc.lodMin) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::CreateSampler, 0, 0, 0,
         "invalid sampler descriptor");
    return SamplerHandle();
  }
  if (desc.maxAnisotropy > 1.0f && !mAnisotropicFiltering) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreateSampler, 0, 0, 0,
         "anisotropic filtering is unavailable on this device");
    return SamplerHandle();
  }
  SamplerObject object;
  drainDriverErrors();
  glGenSamplers(1, &object.id);
  if (!object.id) {
    push(::gpu::GPUErrorCode::OutOfMemory, ::gpu::GPUOperation::CreateSampler,
         0, 0, 0, "OpenGL sampler creation failed");
    return SamplerHandle();
  }
  glSamplerParameteri(
      object.id, GL_TEXTURE_MIN_FILTER,
      static_cast<GLint>(samplerMinFilter(desc.minFilter, desc.mipFilter)));
  glSamplerParameteri(object.id, GL_TEXTURE_MAG_FILTER,
                     static_cast<GLint>(samplerFilter(desc.magFilter)));
  glSamplerParameteri(object.id, GL_TEXTURE_WRAP_S,
                     static_cast<GLint>(addressMode(desc.addressU)));
  glSamplerParameteri(object.id, GL_TEXTURE_WRAP_T,
                     static_cast<GLint>(addressMode(desc.addressV)));
  glSamplerParameteri(object.id, GL_TEXTURE_WRAP_R,
                     static_cast<GLint>(addressMode(desc.addressW)));
  glSamplerParameterf(object.id, GL_TEXTURE_MIN_LOD, desc.lodMin);
  glSamplerParameterf(object.id, GL_TEXTURE_MAX_LOD, desc.lodMax);
  if (desc.compareEnabled) {
    glSamplerParameteri(object.id, GL_TEXTURE_COMPARE_MODE,
                       GL_COMPARE_REF_TO_TEXTURE);
    glSamplerParameteri(object.id, GL_TEXTURE_COMPARE_FUNC,
                       static_cast<GLint>(compareValue(desc.compare)));
  } else {
    glSamplerParameteri(object.id, GL_TEXTURE_COMPARE_MODE, GL_NONE);
  }
  if (mAnisotropicFiltering) {
    const float anisotropy =
        std::min(desc.maxAnisotropy, mCapabilities.maxAnisotropy);
#if GPU_GL_DESKTOP
    glSamplerParameterf(object.id, GL_TEXTURE_MAX_ANISOTROPY, anisotropy);
#else
    glSamplerParameterf(object.id, kTextureMaxAnisotropyExt, anisotropy);
#endif
  }
  const GLenum samplerError = glGetError();
  if (samplerError != GL_NO_ERROR) {
    glDeleteSamplers(1, &object.id);
    push(::gpu::GPUErrorCode::BackendFailure,
         ::gpu::GPUOperation::CreateSampler, 0, samplerError, 0,
         "the driver rejected the sampler descriptor");
    return SamplerHandle();
  }
  setObjectLabel(GL_SAMPLER, object.id, desc.debugName);
  const SamplerHandle handle = insert<SamplerHandle>(
      mSamplers, object, ::gpu::GPUOperation::CreateSampler);
  if (!handle.valid())
    glDeleteSamplers(1, &object.id);
  return handle;
}

bool GPU_GL_DEVICE_CLASS::beginQuery(QueryHandle handle) {
  QueryObject *query = find(handle, mQueries, GPUOperation::CreateQuery);
  if (!mInRenderPass || !query || query->type != QueryType::Occlusion ||
      query->active || mActiveOcclusionQuery.valid()) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::CreateQuery,
         handle.value(), 0, 0, "invalid occlusion query begin state");
    return false;
  }
#if GPU_GL_DESKTOP
  glBeginQuery(GL_SAMPLES_PASSED, query->id);
#else
  glBeginQuery(GL_ANY_SAMPLES_PASSED, query->id);
#endif
  query->active = true;
  query->written = false;
  mActiveOcclusionQuery = handle;
  return true;
}

void GPU_GL_DEVICE_CLASS::endQuery(QueryHandle handle) {
  QueryObject *query = find(handle, mQueries, GPUOperation::CreateQuery);
  if (!query || !query->active || mActiveOcclusionQuery != handle) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::CreateQuery,
         handle.value(), 0, 0, "invalid occlusion query end state");
    return;
  }
#if GPU_GL_DESKTOP
  glEndQuery(GL_SAMPLES_PASSED);
#else
  glEndQuery(GL_ANY_SAMPLES_PASSED);
#endif
  query->active = false;
  query->written = true;
  mActiveOcclusionQuery = QueryHandle();
}

void GPU_GL_DEVICE_CLASS::setObjectLabel(GLenum identifier, GLuint name,
                                  const char *label) {
  if (!mDebugLabels || !label || name == 0)
    return;
  std::size_t length = std::strlen(label);
  if (length > mMaxLabelLength)
    length = mMaxLabelLength;
#if GPU_GL_DESKTOP || GPU_GLES_HAS_KHR_DEBUG
  glObjectLabel(identifier, name, static_cast<GLsizei>(length), label);
#else
  (void)identifier;
  (void)name;
  (void)length;
#endif
}

bool GPU_GL_DEVICE_CLASS::beginRenderPass(const RenderPassDesc &desc) {
  if (!mAlive || mInRenderPass || mSurfaceState != SurfaceState::Ready ||
      desc.colorCount > RenderPassDesc::MaxColorAttachments ||
      desc.colorCount > mCapabilities.maxColorAttachments ||
      (desc.colorCount == 0 && !desc.hasDepthStencil)) {
    push(mAlive ? ::gpu::GPUErrorCode::InvalidArgument
                : ::gpu::GPUErrorCode::DeviceLost,
         ::gpu::GPUOperation::BeginRenderPass, 0, desc.colorCount, 0,
         mAlive ? "invalid render pass descriptor or state"
                : "device is shut down");
    return false;
  }
  bool usesSurface = false;
  for (std::uint32_t index = 0; index < desc.colorCount; ++index)
    usesSurface = usesSurface || desc.colors[index].surface;
  if (usesSurface &&
      (desc.colorCount != 1 || !desc.colors[0].surface ||
       desc.colors[0].target.texture.valid() ||
       desc.depthStencil.target.texture.valid())) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::BeginRenderPass, 0, desc.colorCount,
         desc.hasDepthStencil, "surface attachments cannot be mixed with textures");
    return false;
  }
  mDiscardAttachmentCount = 0;
  glDisable(GL_SCISSOR_TEST);
  glDepthMask(GL_TRUE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (usesSurface) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mSurface->drawableSize(mSurface->userData, mSurfaceWidth, mSurfaceHeight);
    mRenderWidth = mSurfaceWidth;
    mRenderHeight = mSurfaceHeight;
    const RenderPassColorAttachment &color = desc.colors[0];
    if (color.loadOp == LoadOp::DontCare
#if GPU_GL_DESKTOP
        && glad_glInvalidateFramebuffer
#endif
    ) {
      const GLenum surfaceAttachment = GL_COLOR;
      glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &surfaceAttachment);
    }
    if (color.loadOp == LoadOp::Clear) {
      glClearColor(color.clearColor[0], color.clearColor[1], color.clearColor[2],
                   color.clearColor[3]);
      glClear(GL_COLOR_BUFFER_BIT);
    }
    if (desc.hasDepthStencil &&
        desc.depthStencil.depthLoadOp == LoadOp::Clear) {
      glClearDepthf(desc.depthStencil.clearDepth);
      glClear(GL_DEPTH_BUFFER_BIT |
              (desc.depthStencil.stencilLoadOp == LoadOp::Clear
                   ? GL_STENCIL_BUFFER_BIT
                   : 0));
    }
    if (color.storeOp == StoreOp::Discard)
      mDiscardAttachments[mDiscardAttachmentCount++] = GL_COLOR;
    if (desc.hasDepthStencil &&
        desc.depthStencil.depthStoreOp == StoreOp::Discard)
      mDiscardAttachments[mDiscardAttachmentCount++] = GL_DEPTH;
  } else {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t sampleCount = 0;
    FramebufferKey key;
    GLenum invalidateAttachments[RenderPassDesc::MaxColorAttachments + 2] = {};
    std::uint32_t invalidateCount = 0;
    for (std::uint32_t index = 0; index < desc.colorCount; ++index) {
      const RenderPassColorAttachment &color = desc.colors[index];
      TextureObject *texture = find(color.target.texture, mTextures,
                                    ::gpu::GPUOperation::BeginRenderPass);
      if (!texture)
        return false;
      const TextureDimension dimension =
          texture->target == GL_TEXTURE_CUBE_MAP
              ? TextureDimension::TextureCube
              : texture->target == GL_TEXTURE_2D_ARRAY
                    ? TextureDimension::Texture2DArray
                    : texture->target == GL_TEXTURE_3D
                          ? TextureDimension::Texture3D
                          : TextureDimension::Texture2D;
      const std::uint32_t attachmentWidth =
          mipDimension(texture->width, color.target.mipLevel);
      const std::uint32_t attachmentHeight =
          mipDimension(texture->height, color.target.mipLevel);
      if (!(texture->usage & TextureUsageRenderTarget) ||
          depthFormat(texture->format) ||
          color.target.mipLevel >= texture->mipCount ||
          !validAttachmentLayer(dimension, texture->depthOrLayers,
                                color.target.mipLevel, color.target.layer) ||
          (index != 0 &&
           (width != attachmentWidth || height != attachmentHeight ||
            sampleCount != texture->sampleCount))) {
        push(::gpu::GPUErrorCode::InvalidArgument,
             ::gpu::GPUOperation::BeginRenderPass,
             color.target.texture.value(), color.target.mipLevel,
             color.target.layer, "invalid render pass color attachment");
        return false;
      }
      if (index == 0) {
        width = attachmentWidth;
        height = attachmentHeight;
        sampleCount = texture->sampleCount;
      }
      key.colors[index].texture = texture->id;
      key.colors[index].target = texture->target;
      key.colors[index].mipLevel = color.target.mipLevel;
      key.colors[index].layer = color.target.layer;
      const GLenum attachmentPoint = GL_COLOR_ATTACHMENT0 + index;
      if (color.storeOp == StoreOp::Discard)
        mDiscardAttachments[mDiscardAttachmentCount++] = attachmentPoint;
      if (color.loadOp == LoadOp::DontCare)
        invalidateAttachments[invalidateCount++] = attachmentPoint;
    }
    key.colorCount = desc.colorCount;
    if (desc.hasDepthStencil) {
      const TargetAttachment &attachment = desc.depthStencil.target;
      TextureObject *texture = find(attachment.texture, mTextures,
                                    ::gpu::GPUOperation::BeginRenderPass);
      if (!texture)
        return false;
      const TextureDimension dimension =
          texture->target == GL_TEXTURE_CUBE_MAP
              ? TextureDimension::TextureCube
              : texture->target == GL_TEXTURE_2D_ARRAY
                    ? TextureDimension::Texture2DArray
                    : texture->target == GL_TEXTURE_3D
                          ? TextureDimension::Texture3D
                          : TextureDimension::Texture2D;
      const std::uint32_t attachmentWidth =
          mipDimension(texture->width, attachment.mipLevel);
      const std::uint32_t attachmentHeight =
          mipDimension(texture->height, attachment.mipLevel);
      if (!(texture->usage & TextureUsageRenderTarget) ||
          !depthFormat(texture->format) ||
          attachment.mipLevel >= texture->mipCount ||
          !validAttachmentLayer(dimension, texture->depthOrLayers,
                                attachment.mipLevel, attachment.layer) ||
          (desc.colorCount != 0 &&
           (width != attachmentWidth || height != attachmentHeight ||
            sampleCount != texture->sampleCount))) {
        push(::gpu::GPUErrorCode::InvalidArgument,
             ::gpu::GPUOperation::BeginRenderPass, attachment.texture.value(),
             attachment.mipLevel, attachment.layer,
             "invalid render pass depth stencil attachment");
        return false;
      }
      if (desc.colorCount == 0) {
        width = attachmentWidth;
        height = attachmentHeight;
      }
      const bool hasStencil = stencilFormat(texture->format);
      key.hasDepthStencil = true;
      key.depthStencil.texture = texture->id;
      key.depthStencil.target = texture->target;
      key.depthStencil.mipLevel = attachment.mipLevel;
      key.depthStencil.layer = attachment.layer;
      key.depthStencil.stencil = hasStencil;
      if (desc.depthStencil.depthStoreOp == StoreOp::Discard)
        mDiscardAttachments[mDiscardAttachmentCount++] = GL_DEPTH_ATTACHMENT;
      if (hasStencil &&
          desc.depthStencil.stencilStoreOp == StoreOp::Discard)
        mDiscardAttachments[mDiscardAttachmentCount++] = GL_STENCIL_ATTACHMENT;
      if (desc.depthStencil.depthLoadOp == LoadOp::DontCare)
        invalidateAttachments[invalidateCount++] = GL_DEPTH_ATTACHMENT;
      if (hasStencil && desc.depthStencil.stencilLoadOp == LoadOp::DontCare)
        invalidateAttachments[invalidateCount++] = GL_STENCIL_ATTACHMENT;
    }
    const GLuint framebuffer = acquireFramebuffer(key);
    if (framebuffer == 0)
      return false;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (invalidateCount != 0
#if GPU_GL_DESKTOP
        && glad_glInvalidateFramebuffer
#endif
    )
      glInvalidateFramebuffer(GL_FRAMEBUFFER,
                              static_cast<GLsizei>(invalidateCount),
                              invalidateAttachments);
    for (std::uint32_t index = 0; index < desc.colorCount; ++index) {
      const RenderPassColorAttachment &color = desc.colors[index];
      if (color.loadOp != LoadOp::Clear)
        continue;
      const TextureObject *texture = mTextures.find(color.target.texture);
      if (unsignedIntegerFormat(texture->format)) {
        const GLuint clear[4] = {
            static_cast<GLuint>(std::max(color.clearColor[0], 0.0f)),
            static_cast<GLuint>(std::max(color.clearColor[1], 0.0f)),
            static_cast<GLuint>(std::max(color.clearColor[2], 0.0f)),
            static_cast<GLuint>(std::max(color.clearColor[3], 0.0f))};
        glClearBufferuiv(GL_COLOR, static_cast<GLint>(index), clear);
      } else {
        glClearBufferfv(GL_COLOR, static_cast<GLint>(index), color.clearColor);
      }
    }
    if (desc.hasDepthStencil) {
      const TextureObject *texture =
          mTextures.find(desc.depthStencil.target.texture);
      const bool hasStencil = stencilFormat(texture->format);
      if (hasStencil &&
          desc.depthStencil.depthLoadOp == LoadOp::Clear &&
          desc.depthStencil.stencilLoadOp == LoadOp::Clear) {
        glClearBufferfi(GL_DEPTH_STENCIL, 0, desc.depthStencil.clearDepth,
                        static_cast<GLint>(desc.depthStencil.clearStencil));
      } else {
        if (desc.depthStencil.depthLoadOp == LoadOp::Clear)
          glClearBufferfv(GL_DEPTH, 0, &desc.depthStencil.clearDepth);
        if (hasStencil &&
            desc.depthStencil.stencilLoadOp == LoadOp::Clear) {
          const GLint clearStencil =
              static_cast<GLint>(desc.depthStencil.clearStencil);
          glClearBufferiv(GL_STENCIL, 0, &clearStencil);
        }
      }
    }
    mRenderWidth = width;
    mRenderHeight = height;
  }
  glViewport(0, 0, static_cast<GLsizei>(mRenderWidth),
             static_cast<GLsizei>(mRenderHeight));
  mPassAttachmentCount = 0;
  for (std::uint32_t index = 0; index < desc.colorCount; ++index)
    if (desc.colors[index].target.texture.valid())
      mPassAttachments[mPassAttachmentCount++] = desc.colors[index].target.texture;
  if (desc.hasDepthStencil && desc.depthStencil.target.texture.valid())
    mPassAttachments[mPassAttachmentCount++] = desc.depthStencil.target.texture;
  mInRenderPass = true;
  mRenderSurface = usesSurface;
  mPipeline = PipelineHandle();
  mIndexBuffer = BufferHandle();
  return true;
}

void GPU_GL_DEVICE_CLASS::endRenderPass() {
  if (!mInRenderPass) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::BeginRenderPass, 0, 0, 0,
         "no render pass is active");
    return;
  }
  if (mDiscardAttachmentCount != 0
#if GPU_GL_DESKTOP
      && glad_glInvalidateFramebuffer
#endif
     )
    glInvalidateFramebuffer(GL_FRAMEBUFFER,
                            static_cast<GLsizei>(mDiscardAttachmentCount),
                            mDiscardAttachments);
  mInRenderPass = false;
  mRenderSurface = false;
  mDiscardAttachmentCount = 0;
}

bool GPU_GL_DEVICE_CLASS::setViewport(const Viewport &viewport) {
  if (!mInRenderPass || viewport.width <= 0.0f || viewport.height <= 0.0f ||
      viewport.minDepth < 0.0f || viewport.maxDepth > 1.0f ||
      viewport.minDepth > viewport.maxDepth) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource, 0, 0, 0,
         "invalid viewport");
    return false;
  }
  glViewport(static_cast<GLint>(viewport.x), static_cast<GLint>(viewport.y),
             static_cast<GLsizei>(viewport.width),
             static_cast<GLsizei>(viewport.height));
#if GPU_GL_DESKTOP
  glDepthRange(viewport.minDepth, viewport.maxDepth);
#else
  glDepthRangef(viewport.minDepth, viewport.maxDepth);
#endif
  return true;
}

bool GPU_GL_DEVICE_CLASS::getQueryResult(QueryHandle handle,
                                      std::uint64_t &result) {
  QueryObject *query = find(handle, mQueries, GPUOperation::CreateQuery);
  if (!query || !query->written || query->active) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::CreateQuery,
         handle.value(), 0, 0, "query has no pending result");
    return false;
  }
  GLuint available = GL_FALSE;
  glGetQueryObjectuiv(query->id, GL_QUERY_RESULT_AVAILABLE, &available);
  if (available == GL_FALSE)
    return false;
#if GPU_GL_DESKTOP
  GLuint64 value = 0;
  glGetQueryObjectui64v(query->id, GL_QUERY_RESULT, &value);
#else
  GLuint value = 0;
  glGetQueryObjectuiv(query->id, GL_QUERY_RESULT, &value);
#endif
  result = static_cast<std::uint64_t>(value);
  query->written = false;
  return true;
}

bool GPU_GL_DEVICE_CLASS::bindStorageTexture(std::uint32_t slot,
                                          TextureHandle texture,
                                          std::uint32_t mipLevel) {
  TextureObject *textureObject =
      find(texture, mTextures, GPUOperation::BindResource);
  if (!textureObject || !(textureObject->usage & TextureUsageStorage) ||
      mipLevel >= textureObject->mipCount ||
      slot >= mMaxStorageTextureBindings) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::BindResource,
         texture.value(), slot, mipLevel, "invalid storage texture binding");
    return false;
  }
  GLint internalFormat = 0;
  GLenum layout = 0;
  GLenum type = 0;
  CompressedFormatInfo compressedInfo;
  if (compressedTextureFormat(textureObject->format, compressedInfo) ||
      !textureFormat(textureObject->format, internalFormat, layout, type)) {
    push(GPUErrorCode::UnsupportedFormat, GPUOperation::BindResource,
         texture.value(), static_cast<std::uint64_t>(textureObject->format), 0,
         "texture format cannot be bound as a storage image");
    return false;
  }
  const bool layered = textureObject->target == GL_TEXTURE_2D_ARRAY ||
                       textureObject->target == GL_TEXTURE_3D ||
                       textureObject->target == GL_TEXTURE_CUBE_MAP;
#if GPU_GL_DESKTOP || GPU_GLES_HAS_ES31
  glBindImageTexture(slot, textureObject->id, static_cast<GLint>(mipLevel),
                     layered ? GL_TRUE : GL_FALSE, 0, GL_READ_WRITE,
                     static_cast<GLenum>(internalFormat));
#endif
  return true;
}

bool GPU_GL_DEVICE_CLASS::memoryBarrier(std::uint32_t barriers) {
  if (!mCapabilities.memoryBarriers || barriers == 0) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::Dispatch, 0, barriers,
         0, "memory barriers are unavailable on this device");
    return false;
  }
  GLbitfield mask = 0;
  if (barriers & BarrierVertex)
    mask |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
  if (barriers & BarrierIndex)
    mask |= GL_ELEMENT_ARRAY_BARRIER_BIT;
  if (barriers & BarrierUniform)
    mask |= GL_UNIFORM_BARRIER_BIT;
  if (barriers & BarrierStorage)
    mask |= GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT;
  if (barriers & BarrierTexture)
    mask |= GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT |
            GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT;
  if (barriers & BarrierIndirect)
    mask |= GL_COMMAND_BARRIER_BIT;
  if (barriers == BarrierAll)
    mask = GL_ALL_BARRIER_BITS;
#if GPU_GL_DESKTOP || GPU_GLES_HAS_ES31
  glMemoryBarrier(mask);
#endif
  return true;
}

bool GPU_GL_DEVICE_CLASS::dispatch(std::uint32_t groupCountX,
                                std::uint32_t groupCountY,
                                std::uint32_t groupCountZ) {
  if (mInRenderPass || !mPipeline.valid() || groupCountX == 0 ||
      groupCountY == 0 || groupCountZ == 0) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::Dispatch,
         mPipeline.value(), groupCountX, groupCountY, "invalid dispatch state");
    return false;
  }
  PipelineObject *pipeline = find(mPipeline, mPipelines, GPUOperation::Dispatch);
  if (!pipeline)
    return false;
  if (!pipeline->isCompute) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::Dispatch,
         mPipeline.value(), 0, 0, "bound pipeline is not a compute pipeline");
    return false;
  }
#if GPU_GL_DESKTOP || GPU_GLES_HAS_ES31
  glDispatchCompute(groupCountX, groupCountY, groupCountZ);
#endif
  return true;
}

bool GPU_GL_DEVICE_CLASS::drawIndirect(BufferHandle buffer, std::uint64_t offset) {
  BufferObject *indirectBuffer =
      find(buffer, mBuffers, GPUOperation::DrawIndirect);
  if (!mInRenderPass || !mPipeline.valid() || !indirectBuffer ||
      !(indirectBuffer->usage & BufferUsageIndirect) || (offset % 4) != 0 ||
      offset > indirectBuffer->size ||
      indirectBuffer->size - offset < sizeof(DrawIndirectArgs) ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         buffer.value(), offset, 0, "invalid indirect draw state");
    return false;
  }
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::DrawIndirect);
  if (!pipeline)
    return false;
  if (pipeline->isCompute) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         mPipeline.value(), 0, 0, "bound pipeline is not a graphics pipeline");
    return false;
  }
#if GPU_GL_DESKTOP || GPU_GLES_HAS_ES31
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer->id);
  glDrawArraysIndirect(
      pipeline->topology,
      reinterpret_cast<const void *>(static_cast<std::uintptr_t>(offset)));
#endif
  return true;
}

bool GPU_GL_DEVICE_CLASS::drawIndexedIndirect(BufferHandle buffer,
                                           std::uint64_t offset) {
  BufferObject *indirectBuffer =
      find(buffer, mBuffers, GPUOperation::DrawIndirect);
  if (!mInRenderPass || !mPipeline.valid() || !mBuffers.find(mIndexBuffer) ||
      !indirectBuffer || !(indirectBuffer->usage & BufferUsageIndirect) ||
      (offset % 4) != 0 || offset > indirectBuffer->size ||
      indirectBuffer->size - offset < sizeof(DrawIndexedIndirectArgs) ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         buffer.value(), offset, 0, "invalid indexed indirect draw state");
    return false;
  }
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::DrawIndirect);
  if (!pipeline)
    return false;
  if (pipeline->isCompute) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         mPipeline.value(), 0, 0, "bound pipeline is not a graphics pipeline");
    return false;
  }
#if GPU_GL_DESKTOP || GPU_GLES_HAS_ES31
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer->id);
  glDrawElementsIndirect(
      pipeline->topology, mIndexType,
      reinterpret_cast<const void *>(static_cast<std::uintptr_t>(offset)));
#endif
  return true;
}

#if GPU_GL_DESKTOP
void GLAD_API_PTR GPU_GL_DEVICE_CLASS::debugMessage(
#else
void GL_APIENTRY GPU_GL_DEVICE_CLASS::debugMessage(
#endif
    GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
    const GLchar *message, const void *userData) {
  (void)source;
  (void)type;
  GPU_GL_DEVICE_CLASS *device =
      const_cast<GPU_GL_DEVICE_CLASS *>(static_cast<const GPU_GL_DEVICE_CLASS *>(userData));
  if (!device)
    return;
  char *text = device->nextDiagnostic();
  const std::size_t limit = ShaderDiagnosticSize - 1;
  std::size_t copied = length < 0 ? std::strlen(message)
                                  : static_cast<std::size_t>(length);
  if (copied > limit)
    copied = limit;
  std::memcpy(text, message, copied);
  text[copied] = '\0';
  ::gpu::GPUError error;
  error.code = ::gpu::GPUErrorCode::BackendFailure;
  error.operation = ::gpu::GPUOperation::None;
  error.resource = 0;
  error.value0 = id;
  error.value1 = severity;
  error.message = text;
  if (severity == GL_DEBUG_SEVERITY_HIGH)
    error.severity = ::gpu::GPUErrorSeverity::Error;
  else if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    error.severity = ::gpu::GPUErrorSeverity::Info;
  else
    error.severity = ::gpu::GPUErrorSeverity::Warning;
  device->mErrors.push(error);
}

bool GPU_GL_DEVICE_CLASS::reflectPipeline(PipelineHandle handle,
                                  PipelineReflection &reflection) {
  reflection = PipelineReflection();
  PipelineObject *pipeline =
      find(handle, mPipelines, GPUOperation::ReflectPipeline);
  if (!pipeline || pipeline->program == 0) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::ReflectPipeline,
         handle.value(), 0, 0, "pipeline is not valid for reflection");
    return false;
  }
  const auto append = [&reflection](const char *name, ShaderResourceType type,
                                    std::uint32_t slot,
                                    std::uint32_t elementCount,
                                    std::uint32_t blockSize) {
    if (reflection.resourceCount >= PipelineReflection::MaxResources) {
      reflection.truncated = true;
      return;
    }
    ShaderResource &resource = reflection.resources[reflection.resourceCount++];
    std::size_t length = 0;
    while (name[length] != '\0' &&
           length + 1 < ShaderResource::MaxNameLength) {
      resource.name[length] = name[length];
      ++length;
    }
    resource.name[length] = '\0';
    resource.type = type;
    resource.slot = slot;
    resource.elementCount = elementCount;
    resource.blockSize = blockSize;
  };

  // Samplers e storage textures: o valor do uniform e a unidade atribuida,
  // seja ela explicita no shader ou dada por assignSamplerUnits.
  GLint uniformCount = 0;
  glGetProgramiv(pipeline->program, GL_ACTIVE_UNIFORMS, &uniformCount);
  for (GLint index = 0; index < uniformCount; ++index) {
    GLint elements = 0;
    GLenum type = 0;
    GLchar name[256] = {};
    glGetActiveUniform(pipeline->program, static_cast<GLuint>(index),
                       sizeof(name), nullptr, &elements, &type, name);
    const bool isSampler = samplerUniformType(type);
    const bool isImage = imageUniformType(type);
    if ((!isSampler && !isImage) || elements <= 0)
      continue;
    // Um array chega como "nome[0]": o sufixo nao interessa a quem liga.
    for (std::size_t position = 0; name[position] != '\0'; ++position) {
      if (name[position] == '[') {
        name[position] = '\0';
        break;
      }
    }
    const GLint location = glGetUniformLocation(pipeline->program, name);
    if (location < 0)
      continue;
    GLint unit = 0;
    glGetUniformiv(pipeline->program, location, &unit);
    append(name, isSampler ? ShaderResourceType::Sampler
                           : ShaderResourceType::StorageTexture,
           static_cast<std::uint32_t>(unit < 0 ? 0 : unit),
           static_cast<std::uint32_t>(elements), 0);
  }

  GLint blockCount = 0;
  glGetProgramiv(pipeline->program, GL_ACTIVE_UNIFORM_BLOCKS, &blockCount);
  for (GLint index = 0; index < blockCount; ++index) {
    GLchar name[256] = {};
    glGetActiveUniformBlockName(pipeline->program, static_cast<GLuint>(index),
                                sizeof(name), nullptr, name);
    GLint binding = 0;
    GLint blockSize = 0;
    glGetActiveUniformBlockiv(pipeline->program, static_cast<GLuint>(index),
                              GL_UNIFORM_BLOCK_BINDING, &binding);
    glGetActiveUniformBlockiv(pipeline->program, static_cast<GLuint>(index),
                              GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
    append(name, ShaderResourceType::UniformBuffer,
           static_cast<std::uint32_t>(binding < 0 ? 0 : binding), 1,
           static_cast<std::uint32_t>(blockSize < 0 ? 0 : blockSize));
  }

#if GPU_GL_DESKTOP || GPU_GLES_HAS_ES31
  // Storage blocks so existem com GL 4.3 / ES 3.1 para cima; em WebGL2 nao ha.
  if (mCapabilities.storageBuffers) {
    GLint storageCount = 0;
    glGetProgramInterfaceiv(pipeline->program, GL_SHADER_STORAGE_BLOCK,
                            GL_ACTIVE_RESOURCES, &storageCount);
    for (GLint index = 0; index < storageCount; ++index) {
      GLchar name[256] = {};
      glGetProgramResourceName(pipeline->program, GL_SHADER_STORAGE_BLOCK,
                               static_cast<GLuint>(index), sizeof(name),
                               nullptr, name);
      const GLenum property = GL_BUFFER_BINDING;
      GLint binding = 0;
      glGetProgramResourceiv(pipeline->program, GL_SHADER_STORAGE_BLOCK,
                             static_cast<GLuint>(index), 1, &property, 1,
                             nullptr, &binding);
      append(name, ShaderResourceType::StorageBuffer,
             static_cast<std::uint32_t>(binding < 0 ? 0 : binding), 1, 0);
    }
  }
#endif

  return true;
}

bool GPU_GL_DEVICE_CLASS::readTexture(TextureHandle handle,
                                   const TextureRegion &region,
                                   MutableDataView data,
                                   const TextureDataLayout &layout) {
  TextureObject *texture = find(handle, mTextures, GPUOperation::ReadTexture);
  if (mInRenderPass || !texture || !(texture->usage & TextureUsageCopySource) ||
      !data.data || data.size == 0 || region.width == 0 ||
      region.height == 0 || region.depthOrLayers == 0 ||
      region.mipLevel >= texture->mipCount) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::ReadTexture,
         handle.value(), region.mipLevel, data.size,
         "invalid texture read state or descriptor");
    return false;
  }
  CompressedFormatInfo compressedInfo;
  if (compressedTextureFormat(texture->format, compressedInfo)) {
    push(GPUErrorCode::UnsupportedFormat, GPUOperation::ReadTexture,
         handle.value(), static_cast<std::uint64_t>(texture->format), 0,
         "compressed texture read is not supported");
    return false;
  }
  if (texture->sampleCount != 1) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::ReadTexture,
         handle.value(), texture->sampleCount, 0,
         "multisample texture read is not supported");
    return false;
  }
  GLenum probeMask = 0;
  textureAttachmentPoint(texture->format, probeMask);
  if (probeMask != GL_COLOR_BUFFER_BIT && !mCapabilities.depthReadback) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::ReadTexture,
         handle.value(), static_cast<std::uint64_t>(texture->format), 0,
         "depth or stencil texture read is not supported by this backend");
    return false;
  }
  const TextureDimension dimension = dimensionOfTarget(texture->target);
  const std::uint32_t mipWidth = mipDimension(texture->width, region.mipLevel);
  const std::uint32_t mipHeight =
      mipDimension(texture->height, region.mipLevel);
  std::uint32_t mipDepth = 1;
  if (dimension == TextureDimension::TextureCube)
    mipDepth = 6;
  else if (dimension == TextureDimension::Texture2DArray)
    mipDepth = texture->depthOrLayers;
  else if (dimension == TextureDimension::Texture3D)
    mipDepth = mipDimension(texture->depthOrLayers, region.mipLevel);
  if (region.x > mipWidth || region.width > mipWidth - region.x ||
      region.y > mipHeight || region.height > mipHeight - region.y ||
      region.z > mipDepth || region.depthOrLayers > mipDepth - region.z ||
      (dimension == TextureDimension::Texture2D &&
       (region.z != 0 || region.depthOrLayers != 1))) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::ReadTexture, handle.value(),
         region.width, region.height, "texture read region is out of bounds");
    return false;
  }
  const std::uint32_t texelBytes = bytesPerTexel(texture->format);
  const std::uint64_t tightRowBytes =
      static_cast<std::uint64_t>(region.width) * texelBytes;
  const std::uint64_t bytesPerRow =
      layout.bytesPerRow == 0 ? tightRowBytes : layout.bytesPerRow;
  const std::uint64_t rowsPerImage =
      layout.rowsPerImage == 0 ? region.height : layout.rowsPerImage;
  std::uint64_t required = 0;
  if (bytesPerRow < tightRowBytes || rowsPerImage < region.height ||
      texelBytes == 0 || bytesPerRow % texelBytes != 0 ||
      bytesPerRow > static_cast<std::uint64_t>((std::numeric_limits<GLint>::max)()) ||
      rowsPerImage > static_cast<std::uint64_t>((std::numeric_limits<GLint>::max)()) ||
      !requiredUploadSize(layout.offset, bytesPerRow, rowsPerImage,
                          region.height, tightRowBytes, region.depthOrLayers,
                          required) ||
      required > data.size ||
      required > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
    push(GPUErrorCode::OutOfBounds, GPUOperation::ReadTexture, handle.value(),
         required, data.size, "texture read layout is invalid or too small");
    return false;
  }
  GLint readFormat = 0;
  GLenum readLayout = 0;
  GLenum readType = 0;
  textureFormat(texture->format, readFormat, readLayout, readType);
  drainDriverErrors();
  ensureFramebuffer(mCopyReadFramebuffer);
  GLenum mask = 0;
  const GLenum attachment = textureAttachmentPoint(texture->format, mask);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, mCopyReadFramebuffer);
  detachAllAttachments(GL_READ_FRAMEBUFFER);
  glReadBuffer(attachment == GL_COLOR_ATTACHMENT0 ? GL_COLOR_ATTACHMENT0
                                                  : GL_NONE);
  std::uint8_t *bytes =
      static_cast<std::uint8_t *>(data.data) +
      static_cast<std::size_t>(layout.offset);
  const std::uint64_t imageStride = bytesPerRow * rowsPerImage;
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH,
               static_cast<GLint>(bytesPerRow / texelBytes));
  for (std::uint32_t layer = 0; layer < region.depthOrLayers; ++layer) {
    attachTexture(GL_READ_FRAMEBUFFER, attachment, texture->id,
                 texture->target, region.mipLevel, region.z + layer);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
      glPixelStorei(GL_PACK_ROW_LENGTH, 0);
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      push(GPUErrorCode::BackendFailure, GPUOperation::ReadTexture,
           handle.value(), layer, 0, "texture read framebuffer is incomplete");
      return false;
    }
    glReadPixels(static_cast<GLint>(region.x), static_cast<GLint>(region.y),
                static_cast<GLsizei>(region.width),
                static_cast<GLsizei>(region.height), readLayout, readType,
                bytes + layer * imageStride);
  }
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  const GLenum readError = glGetError();
  if (readError != GL_NO_ERROR) {
    push(GPUErrorCode::BackendFailure, GPUOperation::ReadTexture,
         handle.value(), readError, region.mipLevel,
         "OpenGL rejected texture read");
    return false;
  }
  return true;
}

void *GPU_GL_DEVICE_CLASS::mapBuffer(BufferHandle handle, std::uint64_t offset,
                                  std::uint64_t size, MapMode mode) {
  BufferObject *buffer = find(handle, mBuffers, GPUOperation::MapBuffer);
  const std::uint32_t requiredUsage =
      mode == MapMode::Read ? BufferUsageReadback : BufferUsageStaging;
  if (!buffer || buffer->mapped || !(buffer->usage & requiredUsage) ||
      size == 0 || offset > buffer->size || size > buffer->size - offset ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      size > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::MapBuffer,
         handle.value(), offset, size, "invalid buffer map range or usage");
    return nullptr;
  }
#if !GPU_GL_DESKTOP && !GPU_GLES_HAS_BUFFER_MAP
  push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::MapBuffer,
       handle.value(), 0, 0, "buffer mapping is unavailable on this device");
  return nullptr;
#else
  const GLbitfield access =
      mode == MapMode::Read ? GL_MAP_READ_BIT : GL_MAP_WRITE_BIT;
  glBindBuffer(buffer->target, buffer->id);
  void *pointer = glMapBufferRange(buffer->target,
                                   static_cast<GLintptr>(offset),
                                   static_cast<GLsizeiptr>(size), access);
  if (!pointer) {
    push(GPUErrorCode::BackendFailure, GPUOperation::MapBuffer, handle.value(),
         offset, size, "OpenGL rejected buffer map");
    return nullptr;
  }
  buffer->mapped = true;
  return pointer;
#endif
}

bool GPU_GL_DEVICE_CLASS::unmapBuffer(BufferHandle handle) {
  BufferObject *buffer = find(handle, mBuffers, GPUOperation::MapBuffer);
  if (!buffer || !buffer->mapped) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::MapBuffer,
         handle.value(), 0, 0, "buffer is not mapped");
    return false;
  }
#if !GPU_GL_DESKTOP && !GPU_GLES_HAS_BUFFER_MAP
  push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::MapBuffer,
       handle.value(), 0, 0, "buffer mapping is unavailable on this device");
  return false;
#else
  glBindBuffer(buffer->target, buffer->id);
  const GLboolean preserved = glUnmapBuffer(buffer->target);
  buffer->mapped = false;
  if (preserved == GL_FALSE) {
    push(GPUErrorCode::BackendFailure, GPUOperation::MapBuffer, handle.value(),
         0, 0, "OpenGL invalidated the mapped buffer contents");
    return false;
  }
  return true;
#endif
}

bool GPU_GL_DEVICE_CLASS::drawIndirectCount(BufferHandle buffer,
                                         std::uint64_t offset,
                                         BufferHandle countBuffer,
                                         std::uint64_t countOffset,
                                         std::uint32_t maxDrawCount,
                                         std::uint32_t stride) {
  if (!mCapabilities.indirectCount) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::DrawIndirect,
         buffer.value(), 0, 0,
         "indirect draw count is unavailable on this device");
    return false;
  }
#if GPU_GL_DESKTOP
  BufferObject *indirectBuffer =
      find(buffer, mBuffers, GPUOperation::DrawIndirect);
  BufferObject *countBufferObject =
      find(countBuffer, mBuffers, GPUOperation::DrawIndirect);
  const std::uint64_t maxU64 = (std::numeric_limits<std::uint64_t>::max)();
  const std::uint64_t lastEntryOffset =
      maxDrawCount == 0 ? 0
                        : static_cast<std::uint64_t>(maxDrawCount - 1) * stride;
  const bool spanOverflows =
      offset > maxU64 - sizeof(DrawIndirectArgs) ||
      lastEntryOffset > maxU64 - offset - sizeof(DrawIndirectArgs);
  if (!mInRenderPass || !mPipeline.valid() || !indirectBuffer ||
      !(indirectBuffer->usage & BufferUsageIndirect) || !countBufferObject ||
      !(countBufferObject->usage & BufferUsageIndirect) || maxDrawCount == 0 ||
      stride < sizeof(DrawIndirectArgs) || (offset % 4) != 0 ||
      (countOffset % 4) != 0 || spanOverflows ||
      offset + lastEntryOffset + sizeof(DrawIndirectArgs) >
          indirectBuffer->size ||
      countOffset > countBufferObject->size ||
      countBufferObject->size - countOffset < sizeof(std::uint32_t) ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      countOffset > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         buffer.value(), offset, maxDrawCount,
         "invalid indirect draw count state");
    return false;
  }
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::DrawIndirect);
  if (!pipeline)
    return false;
  if (pipeline->isCompute) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         mPipeline.value(), 0, 0, "bound pipeline is not a graphics pipeline");
    return false;
  }
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer->id);
  glBindBuffer(GL_PARAMETER_BUFFER, countBufferObject->id);
  glMultiDrawArraysIndirectCount(
      pipeline->topology,
      reinterpret_cast<const void *>(static_cast<std::uintptr_t>(offset)),
      static_cast<GLintptr>(countOffset), static_cast<GLsizei>(maxDrawCount),
      static_cast<GLsizei>(stride));
  return true;
#else
  (void)offset;
  (void)countBuffer;
  (void)countOffset;
  (void)maxDrawCount;
  (void)stride;
  return false;
#endif
}

bool GPU_GL_DEVICE_CLASS::drawIndexedIndirectCount(BufferHandle buffer,
                                                std::uint64_t offset,
                                                BufferHandle countBuffer,
                                                std::uint64_t countOffset,
                                                std::uint32_t maxDrawCount,
                                                std::uint32_t stride) {
  if (!mCapabilities.indirectCount) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::DrawIndirect,
         buffer.value(), 0, 0,
         "indirect draw count is unavailable on this device");
    return false;
  }
#if GPU_GL_DESKTOP
  BufferObject *indirectBuffer =
      find(buffer, mBuffers, GPUOperation::DrawIndirect);
  BufferObject *countBufferObject =
      find(countBuffer, mBuffers, GPUOperation::DrawIndirect);
  const std::uint64_t maxU64 = (std::numeric_limits<std::uint64_t>::max)();
  const std::uint64_t lastEntryOffset =
      maxDrawCount == 0
          ? 0
          : static_cast<std::uint64_t>(maxDrawCount - 1) * stride;
  const bool spanOverflows =
      offset > maxU64 - sizeof(DrawIndexedIndirectArgs) ||
      lastEntryOffset > maxU64 - offset - sizeof(DrawIndexedIndirectArgs);
  if (!mInRenderPass || !mPipeline.valid() || !mBuffers.find(mIndexBuffer) ||
      !indirectBuffer || !(indirectBuffer->usage & BufferUsageIndirect) ||
      !countBufferObject ||
      !(countBufferObject->usage & BufferUsageIndirect) || maxDrawCount == 0 ||
      stride < sizeof(DrawIndexedIndirectArgs) || (offset % 4) != 0 ||
      (countOffset % 4) != 0 || spanOverflows ||
      offset + lastEntryOffset + sizeof(DrawIndexedIndirectArgs) >
          indirectBuffer->size ||
      countOffset > countBufferObject->size ||
      countBufferObject->size - countOffset < sizeof(std::uint32_t) ||
      offset > static_cast<std::uint64_t>(PTRDIFF_MAX) ||
      countOffset > static_cast<std::uint64_t>(PTRDIFF_MAX)) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         buffer.value(), offset, maxDrawCount,
         "invalid indexed indirect draw count state");
    return false;
  }
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::DrawIndirect);
  if (!pipeline)
    return false;
  if (pipeline->isCompute) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::DrawIndirect,
         mPipeline.value(), 0, 0, "bound pipeline is not a graphics pipeline");
    return false;
  }
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer->id);
  glBindBuffer(GL_PARAMETER_BUFFER, countBufferObject->id);
  glMultiDrawElementsIndirectCount(
      pipeline->topology, mIndexType,
      reinterpret_cast<const void *>(static_cast<std::uintptr_t>(offset)),
      static_cast<GLintptr>(countOffset), static_cast<GLsizei>(maxDrawCount),
      static_cast<GLsizei>(stride));
  return true;
#else
  (void)offset;
  (void)countBuffer;
  (void)countOffset;
  (void)maxDrawCount;
  (void)stride;
  return false;
#endif
}

bool GPU_GL_DEVICE_CLASS::writeTimestamp(QueryHandle handle) {
#if GPU_GL_DESKTOP
  QueryObject *query = find(handle, mQueries, GPUOperation::CreateQuery);
  if (!query || query->type != QueryType::Timestamp || query->active) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::CreateQuery,
         handle.value(), 0, 0, "invalid timestamp query state");
    return false;
  }
  glQueryCounter(query->id, GL_TIMESTAMP);
  query->written = true;
  return true;
#else
  (void)handle;
  push(GPUErrorCode::UnsupportedFeature, GPUOperation::CreateQuery, 0, 0, 0,
       "timestamp queries are unavailable on this device");
  return false;
#endif
}

bool GPU_GL_DEVICE_CLASS::draw(std::uint32_t vertexCount,
                            std::uint32_t instanceCount,
                            std::uint32_t firstVertex,
                            std::uint32_t firstInstance) {
#if !GPU_GL_DESKTOP
  if (firstInstance != 0) {
    push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::Draw,
         mPipeline.value(), firstInstance, 0,
         "a non-zero first instance is unavailable on this device");
    return false;
  }
#endif
  if (!mInRenderPass || !mPipeline.valid() || vertexCount == 0 ||
      instanceCount == 0) {
    push(::gpu::GPUErrorCode::InvalidArgument, ::gpu::GPUOperation::Draw,
         mPipeline.value(), vertexCount, instanceCount, "invalid draw state");
    return false;
  }
#if GPU_GL_DESKTOP
  if (firstInstance != 0 && !mCapabilities.baseInstance) {
    push(::gpu::GPUErrorCode::UnsupportedFeature, ::gpu::GPUOperation::Draw,
         mPipeline.value(), firstInstance, 0,
         "a non-zero first instance is unavailable on this device");
    return false;
  }
#endif
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::Draw);
  if (!pipeline)
    return false;
#if GPU_GL_DESKTOP
  if (firstInstance != 0)
    glDrawArraysInstancedBaseInstance(
        pipeline->topology, static_cast<GLint>(firstVertex),
        static_cast<GLsizei>(vertexCount),
        static_cast<GLsizei>(instanceCount), firstInstance);
  else
#endif
    glDrawArraysInstanced(pipeline->topology,
                         static_cast<GLint>(firstVertex),
                         static_cast<GLsizei>(vertexCount),
                         static_cast<GLsizei>(instanceCount));
  return true;
}

bool GPU_GL_DEVICE_CLASS::drawIndexed(std::uint32_t indexCount,
                                   std::uint32_t instanceCount,
                                   std::uint32_t firstIndex,
                                   std::int32_t baseVertex,
                                   std::uint32_t firstInstance) {
  PipelineObject *pipeline =
      find(mPipeline, mPipelines, GPUOperation::Draw);
#if !GPU_GL_DESKTOP
  if (firstInstance != 0) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::Draw,
         mIndexBuffer.value(), firstInstance, 0,
         "a non-zero first instance is unavailable on this device");
    return false;
  }
#endif
  if (!mInRenderPass || !pipeline || !mBuffers.find(mIndexBuffer) ||
      indexCount == 0 || instanceCount == 0) {
    push(GPUErrorCode::InvalidArgument, GPUOperation::Draw,
         mIndexBuffer.value(), indexCount, instanceCount,
         "invalid indexed draw state");
    return false;
  }
#if GPU_GL_DESKTOP
  if (firstInstance != 0 && !mCapabilities.baseInstance) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::Draw,
         mIndexBuffer.value(), firstInstance, 0,
         "a non-zero first instance is unavailable on this device");
    return false;
  }
#else
  if (baseVertex != 0 && !mHasES32) {
    push(GPUErrorCode::UnsupportedFeature, GPUOperation::Draw,
         mIndexBuffer.value(), 0, 0,
         "non-zero base vertex requires OpenGL ES 3.2");
    return false;
  }
#endif
  const std::uint64_t indexSize = mIndexType == GL_UNSIGNED_SHORT ? 2u : 4u;
  const std::uint64_t byteOffset = mIndexOffset + firstIndex * indexSize;
  const void *indices =
      reinterpret_cast<const void *>(static_cast<std::uintptr_t>(byteOffset));
#if GPU_GL_DESKTOP
  if (firstInstance != 0)
    glDrawElementsInstancedBaseVertexBaseInstance(
        pipeline->topology, static_cast<GLsizei>(indexCount), mIndexType,
        indices, static_cast<GLsizei>(instanceCount), baseVertex,
        firstInstance);
  else
    glDrawElementsInstancedBaseVertex(
        pipeline->topology, static_cast<GLsizei>(indexCount), mIndexType,
        indices, static_cast<GLsizei>(instanceCount), baseVertex);
#else
#if GPU_GLES_HAS_ES32
  if (mHasES32) {
    glDrawElementsInstancedBaseVertex(
        pipeline->topology, static_cast<GLsizei>(indexCount), mIndexType,
        indices, static_cast<GLsizei>(instanceCount), baseVertex);
  } else
#endif
  {
    glDrawElementsInstanced(pipeline->topology,
                            static_cast<GLsizei>(indexCount), mIndexType,
                            indices, static_cast<GLsizei>(instanceCount));
  }
#endif
  return true;
}

bool GPU_GL_DEVICE_CLASS::setPipeline(PipelineHandle handle) {
  PipelineObject *pipeline =
      find(handle, mPipelines, ::gpu::GPUOperation::SetPipeline);
  if (!pipeline)
    return false;
  if (pipeline->isCompute && mInRenderPass) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::SetPipeline, handle.value(), 0, 0,
         "compute pipeline cannot be set inside a render pass");
    return false;
  }
  if (!pipeline->isCompute && !mInRenderPass) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::SetPipeline, handle.value(), 0, 0,
         "pipeline must be set inside a render pass");
    return false;
  }
  if (mPipeline != handle)
    mIndexBuffer = BufferHandle();
  if (pipeline->isCompute) {
    glUseProgram(pipeline->program);
    mPipeline = handle;
    return true;
  }
  glUseProgram(pipeline->program);
  glBindVertexArray(pipeline->vertexArray);
  if (pipeline->raster.cullMode == CullMode::None) {
    glDisable(GL_CULL_FACE);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(pipeline->raster.cullMode == CullMode::Back ? GL_BACK
                                                                 : GL_FRONT);
  }
  glFrontFace(pipeline->raster.frontFace == FrontFace::CounterClockwise
                  ? GL_CCW
                  : GL_CW);
  if (pipeline->depthStencil.depthTestEnabled) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(compareValue(pipeline->depthStencil.depthCompare));
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  glDepthMask(pipeline->depthStencil.depthWriteEnabled ? GL_TRUE
                                                               : GL_FALSE);
  const DepthStencilState &depthStencil = pipeline->depthStencil;
  if (depthStencil.stencilEnabled) {
    glEnable(GL_STENCIL_TEST);
    glStencilMaskSeparate(GL_FRONT, depthStencil.stencilWriteMask);
    glStencilMaskSeparate(GL_BACK, depthStencil.stencilWriteMask);
    glStencilFuncSeparate(GL_FRONT, compareValue(depthStencil.stencilFront.compare),
                          0, depthStencil.stencilReadMask);
    glStencilFuncSeparate(GL_BACK, compareValue(depthStencil.stencilBack.compare),
                          0, depthStencil.stencilReadMask);
    glStencilOpSeparate(
        GL_FRONT,
        stencilOperationValue(depthStencil.stencilFront.failOperation),
        stencilOperationValue(depthStencil.stencilFront.depthFailOperation),
        stencilOperationValue(depthStencil.stencilFront.passOperation));
    glStencilOpSeparate(
        GL_BACK, stencilOperationValue(depthStencil.stencilBack.failOperation),
        stencilOperationValue(depthStencil.stencilBack.depthFailOperation),
        stencilOperationValue(depthStencil.stencilBack.passOperation));
  } else {
    glDisable(GL_STENCIL_TEST);
  }
  if (!mCapabilities.independentBlend) {
    const ColorTargetState &color = pipeline->colorTargets[0];
    glBlendFuncSeparate(
        blendFactorValue(color.colorBlend.sourceFactor),
        blendFactorValue(color.colorBlend.destinationFactor),
        blendFactorValue(color.alphaBlend.sourceFactor),
        blendFactorValue(color.alphaBlend.destinationFactor));
    glBlendEquationSeparate(blendOperationValue(color.colorBlend.operation),
                            blendOperationValue(color.alphaBlend.operation));
  }
#if GPU_GL_DESKTOP
  for (std::uint32_t index = 0; index < pipeline->colorTargetCount; ++index) {
    const ColorTargetState &color = pipeline->colorTargets[index];
    if (color.blendEnabled)
      glEnablei(GL_BLEND, index);
    else
      glDisablei(GL_BLEND, index);
    if (mCapabilities.independentBlend) {
      if (GLAD_GL_VERSION_4_0) {
        glBlendFuncSeparatei(
            index, blendFactorValue(color.colorBlend.sourceFactor),
            blendFactorValue(color.colorBlend.destinationFactor),
            blendFactorValue(color.alphaBlend.sourceFactor),
            blendFactorValue(color.alphaBlend.destinationFactor));
        glBlendEquationSeparatei(
            index, blendOperationValue(color.colorBlend.operation),
            blendOperationValue(color.alphaBlend.operation));
      } else {
        glBlendFuncSeparateiARB(
            index, blendFactorValue(color.colorBlend.sourceFactor),
            blendFactorValue(color.colorBlend.destinationFactor),
            blendFactorValue(color.alphaBlend.sourceFactor),
            blendFactorValue(color.alphaBlend.destinationFactor));
        glBlendEquationSeparateiARB(
            index, blendOperationValue(color.colorBlend.operation),
            blendOperationValue(color.alphaBlend.operation));
      }
    }
    glColorMaski(index, (color.writeMask & ColorWriteRed) != 0,
                 (color.writeMask & ColorWriteGreen) != 0,
                 (color.writeMask & ColorWriteBlue) != 0,
                 (color.writeMask & ColorWriteAlpha) != 0);
  }
  for (std::uint32_t index = pipeline->colorTargetCount;
       index < mMaxDrawBuffers; ++index) {
    glDisablei(GL_BLEND, index);
    glColorMaski(index, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
#else
  if (!mHasES32) {
    const ColorTargetState &color = pipeline->colorTargets[0];
    if (color.blendEnabled)
      glEnable(GL_BLEND);
    else
      glDisable(GL_BLEND);
    glColorMask((color.writeMask & ColorWriteRed) != 0,
               (color.writeMask & ColorWriteGreen) != 0,
               (color.writeMask & ColorWriteBlue) != 0,
               (color.writeMask & ColorWriteAlpha) != 0);
  } else {
#if GPU_GLES_HAS_ES32
    for (std::uint32_t index = 0; index < pipeline->colorTargetCount;
        ++index) {
      const ColorTargetState &color = pipeline->colorTargets[index];
      if (color.blendEnabled)
        glEnablei(GL_BLEND, index);
      else
        glDisablei(GL_BLEND, index);
      if (mCapabilities.independentBlend) {
        glBlendFuncSeparatei(
            index, blendFactorValue(color.colorBlend.sourceFactor),
            blendFactorValue(color.colorBlend.destinationFactor),
            blendFactorValue(color.alphaBlend.sourceFactor),
            blendFactorValue(color.alphaBlend.destinationFactor));
        glBlendEquationSeparatei(
            index, blendOperationValue(color.colorBlend.operation),
            blendOperationValue(color.alphaBlend.operation));
      }
      glColorMaski(index, (color.writeMask & ColorWriteRed) != 0,
                  (color.writeMask & ColorWriteGreen) != 0,
                  (color.writeMask & ColorWriteBlue) != 0,
                  (color.writeMask & ColorWriteAlpha) != 0);
    }
    for (std::uint32_t index = pipeline->colorTargetCount;
        index < mMaxDrawBuffers; ++index) {
      glDisablei(GL_BLEND, index);
      glColorMaski(index, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
#endif
  }
#endif
  if (pipeline->raster.depthBiasConstant != 0.0f ||
      pipeline->raster.depthBiasSlope != 0.0f) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(pipeline->raster.depthBiasSlope,
                    pipeline->raster.depthBiasConstant);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  if (pipeline->raster.scissorEnabled)
    glEnable(GL_SCISSOR_TEST);
  else
    glDisable(GL_SCISSOR_TEST);
  mPipeline = handle;
  return true;
}

bool GPU_GL_DEVICE_CLASS::initialize() {
  if (!mSurface || !mSurface->makeCurrent || !mSurface->getProcAddress ||
      !mSurface->present || !mSurface->drawableSize)
    return false;
#if GPU_GL_DESKTOP
  if (!mSurface->makeCurrent(mSurface->userData) ||
      gladLoadGLUserPtr(loadGLProcedure,
                        const_cast<GLSurface *>(mSurface)) == 0)
    return false;
#else
  if (!mSurface->makeCurrent(mSurface->userData))
    return false;

  GLint majorVersion = 0;
  GLint minorVersion = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
  glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
  if (majorVersion < 3)
    return false;
  mHasES31 =
      majorVersion > 3 || (majorVersion == 3 && minorVersion >= 1);
  mHasES32 =
      majorVersion > 3 || (majorVersion == 3 && minorVersion >= 2);
#if !GPU_GLES_HAS_ES31
  mHasES31 = false;
#endif
#if !GPU_GLES_HAS_ES32
  mHasES32 = false;
#endif
#endif

  GLint value = 0;
  glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &value);
  mCapabilities.maxColorAttachments =
      static_cast<std::uint32_t>(std::max(value, 1));
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
  mCapabilities.maxTextureDimension2D =
      static_cast<std::uint32_t>(std::max(value, 0));
  glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &value);
  mCapabilities.maxTextureDimension3D =
      static_cast<std::uint32_t>(std::max(value, 0));
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &value);
  mCapabilities.maxTextureArrayLayers =
      static_cast<std::uint32_t>(std::max(value, 0));
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &value);
  mCapabilities.maxTextureBindings =
      static_cast<std::uint32_t>(std::max(value, 0));
  glGetIntegerv(GL_MAX_SAMPLES, &value);
#if GPU_GL_DESKTOP
  mCapabilities.maxSampleCount =
      static_cast<std::uint32_t>(std::max(value, 1));
#else
  mCapabilities.maxSampleCount =
      mHasES31 ? static_cast<std::uint32_t>(std::max(value, 1)) : 1u;
#endif
  glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &value);
  mCapabilities.maxUniformBufferBindings =
      static_cast<std::uint32_t>(std::max(value, 0));
#if GPU_GL_DESKTOP
  mCapabilities.textureArrays = true;
  mCapabilities.textureCompressionBC1 = GLAD_GL_EXT_texture_compression_s3tc;
  mCapabilities.textureCompressionBC3 = GLAD_GL_EXT_texture_compression_s3tc;
  mCapabilities.textureCompressionBC5 =
      GLAD_GL_VERSION_3_0 || GLAD_GL_ARB_texture_compression_rgtc;
  mCapabilities.textureCompressionBC7 =
      GLAD_GL_VERSION_4_2 || GLAD_GL_ARB_texture_compression_bptc;
  mCapabilities.textureCompressionBC =
      mCapabilities.textureCompressionBC1 &&
      mCapabilities.textureCompressionBC3 &&
      mCapabilities.textureCompressionBC5 &&
      mCapabilities.textureCompressionBC7;
  mCapabilities.textureCompressionETC2 =
      GLAD_GL_VERSION_4_3 || GLAD_GL_ARB_ES3_compatibility;
  mCapabilities.textureCompressionASTC =
      GLAD_GL_KHR_texture_compression_astc_ldr;
  mCapabilities.samplerBorderColor = true;
  mCapabilities.wireframe = true;
  mCapabilities.independentBlend =
      GLAD_GL_VERSION_4_0 || GLAD_GL_ARB_draw_buffers_blend;
  mCapabilities.baseInstance =
      GLAD_GL_VERSION_4_2 || GLAD_GL_ARB_base_instance;
  mCapabilities.compute =
      GLAD_GL_VERSION_4_3 || GLAD_GL_ARB_compute_shader;
  mCapabilities.storageBuffers =
      GLAD_GL_VERSION_4_3 || GLAD_GL_ARB_shader_storage_buffer_object;
  mCapabilities.storageTextures =
      GLAD_GL_VERSION_4_2 || GLAD_GL_ARB_shader_image_load_store;
  mCapabilities.memoryBarriers =
      GLAD_GL_VERSION_4_2 || GLAD_GL_ARB_shader_image_load_store;
  mCapabilities.indirectDraw =
      GLAD_GL_VERSION_4_0 || GLAD_GL_ARB_draw_indirect;
  mCapabilities.indirectCount =
      GLAD_GL_VERSION_4_6 || GLAD_GL_ARB_indirect_parameters;
  mCapabilities.timestampQueries =
      GLAD_GL_VERSION_3_3 || GLAD_GL_ARB_timer_query;
  mCapabilities.occlusionQueries = true;
  mCapabilities.asyncReadback = GLAD_GL_VERSION_3_2 || GLAD_GL_ARB_sync;
  mCapabilities.depthReadback = true;
#else
  mCapabilities.textureArrays = true;
  mCapabilities.textureCompressionBC1 = hasExtension("GL_EXT_texture_compression_s3tc") ||
                                        hasExtension("GL_EXT_texture_compression_dxt1");
  mCapabilities.textureCompressionBC3 = hasExtension("GL_EXT_texture_compression_s3tc");
  mCapabilities.textureCompressionBC5 = hasExtension("GL_EXT_texture_compression_rgtc");
  mCapabilities.textureCompressionBC7 = hasExtension("GL_EXT_texture_compression_bptc");
  mCapabilities.textureCompressionBC =
      mCapabilities.textureCompressionBC1 &&
      mCapabilities.textureCompressionBC3 &&
      mCapabilities.textureCompressionBC5 &&
      mCapabilities.textureCompressionBC7;
  mCapabilities.textureCompressionETC2 = true;
  mCapabilities.textureCompressionASTC =
      hasExtension("GL_KHR_texture_compression_astc_ldr");
  mCapabilities.samplerBorderColor = mHasES32;
  mCapabilities.wireframe = false;
  mCapabilities.independentBlend = mHasES32;
  mCapabilities.compute = mHasES31;
  mCapabilities.storageBuffers = mHasES31;
  mCapabilities.storageTextures = mHasES31;
  mCapabilities.memoryBarriers = mHasES31;
  mCapabilities.indirectDraw = mHasES31;
  mCapabilities.timestampQueries = false;
  mCapabilities.occlusionQueries = true;
  mCapabilities.asyncReadback = true;
  // ES 3.x glReadPixels only accepts colour formats; depth attachments cannot
  // be read back. Some desktop ES implementations allow it, the spec does not.
  mCapabilities.depthReadback = false;
#endif
  glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &value);
  mCapabilities.maxUniformBufferSize =
      static_cast<std::uint32_t>(std::max(value, 0));
  glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
  mCapabilities.uniformBufferOffsetAlignment =
      static_cast<std::uint32_t>(std::max(value, 1));
  if (mCapabilities.storageBuffers) {
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &value);
    mCapabilities.maxStorageBufferBindings =
        static_cast<std::uint32_t>(std::max(value, 0));
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &value);
    mCapabilities.storageBufferOffsetAlignment =
        static_cast<std::uint32_t>(std::max(value, 1));
  }
  if (mCapabilities.storageTextures) {
    glGetIntegerv(GL_MAX_IMAGE_UNITS, &value);
    mMaxStorageTextureBindings = static_cast<std::uint32_t>(std::max(value, 0));
  }
#if GPU_GL_DESKTOP
  mAnisotropicFiltering = GLAD_GL_VERSION_4_6 || GLAD_GL_EXT_texture_filter_anisotropic;
  mDebugLabels = GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug;
#else
  mAnisotropicFiltering = hasExtension("GL_EXT_texture_filter_anisotropic");
#endif
  mCapabilities.anisotropicFiltering = mAnisotropicFiltering;
  mCapabilities.maxAnisotropy = 1.0f;
  if (mAnisotropicFiltering) {
    GLfloat maxAnisotropy = 1.0f;
#if GPU_GL_DESKTOP
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAnisotropy);
#else
    glGetFloatv(kMaxTextureMaxAnisotropyExt, &maxAnisotropy);
#endif
    mCapabilities.maxAnisotropy = std::max(maxAnisotropy, 1.0f);
  }
#if !GPU_GL_DESKTOP
#if GPU_GLES_HAS_KHR_DEBUG
  mDebugLabels = mHasES32 || hasExtension("GL_KHR_debug");
#else
  mDebugLabels = false;
#endif
#endif
  if (mDebugLabels) {
    glGetIntegerv(GL_MAX_LABEL_LENGTH, &value);
    mMaxLabelLength = static_cast<std::uint32_t>(std::max(value - 1, 0));
    mDebugLabels = mMaxLabelLength != 0;
  }
  glGetIntegerv(GL_MAX_DRAW_BUFFERS, &value);
  mMaxDrawBuffers = static_cast<std::uint32_t>(std::max(value, 1));
  if (mCapabilities.maxColorAttachments > mMaxDrawBuffers)
    mCapabilities.maxColorAttachments = mMaxDrawBuffers;
#if GPU_GL_DESKTOP
  GLint contextFlags = 0;
  glGetIntegerv(GL_CONTEXT_FLAGS, &contextFlags);
  if ((contextFlags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0 && (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug)) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&debugMessage, this);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                          GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
  }
#else
  const bool debugAvailable = mHasES32 || hasExtension("GL_KHR_debug");
  GLint contextFlags = 0;
  if (debugAvailable)
    glGetIntegerv(GL_CONTEXT_FLAGS, &contextFlags);
#if GPU_GLES_HAS_KHR_DEBUG
  if (debugAvailable && (contextFlags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&debugMessage, this);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                          GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
  }
#else
  (void)contextFlags;
#endif
#endif
  mAlive = true;
  return mAlive;
}

TextureHandle GPU_GL_DEVICE_CLASS::createTexture(const TextureDesc &desc) {
  GLint internalFormat = 0;
  GLenum layout = 0;
  GLenum type = 0;
  CompressedFormatInfo compressedInfo;
  const bool compressed = compressedTextureFormat(desc.format, compressedInfo);
  if (!mAlive) {
    push(::gpu::GPUErrorCode::DeviceLost,
         ::gpu::GPUOperation::CreateTexture, 0, desc.width, desc.height,
         "device is shut down");
    return TextureHandle();
  }
  if (!compressed && !textureFormat(desc.format, internalFormat, layout, type)) {
    push(::gpu::GPUErrorCode::UnsupportedFormat,
         ::gpu::GPUOperation::CreateTexture, 0,
         static_cast<std::uint64_t>(desc.format), 0,
         "texture format is unsupported by the OpenGL backend");
    return TextureHandle();
  }
  if ((desc.usage & TextureUsageStorage) && !mCapabilities.storageTextures) {
    push(::gpu::GPUErrorCode::UnsupportedFeature,
         ::gpu::GPUOperation::CreateTexture, 0, desc.usage, 0,
         "storage textures are unavailable on this device");
    return TextureHandle();
  }
  if (compressed && !compressedFormatSupported(desc.format, mCapabilities)) {
    push(::gpu::GPUErrorCode::UnsupportedFormat,
         ::gpu::GPUOperation::CreateTexture, 0,
         static_cast<std::uint64_t>(desc.format), 0,
         "compressed texture format is unavailable on this device");
    return TextureHandle();
  }
  if (compressed)
    internalFormat = static_cast<GLint>(compressedInfo.internalFormat);
  if (desc.width == 0 || desc.height == 0 || desc.depthOrLayers == 0 ||
      desc.mipCount == 0 || desc.sampleCount == 0 || desc.usage == 0) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::CreateTexture, 0, desc.width, desc.height,
         "invalid texture descriptor");
    return TextureHandle();
  }
  std::uint64_t requiredDataSize = 0;
  const bool hasInitialData = desc.initialData.data != nullptr;
  const bool initialDataMismatch = hasInitialData != (desc.initialData.size != 0);
  const bool invalid2D = desc.dimension == TextureDimension::Texture2D &&
                         desc.depthOrLayers != 1;
  const bool invalidCube = desc.dimension == TextureDimension::TextureCube &&
                           (desc.depthOrLayers != 1 ||
                            desc.width != desc.height);
  const bool invalidArray =
      desc.dimension == TextureDimension::Texture2DArray &&
      (!mCapabilities.textureArrays ||
       desc.depthOrLayers > mCapabilities.maxTextureArrayLayers);
  const bool invalid3D = desc.dimension == TextureDimension::Texture3D &&
                         desc.depthOrLayers >
                             mCapabilities.maxTextureDimension3D;
  const bool invalidSamples =
      desc.sampleCount > 1 &&
      (desc.dimension != TextureDimension::Texture2D || desc.mipCount != 1 ||
       hasInitialData || !(desc.usage & TextureUsageRenderTarget));
  const bool invalidCompressed =
      compressed &&
      (desc.dimension == TextureDimension::Texture3D || desc.sampleCount > 1 ||
       (desc.usage & TextureUsageRenderTarget) ||
       (desc.usage & TextureUsageStorage));
  std::uint32_t compressedImages = 1;
  if (desc.dimension == TextureDimension::TextureCube)
    compressedImages = 6;
  else if (desc.dimension == TextureDimension::Texture2DArray)
    compressedImages = desc.depthOrLayers;
  const bool invalidDataSize =
      hasInitialData &&
      (!(compressed
             ? compressedLevelSize(desc.width, desc.height, compressedImages,
                                   compressedInfo.blockBytes, requiredDataSize)
             : checkedTextureDataSize(desc, requiredDataSize)) ||
       desc.initialData.size < requiredDataSize);
  if (desc.width > mCapabilities.maxTextureDimension2D ||
      desc.height > mCapabilities.maxTextureDimension2D || invalid2D ||
      invalidCube || invalidArray || invalid3D || invalidSamples ||
      invalidCompressed || initialDataMismatch || invalidDataSize ||
      requiredDataSize >
          static_cast<std::uint64_t>((std::numeric_limits<GLsizei>::max)()) ||
      desc.mipCount > maximumMipCount(desc) ||
      desc.sampleCount > mCapabilities.maxSampleCount ||
      (desc.sampleCount & (desc.sampleCount - 1)) != 0) {
    push(::gpu::GPUErrorCode::InvalidArgument,
         ::gpu::GPUOperation::CreateTexture, 0, desc.width, desc.height,
         "invalid texture descriptor");
    return TextureHandle();
  }
  TextureObject object;
  object.target = textureTarget(desc.dimension, desc.sampleCount);
  object.format = desc.format;
  object.width = desc.width;
  object.height = desc.height;
  object.depthOrLayers = desc.depthOrLayers;
  object.mipCount = desc.mipCount;
  object.sampleCount = desc.sampleCount;
  object.usage = desc.usage;
  drainDriverErrors();
  glGenTextures(1, &object.id);
  if (!object.id) {
    push(::gpu::GPUErrorCode::OutOfMemory,
         ::gpu::GPUOperation::CreateTexture, 0, 0, 0,
         "OpenGL texture creation failed");
    return TextureHandle();
  }
  glBindTexture(object.target, object.id);
  if (desc.sampleCount > 1) {
#if GPU_GL_DESKTOP
    glTexImage2DMultisample(object.target, static_cast<GLsizei>(desc.sampleCount),
                            internalFormat, static_cast<GLsizei>(desc.width),
                            static_cast<GLsizei>(desc.height), GL_TRUE);
#elif GPU_GLES_HAS_ES31
    glTexStorage2DMultisample(object.target,
                              static_cast<GLsizei>(desc.sampleCount),
                              static_cast<GLenum>(internalFormat),
                              static_cast<GLsizei>(desc.width),
                              static_cast<GLsizei>(desc.height), GL_TRUE);
#endif
  } else {
    glTexParameteri(object.target, GL_TEXTURE_MIN_FILTER,
                    desc.mipCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(object.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(object.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(object.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(object.target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(object.target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(object.target, GL_TEXTURE_MAX_LEVEL,
                    static_cast<GLint>(desc.mipCount - 1));
#if !GPU_GL_DESKTOP
    // Armazenamento imutavel: aloca toda a cadeia de mips de uma vez.
    const GLenum storageFormat = compressed
                                     ? compressedInfo.internalFormat
                                     : static_cast<GLenum>(internalFormat);
    if (desc.dimension == TextureDimension::Texture2D ||
        desc.dimension == TextureDimension::TextureCube) {
      glTexStorage2D(object.target, static_cast<GLsizei>(desc.mipCount),
                     storageFormat, static_cast<GLsizei>(desc.width),
                     static_cast<GLsizei>(desc.height));
    } else {
      glTexStorage3D(object.target, static_cast<GLsizei>(desc.mipCount),
                     storageFormat, static_cast<GLsizei>(desc.width),
                     static_cast<GLsizei>(desc.height),
                     static_cast<GLsizei>(desc.depthOrLayers));
    }
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const std::uint8_t *initialBytes =
        static_cast<const std::uint8_t *>(desc.initialData.data);
#if GPU_GL_DESKTOP
    // Armazenamento mutavel: cada mip e alocado e preenchido por
    // glTexImage2D/3D, nivel a nivel.
    for (std::uint32_t mip = 0; mip < desc.mipCount; ++mip) {
      const GLsizei width =
          static_cast<GLsizei>(std::max(desc.width >> mip, 1u));
      const GLsizei height =
          static_cast<GLsizei>(std::max(desc.height >> mip, 1u));
      const void *data = mip == 0 ? desc.initialData.data : nullptr;
      if (compressed) {
        std::uint64_t levelSize = 0;
        compressedLevelSize(static_cast<std::uint32_t>(width),
                            static_cast<std::uint32_t>(height),
                            compressedImages, compressedInfo.blockBytes,
                            levelSize);
        if (desc.dimension == TextureDimension::Texture2D) {
          glCompressedTexImage2D(
              object.target, static_cast<GLint>(mip),
              compressedInfo.internalFormat, width, height, 0,
              static_cast<GLsizei>(levelSize), data);
        } else if (desc.dimension == TextureDimension::TextureCube) {
          const std::uint64_t faceSize = levelSize / 6;
          for (std::uint32_t face = 0; face < 6; ++face) {
            const void *faceData =
                mip == 0 && initialBytes
                    ? static_cast<const void *>(initialBytes + face * faceSize)
                    : nullptr;
            glCompressedTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                static_cast<GLint>(mip), compressedInfo.internalFormat, width,
                height, 0, static_cast<GLsizei>(faceSize), faceData);
          }
        } else {
          glCompressedTexImage3D(
              object.target, static_cast<GLint>(mip),
              compressedInfo.internalFormat, width, height,
              static_cast<GLsizei>(desc.depthOrLayers), 0,
              static_cast<GLsizei>(levelSize), data);
        }
      } else if (desc.dimension == TextureDimension::Texture2D) {
        glTexImage2D(object.target, static_cast<GLint>(mip), internalFormat,
                     width, height, 0, layout, type, data);
      } else if (desc.dimension == TextureDimension::TextureCube) {
        const std::uint64_t faceSize =
            static_cast<std::uint64_t>(desc.width) * desc.height *
            bytesPerTexel(desc.format);
        for (std::uint32_t face = 0; face < 6; ++face) {
          const void *faceData =
              mip == 0 && initialBytes
                  ? static_cast<const void *>(initialBytes + face * faceSize)
                  : nullptr;
          glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                       static_cast<GLint>(mip), internalFormat, width, height, 0,
                       layout, type, faceData);
        }
      } else {
        const GLsizei depth = desc.dimension == TextureDimension::Texture3D
                                  ? static_cast<GLsizei>(
                                        std::max(desc.depthOrLayers >> mip, 1u))
                                  : static_cast<GLsizei>(desc.depthOrLayers);
        glTexImage3D(object.target, static_cast<GLint>(mip), internalFormat,
                     width, height, depth, 0, layout, type,
                     mip == 0 ? desc.initialData.data : nullptr);
      }
    }
    if (hasInitialData && desc.mipCount > 1)
      glGenerateMipmap(object.target);
#else
    // Armazenamento ja alocado por glTexStorage acima; so falta preencher o
    // nivel 0 (glTexSubImage) e, se houver mais mips, gera-los.
    if (hasInitialData) {
      const GLsizei width = static_cast<GLsizei>(desc.width);
      const GLsizei height = static_cast<GLsizei>(desc.height);
      if (compressed) {
        std::uint64_t levelSize = 0;
        compressedLevelSize(desc.width, desc.height, compressedImages,
                            compressedInfo.blockBytes, levelSize);
        if (desc.dimension == TextureDimension::Texture2D) {
          glCompressedTexSubImage2D(object.target, 0, 0, 0, width, height,
                                    compressedInfo.internalFormat,
                                    static_cast<GLsizei>(levelSize),
                                    desc.initialData.data);
        } else if (desc.dimension == TextureDimension::TextureCube) {
          const std::uint64_t faceSize = levelSize / 6;
          for (std::uint32_t face = 0; face < 6; ++face)
            glCompressedTexSubImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, width, height,
                compressedInfo.internalFormat, static_cast<GLsizei>(faceSize),
                initialBytes + face * faceSize);
        } else {
          glCompressedTexSubImage3D(
              object.target, 0, 0, 0, 0, width, height,
              static_cast<GLsizei>(desc.depthOrLayers),
              compressedInfo.internalFormat, static_cast<GLsizei>(levelSize),
              desc.initialData.data);
        }
      } else if (desc.dimension == TextureDimension::Texture2D) {
        glTexSubImage2D(object.target, 0, 0, 0, width, height, layout, type,
                        desc.initialData.data);
      } else if (desc.dimension == TextureDimension::TextureCube) {
        const std::uint64_t faceSize =
            static_cast<std::uint64_t>(desc.width) * desc.height *
            bytesPerTexel(desc.format);
        for (std::uint32_t face = 0; face < 6; ++face)
          glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0,
                          width, height, layout, type,
                          initialBytes + face * faceSize);
      } else {
        glTexSubImage3D(object.target, 0, 0, 0, 0, width, height,
                        static_cast<GLsizei>(desc.depthOrLayers), layout,
                        type, desc.initialData.data);
      }
      if (desc.mipCount > 1)
        glGenerateMipmap(object.target);
    }
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  }
  const GLenum textureError = glGetError();
  if (textureError != GL_NO_ERROR) {
    glDeleteTextures(1, &object.id);
    push(::gpu::GPUErrorCode::BackendFailure,
         ::gpu::GPUOperation::CreateTexture, 0, textureError,
         static_cast<std::uint64_t>(desc.format),
         "OpenGL rejected texture creation or upload");
    return TextureHandle();
  }
  setObjectLabel(GL_TEXTURE, object.id, desc.debugName);
  const TextureHandle handle = insert<TextureHandle>(
      mTextures, object, ::gpu::GPUOperation::CreateTexture);
  if (!handle.valid())
    glDeleteTextures(1, &object.id);
  return handle;
}
