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
#include <math.h>


#ifdef GEGL_PROPERTIES

property_double (xo, _("X Offset"), 0.0)
value_range (-1.0, 1.0)

property_double (yo, _("Y Offset"), 0.0)
value_range (-1.0, 1.0)

property_double (sz, _("Sphere Size"), 1.0)
value_range (0.0, 1.0)

property_double (ts, _("Texture Scale"), 1.0)
value_range (0.0, 10.0)

property_double (xAngle, _("x Angle"), 0.0)
value_range (-3.14, 3.14)

property_double (yAngle, _("y Angle"), 0.0)
value_range (-3.14, 3.14)

property_double (zAngle, _("z Angle"), 0.0)
value_range (-3.14, 3.14)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     ggt
#define GEGL_OP_C_SOURCE ggt.c

#include "gegl-op.h"
#include <stdio.h>

void rotate
(gdouble *a, gdouble *b, gdouble c, gdouble s)
{
	gdouble t = *a;
	*a = c*(*a) + s*(*b);
	*b = c*(*b) - s*t;
	return;
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
	GeglRectangle  result = {0,0,0,0};
	GeglRectangle *in_rect;

	in_rect = gegl_operation_source_get_bounding_box (operation, "input");
	if (!in_rect)
		return result;

	return *in_rect;
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
		const gchar         *input_pad,
		const GeglRectangle *roi)
{
	return get_bounding_box (operation);
}

static void
prepare (GeglOperation *operation)
{
	gegl_operation_set_format (operation, "input", babl_format ("RGBA u8"));
	gegl_operation_set_format (operation, "output", babl_format ("RGBA u8"));
}

static gboolean
process (GeglOperation       *operation,
		GeglBuffer          *input,
		GeglBuffer          *output,
		const GeglRectangle *result,
		gint                 level)
{
	GeglProperties *o = GEGL_PROPERTIES (operation);
	GeglRectangle bound =
	*gegl_operation_source_get_bounding_box(operation, "input");
	GeglSampler *sampler;

	gint inOffset, outOffset;
	gint X, Y;
	gdouble	t, x, y, z;
	guchar	*src_buf, *dst_buf;

	gdouble rWidth = 1.0/bound.width;
	gdouble rHeight = 1.0/bound.height;
	gdouble rSize = 1.0/o->sz;

	gdouble xCos = cos(o->xAngle);
	gdouble xSin = sin(o->xAngle);
	gdouble yCos = cos(o->yAngle);
	gdouble ySin = sin(o->yAngle);
	gdouble zCos = cos(o->zAngle);
	gdouble zSin = sin(o->zAngle);

	//sampler = gegl_buffer_sampler_new(input, babl_format("RGBA u8"), GEGL_SAMPLER_NEAREST);

	dst_buf	= g_new0 (guchar, result->width * result->height * 4);
	src_buf	= g_new0 (guchar, bound.width * bound.height * 4);

	gegl_buffer_get
	(input, NULL, 1.0, babl_format("RGBA u8"), src_buf, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

	//FILE *imgDump = fopen("/tmp/imgDump", "w");
	//if(imgDump == NULL)
	//{
	//	puts("File open error.");
	//	return 1;
	//}
	//fwrite(src_buf, 1, 4*bound.width*bound.height, imgDump);
	//g_free(src_buf);
	//return TRUE;

	for (Y = result->y; Y < result->y + result->height; ++Y)
	for (X = result->x; X < result->x + result->width; ++X)
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
		//for (int j=0; j<4; j++) dst_buf[outOffset + j] = color[j];

		inOffset  = y*bound.height;
		inOffset *= bound.width;
		inOffset += x*bound.width;
		inOffset *= 4;

		printf("%d\n", inOffset);
		if(inOffset >= 4*bound.height*bound.width || inOffset < 0) continue;
		for (int j=0; j<4; j++) dst_buf[outOffset + j] = src_buf[inOffset + j];
	}

	gegl_buffer_set
	(output, result, 0, babl_format ("RGBA u8"), dst_buf, GEGL_AUTO_ROWSTRIDE);

	g_free (dst_buf);
	g_free (src_buf);

	return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
	GeglOperationClass       *operation_class;
	GeglOperationFilterClass *filter_class;

	operation_class = GEGL_OPERATION_CLASS (klass);
	filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

	filter_class->process = process;
	operation_class->prepare = prepare;
	//operation_class->get_bounding_box = get_bounding_box;
	//operation_class->get_required_for_output = get_required_for_output;
	operation_class->threaded                = FALSE;

	gegl_operation_class_set_keys (operation_class,
			"name"       , "gegl:ggt",
			"categories" , "blur",
			"description",
			_("Copies image performing lens distortion correction."),
			NULL);
}

#endif
