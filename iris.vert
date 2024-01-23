#version 330 core

//layout(location = 0) in vec2 icv;
in vec2 icv;
out vec2 icf;

void main()
{
	icf = icv;
	gl_Position.x = abs(icv.x)*icv.x;
	gl_Position.y = icv.y;
	gl_Position.z = 0.0;
	gl_Position.w = 1.0;
}
