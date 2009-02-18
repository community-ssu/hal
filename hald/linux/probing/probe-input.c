/***************************************************************************
 * CVSID: $Id$
 *
 * probe-input.c : Probe input devices
 *
 * Copyright (C) 2004 David Zeuthen, <david@fubar.dk>
 * Copyright (C) 2008, 2009 Nokia Corporation
 *
 * Licensed under the Academic Free License version 2.1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "../input_local.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libhal/libhal.h"
#include "../../logger.h"

/* we must use this kernel-compatible implementation */
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

int 
main (int argc, char *argv[])
{
	int fd;
	int ret;
	char *udi;
	char *device_file;
	char *button_type;
	int sw;
	LibHalContext *ctx = NULL;
	DBusError error;
	long bitmask[NBITS(SW_MAX)];

	/* assume failure */
	ret = 1;
	fd = -1;

	setup_logger ();

	button_type = getenv ("HAL_PROP_BUTTON_TYPE");
	if (button_type == NULL)
		goto out;

	device_file = getenv ("HAL_PROP_INPUT_DEVICE");
	if (device_file == NULL)
		goto out;

	udi = getenv ("UDI");
	if (udi == NULL)
		goto out;

	dbus_error_init (&error);
	if ((ctx = libhal_ctx_init_direct (&error)) == NULL)
		goto out;

	HAL_DEBUG (("Doing probe-input for %s (udi=%s)", device_file, udi));

	fd = open (device_file, O_RDONLY);
	if (fd < 0) {
		HAL_ERROR (("Cannot open %s: %s", device_file, strerror (errno)));
		goto out;
	}

	if (ioctl (fd, EVIOCGSW(sizeof (bitmask)), bitmask) < 0) {
		HAL_DEBUG (("ioctl EVIOCGSW failed"));
		goto out;
	}

	if (strcmp (button_type, "lid") == 0)
		sw = test_bit (SW_LID, bitmask);
	else if (strcmp (button_type, "tablet_mode") == 0)
		sw = test_bit (SW_TABLET_MODE, bitmask);
	else if (strcmp (button_type, "radio") == 0)
		sw = test_bit (SW_RADIO, bitmask);
	else if (strcmp (button_type, "jack_insert") == 0) {
		sw = test_bit (SW_HEADPHONE_INSERT, bitmask) ||
		     test_bit (SW_MICROPHONE_INSERT, bitmask) ||
		     test_bit (SW_LINEOUT_INSERT, bitmask) ||
		     test_bit (SW_VIDEOOUT_INSERT, bitmask) ||
		     test_bit (SW_JACK_PHYSICAL_INSERT, bitmask);
	} else
		goto out;

	dbus_error_init (&error);
	libhal_device_set_property_bool (ctx, udi, "button.state.value", sw, &error);
	
	ret = 0;

out:
	if (fd >= 0)
		close (fd);

	if (ctx != NULL) {
		dbus_error_init (&error);
		libhal_ctx_shutdown (ctx, &error);
		libhal_ctx_free (ctx);
	}

	return ret;
}
