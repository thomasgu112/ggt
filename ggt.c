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

property_double (cx, _("Lens center X"), 0.05)
description (_("Coordinates of lens center"))
value_range (-1.0, 1.0)

property_double (cy, _("Lens center Y"), 0.05)
description (_("Coordinates of lens center"))
value_range (-1.0, 1.0)

property_double (rscale, _("Scale"), 0.5)
description (_("Scale of the image"))
value_range (0.0, 100.0)

property_double (angle, _("Angle"), 0.05)
description (_("Spin"))
value_range (-4.0, 4.0)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     ggt
#define GEGL_OP_C_SOURCE ggt.c

#include "gegl-op.h"
#include <stdio.h>

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
	GeglRectangle *bound =
	gegl_operation_source_get_bounding_box(operation, "input");

	gint X, Y;
	gdouble	t, x, y, z;
	guchar	*src_buf, *dst_buf;

	gint across =
	bound->width > bound->height ? bound->width : bound->height;
	gfloat rExtent = 1.0/across;
	gfloat cosi = cos(o->angle);
	gfloat sine = sin(o->angle);

	src_buf	= g_new0 (guchar, result->width * result->height * 4);
	dst_buf	= g_new0 (guchar, result->width * result->height * 4);

	gegl_buffer_get (input, result, 1.0, babl_format ("RGBA u8"),
			src_buf, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

	for (Y = result->y; Y < result->y + result->height; ++Y)
	for (X = result->x; X < result->x + result->width; ++X)
	{
		x = ((2*X - bound->width)*rExtent - o->cx)*o->rscale;
		y = ((2*Y - bound->height)*rExtent - o->cy)*o->rscale;
		if(x*x + y*y > 1.0) continue;

		//z = sqrt(1 - x*x - y*y);
		//t = x;
		//x = cosi*x + sine*z;
		//z = cosi*z - sine*t;

		//z = z*cosi - x*sine;
		//z = 1/(1 - z);
		//x = x*z; x = 0.5*(x + 1.0);
		//y = y*z; y = 0.5*(x + 1.0);
		

		gint offset = (Y - result->y) * result->width * 4 + (X - result->x) * 4;
		for (int j=0; j<4; j++)
			dst_buf[offset++] = 255;
	}

	gegl_buffer_set (output, result, 0, babl_format ("RGBA u8"),
			dst_buf, GEGL_AUTO_ROWSTRIDE);

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
	operation_class->get_bounding_box = get_bounding_box;
	operation_class->get_required_for_output = get_required_for_output;
	operation_class->threaded                = FALSE;

	gegl_operation_class_set_keys (operation_class,
			"name"       , "gegl:ggt",
			"categories" , "blur",
			"description",
			_("Copies image performing lens distortion correction."),
			NULL);
}

#endif
