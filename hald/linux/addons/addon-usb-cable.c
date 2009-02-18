/***************************************************************************
 * CVSID: $Id$
 *
 * addon-usb-cable.c : Poll the USB cable for state changes
 *
 * Copyright (C) 2007 Nokia Corporation
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

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <poll.h>

#include <glib.h>

#include "libhal/libhal.h"
#include "../../util_helper.h"
#include "../../logger.h"

LibHalContext *ctx;
char *udi;
char *button_type;

static int
emit_state (char *usb_mode)
{
	DBusError error;
	LibHalChangeSet *cs;
	char *cable_type;
	int cable_connected;

	cs = libhal_device_new_changeset (udi);
	if (cs == NULL) {
		HAL_ERROR (("Cannot initialize changeset"));
		return 0;
	}

	/* XXX: Map mode to old cable sysfs file values. To be deprecated. */
	if (strcmp(usb_mode, "a_host") == 0 ||
	    strcmp(usb_mode, "a_suspend") == 0) {
		cable_type = "Mini-A";
		cable_connected = TRUE;
	} else if (strcmp(usb_mode, "b_peripheral") == 0) {
		cable_type = "Mini-B";
		cable_connected = TRUE;
	} else {
		cable_type = "disconnected";
		cable_connected = FALSE;
	}

	libhal_changeset_set_property_string (cs, "usb_device.mode", usb_mode);
	libhal_changeset_set_property_string (cs, "usb_device.cable_type",
	                                      cable_type);
	libhal_changeset_set_property_bool (cs, "button.state.value",
	                                    cable_connected);

	dbus_error_init (&error);

	libhal_device_commit_changeset (ctx, cs, &error);
	libhal_device_free_changeset (cs);

	if (dbus_error_is_set (&error)) {
		HAL_ERROR (("Failed to set property: %s", error.message));
		dbus_error_free (&error);
		return 0;
	}

	libhal_device_emit_condition (ctx, udi, "ButtonPressed", button_type,
	                              &error);
	if (dbus_error_is_set (&error)) {
		HAL_ERROR (("Failed to send condition: %s", error.message));
		dbus_error_free (&error);
		return 0;
	}

	return 1;
}

static int
read_state (int fd)
{
	static char old_value[128] = "\0";
	char value[128];
	int r;

	memset (value, 0, sizeof (value));
	r = read (fd, value, sizeof (value));
	if (r < 0) {
		HAL_ERROR (("Could not read the value from the file"));
		return r;
	}

	/* XXX: The only glib dependency... */
	g_strchomp (value);

	if (strcmp (old_value, value) != 0) {
		emit_state (value);
		strcpy (old_value, value);
	}

	/* Rewind back to beginning, needed by sysfs property files */
	if (lseek (fd, 0, SEEK_SET) < 0) {
		HAL_ERROR (("Could not seek in the file"));
		return r;
	}

	return 0;
}

static int
watch_state (int fd)
{
	struct pollfd pollfd;
	int r;

	pollfd.fd = fd;
	pollfd.events = POLLIN | POLLPRI;
	pollfd.revents = 0;

	while (poll (&pollfd, 1, -1) == 1) {
		r = read_state (fd);
		if (r < 0)
			break;
	}

	return r;
}

int
main (int argc, char **argv)
{
	DBusError error;
	char *sysfs_path, *state_path;
	int fd;

	hal_set_proc_title_init (argc, argv);

	setup_logger();

	HAL_INFO (("addon-usb-cable running"));

	udi = getenv("UDI");
	if (!udi) {
		HAL_ERROR (("Failed to get UDI"));
		return 1;
	}

	dbus_error_init (&error);

	ctx = libhal_ctx_init_direct (&error);
	if (ctx == NULL) {
		HAL_ERROR (("Unable to initialise libhal context: %s",
			   error.message));
		return 1;
	}

	if (!libhal_device_addon_is_ready (ctx, udi, &error)) {
		HAL_INFO (("Device %s not ready yet: %s", udi, error.message));
		return 1;
	}

	sysfs_path = getenv ("HAL_PROP_LINUX_SYSFS_PATH");
	if (!sysfs_path) {
		HAL_ERROR (("Property linux.sysfs_path not set for device"));
		return 1;
	}

	HAL_INFO (("sysfs_path=%s", sysfs_path));

	button_type = getenv ("HAL_PROP_BUTTON_TYPE");
	if (!button_type) {
		HAL_ERROR (("Property button.type not set for device"));
		return 1;
	}

	libhal_device_set_property_bool (ctx, udi, "button.has_state", TRUE,
	                                 &error);

	if (!asprintf (&state_path, "%s/../mode", sysfs_path)) {
		HAL_ERROR (("Memory allocation problem"));
		return 1;
	}

	drop_privileges (0);

	hal_set_proc_title ("hald-addon-usb-cable: listening on %s", state_path);

	fd = open (state_path, O_RDONLY);
	if (fd < 0) {
		HAL_ERROR (("Failed to open '%s'", state_path));
		return 1;
	}

	free (state_path);

	read_state (fd);
	watch_state (fd);

	close (fd);

	return 0;
}
