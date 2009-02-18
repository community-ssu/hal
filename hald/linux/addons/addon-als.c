/***************************************************************************
 *
 * addon-als.c : Interface to the Ambient Light Sensors
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

/* Needed for vfscanf */
#define _ISOC99_SOURCE 1

// TODO: Add error checks to the dbus calls missing them.

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "libhal/libhal.h"
#include "../../logger.h"


#define ALS_OBJECT "/org/freedesktop/Hal/devices/als"
#define ALS_IFACE "org.freedesktop.Hal.Device.LightSensor"

/* definitions of the types we use to handle brightness values */
#define DBUS_BRIGHTNESS_T DBUS_TYPE_INT32
#define BRIGHTNESS_SCAN_CONVERSION_SPECIFIER "%d"
typedef int brightness_t;


/* FIXME: Major hack!! The device must not be hardcoded. */
static const char *ALS_SYSFS_BRIGHTNESS_TMPL =
  "/sys/class/hwmon/hwmon%d/device/lux";
static const char *ALS_SYSFS_BRIGHTNESS_FILE;


static gboolean
scan_file (const char *filename, const char *format, int expected_items, ...)
{
	FILE *fp;
	int scanned_items;
	va_list ap;

	fp = fopen(filename, "r");
	if (fp == NULL) {
		HAL_WARNING (("Could not open '%s': %s", filename, strerror(errno)));
		return FALSE;
	}

	va_start (ap, expected_items);
	scanned_items = vfscanf (fp, format, ap);
	va_end (ap);

	fclose (fp);

	return scanned_items == expected_items;
}

static gboolean
is_supported (void)
{
	int i;
	char filename[100];

	for (i = 0; i < 10; i++) {
		sprintf (filename, ALS_SYSFS_BRIGHTNESS_TMPL, i);
		if (access (filename, F_OK) == 0) {
			ALS_SYSFS_BRIGHTNESS_FILE = g_strdup(filename);
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
get_brightness (brightness_t *brightness)
{
	return scan_file (ALS_SYSFS_BRIGHTNESS_FILE,
	                  BRIGHTNESS_SCAN_CONVERSION_SPECIFIER,
	                  1, brightness);
}

static gboolean
send_dbus_method_return (DBusConnection *connection,
                         DBusMessage *method_call,
                         int dbus_type,
                         void *data)
{
	DBusMessage *method_return;

	method_return = dbus_message_new_method_return (method_call);
	if (method_return == NULL) {
		HAL_WARNING (("Out of memory"));
		return FALSE;
	}

	if (data != NULL) {
		dbus_message_append_args (method_return, dbus_type, data,
		                          DBUS_TYPE_INVALID);
	}

	if (!dbus_connection_send (connection, method_return, NULL)) {
		HAL_WARNING(("Could not send method return"));
		return FALSE;
	}

	dbus_connection_flush (connection);
	dbus_message_unref (method_return);

	return TRUE;
}

static DBusHandlerResult
filter_dbus (DBusConnection *connection,
             DBusMessage *message,
             void *user_data)
{
	DBusError error;
	DBusHandlerResult result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_error_init (&error);

	if (dbus_message_is_method_call (message, ALS_IFACE, "GetBrightness")) {
		brightness_t brightness;

		if (get_brightness (&brightness)) {
			send_dbus_method_return (connection,                                   message,
			                         DBUS_BRIGHTNESS_T,
			                         &brightness);
			result = DBUS_HANDLER_RESULT_HANDLED;
		}
	}

	return result;
}

static gboolean
initialize (void)
{
	DBusConnection *connection;
	DBusError error;
	LibHalContext *ctx = NULL;
	const char *udi;

	setup_logger ();

	dbus_error_init (&error);

	ctx = libhal_ctx_init_direct (&error);
	if (ctx == NULL) {
		HAL_WARNING (("Cannot connect to hald"));
		goto return_error;
	}

	connection = libhal_ctx_get_dbus_connection (ctx);
	if (connection == NULL) {
		HAL_WARNING (("Cannot get DBus connection"));
		goto return_error;
	}

	udi = getenv ("UDI");
	if (udi == NULL) {
		HAL_WARNING (("No device specified"));
		goto return_error;
	}

	if (!libhal_device_claim_interface (ctx,
	                                    ALS_OBJECT,
	                                    ALS_IFACE,
	                                    "    <method name=\"GetBrightness\">\n"
	                                    "      <arg name=\"brightness_value\" direction=\"out\" type=\"ai\"/>\n"
	                                    "    </method>\n",
	                                    &error)) {
		HAL_WARNING (("Cannot claim interface"));
		goto return_error;
	}

	if (!libhal_device_addon_is_ready (ctx, udi, &error)) {
		HAL_WARNING (("Addon not ready"));
		goto return_error;
	}

	dbus_connection_setup_with_g_main (connection, NULL);
	dbus_connection_add_filter (connection, filter_dbus, NULL, NULL);
	dbus_connection_set_exit_on_disconnect (connection, 0);

	return TRUE;

return_error:
	dbus_error_free (&error);

	return FALSE;
}

int
main (int argc, char* argv[])
{
	GMainLoop* main_loop;

	if (!is_supported ()) {
		HAL_ERROR (("ALS not supported. Exiting."));
		return EXIT_FAILURE;
	}

	if (!initialize ()) {
		return EXIT_FAILURE;
	}

	main_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (main_loop);

	return EXIT_SUCCESS;
}

