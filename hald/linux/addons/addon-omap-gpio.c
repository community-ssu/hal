/***************************************************************************
 *
 * addon-omap-gpio.c : Poll gpio switches for state changes
 *
 * Copyright (C) 2007, 2008 Nokia Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "libhal/libhal.h"

#include "../../logger.h"
#include "../../util_helper.h"


enum
{
	SWITCH_TYPE_UNKNOWN = 0,
	SWITCH_TYPE_COVER,
	SWITCH_TYPE_CONNECTION,
	SWITCH_TYPE_ACTIVITY,
};

gchar *switch_type_names[] =
{
	"unknown",
	"cover",
	"connection",
	"activity"
};

gchar *switch_type_states[][2] =
{
	{"", ""},
	{"open", "closed"},
	{"disconnected", "connected"},
	{"inactive", "active"}
};

typedef struct _SwitchDesc SwitchDesc;
struct _SwitchDesc
{
	gint type;
	gboolean state;
	gchar udi[1];
};

LibHalContext *ctx = NULL;
GMainLoop *main_loop;
static GHashTable *switches = NULL;
static GList *devices = NULL;

static int
gpio_switch_str_to_bool (gint type, gchar *state_str, gboolean *state_bool)
{
	int error = 0;
	gchar *str;

	str = g_strchomp (state_str);
	if (0 == strcmp (str, switch_type_states[type][1]))
		*state_bool = TRUE;
	else if (0 == strcmp (str, switch_type_states[type][0]))
		*state_bool = FALSE;
	else {
		HAL_ERROR (("unknown switch state %s", str));
		error = 1;
	}

	return error;
}

static gboolean
gpio_process_switch (SwitchDesc *desc, gchar *state_str, gsize len)
{
	gboolean new_state;

	if (gpio_switch_str_to_bool (desc->type, state_str, &new_state))
		return TRUE;

	HAL_DEBUG (("new_state=%d", new_state));
	if (new_state != desc->state) {
		DBusError error;

		dbus_error_init (&error);
		libhal_device_set_property_bool (ctx, desc->udi,
			"button.state.value", new_state,
			&error);

		if (dbus_error_is_set (&error)) {
			HAL_ERROR (("Failed to set property: %s",
				   error.message));
			dbus_error_free (&error);
			return FALSE;
		}

		dbus_error_init (&error);
		libhal_device_emit_condition (ctx, desc->udi,
			"ButtonPressed", switch_type_names[desc->type],
			&error);
		if (dbus_error_is_set (&error)) {
			HAL_ERROR (("Failed to send condition: %s",
				   error.message));
			dbus_error_free (&error);
			return FALSE;
		}
		desc->state = new_state;
	}

	return TRUE;
}

static gboolean
gpio_state_changed (GIOChannel *source,
		    GIOCondition condition,
		    gpointer data)
{
	SwitchDesc *desc = (SwitchDesc*) data;
	GIOStatus ret;

	HAL_DEBUG (("%s state changed, condition=%d", desc->udi, condition));
	if (condition & G_IO_IN || condition & G_IO_PRI) {
		gchar *state_str = NULL;
		gsize len;
		GError *gerror = NULL;

		HAL_DEBUG (("Reading line"));
		ret = g_io_channel_read_line (source, &state_str, &len, NULL,
					      &gerror);

		HAL_DEBUG (("read line: str=%s, len=%d", state_str, len));
		g_io_channel_seek_position (source, 0, G_SEEK_SET, &gerror);
		if (state_str)
			return gpio_process_switch (desc, state_str, len);
	} else if (condition & G_IO_ERR) {
		HAL_ERROR (("Error on %s", desc->udi));
		g_io_channel_unref (source);
		return FALSE;
	} else {
		HAL_ERROR (("unknown GIOCondition: %d", condition));
		g_io_channel_unref (source);
		return FALSE;
	}

	return TRUE;
}

static void
update_proc_title (void)
{
	GList *lp;
	gchar *new_command_line, *p;
	gint len = 0;

	for (lp = devices; lp; lp = lp->next)
		len = len + strlen (lp->data) + 1;

	len += strlen ("hald-addon-gpio: listening on");

	new_command_line = g_malloc (len + 1);
	p = new_command_line;
	p += g_sprintf (new_command_line, "hald-addon-gpio: listening on");

	for (lp = g_list_last (devices); lp; lp = g_list_previous (lp))
		p += g_sprintf (p, " %s", (gchar*) lp->data);

	hal_set_proc_title (new_command_line);
	g_free (new_command_line);
}

static void
destroy_data (SwitchDesc *desc)
{
	HAL_DEBUG (("GPIO device '%s' destroyed, free data.", desc->udi));

	g_free (desc);
}

static int
read_line_buffer (const char *filename, char *buf, int len)
{
	FILE *fp;
	int r;

	fp = fopen (filename, "r");
	if (!fp) {
		HAL_ERROR (("Failed to open '%s'", filename));
		return 1;
	}
	/* FIXME: should use len instead of hardcoded 31! */
	r = fscanf (fp, "%31s", buf);
	fclose (fp);

	if (r == 0) {
		HAL_ERROR (("Failed to read file '%s' contents", filename));
		return 1;
	}

	return 0;
}

static void
add_device (LibHalContext *ctx, const char *udi,
            const LibHalPropertySet *properties)
{
	GIOChannel *channel;
	GIOStatus ret;
	GError *gerror = NULL;
	DBusError error;
	SwitchDesc *desc;
	int udi_len = strlen (udi);
	const char *sysfs_path;
	char *type_path, *device_file;
	char type[32], state[32];
	size_t dump_len;
	char *dump = NULL;

	desc = (SwitchDesc *) g_malloc (sizeof (SwitchDesc) + udi_len);

	memcpy (&(desc->udi), udi, udi_len + 1);

	sysfs_path = libhal_ps_get_string (properties, "linux.sysfs_path");
	if (!sysfs_path) {
		HAL_ERROR (("%s has no property linux.sysfs_path", udi));
		return;
	}

	HAL_INFO (("sysfs_path=%s", sysfs_path));

	/* Determine switch type. */
	type_path = g_strconcat (sysfs_path, "/type", NULL);
	if (read_line_buffer (type_path, type, sizeof (type) - 1)) {
		HAL_ERROR (("Unable to determine switch type for %s", udi));
		g_free (type_path);
		g_free (desc);
		return;
	}
	g_free (type_path);

	if (0 == strcmp (type, "activity"))
		desc->type = SWITCH_TYPE_ACTIVITY;
	else if (0 == strcmp (type, "connection"))
		desc->type = SWITCH_TYPE_CONNECTION;
	else if (0 == strcmp (type, "cover"))
		desc->type = SWITCH_TYPE_COVER;
	else {
		HAL_ERROR (("Unknown switch type %s", type));
		g_free (desc);
		return;
	}

	HAL_INFO (("type = %d", desc->type));

	/* Initialize button keys. */
	dbus_error_init (&error);
	libhal_device_set_property_string (ctx, desc->udi,
	                                   "button.type", type, &error);
	libhal_device_set_property_bool (ctx, desc->udi,
	                                 "button.has_state", TRUE, &error);

	/* Initialize state handling */
	device_file = g_strconcat (sysfs_path, "/state", NULL);

	if (read_line_buffer (device_file, state, sizeof (state) - 1)) {
		HAL_ERROR (("Unable to determine switch type for %s", udi));
		g_free (device_file);
		g_free (desc);
		return;
	}
	if (gpio_switch_str_to_bool (desc->type, state, &desc->state)) {
		g_free (device_file);
		g_free (desc);
		return;
	}

	libhal_device_set_property_bool (ctx, desc->udi,
	                                 "button.state.value", desc->state, &error);

	HAL_DEBUG (("%s: listening on %s", udi, device_file));

	devices = g_list_prepend (devices, g_strdup (device_file));
	update_proc_title ();

	channel = g_io_channel_new_file (device_file, "r", &gerror);
	if (channel == NULL) {
		HAL_ERROR (("g_io_channel_new_file() for %s failed: %s",
		           device_file, gerror->message));
		g_error_free (gerror);
		g_free (device_file);
		g_free (desc);
		return;
	}
	g_free (device_file);

	g_io_channel_set_encoding (channel, NULL, NULL);

	/* Have to read the contents even though it's not used */
	ret = g_io_channel_read_to_end (channel, &dump, &dump_len, &gerror);
	if (ret == G_IO_STATUS_NORMAL)
		g_free (dump);
	if (gerror != NULL) {
		HAL_ERROR (("g_io_channel_read_to_end(): %s", gerror->message));
		g_error_free (gerror);
	}

	g_io_channel_seek_position (channel, 0, G_SEEK_SET, &gerror);
	if (gerror != NULL) {
		HAL_ERROR (("g_io_channel_seek_position(): %s", gerror->message));
		g_error_free (gerror);
	}

	/* Setup the watch. */
	g_hash_table_insert (switches, g_strdup (udi), channel);
	g_io_add_watch_full (channel, G_PRIORITY_DEFAULT,
	                     G_IO_IN | G_IO_PRI | G_IO_ERR,
	                     gpio_state_changed, desc,
	                     (GDestroyNotify) destroy_data);
}

static void
remove_device (LibHalContext *ctx, const char *udi,
               const LibHalPropertySet *properties)
{
	GIOChannel *channel, **p_channel = &channel;
	GList *lp;
	gboolean handling_udi;
	const char *sysfs_path;
	char *device_file;

	HAL_DEBUG (("Removing channel for '%s'", udi));

	handling_udi = g_hash_table_lookup_extended (switches, udi, NULL, (gpointer *)p_channel);

	if (!handling_udi) {
		HAL_ERROR(("DeviceRemove called for unknown device: '%s'.", udi));
		return;
	}

	if (channel) {
		g_io_channel_shutdown (channel, FALSE, NULL);
		g_io_channel_unref (channel);
	}

	g_hash_table_remove (switches, udi);

	sysfs_path = libhal_ps_get_string (properties, "linux.sysfs_path");
	if (!sysfs_path) {
		HAL_ERROR (("%s has no property linux.sysfs_path", udi));
		return;
	}

	device_file = g_strconcat (sysfs_path, "/state", NULL);

	lp = g_list_find_custom (devices, device_file, (GCompareFunc) strcmp);
	if (lp) {
		devices = g_list_remove_link (devices, lp);
		g_free (lp->data);
		g_list_free_1 (lp);
		update_proc_title ();
	}

	if (g_hash_table_size (switches) == 0) {
		HAL_INFO (("no more devices, exiting"));
		g_main_loop_quit (main_loop);
	}
}

int
main (int argc, char **argv)
{
	DBusConnection *dbus_connection;
	DBusError error;
	const char *commandline;

	hal_set_proc_title_init (argc, argv);

	setup_logger ();

	HAL_INFO (("addon-omap-gpio running"));

	dbus_error_init (&error);
	ctx = libhal_ctx_init_direct (&error);
	if (ctx == NULL) {
		HAL_ERROR (("Unable to initialise libhal context: %s",
			   error.message));
		return 1;
	}

	if ((dbus_connection = libhal_ctx_get_dbus_connection (ctx)) == NULL) {
		HAL_WARNING (("Cannot get DBus connection"));
		return 1;
	}

	if ((commandline = getenv ("SINGLETON_COMMAND_LINE")) == NULL) {
		HAL_WARNING (("SINGLETON_COMMAND_LINE not set"));
		return 1;
	}

	libhal_ctx_set_singleton_device_added (ctx, add_device);
	libhal_ctx_set_singleton_device_removed (ctx, remove_device);

	dbus_connection_setup_with_g_main (dbus_connection, NULL);
	dbus_connection_set_exit_on_disconnect (dbus_connection, 0);

	dbus_error_init (&error);
	if (!libhal_device_singleton_addon_is_ready (ctx, commandline, &error)) {
		HAL_INFO (("Singleton %s not ready yet: %s", commandline, error.message));
		return 1;
	}

	drop_privileges (0);

	switches = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	main_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (main_loop);

	return 0;
}

