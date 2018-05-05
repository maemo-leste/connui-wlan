#include <connui/connui.h>
#include <connui/iapsettings/wizard.h>
#include <connui/iapsettings/stage.h>
#include <connui/iapsettings/mapper.h>

#include <string.h>

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
  int field_54;
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
    GINT_TO_POINTER(1)
  },
  {
    export_EAP_default_type,
    validate_EAP_default_type,
    "WLAN_EAP_PEAP_CERTIFICATE",
    "EAP_TLS_PEAP_client_certificate_file",
    NULL,
    &mapper_combo2string,
    GINT_TO_POINTER(1)
  },
  {
    export_EAP_default_type,
    validate_EAP_default_type,
    "WLAN_EAP_TTLS_CERTIFICATE",
    "EAP_TLS_PEAP_client_certificate_file",
    NULL,
    &mapper_combo2string,
    GINT_TO_POINTER(1)
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
iap_wizard_wlan_flightmode_status_cb(dbus_bool_t offline, gpointer user_data)
{
  wlan_plugin_private *priv = user_data;

  priv->offline = offline;
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

//  plugin->get_advanced = iap_wizard_wlan_get_advanced;
  plugin->stage_widgets = iap_wizard_wlan_widgets;
//  plugin->pages = iap_wizard_wlan_pages;
/*  plugin->get_widgets = iap_wizard_wlan_advanced_get_widgets;
  plugin->advanced_show = iap_wizard_wlan_advanced_show;
  plugin->advanced_done = iap_wizard_wlan_advanced_done;
  plugin->save_state = iap_wizard_wlan_save_state;
  plugin->priv = priv;
  plugin->restore = iap_wizard_wlan_restore_state;
  plugin->get_page = iap_wizard_wlan_get_page;*/
  plugin->widgets =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  return TRUE;
}
