#include "config.h"
#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

property_double(a_var, _("a"), 0.0)
value_range(-1.0, 1.0)

property_double(b_var, _("b"), 0.0)
value_range(-1.0, 1.0)

property_double(c_var, _("c"), 0.0)
value_range(-1.0, 1.0)

property_boolean(purge, _("Purge"), FALSE)
description("When this is switched on, shaders are \
recompiled and the base image texture is reloaded.")

property_string(vst, _("Vertex Shader"), "\
#version 460 core\n\
\n\
in vec2 icv;\n\
out vec2 icf;\n\
uniform float a;\n\
uniform float b;\n\
uniform float c;\n\
\n\
void main()\n\
{\n\
	icf = icv;\n\
	gl_Position.x = icv.x + a*icv.y;\n\
	gl_Position.y = sin(20*b*icv.x);\n\
	gl_Position.z = cos(20*b*icv.x);\n\
	gl_Position.w = 1.0;\n\
}\n\
")
ui_meta("multiline", "true")

property_string(fst, _("Fragment Shader"), "\
#version 460 core\n\
\n\
in vec2 icf;\n\
out vec4 color;\n\
uniform sampler2D sam;\n\
uniform float a;\n\
uniform float b;\n\
uniform float c;\n\
\n\
void main()\n\
{\n\
	vec2 uv = 0.5*(icf - 1.0);\n\
	uv.x = mod(20*b*uv.x, 1.0);\n\
	color = texture(sam, uv).rgba;\n\
}\n\
")
ui_meta("multiline", "true")

property_int(xVert, _("Vertices along the x-axis"), 512)
value_range(2, 1024)

property_int(yVert, _("Vertices along the y-axis"), 512)
value_range(2, 1024)

#else
#define GEGL_OP_FILTER
#define GEGL_OP_NAME     ggt
#define GEGL_OP_C_SOURCE ggt.c

#include "gegl-op.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

//TODO
//draw distance
//use EGL instead of glfw, make it headless
//figure out what glew does
//write to layer of arbitrary bounds
//uniform arbitration
//cursor coordinates as uniforms
//improve UI
//	shader storage/retrieval
//	proper purge button

typedef struct
{
	int engaged;
	int purged;
	int prog;
	char *dst_buf;
} State;

void reportError(const char *msg)
{
	puts(msg);
	switch(glGetError())
	{
		case GL_NO_ERROR:
		puts(	"None");
		//puts(	"GL_NO_ERROR:\nNo error has been recorded. The value of this symbolic constant is guaranteed to be 0.");
		break;

		case GL_INVALID_ENUM:
		puts(	"GL_INVALID_ENUM:\nAn unacceptable value is specified for an enumerated argument. "
				"The offending command is ignored and has no other side effect than to set the error flag.");
		break;

		case GL_INVALID_VALUE:
		puts(	"GL_INVALID_VALUE:\nA numeric argument is out of range. "
				"The offending command is ignored and has no other side effect than to set the error flag.");
		break;

		case GL_INVALID_OPERATION:
		puts(	"GL_INVALID_OPERATION:\nThe specified operation is not allowed in the current state. "
				"The offending command is ignored and has no other side effect than to set the error flag.");
		break;

		case GL_INVALID_FRAMEBUFFER_OPERATION:
		puts(	"GL_INVALID_FRAMEBUFFER_OPERATION:\nThe framebuffer object is not complete. "
				"The offending command is ignored and has no other side effect than to set the error flag.");
		break;

		case GL_OUT_OF_MEMORY:
		puts(	"GL_OUT_OF_MEMORY:\nThere is not enough memory left to execute the command. "
				"The state of the GL is undefined, except for the state of the error flags, after this error is recorded.");
		break;

		case GL_STACK_UNDERFLOW:
		puts(	"GL_STACK_UNDERFLOW:\nAn attempt has been made to perform an operation that would cause an internal stack to underflow.");
		break;

		case GL_STACK_OVERFLOW:
		puts(	"GL_STACK_OVERFLOW:\nAn attempt has been made to perform an operation that would cause an internal stack to overflow. ");
		break;

		default:
	}
	puts("");
	return;
}

void reloadVertices(int w, int h)
{ 
	float *img_coo;

	size_t img_coo_siz = 4*sizeof(float)*(w + 1)*h;
	img_coo = malloc(img_coo_siz);
	{
		size_t index = 0;
		for(int b = 0; b < h; b += 1)
		{
			for(int a = 0; a < w; a += 1)
			{
				img_coo[index++] = ((float) (2*a + 1 - w))/(w - 1);
				img_coo[index++] = ((float) (2*b + 1 - h))/(h - 1);
				img_coo[index++] = ((float) (2*a + 1 - w))/(w - 1);
				img_coo[index++] = ((float) (2*b + 3 - h))/(h - 1);
			}
			img_coo[index++] = 1.0;
			img_coo[index++] = ((float) (2*b + 3 - h))/(h - 1);
			img_coo[index++] = -1.0;
			img_coo[index++] = ((float) (2*b + 3 - h))/(h - 1);
		}
	}

	static uint32_t VertexArrayID = 0;
	glDeleteVertexArrays(1, &VertexArrayID);
	glGenVertexArrays(1, &VertexArrayID);
	glBindVertexArray(VertexArrayID);

	static uint32_t vertexbuffer = 0;
	glDeleteBuffers(1, &vertexbuffer);
	glGenBuffers(1, &vertexbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
	glBufferData(GL_ARRAY_BUFFER, img_coo_siz, img_coo, GL_STATIC_DRAW);
	free(img_coo);

	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
}

void reloadTexture(GeglRectangle *bound, GeglBuffer *input)
{ 
	char *src_buf;
	int w = bound->width;
	int h = bound->height;

	src_buf = malloc(w * h * 4);
	gegl_buffer_get
	(input, NULL, 1.0, babl_format("RGBA u8"), src_buf, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

	static uint32_t texture = 0;
	glDeleteTextures(1, &texture);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, src_buf);
	free(src_buf);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
}

void shaderTextAttach(int prog, const char *sourceText, int sort)
{
	int s = glCreateShader(sort);
	glShaderSource(s, 1, &sourceText, NULL);

	glCompileShader(s);
	int shaderCompileSuccess = GL_FALSE;
	glGetShaderiv(s, GL_COMPILE_STATUS, &shaderCompileSuccess);
	if(shaderCompileSuccess == GL_FALSE)
	{
		puts("Shader compilation error.");
		if(sourceText == NULL) puts("Shader source string is a NULL pointer");
		else
		{
			printf("Offending shader:\n\n%s\n", sourceText);
			char log[4096];
			glGetShaderInfoLog(s, 4096, NULL, log);
			int error = glGetError();
			if(error == GL_INVALID_OPERATION) puts("Shader object does not exist.");
			else if(error == GL_INVALID_VALUE) printf("Impossible shader value: %d.\n", prog);
			else printf("Info log:\n%s", log);
		}

		//raise(SIGTRAP);
	}

	glAttachShader(prog, s);
	return;
}

void reloadShaders(GeglOperation *operation, int prog)
{
	GeglProperties *o = GEGL_PROPERTIES(operation);

	static int vs = 0;
	static int fs = 0;
	glDetachShader(prog, vs);
	glDetachShader(prog, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	shaderTextAttach(prog, o->vst, GL_VERTEX_SHADER);
	shaderTextAttach(prog, o->fst, GL_FRAGMENT_SHADER);
}

void redraw(GeglOperation *operation)
{
	GeglProperties *o = GEGL_PROPERTIES(operation);
	GeglRectangle bound =
	*gegl_operation_source_get_bounding_box(operation, "input");

	State *state = o->user_data;
	int prog = state->prog;

	int a_loc = glGetUniformLocation(prog, "a");
	glUniform1f(a_loc, o->a_var);
	int b_loc = glGetUniformLocation(prog, "b");
	glUniform1f(b_loc, o->b_var);
	int c_loc = glGetUniformLocation(prog, "c");
	glUniform1f(c_loc, o->c_var);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 2*(o->xVert + 1)*o->yVert);

	char *dst_buf = state->dst_buf;
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	glReadPixels(0, 0, bound.width, bound.height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	dst_buf = (char *) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);

	//glFlush();
	state->dst_buf = dst_buf;
	return;
}

static void
prepare(GeglOperation *operation)
{
	gegl_operation_set_format(operation, "input", babl_format("RGBA u8"));
	gegl_operation_set_format(operation, "output", babl_format("RGBA u8"));

	GeglProperties *o = GEGL_PROPERTIES(operation);
	if(o->user_data == NULL) o->user_data = calloc(1, sizeof(State));
	State *state = o->user_data;
	if(state->engaged) redraw(operation);
}

void purge(GeglOperation *operation)
{
	GeglProperties *o = GEGL_PROPERTIES(operation);
	GeglRectangle bound =
	*gegl_operation_source_get_bounding_box(operation, "input");
	State *state = o->user_data;

	if(!glfwInit())
	{
		puts("GLFW Initialization failed.");
		raise(SIGTRAP);
	}

	static GLFWwindow* window = NULL;
	glfwDestroyWindow(window);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	window =
		glfwCreateWindow
		(bound.width, bound.height, "IRIS", NULL, NULL);
	if(window == NULL)
	{
		printf("%d %d\n", bound.width, bound.height);
		puts("Failed to create GLFW window.");
		raise(SIGTRAP);
	}
	glfwMakeContextCurrent(window);

	if(glewInit() != GLEW_OK)
	{
		puts("GLEW Initialization failed.");
		raise(SIGTRAP);
	}

	int prog = state->prog;
	glDeleteProgram(prog);
	prog = glCreateProgram();
	if(prog == 0)
	{
		puts("Program creation error.");
		raise(SIGTRAP);
	}

	reloadShaders(operation, prog);
	reloadVertices(o->xVert, o->yVert);

	static uint32_t pbo = 0;
	glDeleteBuffers(1, &pbo);
	glGenBuffers(1, &pbo);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
	glBufferData(GL_PIXEL_PACK_BUFFER, bound.width*bound.height*4, NULL, GL_STATIC_READ);

	glLinkProgram(prog);
	int linkSuccess = GL_FALSE;
	glGetProgramiv(prog, GL_LINK_STATUS, &linkSuccess);
	if(linkSuccess != GL_TRUE)
	{
		puts("Program link error.");
		char log[4096];
		glGetProgramInfoLog(prog, 4096, NULL, log);
		int error = glGetError();
		if(error == GL_INVALID_OPERATION) puts("Program object does not exist.");
		else if(error == GL_INVALID_VALUE) printf("Impossible program value: %d.\n", prog);
		else printf("Info log:\n%s", log);
		raise(SIGTRAP);
	}
	glUseProgram(prog);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	state->prog = prog;
}

static gboolean
process(GeglOperation       *operation,
		GeglBuffer          *input,
		GeglBuffer          *output,
		const GeglRectangle *result,
		gint                 level)
{
	GeglProperties *o = GEGL_PROPERTIES(operation);
	GeglRectangle bound =
	*gegl_operation_source_get_bounding_box(operation, "input");
	State *state = o->user_data;

	if((!state->purged && o->purge) || !state->engaged)
	{
		state->engaged = 1;
		purge(operation);
		reloadTexture(&bound, input);
		redraw(operation);
	}
	state->purged = o->purge;

	if(state->dst_buf == NULL)
	{
		puts("Received NULL pointer as destination buffer.");
		raise(SIGTRAP);
	}

	gegl_buffer_set(output, result, 0, babl_format("RGBA u8"),
					state->dst_buf + 4*(result->y*bound.width + result->x), 4*bound.width);

	return TRUE;
}

static void
gegl_op_class_init(GeglOpClass *klass)
{
	GeglOperationClass *operation_class;
	GeglOperationFilterClass *filter_class;

	operation_class = GEGL_OPERATION_CLASS(klass);
	filter_class = GEGL_OPERATION_FILTER_CLASS(klass);

	filter_class->process = process;
	operation_class->prepare = prepare;
	//operation_class->get_bounding_box = get_bounding_box;
	//operation_class->get_required_for_output = get_required_for_output;
	operation_class->threaded = FALSE;


	gegl_operation_class_set_keys(operation_class,
			"name"       , "gegl:ggt",
			"categories" , "map",
			"description",
			_("General geometric transformation."),
			NULL);
}

#endif
