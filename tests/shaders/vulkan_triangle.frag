#version 450

layout(set = 1, binding = 0) uniform sampler2D colorMap;
layout(location = 0) out vec4 outColor0;
layout(location = 1) out vec4 outColor1;

void main()
{
  outColor0 = texture(colorMap, vec2(0.5));
  outColor1 = outColor0;
}
