/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright 2006 Øyvind Kolås <pippin@gimp.org>
 * Copyright 2008 Bradley Broom <bmbroom@gmail.com>
 * Copyright 2011 Robert Sasu <sasu.robert@gmail.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

property_double(xo, _("X Offset"), 0.0)
value_range(-1.0, 1.0)

property_double(yo, _("Y Offset"), 0.0)
value_range(-1.0, 1.0)

property_double(sz, _("Sphere Size"), 1.0)
value_range(0.0, 1.0)

property_double(ts, _("Texture Scale"), 1.0)
value_range(0.0, 10.0)

property_double(xAngle, _("x Angle"), 0.0)
value_range(-3.14, 3.14)

property_double(yAngle, _("y Angle"), 0.0)
value_range(-3.14, 3.14)

property_double(zAngle, _("z Angle"), 0.0)
value_range(-3.14, 3.14)

property_boolean(purge, _("Purge"), FALSE)
description("Flip this on if something is weird.")

#else
#define GEGL_OP_FILTER
#define GEGL_OP_NAME     ggt
#define GEGL_OP_C_SOURCE ggt.c

#include "gegl-op.h"
#include <math.h>
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
	puts("msg");
	raise(SIGTRAP);
	return;
}

void loadFile(const char *path, void **data, uint32_t *length)
{
	FILE *file = fopen(path, "r");
	if(file == NULL)
	{
		puts("File open error.");
		raise(SIGTRAP);
	}
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
	if(!sCompiled)
	{
		puts("Shader compilation error.");
		raise(SIGTRAP);
	}

	glAttachShader(prog, s);
	return s;
}

//this is stuff that should really only be run once per the user
//invoking the plugin, but because I'm not seeing a good way
//to do that it will be run at user discretion
void purge(GeglRectangle *bound, GeglBuffer *input)
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

	static uint32_t prog = 0;
	glDeleteProgram(prog);
	prog = glCreateProgram();

	static uint32_t vs = 0;
	static uint32_t fs = 0;
	glDetachShader(prog, vs);
	glDetachShader(prog, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	vs = shaderFileAttach(prog, "iris.vert", GL_VERTEX_SHADER);
	fs = shaderFileAttach(prog, "iris.frag", GL_FRAGMENT_SHADER);

	glLinkProgram(prog);
	glUseProgram(prog);

	static uint32_t texture = 0;
	glDeleteTextures(1, &texture);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, src_buf);
	free(src_buf);

	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	//glClear( GL_COLOR_BUFFER_BIT );
	//glDrawArrays(GL_TRIANGLE_STRIP, 0, 2*(w + 1)*h);
	//glDisableVertexAttribArray(0);
}

void rotate
(gfloat *a, gfloat *b, gfloat c, gfloat s)
{
	gfloat t = *a;
	*a = c*(*a) + s*(*b);
	*b = c*(*b) - s*t;
	return;
}

static GeglRectangle
get_bounding_box(GeglOperation *operation)
{
	GeglRectangle  result = {0,0,0,0};
	GeglRectangle *in_rect;

	in_rect = gegl_operation_source_get_bounding_box(operation, "input");
	if(!in_rect)
		return result;

	return *in_rect;
}

static GeglRectangle
get_required_for_output(GeglOperation       *operation,
		const gchar         *input_pad,
		const GeglRectangle *roi)
{
	return get_bounding_box(operation);
}

static void
prepare(GeglOperation *operation)
{
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

	static gboolean purged = FALSE;
	if(!purged && o->purge)
	{
		purge(bound, input);
		purged = TRUE;
	}
	purged = o->purge;

	//FILE *imgDump = fopen("/tmp/imgDump", "w");
	//if(imgDump == NULL)
	//{
	//	puts("File open error.");
	//	return 1;
	//}
	//fwrite(src_buf, 1, 4*bound.width*bound.height, imgDump);
	//g_free(src_buf);
	//return TRUE;

	gint inOffset, outOffset;
	gint X, Y;
	gfloat	t, x, y, z;

	gfloat rWidth = 1.0/bound.width;
	gfloat rHeight = 1.0/bound.height;
	gfloat rSize = 1.0/o->sz;

	gfloat xCos = cos(o->xAngle);
	gfloat xSin = sin(o->xAngle);
	gfloat yCos = cos(o->yAngle);
	gfloat ySin = sin(o->yAngle);
	gfloat zCos = cos(o->zAngle);
	gfloat zSin = sin(o->zAngle);

	for(Y = result->y; Y < result->y + result->height; ++Y)
	for(X = result->x; X < result->x + result->width; ++X)
	{
		x = ((2*X - bound.width)*rWidth - o->xo)*rSize;
		y = ((2*Y - bound.height)*rHeight - o->yo)*rSize;
		if(x*x + y*y > 1.0) continue;

		z = -sqrt(1.0 - x*x - y*y);
		rotate(&y, &z, xCos, xSin);
		rotate(&z, &x, yCos, ySin);
		rotate(&x, &y, zCos, zSin);
		//t = x;
		//x = cosi*x + sine*z;
		//z = cosi*z - sine*t;
		//if(z > 0.0) continue;

		//z = 1.0/(1.0 - z);
		//x = x*z;
		//y = y*z;
		t = sqrt(x*x + y*y);
		x *= 0.64*acos(-z)/t;
		y *= 0.64*acos(-z)/t;

		x = 0.5*(o->ts*x + 1.0);
		y = 0.5*(o->ts*y + 1.0);

		if(x < 0.0 || y < 0.0 || x > 1.0 || y > 1.0) continue;
		
		outOffset = (Y - result->y) * result->width * 4 + (X - result->x) * 4;

		//guchar color[4];
		//gegl_sampler_get
		//(sampler, x*bound.width, y*bound.height, NULL, color, GEGL_ABYSS_NONE);
		//for(int j=0; j<4; j++) dst_buf[outOffset + j] = color[j];

		inOffset  = y*bound.height;
		inOffset *= bound.width;
		inOffset += x*bound.width;
		inOffset *= 4;

		if(inOffset >= 4*bound.height*bound.width || inOffset < 0) continue;
		for(int j=0; j<4; j++) dst_buf[outOffset + j] = src_buf[inOffset + j];
	}

	gegl_buffer_set
	(output, result, 0, babl_format("RGBA u8"), dst_buf, GEGL_AUTO_ROWSTRIDE);

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
