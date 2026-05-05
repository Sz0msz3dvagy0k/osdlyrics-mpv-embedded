/* -*- mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2009-2011  Tiger Soldier <tigersoldi@gmail.com>
 *
 * This file is part of OSD Lyrics.
 * 
 * OSD Lyrics is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OSD Lyrics is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OSD Lyrics.  If not, see <https://www.gnu.org/licenses/>. 
 */
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include "ol_metadata.h"
#include "ol_config_proxy.h"
#include "ol_player.h"
#include "ol_osd_window.h"
#include "ol_osd_toolbar.h"
#include "ol_osd_module.h"
#include "ol_lrc.h"
#include "ol_stock.h"
#include "ol_menu.h"
#include "ol_app.h"
#include "ol_utils.h"
#include "ol_debug.h"

const int MESSAGE_DURATION_MS = 3000;
typedef struct _OlOsdModule OlOsdModule;

struct OlLrc;

struct _OlOsdModule
{
  OlPlayer *player;
  OlMetadata *metadata;
  gint lrc_id;
  gint lrc_next_id;
  gint current_line;
  gint line_count;
  gboolean force_refresh_on_set_played_time;
  OlLrc *lrc;
  OlOsdWindow *window;
  OlOsdToolbar *toolbar;
  guint message_source;
  gboolean layer_shell_enabled;
  gboolean layer_shell_visible;
  GPid layer_shell_pid;
  gint layer_shell_stdin;
  GList *config_bindings;
  gboolean visible_when_stopped;
};

typedef void (*_ConfigSetFunc) (OlConfigProxy *config,
                                const gchar *key,
                                OlOsdModule *osd);

struct _ConfigBinding
{
  OlOsdModule *osd;
  guint change_handler;
  _ConfigSetFunc setter;
};

struct _ConfigMapping
{
  const gchar *key;
  _ConfigSetFunc setter;
};

/** interfaces */
static OlOsdModule* ol_osd_module_new (struct OlDisplayModule *module,
                                       OlPlayer *player);
static void ol_osd_module_free (struct OlDisplayModule *module);
static void _metadata_changed_cb (OlPlayer *player,
                                  OlOsdModule *module);
static void _status_changed_cb (OlPlayer *player,
                                OlOsdModule *module);
static void _update_metadata (OlOsdModule *module);
static void _update_status (OlOsdModule *module);
static gboolean _advance_to_nonempty_lyric (OlLrcIter *iter);

static void ol_osd_module_set_played_time (struct OlDisplayModule *module,
                                           guint64 played_time);
static void ol_osd_module_set_lrc (struct OlDisplayModule *module,
                                   OlLrc *lrc_file);
static void ol_osd_module_set_message (struct OlDisplayModule *module,
                                       const char *message,
                                       int duration_ms);
static void ol_osd_module_search_message (struct OlDisplayModule *module,
                                          const char *message);
static void ol_osd_module_search_fail_message (struct OlDisplayModule *module,
                                               const char *message);
static void ol_osd_module_download_fail_message (struct OlDisplayModule *module,
                                                 const char *message);
static void ol_osd_module_clear_message (struct OlDisplayModule *module);
static gboolean layer_shell_helper_available (void);
static gboolean start_layer_shell_helper (OlOsdModule *osd);
static void stop_layer_shell_helper (OlOsdModule *osd);
static void sync_layer_shell_helper (OlOsdModule *osd);
static void hide_legacy_window_if_layer_enabled (OlOsdModule *osd);

/** internal functions */

/** 
 * @brief Gets the real lyric of the given lyric
 * A REAL lyric is the nearest lyric to the given lyric, whose text is not empty
 * If the given lyric text is not empty, the given lyric is a real lyric
 * If not real lyric available, returns NULL
 * @param lrc An const struct OlLrcItem
 * 
 * @return The real lyric of the lrc. returns NULL if not available
 */
static void ol_osd_module_update_next_lyric (OlOsdModule *osd,
                                             OlLrcIter *iter);
static void ol_osd_module_init_osd (OlOsdModule *osd);
static gboolean hide_message (OlOsdModule *osd);
static gboolean is_message_displayed (OlOsdModule *osd);
static void reset_lyrics_state (OlOsdModule *osd);
static void hide_lyrics (OlOsdModule *osd);
static void set_lyric_row (OlOsdModule *osd, gint row, const char *text);
static void set_lyric_percentage (OlOsdModule *osd, gint row, gdouble percentage);
static void set_current_line (OlOsdModule *osd, gint line);
static void set_current_percentage (OlOsdModule *osd, gdouble percentage);

/* OSD Window signal handlers */
static void ol_osd_moved_handler (OlOsdWindow *osd, gpointer data);
static void ol_osd_resize_handler (OlOsdWindow *osd, gpointer data);
static gboolean ol_osd_button_release (OlOsdWindow *osd,
                                       GdkEventButton *event,
                                       gpointer data);
static void ol_osd_scroll (OlOsdWindow *osd,
                           GdkEventScroll *event,
                           gpointer data);

/* Config handlers */
static struct _ConfigBinding *_bind_config (const char *key,
                                            _ConfigSetFunc setter,
                                            OlOsdModule *osd);
static void _unbind_config (struct _ConfigBinding *binding);
static void _bind_all_config (OlOsdModule *osd);
static void _config_changed_cb (OlConfigProxy *config,
                                const char *key,
                                struct _ConfigBinding *binding);
static void _visible_changed_cb (OlConfigProxy *config,
                                 const char *key,
                                 OlOsdModule *osd);
static void _width_changed_cb (OlConfigProxy *config,
                               const char *key,
                               OlOsdModule *osd);
static void _mode_changed_cb (OlConfigProxy *config,
                              const char *key,
                              OlOsdModule *osd);
static void _locked_changed_cb (OlConfigProxy *config,
                                const char *key,
                                OlOsdModule *osd);
static void _line_count_changed_cb (OlConfigProxy *config,
                                    const char *key,
                                    OlOsdModule *osd);
static void _font_changed_cb (OlConfigProxy *config,
                              const char *key,
                              OlOsdModule *osd);
static void _pos_changed_cb (OlConfigProxy *config,
                             const char *key,
                             OlOsdModule *osd);
static void _lrc_align_changed_cb (OlConfigProxy *config,
                                   const char *key,
                                   OlOsdModule *osd);
static void _active_color_changed_cb (OlConfigProxy *config,
                                      const char *key,
                                      OlOsdModule *osd);
static void _inactive_color_changed_cb (OlConfigProxy *config,
                                        const char *key,
                                        OlOsdModule *osd);
static void _translucent_changed_cb (OlConfigProxy *config,
                                     const char *key,
                                     OlOsdModule *osd);
static void _outline_changed_cb (OlConfigProxy *config,
                                 const char *key,
                                 OlOsdModule *osd);
static void _blur_changed_cb (OlConfigProxy *config,
                              const char *key,
                              OlOsdModule *osd);

static struct _ConfigMapping _config_mapping[] = {
  { "OSD/visible_when_stopped", _visible_changed_cb },
  { "OSD/width", _width_changed_cb },
  { "OSD/osd-window-mode", _mode_changed_cb },
  { "OSD/locked", _locked_changed_cb },
  { "OSD/line-count", _line_count_changed_cb },
  { "OSD/font-name", _font_changed_cb },
  { "OSD/x", _pos_changed_cb },
  { "OSD/y", _pos_changed_cb },
  { "OSD/lrc-align-0", _lrc_align_changed_cb },
  { "OSD/lrc-align-1", _lrc_align_changed_cb },
  { "OSD/active-lrc-color", _active_color_changed_cb },
  { "OSD/inactive-lrc-color", _inactive_color_changed_cb },
  { "OSD/translucent-on-mouse-over", _translucent_changed_cb },
  { "OSD/outline-width", _outline_changed_cb },
  { "OSD/blur-radius", _blur_changed_cb },
};

static gboolean _config_is_setting = FALSE;

static void
ol_osd_moved_handler (OlOsdWindow *osd, gpointer data)
{
  ol_log_func ();
  if (_config_is_setting)
    return;
  _config_is_setting = TRUE;
  OlConfigProxy *config = ol_config_proxy_get_instance ();
  int x, y;
  ol_osd_window_get_pos (osd, &x, &y);
  ol_config_proxy_set_int (config, "OSD/x", x);
  ol_config_proxy_set_int (config, "OSD/y", y);
  _config_is_setting = FALSE;
}

static void
ol_osd_resize_handler (OlOsdWindow *osd, gpointer data)
{
  ol_log_func ();
  if (_config_is_setting)
    return;
  OlConfigProxy *config = ol_config_proxy_get_instance ();
  int width = ol_osd_window_get_width (osd);
  ol_config_proxy_set_int (config, "OSD/width", width);
}

static gboolean
ol_osd_button_release (OlOsdWindow *osd,
                       GdkEventButton *event,
                       gpointer data)
{
  if (event->button == 3)
  {
    gtk_menu_popup (GTK_MENU (ol_menu_get_popup ()),
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    event->button,
                    event->time);
    return TRUE;
  }
  return FALSE;
}

static void
ol_osd_scroll (OlOsdWindow *osd,
               GdkEventScroll *event,
               gpointer data)
{
  int doffset = 0;
  if (event->direction == GDK_SCROLL_DOWN ||
      event->direction == GDK_SCROLL_RIGHT)
    doffset = -200;
  else if (event->direction == GDK_SCROLL_UP ||
           event->direction == GDK_SCROLL_LEFT)
    doffset = 200;
  ol_app_adjust_lyric_offset (doffset);
}

static void
_config_changed_cb (OlConfigProxy *config,
                    const char *key,
                    struct _ConfigBinding *binding)
{
  if (_config_is_setting)
    return;
  _config_is_setting = TRUE;
  binding->setter (config, key, binding->osd);
  _config_is_setting = FALSE;
}

static void
_visible_changed_cb (OlConfigProxy *config,
                     const char *key,
                     OlOsdModule *osd)
{
  osd->visible_when_stopped = ol_config_proxy_get_bool (config, key);
  _update_status (osd);
}

static void
_width_changed_cb (OlConfigProxy *config,
                   const char *key,
                   OlOsdModule *osd)
{
  ol_osd_window_set_width (osd->window,
                           ol_config_proxy_get_int (config, key));
}

static void
_mode_changed_cb (OlConfigProxy *config,
                  const char *key,
                  OlOsdModule *osd)
{
  gchar *mode = ol_config_proxy_get_string (config, key);
  if (strcmp (mode, "dock") == 0)
    ol_osd_window_set_mode (osd->window, OL_OSD_WINDOW_DOCK);
  else
    ol_osd_window_set_mode (osd->window, OL_OSD_WINDOW_NORMAL);
  g_free (mode);
}

static void
_locked_changed_cb (OlConfigProxy *config,
                    const char *key,
                    OlOsdModule *osd)
{
  ol_osd_window_set_locked (osd->window,
                            ol_config_proxy_get_bool (config, key));
}

static void
_line_count_changed_cb (OlConfigProxy *config,
                        const char *key,
                        OlOsdModule *osd)
{
  osd->line_count = ol_config_proxy_get_int (config, key);
  ol_osd_window_set_line_count (osd->window, osd->line_count);
  if (osd->line_count == 1 && !osd->layer_shell_enabled)
  {
    set_lyric_row (osd, 1, NULL);
    set_lyric_percentage (osd, 1, 0.0);
  }
  else if (osd->layer_shell_enabled)
  {
    osd->lrc_next_id = -1;
  }
  sync_layer_shell_helper (osd);
}

static void
_font_changed_cb (OlConfigProxy *config,
                  const char *key,
                  OlOsdModule *osd)
{
  gchar *font = ol_config_proxy_get_string (config, key);
  ol_assert (font != NULL);
  ol_osd_window_set_font_name (osd->window, font);
  g_free (font);
}

static void
_pos_changed_cb (OlConfigProxy *config,
                 const char *key,
                 OlOsdModule *osd)
{
  ol_osd_window_move (osd->window,
                      ol_config_proxy_get_int (config, "OSD/x"),
                      ol_config_proxy_get_int (config, "OSD/y"));
}

static void
_lrc_align_changed_cb (OlConfigProxy *config,
                       const char *key,
                       OlOsdModule *osd)
{
  int line = 0;
  if (key[strlen (key) - 1] == '1')
    line = 1;
  ol_osd_window_set_line_alignment (osd->window, line,
                                    ol_config_proxy_get_double (config, key));
}

static void
_active_color_changed_cb (OlConfigProxy *config,
                          const char *key,
                          OlOsdModule *osd)
{
  gsize len;
  char **color_str = ol_config_proxy_get_str_list (config, key, &len);
  ol_debugf ("len = %d\n", (int)len);
  if (len != OL_LINEAR_COLOR_COUNT) return;
  if (color_str != NULL)
  {
    OlColor *colors = ol_color_from_str_list ((const char**)color_str, NULL);
    ol_osd_window_set_active_colors (osd->window, colors[0], colors[1], colors[2]);
    g_free (colors);
    g_strfreev (color_str);
  }
}

static void
_inactive_color_changed_cb (OlConfigProxy *config,
                            const char *key,
                            OlOsdModule *osd)
{
  gsize len;
  char **color_str = ol_config_proxy_get_str_list (config, key, &len);
  ol_debugf ("len = %d\n", (int)len);
  if (len != OL_LINEAR_COLOR_COUNT) return;
  if (color_str != NULL)
  {
    OlColor *colors = ol_color_from_str_list ((const char**)color_str, NULL);
    ol_osd_window_set_inactive_colors (osd->window, colors[0], colors[1], colors[2]);
    g_free (colors);
    g_strfreev (color_str);
  }
}

static void
_translucent_changed_cb (OlConfigProxy *config,
                         const char *key,
                         OlOsdModule *osd)
{
  ol_osd_window_set_translucent_on_mouse_over (osd->window,
                                               ol_config_proxy_get_bool (config, key));
}

static void
_outline_changed_cb (OlConfigProxy *config,
                     const char *key,
                     OlOsdModule *osd)
{
  ol_osd_window_set_outline_width (osd->window, ol_config_proxy_get_int (config, key));
}

static void
_blur_changed_cb (OlConfigProxy *config,
                  const char *key,
                  OlOsdModule *osd)
{
  ol_osd_window_set_blur_radius (osd->window, ol_config_proxy_get_double (config, key));
}

static void
ol_osd_module_update_next_lyric (OlOsdModule *osd, OlLrcIter *iter)
{
  if (osd->line_count == 1 && !osd->layer_shell_enabled)
  {
    osd->lrc_next_id = -1;
    set_lyric_row (osd, 1, NULL);
    set_lyric_percentage (osd, 1, 0.0);
    return;
  }
  if (ol_lrc_iter_next (iter))
    _advance_to_nonempty_lyric (iter);
  gint id;
  const char *text = NULL;
  if (ol_lrc_iter_is_valid (iter))
  {
    id = ol_lrc_iter_get_id (iter);
    text = ol_lrc_iter_get_text (iter);
  }
  else
  {
    id = -1;
    text = "";
  }
  if (osd->lrc_next_id != id)
  {
    osd->lrc_next_id = id;
    set_lyric_row (osd, 1, text);
    set_lyric_percentage (osd, 1, 0.0);
  }
}

static void
_bind_all_config (OlOsdModule *osd)
{
  int i;
  for (i = 0; i < G_N_ELEMENTS (_config_mapping); i++)
  {
    struct _ConfigBinding *binding = _bind_config (_config_mapping[i].key,
                                                   _config_mapping[i].setter,
                                                   osd);
    osd->config_bindings = g_list_prepend (osd->config_bindings, binding);
  }
}

static struct _ConfigBinding *
_bind_config (const char *key,
              _ConfigSetFunc setter,
              OlOsdModule *osd)
{
  ol_assert_ret (key != NULL, FALSE);
  OlConfigProxy *config = ol_config_proxy_get_instance ();
  struct _ConfigBinding *binding = g_new (struct _ConfigBinding, 1);
  gchar *signal = g_strdup_printf ("changed::%s", key);
  binding->osd = osd;
  binding->setter = setter;
  binding->change_handler = g_signal_connect (config,
                                              signal,
                                              (GCallback) _config_changed_cb,
                                              binding);
  g_free (signal);
  setter (config, key, osd);
  return binding;
}

static void
_unbind_config (struct _ConfigBinding *binding)
{
  OlConfigProxy *config = ol_config_proxy_get_instance ();
  ol_assert (binding != NULL);
  g_signal_handler_disconnect (config, binding->change_handler);
  g_free (binding);
}

static void
ol_osd_module_init_osd (OlOsdModule *osd)
{
  osd->window = OL_OSD_WINDOW (ol_osd_window_new ());
  if (osd->window == NULL)
    return;

  if (!osd->layer_shell_enabled)
  {
    GtkIconTheme *icontheme = gtk_icon_theme_get_default ();
    GdkPixbuf *bg = gtk_icon_theme_load_icon (icontheme,
                                              OL_STOCK_OSD_BG,
                                              32,
                                              0,
                                              NULL);
    if (bg != NULL)
    {
      ol_osd_window_set_bg (osd->window, bg);
      g_object_unref (bg);
    }
    osd->toolbar = OL_OSD_TOOLBAR (ol_osd_toolbar_new ());
    if (osd->toolbar != NULL)
    {
      gtk_container_add (GTK_CONTAINER (osd->window),
                         GTK_WIDGET (osd->toolbar));
      gtk_widget_show_all (GTK_WIDGET (osd->toolbar));
      g_object_ref (osd->toolbar);
      ol_osd_toolbar_set_player (osd->toolbar, osd->player);
    }
  }

  OlConfigProxy *config = ol_config_proxy_get_instance ();
  ol_assert (config != NULL);
  
  _bind_all_config (osd);
  
  g_signal_connect (osd->window, "moved",
                    G_CALLBACK (ol_osd_moved_handler),
                    NULL);
  g_signal_connect (osd->window, "resize",
                    G_CALLBACK (ol_osd_resize_handler),
                    NULL);
  g_signal_connect (osd->window, "button-release-event",
                    G_CALLBACK (ol_osd_button_release),
                    NULL);
  g_signal_connect (osd->window, "scroll-event",
                    G_CALLBACK (ol_osd_scroll),
                    NULL);

  hide_legacy_window_if_layer_enabled (osd);
}

static OlOsdModule*
ol_osd_module_new (struct OlDisplayModule *module,
                   OlPlayer *player)
{
  ol_log_func ();
  OlOsdModule *data = g_new (OlOsdModule, 1);
  g_object_ref (player);
  data->player = player;
  data->window = NULL;
  data->toolbar = NULL;
  data->lrc = NULL;
  reset_lyrics_state (data);
  data->force_refresh_on_set_played_time = FALSE;
  data->message_source = 0;
  data->layer_shell_enabled = FALSE;
  data->layer_shell_visible = FALSE;
  data->layer_shell_pid = 0;
  data->layer_shell_stdin = -1;
  data->metadata = ol_metadata_new ();
  data->config_bindings = NULL;
  data->visible_when_stopped = TRUE;
  signal (SIGPIPE, SIG_IGN);
  if (layer_shell_helper_available ())
    start_layer_shell_helper (data);
  ol_osd_module_init_osd (data);
  g_signal_connect (player,
                    "track-changed",
                    G_CALLBACK (_metadata_changed_cb),
                    data);
  g_signal_connect (player,
                    "status-changed",
                    G_CALLBACK (_status_changed_cb),
                    data);
  _update_metadata (data);
  _update_status (data);
  return data;
}

static void
ol_osd_module_free (struct OlDisplayModule *module)
{
  ol_log_func ();
  ol_assert (module != NULL);
  OlOsdModule *priv = ol_display_module_get_data (module);
  ol_assert (priv != NULL);
  if (priv->lrc)
  {
    g_object_unref (priv->lrc);
    priv->lrc = NULL;
  }
  if (priv->toolbar)
  {
    g_object_unref (priv->toolbar);
    priv->toolbar = NULL;
  }
  stop_layer_shell_helper (priv);
  if (priv->window != NULL)
  {
    gtk_widget_destroy (GTK_WIDGET (priv->window));
    priv->window = NULL;
  }
  if (is_message_displayed (priv))
  {
    g_source_remove (priv->message_source);
    priv->message_source = 0;
  }
  if (priv->metadata != NULL)
  {
    ol_metadata_free (priv->metadata);
    priv->metadata = NULL;
  }
  g_signal_handlers_disconnect_by_func (priv->player,
                                        _metadata_changed_cb,
                                        priv);
  g_signal_handlers_disconnect_by_func (priv->player,
                                        _status_changed_cb,
                                        priv);
  g_object_unref (priv->player);
  priv->player = NULL;
  while (priv->config_bindings != NULL)
  {
    _unbind_config (priv->config_bindings->data);
    priv->config_bindings = g_list_delete_link (priv->config_bindings,
                                                priv->config_bindings);
  }
  g_free (priv);
}

static void
_metadata_changed_cb (OlPlayer *player,
                      OlOsdModule *module)
{
  ol_log_func ();
  _update_metadata (module);
  
}

static void
_status_changed_cb (OlPlayer *player,
                    OlOsdModule *module)
{
  _update_status (module);
}

static void
_update_metadata (OlOsdModule *module)
{
  ol_log_func ();
  ol_assert (module != NULL);
  ol_player_get_metadata (module->player, module->metadata);
}

static void
_update_status (OlOsdModule *module)
{
  ol_log_func ();

  enum OlPlayerStatus status;
  if (module->player)
    status = ol_player_get_status (module->player);
  else
    status = OL_PLAYER_UNKNOWN;

  gboolean visible = (status != OL_PLAYER_STOPPED ||
                      module->visible_when_stopped);

  module->layer_shell_visible = visible;
  if (!module->layer_shell_enabled)
    gtk_widget_set_visible (GTK_WIDGET (module->window), visible);
  if (!module->layer_shell_enabled && module->toolbar != NULL && visible)
    ol_osd_toolbar_set_status (module->toolbar, status);
  hide_legacy_window_if_layer_enabled (module);
  sync_layer_shell_helper (module);
}

static void
ol_osd_module_set_played_time (struct OlDisplayModule *module,
                               guint64 played_time)
{
  ol_assert (module != NULL);
  OlOsdModule *priv = ol_display_module_get_data (module);
  ol_assert (priv != NULL);
  if (priv->lrc != NULL && priv->window != NULL)
  {
    OlLrcIter *iter = ol_lrc_iter_from_timestamp (priv->lrc,
                                                  played_time);
    if (_advance_to_nonempty_lyric (iter))
    {
      gint id = ol_lrc_iter_get_id (iter);
      if (id != priv->lrc_id)
      {
        /* Keep the active lyric on row 0 and the upcoming lyric on row 1. */
        priv->lrc_id = id;
        priv->current_line = 0;
        priv->lrc_next_id = -1;
        set_current_line (priv, 0);
        set_lyric_row (priv, 0, ol_lrc_iter_get_text (iter));
        set_lyric_percentage (priv, 0, 0.0);
        ol_osd_module_update_next_lyric (priv, iter);
      }
      gdouble percentage = ol_lrc_iter_compute_percentage (iter, played_time);
      set_current_percentage (priv, percentage);
      if (percentage > 0.5 && priv->lrc_next_id == -1)
        ol_osd_module_update_next_lyric (priv, iter);
      sync_layer_shell_helper (priv);
    }
    else if (priv->lrc_id != -1 || priv->force_refresh_on_set_played_time)
    {
      hide_lyrics (priv);
      reset_lyrics_state (priv);
    }
    ol_lrc_iter_free (iter);

    priv->force_refresh_on_set_played_time = FALSE;
  }
}

static gboolean
_advance_to_nonempty_lyric (OlLrcIter *iter)
{
  for (; ol_lrc_iter_is_valid (iter); ol_lrc_iter_next (iter))
  {
    if (!ol_is_string_empty (ol_lrc_iter_get_text (iter)))
      return TRUE;
  }
  return FALSE;
}

static void
ol_osd_module_set_lrc (struct OlDisplayModule *module, OlLrc *lrc_file)
{
  ol_log_func ();
  ol_assert (module != NULL);
  OlOsdModule *priv = ol_display_module_get_data (module);
  ol_assert (priv != NULL);
  if (priv->lrc)
    g_object_unref (priv->lrc);
  if (lrc_file)
    g_object_ref (lrc_file);

  if (is_message_displayed (priv))
  {
    /* A message can only be displayed if no lyrics are currently assigned. */
    ol_assert (priv->lrc == NULL);
    ol_osd_module_clear_message (module);
  }
  else if (lrc_file == NULL)
  {
    hide_lyrics (priv);
  }
  else
  {
    priv->force_refresh_on_set_played_time = TRUE;
  }

  priv->lrc = lrc_file;
  reset_lyrics_state (priv);
}

static void
ol_osd_module_set_message (struct OlDisplayModule *module,
                           const char *message,
                           int duration_ms)
{
  ol_log_func ();
  ol_assert (module != NULL);
  OlOsdModule *priv = ol_display_module_get_data (module);
  ol_assert (priv != NULL);
  ol_assert (message != NULL);
  ol_assert (priv->window != NULL);
  if (priv->lrc != NULL)
    return;
  ol_debugf ("  message:%s\n", message);
  set_current_line (priv, 0);
  set_current_percentage (priv, 1.0);
  set_lyric_row (priv, 0, message);
  set_lyric_row (priv, 1, NULL);
  if (is_message_displayed (priv))
    g_source_remove (priv->message_source);
  priv->message_source = g_timeout_add (duration_ms,
                                        (GSourceFunc) hide_message,
                                        (gpointer) priv);
  sync_layer_shell_helper (priv);
}

static void
ol_osd_module_search_message (struct OlDisplayModule *module, const char *message)
{
  ol_osd_module_set_message (module, message, -1);
}

static void
ol_osd_module_search_fail_message (struct OlDisplayModule *module, const char *message)
{
  ol_osd_module_set_message (module, message, MESSAGE_DURATION_MS);
}

static void
ol_osd_module_download_fail_message (struct OlDisplayModule *module, const char *message)
{
  ol_osd_module_set_message (module, message, MESSAGE_DURATION_MS);
}

static gboolean
hide_message (OlOsdModule *osd)
{
  ol_log_func ();
  ol_assert_ret (osd != NULL, FALSE);
  ol_assert_ret (osd->lrc == NULL, FALSE);
  set_lyric_row (osd, 0, NULL);
  set_lyric_row (osd, 1, NULL);
  osd->message_source = 0;
  sync_layer_shell_helper (osd);
  return FALSE;
}

static gboolean
is_message_displayed (OlOsdModule *osd)
{
  ol_assert_ret (osd != NULL, FALSE);
  return osd->message_source != 0;
}

static void
reset_lyrics_state (OlOsdModule *osd)
{
  osd->current_line = 0;
  osd->lrc_id = -1;
  osd->lrc_next_id = -1;
}

static void
hide_lyrics (OlOsdModule *osd)
{
  ol_log_func ();
  if (osd->window != NULL && !is_message_displayed (osd))
  {
    set_lyric_row (osd, 0, NULL);
    set_lyric_row (osd, 1, NULL);
    sync_layer_shell_helper (osd);
  }
}

static void
set_lyric_row (OlOsdModule *osd, gint row, const char *text)
{
  ol_assert (osd != NULL);
  ol_assert (osd->window != NULL);
  if (!osd->layer_shell_enabled)
  {
    ol_osd_window_set_lyric (osd->window, row, text);
    return;
  }

  g_free (osd->window->lyrics[row]);
  osd->window->lyrics[row] = g_strdup (text);
}

static void
set_lyric_percentage (OlOsdModule *osd, gint row, gdouble percentage)
{
  ol_assert (osd != NULL);
  ol_assert (osd->window != NULL);
  if (!osd->layer_shell_enabled)
  {
    ol_osd_window_set_percentage (osd->window, row, percentage);
    return;
  }

  osd->window->percentage[row] = percentage;
}

static void
set_current_line (OlOsdModule *osd, gint line)
{
  ol_assert (osd != NULL);
  ol_assert (osd->window != NULL);
  if (!osd->layer_shell_enabled)
  {
    ol_osd_window_set_current_line (osd->window, line);
    return;
  }

  osd->window->current_line = line;
}

static void
set_current_percentage (OlOsdModule *osd, gdouble percentage)
{
  ol_assert (osd != NULL);
  ol_assert (osd->window != NULL);
  set_lyric_percentage (osd, osd->window->current_line, percentage);
}

static gboolean
layer_shell_helper_available (void)
{
  return g_getenv ("WAYLAND_DISPLAY") != NULL;
}

static void
stop_layer_shell_helper (OlOsdModule *osd)
{
  if (osd->layer_shell_stdin >= 0)
  {
    close (osd->layer_shell_stdin);
    osd->layer_shell_stdin = -1;
  }
  if (osd->layer_shell_pid != 0)
  {
    g_spawn_close_pid (osd->layer_shell_pid);
    osd->layer_shell_pid = 0;
  }
  osd->layer_shell_enabled = FALSE;
}

static gboolean
start_layer_shell_helper (OlOsdModule *osd)
{
  gchar *helper = NULL;
  gchar *fallback_helper = NULL;
  fallback_helper = g_build_filename (g_get_current_dir (),
                                      "tools",
                                      "osdlyrics-layer-osd",
                                      NULL);
  if (g_file_test (fallback_helper, G_FILE_TEST_IS_EXECUTABLE))
  {
    helper = g_strdup (fallback_helper);
  }
  if (helper == NULL && g_file_test ("/usr/bin/osdlyrics-layer-osd", G_FILE_TEST_IS_EXECUTABLE))
  {
    helper = g_strdup ("/usr/bin/osdlyrics-layer-osd");
  }
  if (helper == NULL && g_file_test ("/bin/osdlyrics-layer-osd", G_FILE_TEST_IS_EXECUTABLE))
  {
    helper = g_strdup ("/bin/osdlyrics-layer-osd");
  }
  if (helper == NULL)
    helper = g_find_program_in_path ("osdlyrics-layer-osd");
  if (helper == NULL)
  {
    g_free (fallback_helper);
    return FALSE;
  }

  gchar *argv[] = { helper, NULL };
  GError *error = NULL;
  GPid pid = 0;
  gint stdin_fd = -1;
  gboolean ok = g_spawn_async_with_pipes (NULL,
                                          argv,
                                          NULL,
                                          G_SPAWN_SEARCH_PATH,
                                          NULL,
                                          NULL,
                                          &pid,
                                          NULL,
                                          &stdin_fd,
                                          NULL,
                                          &error);
  g_free (helper);
  g_free (fallback_helper);
  if (!ok)
  {
    if (error != NULL)
      g_error_free (error);
    return FALSE;
  }

  osd->layer_shell_pid = pid;
  osd->layer_shell_stdin = stdin_fd;
  osd->layer_shell_enabled = TRUE;
  return TRUE;
}

static void
sync_layer_shell_helper (OlOsdModule *osd)
{
  if (!osd->layer_shell_enabled || osd->layer_shell_stdin < 0 || osd->window == NULL)
    return;

  hide_legacy_window_if_layer_enabled (osd);

  const char *current = osd->window->lyrics[0] != NULL ? osd->window->lyrics[0] : "";
  const char *next = osd->window->lyrics[1] != NULL ? osd->window->lyrics[1] : "";
  gchar *current_b64 = g_base64_encode ((const guchar *) current, strlen (current));
  gchar *next_b64 = g_base64_encode ((const guchar *) next, strlen (next));
  gchar *payload = g_strdup_printf ("STATE\t%d\t%.6f\t%s\t%s\n",
                                    osd->layer_shell_visible ? 1 : 0,
                                    ol_osd_window_get_current_percentage (osd->window),
                                    current_b64,
                                    next_b64);
  gsize payload_len = strlen (payload);
  gsize offset = 0;
  while (offset < payload_len)
  {
    ssize_t written = write (osd->layer_shell_stdin,
                             payload + offset,
                             payload_len - offset);
    if (written > 0)
    {
      offset += written;
      continue;
    }
    if (written < 0 && errno == EINTR)
      continue;
    stop_layer_shell_helper (osd);
    break;
  }
  g_free (payload);
  g_free (current_b64);
  g_free (next_b64);
}

static void
hide_legacy_window_if_layer_enabled (OlOsdModule *osd)
{
  if (osd == NULL || !osd->layer_shell_enabled || osd->window == NULL)
    return;
  if (gtk_widget_get_visible (GTK_WIDGET (osd->window)))
    gtk_widget_hide (GTK_WIDGET (osd->window));
}

static void
ol_osd_module_clear_message (struct OlDisplayModule *module)
{
  ol_log_func ();
  ol_assert (module != NULL);
  OlOsdModule *priv = ol_display_module_get_data (module);
  ol_assert (priv != NULL);
  if (is_message_displayed (priv))
  {
    g_source_remove (priv->message_source);
    hide_message (priv);
  }
  ol_debug ("  clear message done");
}

struct OlDisplayClass*
ol_osd_module_get_class ()
{
  struct OlDisplayClass *klass = ol_display_class_new ("OSD",
                                                       (OlDisplayInitFunc) ol_osd_module_new,
                                                       ol_osd_module_free);
  klass->clear_message = ol_osd_module_clear_message;
  klass->download_fail_message = ol_osd_module_download_fail_message;
  klass->search_fail_message = ol_osd_module_search_fail_message;
  klass->search_message = ol_osd_module_search_message;
  klass->set_lrc = ol_osd_module_set_lrc;
  klass->set_message = ol_osd_module_set_message;
  klass->set_played_time = ol_osd_module_set_played_time;
  return klass;
}
