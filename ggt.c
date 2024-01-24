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
	gl_Position.x = icv.x + a;\n\
	gl_Position.y = icv.y;\n\
	gl_Position.z = 0.0;\n\
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
\n\
void main()\n\
{\n\
	vec2 uv = 0.5*(icf - 1.0);\n\
	color = texture(sam, uv).rgba;\n\
}\n\
")
ui_meta("multiline", "true")

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
//optimize DMA/pixel buffer transfer

void reloadBuffers(GeglRectangle *bound, GeglBuffer *input)
{ 
	char *src_buf;
	float *img_coo;
	int32_t w = bound->width;
	int32_t h = bound->height;

	src_buf = malloc(w * h * 4);
	gegl_buffer_get
	(input, NULL, 1.0, babl_format("RGBA u8"), src_buf, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

	uint32_t img_coo_siz = 4*sizeof(float)*(w + 1)*h;
	img_coo = malloc(img_coo_siz);
	{
		uint32_t index = 0;
		for(int32_t b = 0; b < h; b += 1)
		{
			for(int32_t a = 0; a < w; a += 1)
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

	static uint32_t texture = 0;
	glDeleteTextures(1, &texture);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, src_buf);
	free(src_buf);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
}

void shaderTextAttach(uint32_t prog, const char *sourceText, uint32_t sort)
{
	int32_t s = glCreateShader(sort);
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

		raise(SIGTRAP);
	}

	glAttachShader(prog, s);
	return s;
}

void reloadShaders(GeglOperation *operation, int prog)
{
	GeglProperties *o = GEGL_PROPERTIES(operation);

	static uint32_t vs = 0;
	static uint32_t fs = 0;
	glDetachShader(prog, vs);
	glDetachShader(prog, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	shaderTextAttach(prog, o->vst, GL_VERTEX_SHADER);
	shaderTextAttach(prog, o->fst, GL_FRAGMENT_SHADER);
}

static void
prepare(GeglOperation *operation)
{
	GeglProperties *o = GEGL_PROPERTIES(operation);
	GeglRectangle *boundRef =
	gegl_operation_source_get_bounding_box(operation, "input");
	if(boundRef == NULL) return;
	GeglRectangle bound = *boundRef;

	gegl_operation_set_format(operation, "input", babl_format("RGBA u8"));
	gegl_operation_set_format(operation, "output", babl_format("RGBA u8"));

	static gboolean purged = FALSE;
	static int prog = 0;
	static int initialized = 0;
	if((!purged && o->purge) || !initialized)
	{
		initialized = 1;

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
			puts("Failed to create GLFW window.");
			raise(SIGTRAP);
		}
		glfwMakeContextCurrent(window);

		if(glewInit() != GLEW_OK)
		{
			puts("GLEW Initialization failed.");
			raise(SIGTRAP);
		}

		glDeleteProgram(prog);
		prog = glCreateProgram();
		if(prog == 0)
		{
			puts("Program creation error.");
			raise(SIGTRAP);
		}

		reloadShaders(operation, prog);
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
	}
	purged = o->purge;

	uint32_t a_loc = glGetUniformLocation(prog, "a");
	glUniform1f(a_loc, o->a_var);
	uint32_t b_loc = glGetUniformLocation(prog, "b");
	glUniform1f(b_loc, o->b_var);
	uint32_t c_loc = glGetUniformLocation(prog, "c");
	glUniform1f(c_loc, o->c_var);

	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 2*(bound.width + 1)*bound.height);
	return;
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

	static int prepped = 0;
	if(!prepped) prepare(operation);
	prepped = 1;

	guchar *dst_buf = g_new0(guchar, result->width * result->height * 4);

	static gboolean purged = FALSE;
	if(!purged && o->purge) reloadBuffers(&bound, input);
	purged = o->purge;

	glReadPixels(	result->x, result->y,
					result->width, result->height,
					GL_RGBA, GL_UNSIGNED_BYTE, dst_buf	);

	gegl_buffer_set(output, result, 0,
					babl_format("RGBA u8"),
					dst_buf, GEGL_AUTO_ROWSTRIDE);

	g_free(dst_buf);

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
