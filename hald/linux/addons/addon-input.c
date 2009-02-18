/***************************************************************************
 * CVSID: $Id$
 *
 * addon-input.c : Listen to key events and modify hal device objects
 *
 * Copyright (C) 2005 David Zeuthen, <david@fubar.dk>
 * Copyright (C) 2005 Ryan Lortie <desrt@desrt.ca>
 * Copyright (C) 2006 Matthew Garrett <mjg59@srcf.ucam.org>
 * Copyright (C) 2007 Codethink Ltd. Author Rob Taylor <rob.taylor@codethink.co.uk>
 * Copyright (C) 2008, 2009 Nokia Corporation
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
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../input_local.h"

#include <glib/gmain.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "libhal/libhal.h"

#include "../../logger.h"
#include "../../util_helper.h"


static char *key_name[KEY_MAX + 1] = {
	[0 ... KEY_MAX] = NULL,
	[KEY_STOP] = "stop",
	[KEY_AGAIN] = "again",
	[KEY_PROPS] = "props",
	[KEY_UNDO] = "undo",
	[KEY_FRONT] = "front",
	[KEY_COPY] = "copy",
	[KEY_OPEN] = "open",
	[KEY_PASTE] = "paste",
	[KEY_FIND] = "find",
	[KEY_CUT] = "cut",
	[KEY_HELP] = "help",
	[KEY_MENU] = "menu",
	[KEY_CALC] = "calc",
	[KEY_SETUP] = "setup",
	[KEY_SLEEP] = "sleep",
	[KEY_WAKEUP] = "wake-up",
	[KEY_FILE] = "file",
	[KEY_SENDFILE] = "send-file",
	[KEY_DELETEFILE] = "delete-file",
	[KEY_XFER] = "xfer",
	[KEY_PROG1] = "prog1",
	[KEY_PROG2] = "prog2",
	[KEY_WWW] = "www",
	[KEY_MSDOS] = "msdos",
	[KEY_COFFEE] = "coffee",
	[KEY_DIRECTION] = "direction",
	[KEY_CYCLEWINDOWS] = "cycle-windows",
	[KEY_MAIL] = "mail",
	[KEY_BOOKMARKS] = "bookmarks",
	[KEY_COMPUTER] = "computer",
	[KEY_BACK] = "back",
	[KEY_FORWARD] = "forward",
	[KEY_CLOSECD] = "close-cd",
	[KEY_EJECTCD] = "eject-cd",
	[KEY_EJECTCLOSECD] = "eject-close-cd",
	[KEY_NEXTSONG] = "next-song",
	[KEY_PLAYPAUSE] = "play-pause",
	[KEY_PREVIOUSSONG] = "previous-song",
	[KEY_STOPCD] = "stop-cd",
	[KEY_RECORD] = "record",
	[KEY_REWIND] = "rewind",
	[KEY_PHONE] = "phone",
	[KEY_ISO] = "iso",
	[KEY_CONFIG] = "config",
	[KEY_HOMEPAGE] = "homepage",
	[KEY_REFRESH] = "refresh",
	[KEY_EXIT] = "exit",
	[KEY_MOVE] = "move",
	[KEY_EDIT] = "edit",
	[KEY_SCROLLUP] = "scroll-up",
	[KEY_SCROLLDOWN] = "scroll-down",
	[KEY_KPLEFTPAREN] = "kp-left-paren",
	[KEY_KPRIGHTPAREN] = "kp-right-paren",
	[KEY_F13] = "f13",
	[KEY_F14] = "f14",
	[KEY_F15] = "f15",
	[KEY_F16] = "f16",
	[KEY_F17] = "f17",
	[KEY_F18] = "f18",
	[KEY_F19] = "f19",
	[KEY_F20] = "f20",
	[KEY_F21] = "f21",
	[KEY_F22] = "f22",
	[KEY_F23] = "f23",
	[KEY_F24] = "f24",
	[KEY_PLAYCD] = "play-cd",
	[KEY_PAUSECD] = "pause-cd",
	[KEY_PROG3] = "prog3",
	[KEY_PROG4] = "prog4",
	[KEY_SUSPEND] = "hibernate",
	[KEY_CLOSE] = "close",
	[KEY_PLAY] = "play",
	[KEY_FASTFORWARD] = "fast-forward",
	[KEY_BASSBOOST] = "bass-boost",
	[KEY_PRINT] = "print",
	[KEY_HP] = "hp",
	[KEY_CAMERA] = "camera",
	[KEY_SOUND] = "sound",
	[KEY_QUESTION] = "question",
	[KEY_EMAIL] = "email",
	[KEY_CHAT] = "chat",
	[KEY_SEARCH] = "search",
	[KEY_CONNECT] = "connect",
	[KEY_FINANCE] = "finance",
	[KEY_SPORT] = "sport",
	[KEY_SHOP] = "shop",
	[KEY_ALTERASE] = "alt-erase",
	[KEY_CANCEL] = "cancel",
	[KEY_BRIGHTNESSDOWN] = "brightness-down",
	[KEY_BRIGHTNESSUP] = "brightness-up",
	[KEY_MEDIA] = "media",
	[KEY_POWER] = "power",
	[KEY_MUTE] = "mute",
	[KEY_VOLUMEDOWN] = "volume-down",
	[KEY_VOLUMEUP] = "volume-up",
	[KEY_SWITCHVIDEOMODE] = "switch-videomode",
	[KEY_KBDILLUMTOGGLE] = "kbd-illum-toggle",
	[KEY_KBDILLUMDOWN] = "kbd-illum-down",
	[KEY_KBDILLUMUP] = "kbd-illum-up",
	[KEY_BATTERY] = "battery",
	[KEY_BLUETOOTH] = "bluetooth",
	[KEY_WLAN] = "wlan"
};

/* we must use this kernel-compatible implementation */
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

typedef struct _InputData InputData;
struct _InputData
{
	char *button_type;
	gboolean button_has_state;
	gboolean button_state;
	gboolean button_new_state;
	gboolean is_jack;
	char udi[1];			/*variable size*/
};

static LibHalContext *ctx = NULL;
static GMainLoop *gmain = NULL;
static GHashTable *inputs = NULL;
static GList *devices = NULL;

static void
emit_switch_condition (InputData *input_data)
{
	if (input_data->button_new_state != input_data->button_state) {
		DBusError error;

		input_data->button_state = input_data->button_new_state;
		input_data->button_new_state = 0;

		dbus_error_init (&error);
		libhal_device_set_property_bool (ctx, input_data->udi, "button.state.value",
						 input_data->button_state, &error);

		dbus_error_init (&error);
		libhal_device_emit_condition (ctx, input_data->udi,
					      "ButtonPressed",
					      input_data->button_type,
					      &error);
		dbus_error_free (&error);
	}
}

static void
set_jack_switch_type (const char *udi, const char *name, gboolean state)
{
	DBusError error;

	dbus_error_init (&error);
	if (state)
		libhal_device_property_strlist_append (ctx, udi, "input.jack.type", name, &error);
	else
		libhal_device_property_strlist_remove (ctx, udi, "input.jack.type", name, &error);
	dbus_error_free (&error);
}

static gboolean
event_io (GIOChannel *channel, GIOCondition condition, gpointer data)
{
	InputData *input_data = (InputData*) data;
	struct input_event event;
	DBusError error;
	GError *gerror = NULL;
	gsize read_bytes;

	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL))
		return FALSE;

	/* The kernel guarantees to always provide a complete event. */
	while (g_io_channel_read_chars (channel,
			((gchar *)&event), sizeof (struct input_event),
			&read_bytes, &gerror) == G_IO_STATUS_NORMAL) {

		if (read_bytes < sizeof (struct input_event)) {
			HAL_ERROR (("incomplete input event read"));
			return FALSE;
		}

		if (input_data->button_has_state &&
		    event.type == EV_SW) {
			long bitmask[NBITS(SW_MAX)];

			HAL_INFO (("%s: event.value=%d ; event.code=%d (0x%02x)",
				   input_data->udi,
				   event.value, event.code, event.code));

			switch (event.code) {
			case SW_LID:
			case SW_TABLET_MODE:
			case SW_RFKILL_ALL:
				/* check switch state - cuz apparently we get spurious events (or I don't know
				 * how to use the input layer correctly)
				 *
				 * Lid close:
				 * 19:08:22.911 [I] event.value=1 ; event.code=0 (0x00)
				 * 19:08:22.914 [I] event.value=0 ; event.code=0 (0x00)
				 *
				 * Lid open:
				 * 19:08:26.772 [I] event.value=0 ; event.code=0 (0x00)
				 * 19:08:26.776 [I] event.value=0 ; event.code=0 (0x00)
				 * 19:08:26.863 [I] event.value=1 ; event.code=0 (0x00)
				 * 19:08:26.868 [I] event.value=0 ; event.code=0 (0x00)
				 * 19:08:26.955 [I] event.value=0 ; event.code=0 (0x00)
				 * 19:08:26.960 [I] event.value=0 ; event.code=0 (0x00)
				 *
				 * XXX: with latest kernels we should be able
				 * to just use event.value, as the input layer
				 * removes those duped events.
				 *
				 * XXX: in older kernels and devices SW_LID is
				 * not always followed by an EV_SYN event, so
				 * this code cannot be refactored just yet.
				 */

				if (ioctl (g_io_channel_unix_get_fd(channel), EVIOCGSW(sizeof (bitmask)), bitmask) < 0) {
					HAL_DEBUG (("ioctl EVIOCGSW failed"));
				} else {
					input_data->button_new_state = test_bit (event.code, bitmask);
					emit_switch_condition (input_data);
				}
				break;
			case SW_HEADPHONE_INSERT:
				input_data->button_new_state |= event.value;
				set_jack_switch_type (input_data->udi, "headphone", event.value);
				break;
			case SW_MICROPHONE_INSERT:
				input_data->button_new_state |= event.value;
				set_jack_switch_type (input_data->udi, "microphone", event.value);
				break;
			case SW_LINEOUT_INSERT:
				input_data->button_new_state |= event.value;
				set_jack_switch_type (input_data->udi, "line-out", event.value);
				break;
			case SW_VIDEOOUT_INSERT:
				input_data->button_new_state |= event.value;
				set_jack_switch_type (input_data->udi, "video-out", event.value);
				break;
			case SW_JACK_PHYSICAL_INSERT:
				/* Just mark as inserted, w/o changing the
				 * jack switch type, which didn't. */
				input_data->button_new_state |= event.value;
				break;
			}
		} else if (event.type == EV_SYN && input_data->is_jack) {
			emit_switch_condition (input_data);
		} else if (event.type == EV_KEY && key_name[event.code] != NULL && event.value) {
			dbus_error_init (&error);
			libhal_device_emit_condition (ctx, input_data->udi,
						      "ButtonPressed",
						      key_name[event.code],
						      &error);
			dbus_error_free (&error);
		}
	}

	return TRUE;
}

static void
destroy_data (InputData *data)
{
	HAL_DEBUG (("Input device '%s' destroyed, free data.", data->udi));

	g_free (data->button_type);
	g_free (data);
}


static void
update_proc_title (void)
{
	GList *lp;
	gchar *new_command_line, *p;
	gint len = 0;

	for (lp = devices; lp; lp = lp->next)
		len = len + strlen (lp->data) + 1;

	len += strlen ("hald-addon-input: Listening on");

	new_command_line = g_malloc (len + 1);
	p = new_command_line;
	p += g_sprintf (new_command_line, "hald-addon-input: Listening on");

	for (lp = g_list_last(devices); lp; lp = g_list_previous(lp))
	{
		p += g_sprintf (p, " %s", (gchar*) lp->data);
	}

	hal_set_proc_title (new_command_line);
	g_free (new_command_line);
}

static void
add_device (LibHalContext *ctx,
	    const char *udi,
	    const LibHalPropertySet *properties)
{
	int eventfp;
	GIOChannel *channel;
	InputData *data;
	int len = strlen (udi);
	const char* device_file;
	const char *button_type;
	long bitmask[NBITS(SW_MAX)];

	data = (InputData*) g_malloc (sizeof (InputData) + len);

	memcpy (&(data->udi), udi, len+1);

	if ((device_file = libhal_ps_get_string (properties, "input.device")) == NULL) {
		HAL_ERROR(("%s has no property input.device", udi));
		g_free (data);
		return;
	}

	button_type = libhal_ps_get_string (properties, "button.type");
	if (!button_type)
		button_type = "unknown";
	data->button_type = g_strdup (button_type);

	data->is_jack = strcmp (data->button_type, "jack_insert") == 0;

	/* button_has_state will be false if the key isn't available*/
	data->button_has_state = libhal_ps_get_bool (properties, "button.has_state");
	if (data->button_has_state)
		data->button_state = libhal_ps_get_bool (properties, "button.state.value");

	eventfp = open(device_file, O_RDONLY | O_NONBLOCK);
	if (!eventfp) {
		HAL_ERROR(("Unable to open %s for reading", device_file));
		g_free (data);
		return;
	}

	if (data->is_jack &&
	    ioctl (eventfp, EVIOCGSW (sizeof (bitmask)), bitmask) >= 0) {
		set_jack_switch_type (udi, "headphone", test_bit (SW_HEADPHONE_INSERT, bitmask));
		set_jack_switch_type (udi, "microphone", test_bit (SW_MICROPHONE_INSERT, bitmask));
		set_jack_switch_type (udi, "line-out", test_bit (SW_LINEOUT_INSERT, bitmask));
		set_jack_switch_type (udi, "video-out", test_bit (SW_VIDEOOUT_INSERT, bitmask));
	}

	HAL_DEBUG (("%s: Listening on %s", udi, device_file));

	devices = g_list_prepend (devices, g_strdup (device_file));
	update_proc_title ();

	channel = g_io_channel_unix_new (eventfp);
	g_io_channel_set_encoding (channel, NULL, NULL);

	g_hash_table_insert (inputs, g_strdup(udi), channel);
	g_io_add_watch_full (channel,
			     G_PRIORITY_DEFAULT, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
			     event_io, data, (GDestroyNotify) destroy_data);
}


static void
remove_device (LibHalContext *ctx,
	    const char *udi,
	    const LibHalPropertySet *properties)
{

	GIOChannel *channel, **p_channel = &channel;
	const gchar *device_file;
	GList *lp;
	gboolean handling_udi;

	HAL_DEBUG (("Removing channel for '%s'", udi));

	handling_udi = g_hash_table_lookup_extended (inputs, udi, NULL, (gpointer *)p_channel);

	if (!handling_udi) {
		HAL_ERROR(("DeviceRemove called for unknown device: '%s'.", udi));
		return;
	}

	if (channel) {
		g_io_channel_shutdown(channel, FALSE, NULL);
		g_io_channel_unref (channel);
	}

	g_hash_table_remove (inputs, udi);

	if ((device_file = libhal_ps_get_string (properties, "input.device")) == NULL) {
		HAL_ERROR(("%s has no property input.device", udi));
		return;
	}

	lp = g_list_find_custom (devices, device_file, (GCompareFunc) strcmp);
	if (lp) {
		devices = g_list_remove_link (devices, lp);
		g_free (lp->data);
		g_list_free_1 (lp);
		update_proc_title ();
	}

	if (g_hash_table_size (inputs) == 0) {
		HAL_INFO(("no more devices, exiting"));
		g_main_loop_quit (gmain);
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

	dbus_error_init (&error);
	if ((ctx = libhal_ctx_init_direct (&error)) == NULL) {
		HAL_WARNING (("Unable to init libhal context"));
		goto out;
	}

	if ((dbus_connection = libhal_ctx_get_dbus_connection(ctx)) == NULL) {
		HAL_WARNING (("Cannot get DBus connection"));
		goto out;
	}

	if ((commandline = getenv ("SINGLETON_COMMAND_LINE")) == NULL) {
		HAL_WARNING (("SINGLETON_COMMAND_LINE not set"));
		goto out;
	}

	libhal_ctx_set_singleton_device_added (ctx, add_device);
	libhal_ctx_set_singleton_device_removed (ctx, remove_device);

	dbus_connection_setup_with_g_main (dbus_connection, NULL);
	dbus_connection_set_exit_on_disconnect (dbus_connection, 0);

	dbus_error_init (&error);

	if (!libhal_device_singleton_addon_is_ready (ctx, commandline, &error)) {
		goto out;
	}

/*
 * We should do real privilage dropping here, or we can do drop_privialages
 * if haldaemon user can read input.
 * drop_privileges (0);
*/

	inputs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	gmain = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (gmain);

	return 0;

out:
	HAL_DEBUG (("An error occured, exiting cleanly"));
	if (ctx != NULL) {
		dbus_error_init (&error);
		libhal_ctx_shutdown (ctx, &error);
		libhal_ctx_free (ctx);
	}

	return 0;
}

/* vim:set sw=8 noet: */
