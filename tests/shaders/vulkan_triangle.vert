#version 450

layout(location = 0) in vec2 inPosition;
layout(set = 0, binding = 0) uniform Transform
{
  vec2 offset;
} transform;

void main()
{
  gl_Position = vec4(inPosition + transform.offset, 0.0, 1.0);
}
