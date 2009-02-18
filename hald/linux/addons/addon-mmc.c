/***************************************************************************
 *
 * addon-mmc.c : Poll mmc cover switch for state changes
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "libhal/libhal.h"
#include "../../logger.h"
#include "../../util_helper.h"


gchar *switch_type_states[2] =
{
	"open", "closed",
};

typedef struct _SwitchDesc SwitchDesc;
struct _SwitchDesc
{
	gchar *udi;
	gchar *type;
	gboolean state;
};

LibHalContext *ctx = NULL;

static gint
setup_sysfs_poll (const char *file, GIOFunc sysfs_file_cb, SwitchDesc *desc)
{
	GIOChannel *gioch;
	GError *error = NULL;
	gsize len;
	gchar *dump = NULL;
	GIOStatus ret;

	gioch = g_io_channel_new_file (file, "r", &error);
	if (gioch == NULL) {
		HAL_ERROR (("g_io_channel_new_file() for %s failed: %s", file,
			   error->message));
		g_error_free (error);
		return -1;
	}

	/* Have to read the contents even though it's not used */
	ret = g_io_channel_read_to_end (gioch, &dump, &len, &error);
	if (ret == G_IO_STATUS_NORMAL) {
		g_free (dump);
	}
	if (error != NULL) {
		HAL_ERROR (("g_io_channel_read_to_end(): %s", error->message));
		g_error_free (error);
	}

	g_io_channel_seek_position (gioch, 0, G_SEEK_SET, &error);
	if (error != NULL) {
		HAL_ERROR (("g_io_channel_seek_position(): %s", error->message));
		g_error_free (error);
	}

	return g_io_add_watch (gioch, G_IO_IN | G_IO_PRI | G_IO_ERR,
			       sysfs_file_cb, (gpointer)desc);
}

static gboolean
process_switch_state (SwitchDesc *desc, gchar *state_str, gsize len)
{
	gboolean new_state;
	gchar *str;

	str = g_strchomp (state_str);
	if (0 == strncmp (str, switch_type_states[1], len))
		 new_state = TRUE;
	else if (0 == strncmp (str, switch_type_states[0], len))
		new_state = FALSE;
	else {
		HAL_ERROR (("unknown switch state %s", str));
		return TRUE;
	}

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
			"ButtonPressed", desc->type,
			&error);
		if (dbus_error_is_set (&error)) {
			HAL_ERROR (("Failed to send condition: %s",
				   error.message));
			dbus_error_free (&error);
			return FALSE;
		}
		desc->state = new_state;
	}
	g_free (str);

	return TRUE;
}

static gboolean
switch_state_changed (GIOChannel *source,
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
			return process_switch_state (desc, state_str, len);
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

int
main (int argc, char **argv)
{
	DBusError error;
	LibHalChangeSet *cs;
	GMainLoop *main_loop;
	gchar *udi, *sysfs_path, *state_path;
	gchar state[32];
	SwitchDesc *desc;
	FILE *fp;

	hal_set_proc_title_init (argc, argv);

	setup_logger();

	HAL_INFO (("addon-mmc running"));

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

	dbus_error_init (&error);
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
	desc = g_new (SwitchDesc, 1);

	desc->udi = udi;
	desc->type = "cover";

	HAL_INFO (("type=%d", desc->type));
	state_path = g_strconcat (sysfs_path, "/cover_switch", NULL);
	fp = fopen (state_path, "r");
	if (!fp) {
		HAL_ERROR (("Failed to open %s", state_path));
		return 1;
	}

	if (fscanf (fp, "%31s", state) == 0) {
		HAL_ERROR (("Unable to determine switch state for %s", udi));
		return 1;
	}

	if (0 == strcmp (state, switch_type_states[1]))
		desc->state = TRUE;
	else if (0 == strcmp (state, switch_type_states[0]))
		desc->state = FALSE;
	else {
		HAL_ERROR(("unknown switch state %s", state));
		return 1;
	}

	fclose (fp);

	HAL_INFO (("device type=%s, state=%d", desc->type, desc->state));

	cs = libhal_device_new_changeset (desc->udi);
	if (cs == NULL) {
		HAL_ERROR (("Cannot initialize changeset"));
		return 1;
	}

	libhal_changeset_set_property_string (cs, "button.type", desc->type);
	libhal_changeset_set_property_bool (cs, "button.has_state", TRUE);
	libhal_changeset_set_property_bool (cs, "button.state.value", desc->state);

	libhal_device_commit_changeset (ctx, cs, &error);
	libhal_device_free_changeset (cs);

	drop_privileges (0);

	hal_set_proc_title ("hald-addon-mmc: listening on %s", state_path);

	setup_sysfs_poll (state_path, switch_state_changed, desc);

	g_free (state_path);

	main_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (main_loop);

	return 0;
}

