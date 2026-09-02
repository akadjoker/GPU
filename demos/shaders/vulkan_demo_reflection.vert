#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

// Named the same way as the fragment stage's sampler below - the demo
// looks both of these up by name through reflectPipeline() instead of
// hardcoding their binding slots.
layout(set = 0, binding = 0) uniform Transform
{
  vec2 offset;
}
transform;

void main()
{
  fragUV = inUV;
  gl_Position = vec4(inPosition + transform.offset, 0.0, 1.0);
}
