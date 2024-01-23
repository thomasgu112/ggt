#version 330 core

in vec2 icf;
out vec4 color;
uniform sampler2D sam;

void main()
{
	vec2 uv = 0.5*(icf - 1.0);
    color = texture(sam, uv).rgba;
}
