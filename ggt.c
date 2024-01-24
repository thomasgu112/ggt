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
	gl_Position.y = icv.y + b;\n\
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
//runtime shader swapping
//shader compilation error reporting

void debug(char *msg)
{
	puts(msg);
	raise(SIGTRAP);
	return;
}

void loadFile(const char *path, void **data, uint32_t *length)
{
	FILE *file = fopen(path, "r");
	if(file == NULL) debug("File open error.");
	fseek(file, 0, SEEK_END);
	*length = ftell(file);
	fseek(file, 0, SEEK_SET);
	*data = malloc(*length);
	fread(*data, 1, *length, file);
	fclose(file);
}

uint32_t shaderFileAttach(uint32_t prog, const char *path, uint32_t sort)
{
	char *sourceText; uint32_t len;
	loadFile(path, &sourceText, &len);

	uint32_t s = glCreateShader(sort);
	glShaderSource(s, 1, &sourceText, &len);
	free(sourceText);

	glCompileShader(s);
	uint32_t sCompiled;
	glGetShaderiv(s, GL_COMPILE_STATUS, &sCompiled);
	if(!sCompiled) debug("Shader compilation error.");

	glAttachShader(prog, s);
	return s;
}

uint32_t shaderTextAttach(uint32_t prog, const char *sourceText, uint32_t sort)
{
	uint32_t s = glCreateShader(sort);
	glShaderSource(s, 1, &sourceText, NULL);

	glCompileShader(s);
	uint32_t sCompiled;
	glGetShaderiv(s, GL_COMPILE_STATUS, &sCompiled);
	if(!sCompiled)
	{
		printf("Shader compilation error. Offending shader:\n%s", sourceText);
		raise(SIGTRAP);
	}

	glAttachShader(prog, s);
	return s;
}

uint32_t initialized = 0;
uint32_t prog = 0;

//this is stuff that should really only be run once per the user
//invoking the plugin, but because I'm not seeing a good way
//to do that it will be run at user discretion
void purge(GeglRectangle *bound, GeglBuffer *input, char *vst, char *fst)
{ 
	char *src_buf;
	float *img_coo;
	int32_t w = bound->width;
	int32_t h = bound->height;

	if(!glfwInit()) debug("GLFW Initialization failed.");

	static GLFWwindow* window = NULL;
	glfwDestroyWindow(window);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	window = glfwCreateWindow(w, h, "IRIS", NULL, NULL);
	if(window == NULL) debug("Failed to create GLFW window.");
	glfwMakeContextCurrent(window);

	if(glewInit() != GLEW_OK) puts("GLEW Initialization failed.");

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

	glDeleteProgram(prog);
	prog = glCreateProgram();

	static uint32_t vs = 0;
	static uint32_t fs = 0;
	glDetachShader(prog, vs);
	glDetachShader(prog, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	vs = shaderTextAttach(prog, vst, GL_VERTEX_SHADER);
	fs = shaderTextAttach(prog, fst, GL_FRAGMENT_SHADER);

	glLinkProgram(prog);
	glUseProgram(prog);

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

	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 2*(w + 1)*h);
	//glDisableVertexAttribArray(0);
	initialized = 1;
}

static void
prepare(GeglOperation *operation)
{
	if(initialized)
	{
		GeglProperties *o = GEGL_PROPERTIES(operation);
		GeglRectangle bound =
		*gegl_operation_source_get_bounding_box(operation, "input");

		uint32_t a_loc = glGetUniformLocation(prog, "a");
		glUniform1f(a_loc, o->a_var);
		uint32_t b_loc = glGetUniformLocation(prog, "b");
		glUniform1f(b_loc, o->b_var);
		uint32_t c_loc = glGetUniformLocation(prog, "c");
		glUniform1f(c_loc, o->c_var);

		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 2*(bound.width + 1)*bound.height);
	}

	gegl_operation_set_format(operation, "input", babl_format("RGBA u8"));
	gegl_operation_set_format(operation, "output", babl_format("RGBA u8"));
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
	//GeglSampler *sampler;
	//sampler = gegl_buffer_sampler_new(input, babl_format("RGBA u8"), GEGL_SAMPLER_NEAREST);

	//there is no straightforward way apparent to me to run code
	//exactly once for each time the base image changes, nor when
	//the deed is done, so to prevent memory leaks as well as
	//having to construct a pixel buffer over and over I make use
	//of a static
	//the source image buffer is purged at user discretion which
	//I think is a fine solution
	guchar *dst_buf = g_new0(guchar, result->width * result->height * 4);

	printf(o->fst);

	static gboolean purged = FALSE;
	if(!purged && o->purge) purge(&bound, input, o->vst, o->fst);
	purged = o->purge;

	glReadPixels(	result->x, result->y,
					result->width, result->height,
					GL_RGBA, GL_UNSIGNED_BYTE, dst_buf	);

	gegl_buffer_set(output, result, 0,
					babl_format("RGBA u8"),
					dst_buf, GEGL_AUTO_ROWSTRIDE);

	g_free(dst_buf);
	//g_free(src_buf);

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
