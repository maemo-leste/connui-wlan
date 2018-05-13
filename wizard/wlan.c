#include <connui/connui.h>
#include <connui/connui-log.h>
#include <connui/wlan-common.h>
#include <connui/libicd-network-wlan-dev.h>
#include <connui/iapsettings/stage.h>
#include <connui/iapsettings/mapper.h>
#include <connui/iapsettings/widgets.h>
#include <connui/iapsettings/wizard.h>
#include <connui/iapsettings/advanced.h>
#include <icd/osso-ic-gconf.h>

#include <ctype.h>
#include <string.h>
#include <libintl.h>

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

#define EAP_GTC			6
#define EAP_TLS			13
#define EAP_TTLS		21
#define EAP_PEAP		25
#define EAP_MS			26
#define EAP_TTLS_PAP		98
#define EAP_TTLS_MS	99

struct wlan_plugin_private_t
{
  struct iap_wizard *iw;
  struct iap_wizard_plugin *plugin;
  struct stage stage[2];
  int active_stage;
  GtkTreeIter iter;
  gboolean no_conn_avail;
  int powersave;
  gboolean initialized;
  gboolean offline;
  osso_context_t *osso;
  osso_display_state_t display_prev_state;
};

typedef struct wlan_plugin_private_t wlan_plugin_private;

static gboolean
no_export(struct stage *s, const gchar *key, const gchar *name)
{
  return 0;
}

static gboolean
export_EAP_default_type(struct stage *s, const gchar *name,
                        const gchar *key)
{
  int type = stage_get_int(s, "EAP_default_type");

  return (type == EAP_TTLS || type == EAP_TLS || type == EAP_PEAP);
}

static gboolean
is_wlan(const struct stage *s, const gchar *name, const gchar *key)
{
  gchar *type = stage_get_string(s, "type");
  gboolean rv;


  if (type)
    rv = !strncmp(type, "WLAN_", 4);
  else
    rv = FALSE;

  g_free(type);

  return rv;
}

static gboolean
is_wpa(const struct stage *s, const gchar *name, const gchar *key)
{
  gchar *security = stage_get_string(s, "wlan_security");
  gboolean rv;

  if (!security)
    return FALSE;

  rv = !strncmp(security, "WPA_", 4);
  g_free(security);

  return rv;
}

static gboolean
is_wlan_adhoc(const struct stage *s, const gchar *name, const gchar *key)
{
  gchar *type = stage_get_string(s, "type");
  gboolean rv;

  if (type)
    rv = !strcmp(type, "WLAN_ADHOC");
  else
    rv = FALSE;

  g_free(type);

  return rv;
}

static gboolean
validate_wlan_security(const struct stage *s, const gchar *name,
                       const gchar *key)
{
  gboolean rv;
  gchar *security = stage_get_string(s, "wlan_security");

  if (!security)
    return FALSE;

  if ((!strcmp(security, "WEP") && strstr(name, "_WEP_")) ||
      (!strcmp(security, "WPA_PSK") && strstr(name, "_WPA_")) ||
      (!strcmp(security, "WPA_EAP") && strstr(name, "_EAP_")))
  {
    rv = TRUE;
  }
  else
    rv = FALSE;

  g_free(security);

  return rv;
}

static gboolean
validate_EAP_default_type(const struct stage *s, const gchar *name,
                          const gchar *key)
{
  int type = stage_get_int(s, "EAP_default_type");

  if (strstr(name, "_TLS_") && type == EAP_TLS)
    return TRUE;

  if (strstr(name, "_TTLS_") &&  type == EAP_TTLS)
    return TRUE;

  if (strstr(name, "_PEAP_") && type == EAP_PEAP)
    return TRUE;

  return FALSE;
}

static gboolean
validate_wlan_eap(const struct stage *s, const gchar *name, const gchar *key)
{
  if (key && strstr(key, "_GTC_"))
  {
    if (stage_get_int(s, "PEAP_tunneled_eap_type") == EAP_GTC)
      return TRUE;
  }
  else
  {
    int type = stage_get_int(s, "PEAP_tunneled_eap_type");

    if (type == EAP_MS || type == EAP_TTLS_MS || type == EAP_TTLS_PAP)
      return TRUE;
  }

  return FALSE;
}

static gboolean
validate_wlan_eap_client_auth(const struct stage *s, const gchar *name,
                              const gchar *key)
{
  int type = stage_get_int(s, "EAP_default_type");

  return type == EAP_PEAP || type == EAP_TLS;
}

static char *wlan_modes[] = {"WLAN_INFRA", "WLAN_ADHOC", NULL};
static char *wlan_security[] = {"NONE", "WEP", "WPA_PSK", "WPA_EAP", NULL};
static gint wlan_wepdefkey[] = {1, 2, 3, 4, -1};
static gint EAP_default_type[] = {EAP_PEAP, EAP_TLS, EAP_TTLS, -1};
static gint PEAP_tunneled_eap_type_values[] = {EAP_GTC, EAP_MS, EAP_TTLS_MS, EAP_TTLS_PAP, -1};
static gint wlan_powersave_values[] = {4, 2, 1, -1};

static struct stage_widget iap_wizard_wlan_widgets[] =
{
  {
    NULL,
    is_wlan,
    "WLAN_SSID",
    "wlan_ssid",
    NULL,
    &mapper_entry2bytearray,
    NULL
  },
  {
    NULL,
    is_wlan,
    "WLAN_HIDDEN",
    "wlan_hidden",
    NULL,
    &mapper_toggle2bool,
    NULL
  },
  {
    no_export,
    is_wlan,
    "WLAN_MODE",
    "type",
    NULL,
    &mapper_combo2string,
    wlan_modes
  },
  {
    NULL,
    is_wlan,
    "WLAN_SECURITY",
    "wlan_security",
    NULL,
    &mapper_combo2string,
    wlan_security
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_WEP_DEF_KEY",
    "wlan_wepdefkey",
    NULL,
    &mapper_combo2int,
    wlan_wepdefkey
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_WEP_KEY1",
    "wlan_wepkey1",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_WEP_KEY2",
    "wlan_wepkey2",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_WEP_KEY3",
    "wlan_wepkey3",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_WEP_KEY4",
    "wlan_wepkey4",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_WPA_KEY",
    "EAP_wpa_preshared_passphrase",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_EAP_TYPE",
    "EAP_default_type",
    NULL,
    &mapper_combo2int,
    &EAP_default_type
  },
  {
    export_EAP_default_type,
    validate_EAP_default_type,
    "WLAN_EAP_PEAP_TYPE",
    "PEAP_tunneled_eap_type",
    NULL,
    &mapper_combo2int,
    PEAP_tunneled_eap_type_values
  },
  {
    export_EAP_default_type,
    validate_EAP_default_type,
    "WLAN_EAP_TTLS_TYPE",
    "PEAP_tunneled_eap_type",
    NULL,
    &mapper_combo2int,
    PEAP_tunneled_eap_type_values
  },
  {
    NULL,
    validate_wlan_eap,
    "WLAN_EAP_USERNAME",
    "EAP_MSCHAPV2_username",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_eap,
    "WLAN_EAP_PASSWORD",
    "EAP_MSCHAPV2_password",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_eap,
    "WLAN_EAP_ASK_PASSWORD",
    "EAP_MSCHAPV2_password_prompt",
    NULL,
    &mapper_toggle2bool,
    NULL
  },
  {
    NULL,
    validate_wlan_eap,
    "WLAN_EAP_USERNAME",
    "EAP_GTC_identity",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    export_EAP_default_type,
    validate_EAP_default_type,
    "WLAN_EAP_TLS_CERTIFICATE",
    "EAP_TLS_PEAP_client_certificate_file",
    NULL,
    &mapper_combo2string,
    GUINT_TO_POINTER(1)
  },
  {
    export_EAP_default_type,
    validate_EAP_default_type,
    "WLAN_EAP_PEAP_CERTIFICATE",
    "EAP_TLS_PEAP_client_certificate_file",
    NULL,
    &mapper_combo2string,
    GUINT_TO_POINTER(1)
  },
  {
    export_EAP_default_type,
    validate_EAP_default_type,
    "WLAN_EAP_TTLS_CERTIFICATE",
    "EAP_TLS_PEAP_client_certificate_file",
    NULL,
    &mapper_combo2string,
    GUINT_TO_POINTER(1)
  },
  {
    NULL,
    is_wlan_adhoc,
    "WLAN_ADHOC_CHANNEL",
    "wlan_adhoc_channel",
    NULL,
    &mapper_combo2int,
    NULL
  },
  {
    NULL,
    is_wlan,
    "WLAN_POWERSAVE",
    "wlan_powersave",
    NULL,
    &mapper_combo2int,
    &wlan_powersave_values
  },
  {
    NULL,
    is_wpa,
    "WLAN_WPA2_ONLY",
    "EAP_wpa2_only_mode",
    NULL,
    &mapper_toggle2bool,
    NULL
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_EAP_USE_MANUAL",
    "EAP_use_manual_username",
    NULL,
    &mapper_toggle2bool,
    NULL
  },
  {
    NULL,
    validate_wlan_security,
    "WLAN_EAP_ID",
    "EAP_manual_username",
    NULL,
    &mapper_entry2string,
    NULL
  },
  {
    NULL,
    validate_wlan_eap_client_auth,
    "WLAN_EAP_CLIENT_AUTH",
    "TLS_server_authenticates_client_policy_in_client",
    NULL,
    &mapper_toggle2bool,
    NULL
  },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

static void
wlan_manual_ssid_entry_changed_cb(GtkEditable *editable,
                                  wlan_plugin_private *priv)
{
  iap_wizard_validate_finish_button(priv->iw);
}

static void
wlan_manual_mode_changed_cb(GtkComboBox *widget, wlan_plugin_private *priv)
{
  GtkComboBox *wlan_security = GTK_COMBO_BOX(
        g_hash_table_lookup(priv->plugin->widgets, "WLAN_SECURITY"));

  if (gtk_combo_box_get_active(widget))
  {
    if (gtk_combo_box_get_active(wlan_security) > 1)
      gtk_combo_box_set_active(wlan_security, 0);

    gtk_combo_box_remove_text(wlan_security, 3);
    gtk_combo_box_remove_text(wlan_security, 2);
  }
  else
  {
    gtk_combo_box_remove_text(wlan_security, 3);
    gtk_combo_box_remove_text(wlan_security, 2);
    gtk_combo_box_append_text(wlan_security,
                              _("conn_set_iap_fi_wlan_ap_security_wpa_psk"));
    gtk_combo_box_append_text(wlan_security,
                              _("conn_set_iap_fi_wlan_ap_security_wpa_eap"));
  }
}

static void
wlan_manual_security_changed_cb(GtkComboBox *widget, wlan_plugin_private *priv)
{
  iap_wizard_validate_finish_button(priv->iw);
}

static GtkWidget *
wlan_manual_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  struct iap_wizard *iw = priv->iw;
  GtkWidget *dialog = iap_wizard_get_dialog(priv->iw);
  GtkWidget *vbox;
  GtkSizeGroup *group;
  HildonGtkInputMode im;
  GtkWidget *caption;
  GtkWidget *widget;

  vbox = gtk_vbox_new(FALSE, 0);
  group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

  /* SSID entry */
  widget = gtk_entry_new();
  im = hildon_gtk_entry_get_input_mode(GTK_ENTRY(widget));
  im &= ~(HILDON_GTK_INPUT_MODE_AUTOCAP | HILDON_GTK_INPUT_MODE_DICTIONARY);
  hildon_gtk_entry_set_input_mode(GTK_ENTRY(widget), im);
  g_object_set(widget, "max-length", 32, NULL);
  g_signal_connect(G_OBJECT(widget), "changed",
                   G_CALLBACK(wlan_manual_ssid_entry_changed_cb), priv);
  g_signal_connect(G_OBJECT(widget), "insert_text",
                   G_CALLBACK(iap_widgets_insert_only_ascii_text), dialog);
  g_signal_connect(G_OBJECT(widget), "insert-text",
                   G_CALLBACK(iap_widgets_insert_text_no_8bit_maxval_reach),
                   dialog);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_SSID"), widget);

  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_ap_ssid"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  /* WLAN hidden check buttton */
  widget = gtk_check_button_new();
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_HIDDEN"), widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_ap_hidden_ssid"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  /* WLAN mode combo box */
  widget = iap_widgets_create_static_combo_box(
        _("conn_set_iap_fi_wlan_ap_mode_infra"),
        _("conn_set_iap_fi_wlan_ap_mode_adhoc"),
        NULL);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_MODE"), widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_ap_mode"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);
  g_signal_connect(G_OBJECT(widget), "changed",
                   G_CALLBACK(wlan_manual_mode_changed_cb), priv);

  /* WLAN security combo box */
  widget = iap_widgets_create_static_combo_box(
        _("conn_set_iap_fi_wlan_ap_security_none"),
        _("conn_set_iap_fi_wlan_ap_security_wep"),
        _("conn_set_iap_fi_wlan_ap_security_wpa_psk"),
        _("conn_set_iap_fi_wlan_ap_security_wpa_eap"),
        NULL);

  g_signal_connect(G_OBJECT(widget), "changed",
                   G_CALLBACK(wlan_manual_security_changed_cb), iw);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_SECURITY"), widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_ap_security"),
                               widget,NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  g_object_unref(G_OBJECT(group));

  return vbox;
}

static const char *
wlan_manual_get_page(gpointer user_data, gboolean show_note)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *widget;
  const gchar *ssid;
  const char *result;
  GtkWidget *dialog;

  dialog = iap_wizard_get_dialog(priv->iw);
  widget = GTK_WIDGET(g_hash_table_lookup(priv->plugin->widgets, "WLAN_SSID"));
  ssid = gtk_entry_get_text(GTK_ENTRY(widget));

  if (!GTK_WIDGET_SENSITIVE(widget) || !GTK_WIDGET_PARENT_SENSITIVE(widget) ||
      (ssid && *ssid))
  {
    static const char *page_ids[] =
                 {"COMPLETE", "WLAN_WEP", "WLAN_WPA_PRESHARED", "WLAN_WPA_EAP"};
    gint mode = gtk_combo_box_get_active(
          GTK_COMBO_BOX(g_hash_table_lookup(priv->plugin->widgets,
                                            "WLAN_MODE")));
    gint security = gtk_combo_box_get_active(
          GTK_COMBO_BOX(g_hash_table_lookup(priv->plugin->widgets,
                                            "WLAN_SECURITY")));


    if (mode < 0 || security < 0)
      return NULL;

    result = page_ids[security];
  }
  else
  {
    if (show_note)
    {
      hildon_banner_show_information(GTK_WIDGET(dialog), NULL,
                                     _("conn_ib_enter_name"));
      gtk_widget_grab_focus(widget);
    }

    return NULL;
  }

  return result;
}

static void
iap_wizard_wlan_tree_change(GtkTreeSelection *selection,
                            wlan_plugin_private *priv)
{
  struct iap_wizard *iw = priv->iw;
  dbus_uint32_t caps;
  GtkTreeIter iter;
  connui_scan_entry *scan_entry = NULL;
  gchar *ssid;
  GtkTreeModel *model;

  if (!gtk_tree_selection_get_selected(selection, &model, &iter))
    return;

  gtk_tree_model_get(model, &iter,
                     IAP_SCAN_LIST_SSID, &ssid,
                     IAP_SCAN_LIST_SCAN_ENTRY, &scan_entry,
                     -1);

  if (scan_entry)
    nwattr2cap(scan_entry->network.network_attributes, &caps);
  else
    caps = 0;

  ULOG_DEBUG("iap_wizard_wlan_tree_change(): ssid '%s'", ssid);

  if (ssid)
  {
    GtkWidget *widget = GTK_WIDGET(g_hash_table_lookup(priv->plugin->widgets,
                                                       "WLAN_SSID"));
    struct stage *s = iap_wizard_get_active_stage(iw);
    int wlan_security;

    stage_set_bytearray(s, "wlan_ssid", ssid);
    gtk_widget_set_sensitive(widget,
                             wlan_common_mangle_ssid(ssid, strlen(ssid)));
    gtk_entry_set_text(GTK_ENTRY(widget), ssid);

    widget = GTK_WIDGET(g_hash_table_lookup(priv->plugin->widgets,
                                            "WLAN_MODE"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), caps & WLANCOND_ADHOC);

    if (caps & WLANCOND_WEP)
      wlan_security = 1;
    else if (caps & WLANCOND_WPA_PSK)
      wlan_security = 2;
    else if (caps & WLANCOND_WPA_EAP)
      wlan_security = 3;
    else
      wlan_security = 0;

    gtk_combo_box_set_active(
          GTK_COMBO_BOX(g_hash_table_lookup(priv->plugin->widgets,
                                            "WLAN_SECURITY")),
          wlan_security);
  }
}

static GtkWidget *
wlan_scan_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *scan_tree;
  GtkTreeSelection *selection;

  scan_tree = iap_scan_tree_create(NULL, NULL);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("SCAN_VIEW"), scan_tree);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(scan_tree));
  g_signal_connect(G_OBJECT(selection), "changed",
                   G_CALLBACK(iap_wizard_wlan_tree_change),priv);

  return iap_scan_view_create(scan_tree);
}

static void
scan_cancel(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *dialog;
  GtkWidget *scan_view;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;

  dialog = iap_wizard_get_dialog(priv->iw);

  if (dialog)
    hildon_gtk_window_set_progress_indicator(GTK_WINDOW(dialog), FALSE);

  scan_view = GTK_WIDGET(g_hash_table_lookup(priv->plugin->widgets,
                                             "SCAN_VIEW"));
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(scan_view));
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(scan_view));

  if (!priv->no_conn_avail && !gtk_tree_model_get_iter_first(model, &iter))
  {
    gtk_list_store_append(GTK_LIST_STORE(model), &priv->iter);
    gtk_list_store_set(GTK_LIST_STORE(model), &priv->iter,
                       IAP_SCAN_LIST_SERVICE_TEXT,
                       _("conn_fi_no_conn_available"),
                       -1);
    gtk_widget_set_sensitive(scan_view, FALSE);
    priv->no_conn_avail = TRUE;
  }

  iap_wizard_wlan_tree_change(selection, priv);
}

static void
wlan_scan_prev(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;

  scan_cancel(priv);
  iap_scan_close();

  if (priv)
  {
    osso_deinitialize(priv->osso);
    priv->osso = NULL;
  }
}

static const char *
wlan_scan_get_page(gpointer user_data, gboolean show_note)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *scan_view;
  GtkTreeSelection *selection;
  GtkTreeIter iter;

  scan_view = GTK_WIDGET(g_hash_table_lookup(priv->plugin->widgets,
                                             "SCAN_VIEW"));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(scan_view));

  if (GTK_WIDGET_SENSITIVE(scan_view) &&
      GTK_WIDGET_PARENT_SENSITIVE(scan_view) &&
      gtk_tree_selection_get_selected(selection, NULL, &iter))
  {
    return wlan_manual_get_page(priv, show_note);
  }

  if (show_note)
  {
    wlan_scan_prev(priv);
    iap_wizard_set_current_page(priv->iw, "WLAN_MANUAL");
  }

  return NULL;
}

static void
scan_started(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *dialog = iap_wizard_get_dialog(priv->iw);

  if (dialog)
    hildon_gtk_window_set_progress_indicator(GTK_WINDOW(dialog), TRUE);
}

static gboolean
scan_network_added(connui_scan_entry *scan_entry, gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  gchar *network_type = scan_entry->network.network_type;
  gchar *network_id;
  GtkTreeView *tree_view;

  if (!network_type)
    return FALSE;

  if (strncmp(network_type, "WLAN_", 5))
    return FALSE;

  network_id = scan_entry->network.network_id;

  if (!network_id || !*network_id ||
      (scan_entry->network.service_type && *scan_entry->network.service_type))
  {
    return FALSE;
  }

  if (!priv->no_conn_avail)
    return TRUE;

  tree_view = GTK_TREE_VIEW(g_hash_table_lookup(priv->plugin->widgets,
                                                "SCAN_VIEW"));
  gtk_list_store_remove(GTK_LIST_STORE(gtk_tree_view_get_model(tree_view)),
                        &priv->iter);
  gtk_widget_set_sensitive(GTK_WIDGET(tree_view), TRUE);
  priv->no_conn_avail = FALSE;
  return TRUE;
}

static void
display_event_cb(osso_display_state_t state, gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  gchar *network_types[] = {"WLAN_INFRA", "WLAN_ADHOC", NULL};

  if (!priv)
    return;

  if (state == OSSO_DISPLAY_OFF)
  {
    scan_cancel(priv);
    iap_scan_close();
  }
  else if (state == OSSO_DISPLAY_ON &&
           priv->display_prev_state == OSSO_DISPLAY_OFF)
  {
    GtkTreeView *tree_view =
        GTK_TREE_VIEW(g_hash_table_lookup(priv->plugin->widgets, "SCAN_VIEW"));

    gtk_widget_set_sensitive(GTK_WIDGET(tree_view), OSSO_DISPLAY_OFF);
    gtk_list_store_clear(GTK_LIST_STORE(gtk_tree_view_get_model(tree_view)));
    priv->no_conn_avail = FALSE;

    if (!iap_scan_start_for_network_types(network_types, 0, scan_started,
                                          scan_cancel, scan_network_added,
                                          GTK_WIDGET(tree_view),
                                          NULL, NULL, priv))
      scan_cancel(priv);
  }

  priv->display_prev_state = state;
}

static void
wlan_scan_finish(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;

  priv->osso = connui_utils_inherit_osso_context(priv->iw->osso, PACKAGE_NAME,
                                                 PACKAGE_VERSION);
  if (priv->osso)
  {
    if (priv->iw && priv->iw->osso)
    {
      priv->display_prev_state = OSSO_DISPLAY_OFF;
      osso_hw_set_display_event_cb(priv->osso, display_event_cb, priv);
    }
  }
  else
    CONNUI_ERR("Couldn't init osso context");
}

static const char *wepk_msgid[] =
{
  "conn_set_iap_fi_wlan_wepk_data1",
  "conn_set_iap_fi_wlan_wepk_data2",
  "conn_set_iap_fi_wlan_wepk_data3",
  "conn_set_iap_fi_wlan_wepk_data4"
};

static GtkWidget *
wlan_wep_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  int i;
  GtkWidget *combo_box;
  GtkWidget *caption;
  GtkWidget *vbox;
  GtkSizeGroup *group;
  GtkWidget *dialog;

  dialog = iap_wizard_get_dialog(priv->iw);
  vbox = gtk_vbox_new(0, 0);
  group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  combo_box = iap_widgets_create_static_combo_box(
        _("conn_set_iap_fi_wlan_wepk_ind_value1"),
        _("conn_set_iap_fi_wlan_wepk_ind_value2"),
        _("conn_set_iap_fi_wlan_wepk_ind_value3"),
        _("conn_set_iap_fi_wlan_wepk_ind_value4"),
        NULL);

  g_signal_connect(G_OBJECT(combo_box), "changed",
                   G_CALLBACK(wlan_manual_ssid_entry_changed_cb), priv);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_WEP_DEF_KEY"),
                                                      combo_box);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_wepk_ind"),
                               combo_box, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  for (i = 0; i < G_N_ELEMENTS(wepk_msgid); i++)
  {
    HildonGtkInputMode im;
    GtkWidget *entry = gtk_entry_new();

    im = hildon_gtk_entry_get_input_mode(GTK_ENTRY(entry));
    im &= ~(HILDON_GTK_INPUT_MODE_AUTOCAP | HILDON_GTK_INPUT_MODE_DICTIONARY);
    im |= HILDON_GTK_INPUT_MODE_INVISIBLE;
    hildon_gtk_entry_set_input_mode(GTK_ENTRY(entry), im);
    g_object_set(G_OBJECT(entry), "max-length", 26, NULL);
    g_signal_connect(G_OBJECT(entry), "insert_text",
                     G_CALLBACK(iap_widgets_insert_only_ascii_text), dialog);
    g_signal_connect(G_OBJECT(entry), "insert-text",
                     G_CALLBACK(iap_widgets_insert_text_maxval_reach), dialog);
    g_signal_connect(G_OBJECT(entry), "changed",
                     G_CALLBACK(wlan_manual_ssid_entry_changed_cb), priv);
    g_hash_table_insert(priv->plugin->widgets,
                        g_strdup_printf("WLAN_WEP_KEY%d", i + 1), entry);
    caption = hildon_caption_new(group, _(wepk_msgid[i]), entry, NULL,
                                 HILDON_CAPTION_OPTIONAL);
    gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);
  }

  g_object_unref(G_OBJECT(group));

  return vbox;
}

static const char *
wlan_wep_get_page(gpointer user_data, gboolean show_note)
{
  wlan_plugin_private *priv = user_data;
  int idx;
  gchar *id;
  GtkEntry *entry;
  const char *err_msg = NULL;
  guint entered_keys = 0;

  for (idx = 0; idx < 4; idx++)
  {
    gchar *id = g_strdup_printf("WLAN_WEP_KEY%d", idx + 1);
    const char *key;
    int len;

    entry = GTK_ENTRY(g_hash_table_lookup(priv->plugin->widgets, id));
    key = gtk_entry_get_text(entry);
    len = strlen(key);
    g_free(id);

    if (!len)
      continue;

    entered_keys |= (1 << idx);

    if (len == WLANCOND_MIN_KEY_LEN || len == WLANCOND_MAX_KEY_LEN)
      continue;

    if (len == 2 * WLANCOND_MIN_KEY_LEN || len == 2 * WLANCOND_MAX_KEY_LEN)
    {
      int i;

      for (i = 0; i < len; i++)
      {
        if (!isxdigit(key[i]))
        {
          err_msg = _("conn_ib_wepkey_invalid_characters");
          break;
        }
      }

      if (i != len)
        break;
    }
    else
    {
      err_msg = _("conn_ib_wepkey_invalid_length");
      break;
    }
  }

  if (idx == 4)
  {
    gint active = gtk_combo_box_get_active(
          GTK_COMBO_BOX(g_hash_table_lookup(priv->plugin->widgets,
                                            "WLAN_WEP_DEF_KEY")));

    if (active >= 0 && (entered_keys & (1 << active)))
      return "COMPLETE";

    id = g_strdup_printf("WLAN_WEP_KEY%d", active + 1);

    if (active >= 0 && active < 4)
      entry = GTK_ENTRY(g_hash_table_lookup(priv->plugin->widgets, id));
    else
      entry = NULL;

    g_free(id);
  }

  if (!show_note)
    return NULL;

  if (err_msg)
  {
    hildon_banner_show_information(
          iap_wizard_get_dialog(priv->iw), NULL, err_msg);
  }

  if (!entry)
    return NULL;

  gtk_widget_grab_focus(GTK_WIDGET(entry));

  return NULL;
}

static GtkWidget *
wlan_wpa_preshared_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *dialog = iap_wizard_get_dialog(priv->iw);
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkWidget *entry = gtk_entry_new();
  HildonGtkInputMode im = hildon_gtk_entry_get_input_mode(GTK_ENTRY(entry));
  GtkWidget *caption;

  im &= ~(HILDON_GTK_INPUT_MODE_AUTOCAP | HILDON_GTK_INPUT_MODE_DICTIONARY);
  im |= HILDON_GTK_INPUT_MODE_INVISIBLE;
  hildon_gtk_entry_set_input_mode(GTK_ENTRY(entry), im);
  g_object_set(G_OBJECT(entry), "max-length", 63, NULL);
  g_signal_connect(G_OBJECT(entry), "insert_text",
                   G_CALLBACK(iap_widgets_insert_only_ascii_text), dialog);
  g_signal_connect(G_OBJECT(entry), "insert-text",
                   G_CALLBACK(iap_widgets_insert_text_no_8bit_maxval_reach),
                   dialog);
  g_signal_connect(G_OBJECT(entry), "changed",
                   G_CALLBACK(wlan_manual_ssid_entry_changed_cb), priv);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_WPA_KEY"), entry);
  caption = hildon_caption_new(0, _("conn_set_iap_fi_wlan_wpa_psk_txt"), entry,
                               NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  return vbox;
}

static const char *
wlan_wpa_preshared_get_page(gpointer user_data, gboolean show_note)
{
  wlan_plugin_private *priv = user_data;
  gpointer widget = g_hash_table_lookup(priv->plugin->widgets, "WLAN_WPA_KEY");
  const char *pwd = gtk_entry_get_text(GTK_ENTRY(widget));

  if (pwd && strlen(pwd) > 7)
    return "COMPLETE";

  if (show_note)
  {
    hildon_banner_show_information(iap_wizard_get_dialog(priv->iw), NULL,
                                   _("conn_ib_min8val_req"));
    gtk_widget_grab_focus(GTK_WIDGET(widget));
  }

  return NULL;
}

static GtkWidget *
wlan_wpa_eap_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkWidget *combo_box;
  GtkWidget *caption;

  combo_box = iap_widgets_create_static_combo_box(
        _("conn_set_iap_fi_wlan_wpa_eap_type_peap"),
        _("conn_set_iap_fi_wlan_wpa_eap_type_tls"),
        _("conn_set_iap_fi_wlan_wpa_eap_type_ttls"),
        NULL);
  g_signal_connect(G_OBJECT(combo_box), "changed",
                   G_CALLBACK(wlan_manual_security_changed_cb), priv);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_EAP_TYPE"),
                      combo_box);
  caption = hildon_caption_new(0, _("conn_set_iap_fi_wlan_wpa_eap_type_txt"),
                               combo_box, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  return vbox;
}

static const char *
wlan_wpa_eap_get_page(gpointer user_data, gboolean show_note)
{
  wlan_plugin_private *priv = user_data;
  gpointer widget = g_hash_table_lookup(priv->plugin->widgets, "WLAN_EAP_TYPE");
  gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  static const char *eap_type_pages[] =
  {
    "WLAN_EAP_PEAP",
    "WLAN_EAP_TLS",
    "WLAN_EAP_TTLS"
  };

  if (active >= 0)
    return eap_type_pages[active];

  return NULL;
}

static GtkWidget *
wlan_eap_tls_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkWidget *combo_box = iap_widgets_create_certificate_combo_box();
  GtkWidget *caption;

  g_hash_table_insert(priv->plugin->widgets,
                      g_strdup("WLAN_EAP_TLS_CERTIFICATE"), combo_box);
  caption = hildon_caption_new(0, _("conn_set_iap_fi_wlan_sel_cert"), combo_box,
                               NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  return vbox;
}

static GtkWidget *
wlan_eap_peap_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkSizeGroup *group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  GtkWidget *widget;
  GtkWidget *caption;

  widget = iap_widgets_create_certificate_combo_box();
  g_hash_table_insert(priv->plugin->widgets,
                      g_strdup("WLAN_EAP_PEAP_CERTIFICATE"), widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_sel_cert"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);
  widget = iap_widgets_create_static_combo_box(
        _("conn_set_iap_fi_wlan_peap_meth_gtc"),
        _("conn_set_iap_fi_wlan_peap_meth_mschapv2"),
        NULL);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_EAP_PEAP_TYPE"),
                      widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_peap_meth"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);
  g_object_unref(G_OBJECT(group));

  return vbox;
}

static GtkWidget *
wlan_eap_ttls_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkSizeGroup *group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  GtkWidget *widget;
  GtkWidget *caption;
  GConfClient *gconf;
  gboolean pap_enabled;

  widget = iap_widgets_create_certificate_combo_box();
  g_hash_table_insert(priv->plugin->widgets,
                      g_strdup("WLAN_EAP_TTLS_CERTIFICATE"), widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_sel_cert"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  gconf = gconf_client_get_default();
  pap_enabled = gconf_client_get_bool(gconf,
                                      ICD_GCONF_SETTINGS"/ui/pap_enabled",
                                      NULL);
  g_object_unref(gconf);

  widget = iap_widgets_create_static_combo_box(
        _("conn_set_iap_fi_wlan_ttls_meth_gtc"),
        _("conn_set_iap_fi_wlan_ttls_meth_mschapv2"),
        _("conn_set_iap_fi_wlan_ttls_meth_mschapv2_no_eap"),
        pap_enabled ? "EAP PAP" : NULL,
        NULL);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_EAP_TTLS_TYPE"),
                      widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_ttls_meth"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);
  g_object_unref(G_OBJECT(group));

  return vbox;
}

static void
iap_wizard_wlan_eap_password_toggled_cb(GtkToggleButton *togglebutton,
                                        GtkWidget *caption)
{
  gtk_widget_set_sensitive(GTK_WIDGET(caption),
                           !gtk_toggle_button_get_active(togglebutton));
}

static GtkWidget *
wlan_eap_password_create(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkSizeGroup *group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  GtkWidget *widget;
  GtkWidget *caption;
  HildonGtkInputMode im;

  /* username */
  widget = gtk_entry_new();
  im = hildon_gtk_entry_get_input_mode(GTK_ENTRY(widget));
  im &= ~(HILDON_GTK_INPUT_MODE_AUTOCAP | HILDON_GTK_INPUT_MODE_DICTIONARY);
  hildon_gtk_entry_set_input_mode(GTK_ENTRY(widget), im);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_EAP_USERNAME"),
                      widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_username"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  /* password */
  widget = gtk_entry_new();
  im = hildon_gtk_entry_get_input_mode(GTK_ENTRY(widget));
  im &= ~(HILDON_GTK_INPUT_MODE_AUTOCAP | HILDON_GTK_INPUT_MODE_DICTIONARY);
  im |= HILDON_GTK_INPUT_MODE_INVISIBLE;
  hildon_gtk_entry_set_input_mode(GTK_ENTRY(widget), im);
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_EAP_PASSWORD"),
                      widget);
  caption = hildon_caption_new(group, _("conn_set_iap_fi_wlan_password"),
                               widget, NULL, HILDON_CAPTION_OPTIONAL);
  g_hash_table_insert(priv->plugin->widgets,
                      g_strdup("WLAN_EAP_PASSWORD_CAPTION"), caption);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  g_object_unref(G_OBJECT(group));

  /* password check button */
  widget = gtk_check_button_new();
  g_hash_table_insert(priv->plugin->widgets, g_strdup("WLAN_EAP_ASK_PASSWORD"), widget);
  g_signal_connect(G_OBJECT(widget), "toggled",
                   G_CALLBACK(iap_wizard_wlan_eap_password_toggled_cb),
                   caption);

  caption = hildon_caption_new(0, _("conn_set_iap_fi_wlan_ask_pw"), widget,
                               NULL, HILDON_CAPTION_OPTIONAL);
  g_hash_table_insert(priv->plugin->widgets,
                      g_strdup("WLAN_EAP_ASK_PASSWORD_CAPTION"), caption);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  return vbox;
}

static void
wlan_eap_password_finish(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  int type;
  const char *title;

  type = stage_get_int(iap_wizard_get_active_stage(priv->iw),
                       "PEAP_tunneled_eap_type");

  if (type == EAP_GTC)
  {
    gpointer widget;

    title = _("conn_set_iap_ti_wlan_wpa_eap_gtc");
    widget = g_hash_table_lookup(priv->plugin->widgets,
                                 "WLAN_EAP_PASSWORD_CAPTION");
    gtk_widget_hide(GTK_WIDGET(widget));
    widget = g_hash_table_lookup(priv->plugin->widgets,
                                 "WLAN_EAP_ASK_PASSWORD_CAPTION");
    gtk_widget_hide(GTK_WIDGET(widget));
  }
  else
  {
    gpointer widget;

    if (type == EAP_TTLS_PAP)
      title = "Connection setup: WPA EAP PAP";
    else
      title = _("conn_set_iap_ti_wlan_wpa_eap_mschapv2");

    widget = g_hash_table_lookup(priv->plugin->widgets,
                                 "WLAN_EAP_PASSWORD_CAPTION");
    gtk_widget_show(GTK_WIDGET(widget));
    widget = g_hash_table_lookup(priv->plugin->widgets,
                                 "WLAN_EAP_ASK_PASSWORD_CAPTION");
    gtk_widget_show(GTK_WIDGET(widget));
  }

  gtk_window_set_title(GTK_WINDOW(iap_wizard_get_dialog(priv->iw)), title);
}

static struct iap_wizard_page iap_wizard_wlan_pages[] =
{
  {
    "WLAN_MANUAL",
    "conn_set_iap_ti_wlan_ap",
    wlan_manual_create,
    wlan_manual_get_page,
    NULL,
    NULL,
    NULL,
    "Connectivity_Internetsettings_IAPsetupWLAN",
    NULL
  },
  {
    "WLAN_SCAN",
    "conn_set_iap_ti_wlan_scanned",
    wlan_scan_create,
    wlan_scan_get_page,
    wlan_scan_finish,
    wlan_scan_prev,
    NULL,
    "Connectivity_Internetsettings_IAPsetupscannedWLANs",
    NULL
  },
  {
    "WLAN_WEP",
    "conn_set_iap_ti_wlan_wepkey",
    wlan_wep_create,
    wlan_wep_get_page,
    NULL,
    NULL,
    NULL,
    "Connectivity_Internetsettings_IAPsetupWLANwepkey",
    NULL
  },
  {
    "WLAN_WPA_PRESHARED",
    "conn_set_iap_ti_wlan_wpa_psk",
    wlan_wpa_preshared_create,
    wlan_wpa_preshared_get_page,
    NULL,
    NULL,
    NULL,
    "Connectivity_Internetsettings_IAPsetupWLANwpapsk",
    NULL
  },
  {
    "WLAN_WPA_EAP",
    "conn_set_iap_ti_wlan_wpa_eap_type",
    wlan_wpa_eap_create,
    wlan_wpa_eap_get_page,
    NULL,
    NULL,
    NULL,
    "Connectivity_Internetsettings_IAPsetupWLANwpaeaptype",
    NULL
  },
  {
    "WLAN_EAP_TLS",
    "conn_set_iap_ti_wlan_wpa_eap_tls",
    wlan_eap_tls_create,
    NULL,
    NULL,
    NULL,
    "COMPLETE",
    "Connectivity_Internetsettings_IAPsetupWLANwpaeaptype",
    NULL
  },
  {
    "WLAN_EAP_PEAP",
    "conn_set_iap_ti_wlan_wpa_eap_peap",
    wlan_eap_peap_create,
    NULL,
    NULL,
    NULL,
    "WLAN_EAP_PASSWORD",
    "Connectivity_Internetsettings_IAPsetupWLANwpaeaptype",
    NULL
  },
  {
    "WLAN_EAP_TTLS",
    "conn_set_iap_ti_wlan_wpa_eap_ttls",
    wlan_eap_ttls_create,
    NULL,
    NULL,
    NULL,
    "WLAN_EAP_PASSWORD",
    "Connectivity_Internetsettings_IAPsetupWLANwpaeaptype",
    NULL
  },
  {
    "WLAN_EAP_PASSWORD",
    "conn_set_iap_ti_wlan_wpa_eap_mschapv2",
    wlan_eap_password_create,
    NULL,
    wlan_eap_password_finish,
    NULL,
    "COMPLETE",
    "Connectivity_Internetsettings_IAPsetupWLANwpaeaptype",
    NULL
  },
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

static void
iap_wizard_wlan_flightmode_status_cb(dbus_bool_t offline, gpointer user_data)
{
  wlan_plugin_private *priv = user_data;

  priv->offline = offline;
}

static void
iap_wizard_wlan_save_state(gpointer user_data, GByteArray *state)
{
  wlan_plugin_private *priv = user_data;

  stage_dump_cache(&priv->stage[0], state);
  stage_dump_cache(&priv->stage[1], state);
}

static void
iap_wizard_wlan_restore_state(gpointer user_data, struct stage_cache *s)
{
  wlan_plugin_private *priv = user_data;

  if (stage_restore_cache(&priv->stage[0], s))
    stage_restore_cache(&priv->stage[1], s);
}

static void
iap_wizard_wlan_advanced_show(gpointer user_data, struct stage *s)
{
  wlan_plugin_private *priv = user_data;
  GtkWidget *widget;

  widget = iap_wizard_get_widget(priv->iw, "WLAN_TX_POWER");

  if (widget)
  {
    gint active;
    GConfClient *gconf = gconf_client_get_default();
    gint wlan_tx_power = gconf_client_get_int(gconf,
                                              ICD_GCONF_PATH"/wlan_tx_power",
                                              NULL);
    g_object_unref(gconf);

    if (wlan_tx_power == 4)
      active = 0;
    else
      active = 1;

    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), active);
  }
}

static GtkWidget *
wlan_tx_power_create()
{
  GtkWidget *combo_box = gtk_combo_box_new_text();

  gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box),
                            _("conn_set_iap_fi_adv_misc_txpower_2"));
  gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box),
                            _("conn_set_iap_fi_adv_misc_txpower_3"));

  return combo_box;
}

static GtkWidget *
wlan_adhoc_channel_create()
{
  int i;
  GtkWidget *widget = gtk_combo_box_new_text();
  GtkComboBox *combo_box = GTK_COMBO_BOX(widget);
  char buf[16];

  gtk_combo_box_append_text(combo_box,
                            _("conn_set_iap_fi_adv_misc_adhoc_auto"));

  for (i = 2; i < 12; i++)
  {
    sprintf(buf, "%d", i);
    gtk_combo_box_append_text(combo_box, buf);
  }

  return widget;
}

static GtkWidget *
wlan_powersave_create()
{
  GtkWidget *widget = gtk_combo_box_new_text();
  GtkComboBox *combo_box = GTK_COMBO_BOX(widget);

  gtk_combo_box_append_text(combo_box,
                            _("conn_set_iap_fi_adv_misc_powersave_max"));
  gtk_combo_box_append_text(combo_box,
                            _("conn_set_iap_fi_adv_misc_powersave_med"));
  gtk_combo_box_append_text(combo_box,
                            _("conn_set_iap_fi_adv_misc_powersave_min"));

  return widget;
}

static struct iap_advanced_widget ti_adv_misc_advanced_widgets[] =
{
  {
    NULL,
    "WLAN_TX_POWER",
    NULL,
    NULL,
    "conn_set_iap_fi_adv_misc_txpower",
    wlan_tx_power_create,
    0
  },
  {
    &is_wlan_adhoc,
    "WLAN_ADHOC_CHANNEL",
    NULL,
    NULL,
    "conn_set_iap_fi_adv_misc_adhoc_ch",
    wlan_adhoc_channel_create,
    0
  },
  {
    is_wpa,
    "WLAN_WPA2_ONLY",
    NULL,
    NULL,
    "conn_set_iap_fi_adv_misc_wpa2",
    gtk_check_button_new,
    0
  },
  {
    is_wlan,
    "WLAN_POWERSAVE",
    NULL,
    NULL,
    "conn_set_iap_fi_adv_misc_powersave",
    wlan_powersave_create,
    0
  },
  {NULL, NULL, NULL, NULL, NULL, NULL, 0}
};

static GtkWidget *wlan_eap_use_manual_create()
{
  GtkWidget *entry;

  entry = gtk_entry_new();
  hildon_gtk_entry_set_input_mode(GTK_ENTRY(entry), HILDON_GTK_INPUT_MODE_FULL);

  return entry;
}

static struct iap_advanced_widget ti_adv_eap_advanced_widgets[] =
{
  {
    NULL,
    "WLAN_EAP_USE_MANUAL",
    NULL,
    NULL,
    "conn_set_iap_fi_wlan_use_man_eap_id",
    gtk_check_button_new,
    TRUE
  },
  {
    NULL,
    "WLAN_EAP_ID",
    "WLAN_EAP_USE_MANUAL",
    NULL,
    "conn_set_iap_fi_wlan_eap_id",
    wlan_eap_use_manual_create,
    0
  },
  {
    validate_wlan_eap_client_auth,
    "WLAN_EAP_CLIENT_AUTH",
    NULL,
    NULL,
    "conn_set_iap_fi_wlan_req_cli_auth",
    gtk_check_button_new,
    0
  },
  {NULL, NULL, NULL, NULL, NULL, NULL, 0}
};

static void
wlan_tx_power_changed_cb(GtkComboBox *widget, wlan_plugin_private *priv)
{
  const char *msgid;

  if (iap_wizard_get_import_mode(priv->iw))
    return;

  if (gtk_combo_box_get_active(widget))
    msgid = _("conn_ib_net_tx_to_100");
  else
    msgid = _("conn_ib_net_tx_to_10");

  if (msgid)
  {
    hildon_banner_show_information(gtk_widget_get_toplevel(GTK_WIDGET(widget)),
                                                           NULL, msgid);
  }
}

static void
iap_wizard_wlan_powersave_note_response_cb(GtkDialog *dialog, gint response_id,
                                           wlan_plugin_private *priv)
{
  GtkWidget *widget = iap_wizard_get_widget(priv->iw, "WLAN_POWERSAVE");

  if (response_id == GTK_RESPONSE_CANCEL)
  {
    iap_wizard_set_import_mode(priv->iw, 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), priv->powersave);
    iap_wizard_set_import_mode(priv->iw, 0);
  }
  else
    priv->powersave = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
wlan_powersave_changed_cb(GtkComboBox *widget, wlan_plugin_private *priv)
{
  gint powersave;

  if (iap_wizard_get_import_mode(priv->iw))
    return;

  powersave = gtk_combo_box_get_active(widget);

  if (powersave && powersave != priv->powersave)
  {
    GtkWidget *note;
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(widget));

    note = hildon_note_new_confirmation(GTK_WINDOW(toplevel),
                                        _("conn_nc_power_saving_warning"));
    iap_common_set_close_response(note, GTK_RESPONSE_CANCEL);
    g_signal_connect(note, "response",
                     G_CALLBACK(iap_wizard_wlan_powersave_note_response_cb),
                     priv);
    gtk_widget_show_all(note);
  }
}

static void
ti_adv_misc_advanced_activate(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;

  if (!priv->initialized)
  {
    GtkWidget *widget = iap_wizard_get_widget(priv->iw, "WLAN_TX_POWER");

    if (widget)
    {
      g_signal_connect(G_OBJECT(widget), "changed",
                       G_CALLBACK(wlan_tx_power_changed_cb), priv);
    }

    widget = iap_wizard_get_widget(priv->iw, "WLAN_POWERSAVE");

    if (widget)
      g_signal_connect(G_OBJECT(widget), "changed",
                       G_CALLBACK(wlan_powersave_changed_cb), priv);

    priv->initialized = TRUE;
  }
}

static struct iap_advanced_page iap_wizard_wlan_advanced_pages[] =
{
  {
    0,
    "conn_set_iap_ti_adv_misc",
    ti_adv_misc_advanced_widgets,
    ti_adv_misc_advanced_activate,
    "Connectivity_Internetsettings_IAPsetupAdvancedmiscCSD",
    NULL
  },
  {
    0,
    "conn_set_iap_ti_adv_eap",
    ti_adv_eap_advanced_widgets,
    NULL,
    "Connectivity_Internetsettings_IAPsetupAdvancedeapWLAN",
    NULL
  },
  {0, NULL, NULL, NULL, NULL, NULL}
};

static struct iap_advanced_page iap_wizard_wlan_advanced_pages_no_eap[] =
{
  {
    0,
    "conn_set_iap_ti_adv_misc",
    ti_adv_misc_advanced_widgets,
    ti_adv_misc_advanced_activate,
    "Connectivity_Internetsettings_IAPsetupAdvancedmiscCSD",
    NULL
  },
  {0, NULL, NULL, NULL, NULL, NULL}
};

static struct iap_advanced_page *
iap_wizard_wlan_get_advanced(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  struct stage *s = iap_wizard_get_active_stage(priv->iw);
  gchar *wlan_security;
  struct iap_advanced_page *rv;


  wlan_security = stage_get_string(s, "wlan_security");
  priv->initialized = FALSE;

  if (wlan_security && !strcmp(wlan_security, "WPA_EAP"))
    rv = iap_wizard_wlan_advanced_pages;
  else
    rv = iap_wizard_wlan_advanced_pages_no_eap;

  g_free(wlan_security);

  return rv;
}

static void
iap_wizard_wlan_advanced_done(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  struct stage *s = iap_wizard_get_active_stage(priv->iw);
  gchar *wlan_security = stage_get_string(s, "wlan_security");
  GtkWidget *widget = iap_wizard_get_widget(priv->iw, "WLAN_TX_POWER");

  if (widget)
  {
    gint wlan_tx_power;
    GConfClient *gconf;

    if (gtk_combo_box_get_active(GTK_COMBO_BOX(widget)))
      wlan_tx_power = 8;
    else
      wlan_tx_power = 4;

    gconf = gconf_client_get_default();
    gconf_client_set_int(gconf, ICD_GCONF_PATH"/wlan_tx_power", wlan_tx_power,
                         NULL);
    g_object_unref(gconf);
  }
  else if (wlan_security && !strcmp(wlan_security, "WPA_PSK"))
    stage_set_val(s, "EAP_wpa_preshared_key", NULL);

  g_free(wlan_security);

}

static const gchar **
iap_wizard_wlan_advanced_get_widgets(gpointer user_data)
{
  wlan_plugin_private *priv = user_data;
  struct stage *s;
  gchar *type = NULL;
  static gboolean not_initialized = TRUE;
  static const gchar *widgets[2] = {NULL, NULL};

  s = iap_wizard_get_active_stage(priv->iw);

  if (s)
    type = stage_get_string(s, "type");

  if (not_initialized || !type || (type && !strncmp(type, "WLAN_", 5)))
  {
    not_initialized = FALSE;
    widgets[0] = _("conn_set_iap_fi_wlan");
    g_free(type);
    return widgets;
  }

  widgets[0] = NULL;
  not_initialized = FALSE;
  g_free(type);

  return widgets;
}

static const char *
iap_wizard_wlan_get_page(gpointer user_data, int index, gboolean show_note)
{
  wlan_plugin_private *priv = user_data;
  struct stage *s;
  GtkWidget *dialog;
  gchar *type;
  const char *page;

  s = iap_wizard_get_active_stage(priv->iw);
  dialog = iap_wizard_get_dialog(priv->iw);

  if (!s && index == -1)
    return NULL;

  if (index != -1)
  {
    gpointer widget = iap_wizard_get_widget(priv->iw, "NAME");;
    const char *name = gtk_entry_get_text(GTK_ENTRY(widget));;

    if (name && *name && !iap_settings_is_empty(name))
    {
      gchar *wlan_ssid;

      if (s)
        wlan_ssid = stage_get_bytearray(s, "wlan_ssid");
      else
        wlan_ssid = NULL;

      if ((wlan_ssid && *wlan_ssid) || !show_note)
      {
        if (!priv->active_stage)
          page = "WLAN_SCAN";
        else
          page = "WLAN_MANUAL";
      }
      else if (show_note)
      {
        gint response_id;
        GtkWidget *note;

        priv->active_stage = 1;
        page = "WLAN_MANUAL";

        if (priv->offline)
        {
          note = hildon_note_new_information(GTK_WINDOW(dialog),
                                            _("conn_set_iap_fi_scan_wlan_no"));
          gtk_dialog_run(GTK_DIALOG(note));
          gtk_widget_destroy(note);
        }
        else
        {
          note = hildon_note_new_confirmation(GTK_WINDOW(dialog),
                                              _("conn_set_iap_fi_scan_wlan"));
          response_id = gtk_dialog_run(GTK_DIALOG(note));
          gtk_widget_destroy(note);

          if (response_id == GTK_RESPONSE_OK)
          {
            priv->active_stage = 0;
            page = "WLAN_SCAN";
          }
        }
      }

      g_free(wlan_ssid);
      iap_wizard_set_active_stage(priv->iw, &priv->stage[priv->active_stage]);
      return page;
    }

    if (show_note)
    {
      hildon_banner_show_information(GTK_WIDGET(dialog), NULL,
                                     _("conn_ib_enter_name"));
      gtk_widget_grab_focus(widget);
      return NULL;
    }

    return NULL;
  }

  type = stage_get_string(s, "type");

  if (type && !strncmp(type, "WLAN_", 5))
  {
    iap_wizard_select_plugin_label(priv->iw, "WLAN", 0);
    priv->active_stage = 1;

    if (s != &priv->stage[1])
    {
      stage_copy(s, &priv->stage[1]);
      iap_wizard_set_active_stage(priv->iw, &priv->stage[priv->active_stage]);
    }

    page = "WLAN_MANUAL";
  }
  else
    page = NULL;

  g_free(type);

  return page;
}

gboolean
iap_wizard_plugin_init(struct iap_wizard *iw,
                       struct iap_wizard_plugin *plugin)
{
  wlan_plugin_private *priv = g_new0(wlan_plugin_private, 1);
  struct stage *s;

  priv->iw = iw;
  priv->plugin = plugin;

  connui_flightmode_status(iap_wizard_wlan_flightmode_status_cb, priv);

  s = &priv->stage[0];
  stage_create_cache(s, NULL);
  s->name = g_strdup("WLAN_SCANNED");
  stage_set_string(s, "type", "WLAN_INFRA");
  stage_set_bool(s, "wlan_hidden", FALSE);
  stage_set_int(s, "wlan_adhoc_channel", 0);

  s = &priv->stage[1];
  stage_create_cache(s, NULL);
  s->name = g_strdup("WLAN");
  stage_set_string(s, "type", "WLAN_INFRA");
  stage_set_bool(s, "wlan_hidden", TRUE);
  stage_set_int(s, "wlan_adhoc_channel", 0);

  plugin->name = "WLAN";
  plugin->prio = 1000;
  plugin->priv = priv;

  plugin->get_advanced = iap_wizard_wlan_get_advanced;
  plugin->stage_widgets = iap_wizard_wlan_widgets;
  plugin->pages = iap_wizard_wlan_pages;
  plugin->get_widgets = iap_wizard_wlan_advanced_get_widgets;
  plugin->advanced_show = iap_wizard_wlan_advanced_show;
  plugin->advanced_done = iap_wizard_wlan_advanced_done;
  plugin->save_state = iap_wizard_wlan_save_state;
  plugin->restore = iap_wizard_wlan_restore_state;
  plugin->get_page = iap_wizard_wlan_get_page;
  plugin->widgets =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  return TRUE;
}

void
iap_wizard_plugin_destroy(struct iap_wizard *iw,
                          struct iap_wizard_plugin *plugin)
{
  wlan_plugin_private *priv = (wlan_plugin_private *)plugin->priv;

  connui_flightmode_close(iap_wizard_wlan_flightmode_status_cb);

  if (priv && priv->osso)
  {
    osso_deinitialize(priv->osso);
    priv->osso = NULL;
  }

  iap_scan_close();
  stage_free(&priv->stage[0]);
  stage_free(&priv->stage[1]);
  g_hash_table_destroy(plugin->widgets);
  g_free(plugin->priv);
}
