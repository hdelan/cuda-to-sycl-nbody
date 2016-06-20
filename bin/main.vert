#version 450 core

layout (location = 0) uniform mat4 mv;

in vec4 pos;
out vec4 posrad;

void main()
{
  posrad = vec4(vec3(mv*vec4(pos.xyz,1.0)),pos.w);
}