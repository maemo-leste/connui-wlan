#include <connui/connui.h>
#include <connui/connui-conndlgs.h>
#include <connui/connui-dbus.h>
#include <connui/connui-log.h>
#include <conbtui/gateway/pin.h>
#include <icd/dbus_api.h>
#include <icd/network_api_defines.h>

#include <string.h>
#include <libintl.h>
#include <stdlib.h>

#include "config.h"

#define WPS_UI_DBUS_INTERFACE "com.nokia.wps_ui"
#define WPS_UI_DBUS_PATH "/com/nokia/wps_ui"
#define WPS_UI_SHOW_SELECT_METHOD "show_select_method"
#define WPS_UI_METHOD_SIGNAL "method_sig"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

struct iap_dialog_wps_data_t
{
  GtkWidget *dialog;
  GtkWidget *label1;
  GtkWidget *label2;
  GtkWidget *pin_method_button;
  GtkWidget *done_button;
  GtkWidget *progress_bar;
  GtkWidget *scan_view;
  DBusMessage *dbus_request;
  DBusConnection *connection;
  iap_dialogs_done_fn done_cb;
  int iap_id;
  gboolean eap_signal_connected;
  gboolean selecting;
  guint progress_timeout_id;
  int timeout;
  gchar *iap;
  gchar *method;
  GSList *networks;
};

typedef struct iap_dialog_wps_data_t iap_dialog_wps_data;

static iap_dialog_wps_data plugin_data;

#define WPS_PIN_BUTTON 10

static void iap_dialog_wps_inetstate_cb(enum inetstate_status state, network_entry *entry, gpointer user_data);
static void iap_dialog_wps_dialog_done(iap_dialog_wps_data *data);
static void iap_dialog_wps_dialog_response_cb(GtkDialog *dialog, gint response_id, iap_dialog_wps_data *data);

static gboolean
iap_dialog_wps_send_reply(iap_dialog_wps_data *data, DBusMessage *request,
                          gboolean reply)
{
  const char *sender;
  DBusMessage *message;
  const gchar *method = "";

  g_return_val_if_fail((data != NULL && data->dbus_request != NULL) ||
                       (request != NULL && reply == FALSE), FALSE);

  if (data && data->dbus_request)
    request = data->dbus_request;

  sender = dbus_message_get_sender(request);

  if (reply && data && data->method )
    method = data->method;

  message = dbus_message_new_signal(WPS_UI_DBUS_PATH,
                                    WPS_UI_DBUS_INTERFACE,
                                    WPS_UI_METHOD_SIGNAL);

  if (!message)
    return FALSE;

  if (dbus_message_append_args(message,
                               DBUS_TYPE_STRING, &method,
                               DBUS_TYPE_BOOLEAN, &reply,
                               DBUS_TYPE_INVALID))
  {
    if (sender)
      dbus_message_set_destination(message, sender);

    if (data->connection)
    {
      dbus_connection_send(data->connection, message, NULL);
      dbus_connection_unref(data->connection);
      data->connection = NULL;
    }
    else
      connui_dbus_send_system_msg(message);

    dbus_message_unref(message);
    return TRUE;
  }

  CONNUI_ERR("could not append args to wps reply");
  dbus_message_unref(message);

  return FALSE;
}

static DBusHandlerResult
iap_dialog_wps_eap_signal(DBusConnection *connection, DBusMessage *message,
                          void *user_data)
{
  iap_dialog_wps_data *data = user_data;

  if (data && data->eap_signal_connected)
  {
    dbus_int32_t len = 0;
    gchar **networks = NULL;

    /* FIXME - use eap-dbus.h defines */
    if (dbus_message_is_signal(message, "com.nokia.eap.signal", "wps_success"))
    {
      DBusError error;

      dbus_error_init(&error);

      if (!dbus_message_is_signal(message, "com.nokia.eap.signal",
                                  "wps_success")
          || dbus_message_get_args(
                             message, &error,
                             DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &networks, &len,
                             DBUS_TYPE_INVALID))
      {
        if (len > 0)
        {
          int i;

          for (i = 0; i < len; i++)
          {
            data->networks = g_slist_prepend(data->networks,
                                             g_strdup(networks[i]));
          }
        }
        else
          iap_dialog_wps_dialog_done(data);
      }
      else
      {
        CONNUI_ERR("could not get success signal arguments: %s",
                   error.message);
        dbus_error_free(&error);
      }
    }
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
iap_dialog_wps_dialog_done(iap_dialog_wps_data *data)
{
  iap_scan_close();
  connui_inetstate_close(iap_dialog_wps_inetstate_cb);

  if (data->dbus_request)
  {
    dbus_message_unref(data->dbus_request);
    data->dbus_request = NULL;
  }

  if (data->dialog)
  {
    gtk_widget_destroy(data->dialog);
    data->progress_bar = NULL;
    data->dialog = NULL;
    data->label1 = NULL;
    data->label2 = NULL;
    data->pin_method_button = NULL;
    data->done_button = NULL;
  }

  if (data->scan_view)
  {
    gtk_widget_destroy(data->scan_view);
    data->scan_view = NULL;
  }

  if (data->eap_signal_connected)
  {
    connui_dbus_disconnect_system_bcast_signal(
          "com.nokia.eap.signal", iap_dialog_wps_eap_signal, data, NULL);
    data->eap_signal_connected = FALSE;
  }

  if (data->progress_timeout_id)
  {
    g_source_remove(data->progress_timeout_id);
    data->progress_timeout_id = 0;
  }

  if (data->networks)
  {
    g_slist_foreach(data->networks, (GFunc)&g_free, NULL);
    g_slist_free(data->networks);
    data->networks = NULL;
  }

  g_free(data->iap);
  data->iap = NULL;
  g_free(data->method);
  data->method = NULL;
  data->selecting = FALSE;
  data->done_cb(data->iap_id, FALSE);
}

static void
iap_dialog_wps_error_note_response(GtkDialog *dialog, gint response_id,
                                   iap_dialog_wps_data *data)
{
  if (response_id == GTK_RESPONSE_OK)
  {
    dbus_uint32_t flags = ICD_CONNECTION_FLAG_UI_EVENT;
    DBusMessage *mcall =
        connui_dbus_create_method_call(ICD_DBUS_API_INTERFACE,
                                       ICD_DBUS_API_PATH,
                                       ICD_DBUS_API_INTERFACE,
                                       ICD_DBUS_API_SELECT_REQ,
                                       DBUS_TYPE_UINT32, &flags,
                                       DBUS_TYPE_INVALID);
    if (!mcall || !connui_dbus_send_system_mcall(mcall, -1, NULL, NULL, NULL))
      CONNUI_ERR("Unable to send select_req to ICd2");

    if (mcall)
      dbus_message_unref(mcall);
  }

  gtk_widget_destroy(GTK_WIDGET(dialog));
  iap_dialog_wps_dialog_done(data);
}

static void
iap_dialog_wps_abort_process(iap_dialog_wps_data *data, gboolean ask_for_retry)
{
  DBusMessage * mcall =
      dbus_message_new_method_call(ICD_DBUS_API_INTERFACE,
                                   ICD_DBUS_API_PATH,
                                   ICD_DBUS_API_INTERFACE,
                                   ICD_DBUS_API_DISCONNECT_REQ);
  dbus_uint32_t flags = ICD_CONNECTION_FLAG_UI_EVENT;

  if (mcall)
  {
    if (!dbus_message_append_args(mcall,
                                  DBUS_TYPE_UINT32, &flags,
                                  DBUS_TYPE_INVALID) ||
        !connui_dbus_send_system_mcall(mcall, -1, NULL, NULL, NULL))
    {
      CONNUI_ERR("could not send disconnect request");
    }

    dbus_message_unref(mcall);
  }
  else
    CONNUI_ERR("could not send disconnect request");

  if (ask_for_retry)
  {
    GtkWidget *note = hildon_note_new_confirmation(
          NULL, _("conn_nc_retry_connection_network_error"));

    g_signal_connect(G_OBJECT(note), "response",
                     G_CALLBACK(iap_dialog_wps_error_note_response), data);

    if (data->dialog)
    {
      gtk_widget_destroy(data->dialog);
      data->done_button = NULL;
      data->dialog = NULL;
      data->label1 = NULL;
      data->label2 = NULL;
      data->pin_method_button = NULL;
    }

    gtk_widget_show_all(note);
  }
}

static gboolean
iap_dialog_wps_select_connection(iap_dialog_wps_data *data)
{
  g_return_val_if_fail(data != NULL && data->scan_view != NULL, FALSE);

  iap_scan_stop();
  data->progress_timeout_id = 0;

  if (gtk_tree_model_iter_n_children(
        gtk_tree_view_get_model(GTK_TREE_VIEW(data->scan_view)), 0))
  {
    connui_scan_entry *scan_entry = NULL;
    connui_scan_entry *best_signal_entry = NULL;
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(data->scan_view));
    GtkTreeIter iter;
    network_entry *entries[2];

    gtk_tree_model_get_iter_first(model, &iter);

    do
    {
      gtk_tree_model_get(model, &iter,
                         IAP_SCAN_LIST_SCAN_ENTRY, &scan_entry,
                         -1);

      if (scan_entry &&
          (!best_signal_entry ||
           scan_entry->signal_strength > best_signal_entry->signal_strength))
      {
        best_signal_entry = scan_entry;
      }
    }
    while (gtk_tree_model_iter_next(model, &iter));

    entries[0] = &best_signal_entry->network;
    entries[1] = NULL;

    iap_network_entry_connect(ICD_CONNECTION_FLAG_UI_EVENT, entries);
    iap_common_set_last_used_network(&best_signal_entry->network);
    iap_dialog_wps_dialog_done(data);
  }
  else
  {
    CONNUI_ERR("No configured network found in scan, aborting process");
    iap_dialog_wps_abort_process(data, TRUE);
  }

  return FALSE;
}

static void
iap_dialog_wps_scan_cancel_cb(gpointer user_data)
{
  iap_dialog_wps_data *data = user_data;

  if (!data->selecting && data->scan_view)
  {
    data->selecting = TRUE;

    if (data->progress_timeout_id)
      g_source_remove(data->progress_timeout_id);

    data->progress_timeout_id =
        g_idle_add((GSourceFunc)iap_dialog_wps_select_connection, data);
  }
}

static gboolean
iap_dialog_wps_scan_network_added_cb(connui_scan_entry *entry,
                                     gpointer user_data)
{
  iap_dialog_wps_data *data = user_data;
  GSList *l;

  if (!data || !data->networks || !entry->network.network_type)
    return FALSE;

  if (strncmp(entry->network.network_type, "WLAN_", 5))
    return FALSE;

  if (!(entry->network.network_attributes & ICD_NW_ATTR_IAPNAME))
    return FALSE;

  if (!entry->network.network_id || !*entry->network.network_id)
    return FALSE;

  l = g_slist_find_custom(data->networks, entry->network.network_id,
                          (GCompareFunc)strcmp);
  if (!l)
    return FALSE;

  data->networks = g_slist_remove_link(data->networks, l);
  g_free(l->data);
  g_slist_free(l);

  if (!data->networks)
    iap_scan_stop();

  return TRUE;
}

static gboolean
iap_dialog_wps_select_network(iap_dialog_wps_data *data)
{
  GtkWidget *widget;
  gchar *network_types[] = {"WLAN_INFRA", NULL};

  connui_inetstate_close(iap_dialog_wps_inetstate_cb);

  if (data->progress_timeout_id)
  {
    g_source_remove(data->progress_timeout_id);
    data->progress_timeout_id = 0;
  }

  widget = iap_scan_tree_create(
        (GtkTreeIterCompareFunc)iap_scan_default_sort_func, NULL);
  data->scan_view = widget;

  if (!iap_scan_start_for_network_types(network_types, 0, NULL,
          iap_dialog_wps_scan_cancel_cb, iap_dialog_wps_scan_network_added_cb,
          widget, NULL, NULL, data))
  {
    CONNUI_ERR("Unable to start scan");
    iap_dialog_wps_scan_cancel_cb(data);
  }

  return FALSE;
}

static void
iap_dialog_wps_inetstate_cb(enum inetstate_status state, network_entry *entry,
                            gpointer user_data)
{
  iap_dialog_wps_data *data = user_data;

  g_return_if_fail(data != NULL && data->iap != NULL);

  if (entry && state == INETSTATE_STATUS_ONLINE &&
      !strcmp(entry->network_id, data->iap))
  {
    if (data->networks)
      g_idle_add((GSourceFunc)iap_dialog_wps_select_network, data);
    else
    {
      iap_dialog_wps_send_reply(data, NULL, FALSE);
      iap_dialog_wps_dialog_done(data);
    }
  }
}

static void
iap_dialog_wps_create_dialog(iap_dialog_wps_data *data, const gchar *title)
{
  GtkWidget *dialog = data->dialog;

  if (dialog)
  {
    gtk_widget_destroy(dialog);
    data->progress_bar = NULL;
    data->dialog = NULL;
    data->label1 = NULL;
    data->label2 = NULL;
    data->pin_method_button = NULL;
    data->done_button = NULL;
  }

  dialog = hildon_dialog_new_with_buttons(
        title, NULL, GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_MODAL, NULL);
  data->dialog = dialog;
  data->pin_method_button = gtk_dialog_add_button(
        GTK_DIALOG(dialog), _("conn_set_iap_bd_wlan_wps_pin"), WPS_PIN_BUTTON);
  data->done_button = gtk_dialog_add_button(
        GTK_DIALOG(dialog), dgettext("hildon-libs", "wdgt_bd_done"),
        GTK_RESPONSE_OK);
  iap_common_set_close_response(data->dialog, GTK_RESPONSE_CANCEL);

  data->label1 = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(data->label1), 0.0, 0.0);
  gtk_label_set_line_wrap(GTK_LABEL(data->label1), TRUE);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), data->label1);

  data->label2 = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(data->label2), 0.0, 0.0);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), data->label2);

  data->progress_bar = gtk_progress_bar_new();
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
                    data->progress_bar);
}

static gboolean
iap_dialog_wps_progress_timeout(iap_dialog_wps_data *data)
{
  g_return_val_if_fail(data != NULL, FALSE);

  data->timeout++;
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(data->progress_bar),
                                (double)data->timeout / 120.0);
  if (data->timeout > 119)
  {
    CONNUI_ERR("Timeout occurred, aborting WPS process");
    iap_dialog_wps_abort_process(data, TRUE);
    return FALSE;
  }

  return TRUE;
}

static void
iap_dialog_wps_show_dialog(iap_dialog_wps_data *data)
{
  gtk_label_set_text(GTK_LABEL(data->label1), _("conn_set_iap_fi_wlan_wps"));
  g_signal_connect(G_OBJECT(data->dialog), "response",
                   G_CALLBACK(iap_dialog_wps_dialog_response_cb), data);

  if (!strcmp(data->method, "pin"))
  {
    GConfValue *val = gconf_value_new(GCONF_VALUE_STRING);
    gchar *pin = gateway_pin_random_digit_string(8);
    gchar *markup;
    int i;

    pin[7] = 0;
    i = 10 * strtoul(pin, 0, 10);
    pin[7] = (10 - ((i / 10000) % 10 + (i / 1000000) % 10 + (i / 100) % 10 +
                   3 * ((i / 10000000) % 10 +
                        (i / 100000) % 10 +
                        (i / 1000) % 10 +
                        (i / 10) % 10)) % 10) % 10 + '0';

    gtk_label_set_text(GTK_LABEL(data->label1),
                       _("conn_set_iap_fi_wlan_wps_pin"));
    markup = g_strdup_printf("<big>%s</big>", pin);
    gtk_label_set_markup(GTK_LABEL(data->label2), markup);
    gconf_value_set_string(val, pin);
    g_free(markup);
    g_free(pin);
    iap_settings_set_gconf_value(data->iap, "EAP_SIMPLE_CONFIG_device_password",
                                 val);
    gconf_value_free(val);
  }

  if (!data->eap_signal_connected)
  {
    if (!connui_dbus_connect_system_bcast_signal(
          "com.nokia.eap.signal",
          iap_dialog_wps_eap_signal, data, NULL))
    {
      CONNUI_ERR("Unable to register handler for EAP signals!");
      iap_dialog_wps_send_reply(data, NULL, FALSE);
      iap_dialog_wps_dialog_done(data);
      return;
    }

    data->eap_signal_connected = TRUE;
  }

  gtk_widget_show_all(data->dialog);
  gtk_widget_hide_all(data->progress_bar);
  gtk_widget_grab_focus(data->done_button);
}

static void
iap_dialog_wps_show_pin_dialog(iap_dialog_wps_data *data)
{
  iap_dialog_wps_create_dialog(data, _("conn_set_iap_ti_wlan_wps_pin"));
  gtk_widget_destroy(data->pin_method_button);
  data->pin_method_button = NULL;
  g_free(data->method);
  data->method = g_strdup("pin");
  iap_dialog_wps_send_reply(data, NULL, TRUE);
  iap_dialog_wps_show_dialog(data);
}

static void
iap_dialog_wps_dialog_response_cb(GtkDialog *dialog, gint response_id,
                                  iap_dialog_wps_data *data)
{
  if (response_id == GTK_RESPONSE_OK)
  {
    data->timeout = 0;

    if (data->progress_timeout_id)
      g_source_remove(data->progress_timeout_id);

    data->progress_timeout_id =
        g_timeout_add_seconds(1, (GSourceFunc)iap_dialog_wps_progress_timeout,
                              data);

    if (data->method && !strcmp(data->method, "pushbutton"))
    {
      iap_dialog_wps_send_reply(data, NULL, TRUE);
      gtk_widget_set_sensitive(data->pin_method_button, FALSE);
    }

    gtk_widget_show_all(data->progress_bar);
  }
  else if (response_id == WPS_PIN_BUTTON)
    iap_dialog_wps_show_pin_dialog(data);
  else
  {
    if (!data->method || strcmp(data->method, "pin"))
      iap_dialog_wps_send_reply(data, NULL, FALSE);

    iap_dialog_wps_abort_process(data, FALSE);
    iap_dialog_wps_dialog_done(data);
  }
}

/* This is really an ugly hack, might fix it someday */

#undef ICD_UI_DBUS_INTERFACE
#undef ICD_UI_DBUS_PATH

#define ICD_UI_DBUS_INTERFACE WPS_UI_DBUS_INTERFACE
#define ICD_UI_DBUS_PATH WPS_UI_DBUS_PATH

IAP_DIALOGS_PLUGIN_DEFINE(wps, WPS_UI_SHOW_SELECT_METHOD);

static gboolean
iap_dialog_wps_show(int iap_id, DBusMessage *message,
                    iap_dialogs_showing_fn showing, iap_dialogs_done_fn done,
                    osso_context_t *libosso G_GNUC_UNUSED)
{
  DBusError error;
  dbus_int32_t methods_dbus_len = 0;
  gchar **methods;
  gchar *iap;
  int i;

  dbus_error_init(&error);

  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_STRING, &iap,
                             DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                             &methods, &methods_dbus_len, DBUS_TYPE_INVALID))
  {
    CONNUI_ERR("could not get arguments: %s", error.message);
    dbus_error_free(&error);
    return FALSE;
  }

  g_return_val_if_fail("methods_dbus_len > 0", FALSE);

  dbus_message_ref(message);
  plugin_data.done_cb = done;
  plugin_data.iap_id = iap_id;
  plugin_data.dbus_request = message;
  plugin_data.connection =
      dbus_connection_ref(iap_dialog_get_connection(message));
  g_free(plugin_data.iap);
  plugin_data.iap = g_strdup(iap);
  g_free(plugin_data.method);
  plugin_data.method = NULL;

  if (!connui_inetstate_status(iap_dialog_wps_inetstate_cb, &plugin_data))
    CONNUI_ERR("Unable to register inetstate!");

  showing();

  for (i = 0; i < methods_dbus_len; i++)
  {
    gchar *method = methods[i];

    if (method && !strcmp(method, "pushbutton"))
    {
      iap_dialog_wps_create_dialog(&plugin_data,
                                   _("conn_set_iap_ti_wlan_wps"));
      g_free(plugin_data.method);
      plugin_data.method = g_strdup("pushbutton");
      iap_dialog_wps_show_dialog(&plugin_data);
      return TRUE;
    }
  }

  iap_dialog_wps_show_pin_dialog(&plugin_data);

  return TRUE;

}


static gboolean
iap_dialog_wps_cancel(DBusMessage *message)
{
  return iap_dialog_wps_send_reply(NULL, message, FALSE);
}
