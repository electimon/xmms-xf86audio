/*
 * $Id: xf86audio.c 592 2006-07-09 01:30:47Z aqua $
 * 
 * XF86Audio keys plugin for XMMS
 *
 * This is a "general" XMMS plugin (that is, one that doesn't provide any
 * audio processing functions).  When enabled, it grabs the XF86Audio*
 * keys for play/stop/next/previous, then translates keyrelease events
 * on those keys into XMMS actions.
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
 * Copyright (c) 2003-2005 by Devin Carraway <xf86audio-plugin@devin.com>.
 *
 */

#include <xmms/plugin.h>
#include <xmms/util.h>
#include <xmms/xmmsctrl.h>
#include <xmms/configfile.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>

#define _(string) (string)

#define VERSION "0.4.4"

enum xf86audio_value {
       XF86AUDIO_PLAY = 0,
       XF86AUDIO_PAUSE,
       XF86AUDIO_STOP,
       XF86AUDIO_NEXT,
       XF86AUDIO_PREV,
       XF86AUDIO_RAISEVOLUME,
       XF86AUDIO_LOWERVOLUME,
       XF86AUDIO_MUTE,
       XF86AUDIO_MEDIA,
       XF86AUDIO_MAX
};

static KeyCode map[XF86AUDIO_MAX];
static GeneralPlugin gpi;

enum onplay_value {
	ONPLAY_PAUSE,
	ONPLAY_RESTART
};

struct cf {
	enum onplay_value onplay;
	gint volume_increment;
};
static struct cf cf_active;
static struct cf cf_edited;
static struct cf cf_saved;

static void on_onplay_change(GtkWidget *widget, gpointer data);
static void on_volume_increment_change(GtkWidget *widget, gpointer data);
static void on_config_ok(GtkWidget *widget, gpointer data);
static void on_config_apply(GtkWidget *widget, gpointer data);
static void on_config_cancel(GtkWidget *widget, gpointer data);
static void config_save(const struct cf *c);
static void config_load();

static void grab_keys();
static void ungrab_keys();
static GdkFilterReturn xf86audio_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data);


/* Public API routines -- the general plugin API exposes these four
 * methods.  Most actual entries to the plugin happen through GTK
 * X event callbacks.
 */

/* plugin_init(): called by XMMS at plugin activation time; GTK is
 * initialized by that point and GDK is available.
 */
static void plugin_init()
{
	gdk_window_add_filter(GDK_ROOT_PARENT(), xf86audio_filter, map);
	config_load();
	grab_keys();
}

/* plugin_cleanup(): called by XMMS ad plugin unload time (disable or
 * shutdown).
 */
static void plugin_cleanup()
{
	ungrab_keys();
	gdk_window_remove_filter(NULL, xf86audio_filter, map);
}

/* plugin_about(): called when the "about" button in the plugins
 * configuration is pressed.  Typical behavior here is to present
 * a dialog explaining what the plugin does.
 */
static void plugin_about()
{
	static GtkWidget *about;
	gchar *s;

	if (about != NULL) {
		gdk_window_raise(about->window);
		return;
	}

	const gchar *s1 = _("XF86Audio Keys Control Plugin");
	const gchar *s2 = _(
		"This plugin enables the XF86Audio keysyms produced by\n"
		  "multimedia keyboards to control XMMS playback.\n\n"
		  "Note that this plugin will not set up the initial keysym\n"
		  "mapping -- you should use xmodmap or GNOME's Keyboard\n"
		  "Shortcuts preferences dialog for that.\n\n"
		  "Copyright (c) 2003-2006 by Devin Carraway <xf86audio-plugin@devin.com>.\n"
		  "This plugin is free software, released under the terms of the GNU\n"
		  "General Public License, v2.  You should have received a copy of\n"
		  "the license with this software.\n");
	s = g_strdup_printf("%s %s\n\n%s\n",s1, VERSION, s2);
	about = xmms_show_message(
			_("About XF86Audio Keys Control"),
			s, "OK", 1, NULL, NULL);
	gtk_signal_connect(GTK_OBJECT(about), "destroy",
			GTK_SIGNAL_FUNC(gtk_widget_destroyed), &about);

}

/* plugin_configure(): called when the "configure" button in the plugins
 * configuration dialog is pressed.
 */

static GtkWidget *config_window;

static void plugin_configure()
{

	if (config_window) {
		gdk_window_raise(config_window->window);
		return;
	}

	/* An extra load is necessary here, because we aren't guaranteed
	 * that plugin_init() will be called before plugin_configure()
	 * (this happens if the plugin is configured before being enabled).
	 */
	config_load();

	config_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(config_window),
			_("XF86Audio Keys Configuration"));
	gtk_signal_connect(GTK_OBJECT(config_window), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &config_window);
	gtk_container_border_width(GTK_CONTAINER(config_window), 10);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(config_window), vbox);

	/* "On Play" frame */

	GtkWidget *playaction_frame = gtk_frame_new(_("On Play"));
	gtk_box_pack_start(GTK_BOX(vbox), playaction_frame, TRUE, TRUE, 0);
	GtkWidget *pa_hbox = gtk_hbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(playaction_frame), pa_hbox);

	GtkWidget *pa_vbox = gtk_vbox_new(FALSE, 4);

	gtk_box_pack_start(GTK_BOX(pa_hbox), pa_vbox, TRUE, TRUE, 4);
	GtkWidget *pa_label = gtk_label_new(
			_("If the Play key is pressed while a song is"
			" already playing:")
		);
	gtk_misc_set_alignment(GTK_MISC(pa_label), 0.0, 0.5);
	gtk_box_pack_start(GTK_BOX(pa_vbox), pa_label, TRUE, TRUE, 2);

	GtkWidget *pa_pause = gtk_radio_button_new_with_label(NULL,
			_("Pause playback"));
	GtkWidget *pa_restart = gtk_radio_button_new_with_label_from_widget(
			GTK_RADIO_BUTTON(pa_pause),
			_("Restart the current song"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
				cf_active.onplay == ONPLAY_RESTART ?
				pa_restart : pa_pause), TRUE);
	gtk_signal_connect(GTK_OBJECT(pa_pause), "toggled",
			G_CALLBACK(on_onplay_change), GINT_TO_POINTER(ONPLAY_PAUSE));
	gtk_signal_connect(GTK_OBJECT(pa_restart), "toggled",
			G_CALLBACK(on_onplay_change), GINT_TO_POINTER(ONPLAY_RESTART));
	
	gtk_box_pack_start(GTK_BOX(pa_vbox), pa_pause, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(pa_vbox), pa_restart, FALSE, FALSE, 0);
	GtkWidget *pa_label2 = gtk_label_new(
			_("Regardless of this setting, the current song can be restarted"
			"\nby holding down Shift while pressing the Play key.")
		);
	gtk_label_set_justify(GTK_LABEL(pa_label2), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(pa_label2), 0.0, 0.5);
	gtk_box_pack_start(GTK_BOX(pa_vbox), pa_label2, TRUE, TRUE, 2);
	gtk_widget_show(pa_vbox);

	gtk_widget_show(pa_restart);
	gtk_widget_show(pa_pause);

	gtk_widget_show(pa_label2);
	gtk_widget_show(pa_label);
	gtk_widget_show(pa_vbox);
	gtk_widget_show(pa_hbox);

	/* Volume change frame */

	GtkWidget *volume_frame = gtk_frame_new(_("On Volume Change"));
	gtk_box_pack_start(GTK_BOX(vbox), volume_frame, TRUE, TRUE, 0);
	GtkWidget *v_vbox = gtk_vbox_new(FALSE, 0);
	GtkWidget *v_hbox = gtk_hbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(volume_frame), v_vbox);
	gtk_box_pack_start(GTK_BOX(v_vbox), v_hbox, TRUE, TRUE, 4);

	GtkWidget *vol_label = gtk_label_new(
			_("Volume change increment (%): ")
			);
	gtk_box_pack_start(GTK_BOX(v_hbox), vol_label, FALSE, FALSE, 4);

	GtkObject *vol_adj = gtk_adjustment_new(
			cf_active.volume_increment,
			0, 100, 1, 10, 10);
	GtkWidget *vol_spin = gtk_spin_button_new(GTK_ADJUSTMENT(vol_adj),
			1.0, 0);
	gtk_signal_connect(GTK_OBJECT(vol_spin), "changed",
			GTK_SIGNAL_FUNC(on_volume_increment_change), NULL);
	gtk_box_pack_start(GTK_BOX(v_hbox), vol_spin, FALSE, FALSE, 0);

	gtk_widget_show(vol_spin);
	gtk_widget_show(vol_label);
	gtk_widget_show(v_hbox);
	gtk_widget_show(v_vbox);
	gtk_widget_show(volume_frame);

	/* Button box at bottom of window */

	GtkWidget *button_hbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(button_hbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(button_hbox), 5);
	gtk_box_pack_end(GTK_BOX(vbox), button_hbox, FALSE, FALSE, 0);

	GtkWidget *ok = gtk_button_new_with_label("OK");
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_signal_connect(GTK_OBJECT(ok), "clicked", G_CALLBACK(on_config_ok), NULL);
	gtk_box_pack_start(GTK_BOX(button_hbox), ok, TRUE, TRUE, 0);
	gtk_widget_grab_default(ok);

	GtkWidget *cancel = gtk_button_new_with_label(_("Cancel"));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_signal_connect(GTK_OBJECT(cancel), "clicked", G_CALLBACK(on_config_cancel), NULL);
	gtk_box_pack_start(GTK_BOX(button_hbox), cancel, TRUE, TRUE, 0);

	GtkWidget *apply = gtk_button_new_with_label(_("Apply"));
	GTK_WIDGET_SET_FLAGS(apply, GTK_CAN_DEFAULT);
	gtk_signal_connect(GTK_OBJECT(apply), "clicked", G_CALLBACK(on_config_apply), NULL);
	gtk_box_pack_start(GTK_BOX(button_hbox), apply, TRUE, TRUE, 0);


	gtk_widget_show(ok);
	gtk_widget_show(cancel);
	gtk_widget_show(apply);
	gtk_widget_show(button_hbox);

	gtk_widget_show(playaction_frame);
	gtk_widget_show(vbox);
	gtk_widget_show(config_window);
}

/* gst_gplugin_info(): General plugin query function.  Oddly, XMMS does
 * not supply a pointer to a GeneralPlugin structure, so we keep a static
 * one around.
 */
GeneralPlugin *get_gplugin_info()
{
	gpi.description = _("XF86Audio Keys Control");
	gpi.xmms_session = -1;
	gpi.init = plugin_init;
	gpi.about = plugin_about;
	gpi.configure = plugin_configure;
	gpi.cleanup = plugin_cleanup;
	return &gpi;
}


/* XMMS-public API methods end here; private code commences.
 */

static void on_onplay_change(GtkWidget *widget, gpointer data)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		return;
	cf_edited.onplay =
		(enum onplay_value)data == ONPLAY_RESTART ?
		ONPLAY_RESTART : ONPLAY_PAUSE;
}

static void on_volume_increment_change(GtkWidget *widget, gpointer data)
{
	cf_edited.volume_increment = gtk_spin_button_get_value_as_int(
			GTK_SPIN_BUTTON(widget));
}

static void on_config_ok(GtkWidget *widget, gpointer data)
{
	memcpy((void *)&cf_active, (void *)&cf_edited, sizeof(cf_active));
	memcpy((void *)&cf_saved, (void *)&cf_edited, sizeof(cf_saved));
	config_save(&cf_active);
	gtk_widget_destroy(config_window);
}

static void on_config_apply(GtkWidget *widget, gpointer data)
{
	/* shallow copy */
	memcpy((void *)&cf_active, (void *)&cf_edited, sizeof(cf_active));
}

static void on_config_cancel(GtkWidget *widget, gpointer data)
{
	/* shallow copy */
	memcpy((void *)&cf_active, (void *)&cf_saved, sizeof(cf_active));
	gtk_widget_destroy(config_window);
}

static void config_save(const struct cf *c)
{
	ConfigFile *cfg;
	
	if ((cfg = xmms_cfg_open_default_file()) == NULL) {
		if ((cfg = xmms_cfg_new()) == NULL) {
			g_error(_("Couldn't create new XMMS configuration"));
			return;
		}
	}

	xmms_cfg_write_string(cfg, "xf86audio", "play_action",
			c->onplay == ONPLAY_PAUSE ? "pause" :
			c->onplay == ONPLAY_RESTART ? "restart" :
			"");
	xmms_cfg_write_int(cfg, "xf86audio", "volume_increment",
			c->volume_increment);
	if (!xmms_cfg_write_default_file(cfg)) {
		g_warning(_("Error saving to default XMMS configuration"));
		return;
	}
	xmms_cfg_free(cfg);
}

static void config_load()
{
	ConfigFile *cfg;
	gchar *s;
	gint i;
	static const struct cf cf_default = {
		.onplay = ONPLAY_PAUSE,
		.volume_increment = 5
	};

	memcpy((void *)&cf_saved, (void *)&cf_default, sizeof(cf_saved));
	if ((cfg = xmms_cfg_open_default_file()) == NULL) {
		g_warning(_("Couldn't open default XMMS configuration"));
		return;
	}
	if (xmms_cfg_read_string(cfg, "xf86audio", "play_action", &s)) {
		if (strcmp(s, "pause") == 0)
			cf_saved.onplay = ONPLAY_PAUSE;
		else if (strcmp(s, "restart") == 0)
			cf_saved.onplay = ONPLAY_RESTART;
	}
	if (xmms_cfg_read_int(cfg, "xf86audio", "volume_increment", &i)) {
		cf_saved.volume_increment = i;
	}
	memcpy((void *)&cf_active, (void *)&cf_saved, sizeof(cf_active));
	memcpy((void *)&cf_edited, (void *)&cf_saved, sizeof(cf_edited));
	
	xmms_cfg_free(cfg);
}

static GdkFilterReturn xf86audio_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
	XEvent *xev = (XEvent *)xevent;
	XKeyEvent *keyevent = (XKeyEvent *)xevent;
	KeyCode *k = (KeyCode *)data;
	gint i;
	gint vl, vr;
	static gint saved_vl, saved_vr;

	if (xev->type != KeyRelease)
		return GDK_FILTER_CONTINUE;

	for (i=0; i<XF86AUDIO_MAX; i++) {
		if (k[i] == keyevent->keycode)
			break;
	}
	if (i == XF86AUDIO_MAX) {
		g_warning(_("Received KeyRelease event for unrequested keycode %d"),
				keyevent->keycode);
		return GDK_FILTER_CONTINUE;
	}

	switch (i) {
		case XF86AUDIO_STOP:
			xmms_remote_stop(gpi.xmms_session);
			break;
		case XF86AUDIO_PREV:
			xmms_remote_playlist_prev(gpi.xmms_session);
			break;
		case XF86AUDIO_NEXT:
			xmms_remote_playlist_next(gpi.xmms_session);
			break;
		case XF86AUDIO_PLAY:
			if (cf_active.onplay == ONPLAY_RESTART ||
				keyevent->state & GDK_SHIFT_MASK) {
				xmms_remote_play(gpi.xmms_session);
				break;
			} else
				; /* fallthrough */
		case XF86AUDIO_PAUSE:
			if (xmms_remote_is_playing(gpi.xmms_session))
				xmms_remote_pause(gpi.xmms_session);
			else
				xmms_remote_play(gpi.xmms_session);
			break;
		case XF86AUDIO_RAISEVOLUME:
			xmms_remote_get_volume(gpi.xmms_session, &vl, &vr);
			vl = MIN(100, vl + cf_active.volume_increment);
			vr = MIN(100, vr + cf_active.volume_increment);
			xmms_remote_set_volume(gpi.xmms_session, vl, vr);
			break;
		case XF86AUDIO_LOWERVOLUME:
			xmms_remote_get_volume(gpi.xmms_session, &vl, &vr);
			vl = MAX(0, vl - cf_active.volume_increment);
			vr = MAX(0, vr - cf_active.volume_increment);
			xmms_remote_set_volume(gpi.xmms_session, vl, vr);
			break;
		case XF86AUDIO_MUTE:
			xmms_remote_get_volume(gpi.xmms_session, &vl, &vr);
			if (vl == 0 && vr == 0) {
				xmms_remote_set_volume(gpi.xmms_session,
						saved_vl, saved_vr);
			} else {
				xmms_remote_get_volume(gpi.xmms_session,
						&saved_vl, &saved_vr);
				xmms_remote_set_volume(gpi.xmms_session, 0, 0);
			}
			break;
		case XF86AUDIO_MEDIA:
			xmms_remote_eject(gpi.xmms_session);
			break;
		default: return GDK_FILTER_CONTINUE;
	}
	return GDK_FILTER_REMOVE;
}


static KeyCode grab_key(char *keystring)
{
	KeySym sym;
	KeyCode code;
	gint i;

	if ((sym = XStringToKeysym(keystring)) == NoSymbol)
		return 0;
	if ((code = XKeysymToKeycode(GDK_DISPLAY(), sym)) == 0)
		return 0;

	gdk_error_trap_push();
	for (i = 0; i < ScreenCount(GDK_DISPLAY()); i++) {
		XGrabKey(GDK_DISPLAY(), code,
				AnyModifier, RootWindow(GDK_DISPLAY(),i),
				1, GrabModeAsync, GrabModeAsync);
	}

	gdk_flush();
	if (gdk_error_trap_pop()) {
		g_warning(_("Couldn't grab %s: another client may already have done so"),
				keystring);
		return 0;
	}
	return code;
}

static void grab_keys()
{
	KeyCode code;
	
	if ((code = grab_key("XF86AudioNext")) != 0)
		map[XF86AUDIO_NEXT] = code;
	if ((code = grab_key("XF86AudioPrev")) != 0)
		map[XF86AUDIO_PREV] = code;
	if ((code = grab_key("XF86AudioPlay")) != 0)
		map[XF86AUDIO_PLAY] = code;
	if ((code = grab_key("XF86AudioStop")) != 0)
		map[XF86AUDIO_STOP] = code;
	if ((code = grab_key("XF86AudioPause")) != 0)
		map[XF86AUDIO_PAUSE] = code;
	if ((code = grab_key("XF86AudioRaiseVolume")) != 0)
		map[XF86AUDIO_RAISEVOLUME] = code;
	if ((code = grab_key("XF86AudioLowerVolume")) != 0)
		map[XF86AUDIO_LOWERVOLUME] = code;
	if ((code = grab_key("XF86AudioMute")) != 0)
		map[XF86AUDIO_MUTE] = code;
	if ((code = grab_key("XF86AudioMedia")) != 0)
		map[XF86AUDIO_MEDIA] = code;

}


static void ungrab_key(KeyCode code)
{
	int i;

	gdk_error_trap_push();
	for (i = 0; i < ScreenCount(GDK_DISPLAY()); i++)
		XUngrabKey(GDK_DISPLAY(), code,
				AnyModifier, RootWindow(GDK_DISPLAY(),i));
	gdk_flush();
	if (gdk_error_trap_pop())
		g_warning(_("Couldn't ungrab keycode %d"), code);
}

static void ungrab_keys()
{
	int i;

	for (i = 0; i < XF86AUDIO_MAX; i++)
		if (map[i] != 0) {
			ungrab_key(map[i]);
			map[i] = 0;
		}
}


