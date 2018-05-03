#include <string.h>
#include <glib.h>
#include <connui/connui.h>
#include <connui/connui-conndlgs.h>
#include <connui/connui-dbus.h>
#include <connui/wlan-common.h>
#include <connui/iapsettings/widgets.h>
#include <hildon/hildon-caption.h>
#include <hildon/hildon-dialog.h>
#include <libintl.h>

IAP_DIALOGS_PLUGIN_DEFINE(gtc_challenge, ICD_UI_SHOW_GTC_REQ);

struct iap_dialog_gtc_challenge_data_t
{
  GtkWidget *entry;
  DBusMessage *dbus_request;
  iap_dialogs_done_fn done_cb;
  int iap_id;
};

typedef struct iap_dialog_gtc_challenge_data_t iap_dialog_gtc_challenge_data;

static iap_dialog_gtc_challenge_data plugin_data;

static gboolean
iap_dialog_gtc_challenge_send_reply(gboolean ok, const char *sender,
                                    const char *challenge_response)
{
  DBusMessage *signal;

  signal = dbus_message_new_signal(ICD_UI_DBUS_PATH, ICD_UI_DBUS_INTERFACE,
                                   ICD_UI_GTC_RESPONSE_SIG);

  if (!signal)
    return FALSE;

  if (!dbus_message_append_args(signal,
                                DBUS_TYPE_STRING, &challenge_response,
                                DBUS_TYPE_BOOLEAN, &ok,
                                DBUS_TYPE_INVALID))
  {
    syslog(11, "could not append args to gtc reply");
    dbus_message_unref(signal);

    return FALSE;
  }

  dbus_message_set_destination(signal, sender);
  connui_dbus_send_system_msg(signal);
  dbus_message_unref(signal);

  return TRUE;
}

static gboolean
iap_dialog_gtc_challenge_cancel(DBusMessage *message)
{
  const char *sender = dbus_message_get_sender(message);

  return iap_dialog_gtc_challenge_send_reply(0, sender, "");
}

static void
activate_cb(GtkEntry *entry G_GNUC_UNUSED, gpointer user_data)
{
  GtkDialog *dialog = GTK_DIALOG(user_data);

  if (dialog)
    gtk_dialog_response(dialog, GTK_RESPONSE_OK);
}

static void
response_cb(GtkDialog *dialog, gint arg1, iap_dialog_gtc_challenge_data *data)
{
  gboolean ok = (arg1 == GTK_RESPONSE_OK);
  const char *sender = dbus_message_get_sender(data->dbus_request);
  const char *challenge_response =
      (ok ? iap_widgets_h22_entry_get_text(data->entry) : "");

  /* FIXME - why not closing the dialog in case of error ? */
  if (iap_dialog_gtc_challenge_send_reply(ok, sender, challenge_response))
  {
    gtk_widget_hide_all(GTK_WIDGET(dialog));
    gtk_widget_destroy(GTK_WIDGET(dialog));
    data->done_cb(data->iap_id, 0);
    dbus_message_unref(data->dbus_request);
  }
}

static gboolean
iap_dialog_gtc_challenge_show(int iap_id, DBusMessage *message,
                              iap_dialogs_showing_fn showing,
                              iap_dialogs_done_fn done,
                              osso_context_t *libosso G_GNUC_UNUSED)
{
  gchar *title;
  GtkWidget *label;
  GtkWidget *viewport;
  HildonGtkInputMode im;
  GtkWidget *caption;
  GtkWidget *dialog;
  GtkWidget *window;
  GtkWidget *vbox;
  DBusError error;
  GtkRequisition requisition;
  gsize n = 0;
  gchar *str = NULL;

  dbus_error_init(&error);

  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &str, &n,
                             DBUS_TYPE_INVALID))
  {
    syslog(11, "iap_dialog_gtc_challenge_show(): could not get arguments: %s",
           error.message);
    dbus_error_free(&error);

    return FALSE;
  }

  plugin_data.iap_id = iap_id;
  plugin_data.dbus_request = message;
  plugin_data.done_cb = done;

  dbus_message_ref(message);

  showing();

  vbox = gtk_vbox_new(0, 0);

  if (str)
    title = g_strndup(str, n);
  else
    title = g_strdup("");

  wlan_common_mangle_ssid(title, strlen(title));
  label = gtk_label_new(title);
  g_free(title);

  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  g_object_set(G_OBJECT(label), "wrap", 1, "wrap-mode", 2, NULL);

  viewport = gtk_viewport_new(0, 0);
  gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), 0);
  gtk_container_add(GTK_CONTAINER(viewport), label);

  window = gtk_scrolled_window_new(0, 0);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(window), viewport);

  gtk_box_pack_start(GTK_BOX(vbox), window, 0, 0, 0);

  plugin_data.entry = iap_widgets_create_h22_entry();

  im = hildon_gtk_entry_get_input_mode(GTK_ENTRY(plugin_data.entry));
  im &= ~HILDON_GTK_INPUT_MODE_AUTOCAP;
  im |= HILDON_GTK_INPUT_MODE_INVISIBLE;

  hildon_gtk_entry_set_input_mode(GTK_ENTRY(plugin_data.entry), im);

  caption = hildon_caption_new(0, dgettext("osso-connectivity-ui",
                                           "conn_fi_user_response_eap_gtc"),
                               plugin_data.entry, 0, 0);
  gtk_box_pack_start(GTK_BOX(vbox), caption, 0, 0, 0);

  dialog = hildon_dialog_new_with_buttons(dgettext("osso-connectivity-ui",
                                                   "conn_ti_response_eap_gtc"),
                                          0,
                                          GTK_DIALOG_NO_SEPARATOR|
                                          GTK_DIALOG_MODAL,
                                          dgettext("hildon-libs",
                                                   "wdgt_bd_done"),
                                          GTK_RESPONSE_OK, 0);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), vbox);
  gtk_widget_size_request(label, &requisition);

  if (requisition.height > 120)
  {
    requisition.width = -1;
    requisition.height = 120;
  }

  gtk_widget_set_size_request(GTK_WIDGET(window), requisition.width,
                              requisition.height);
  iap_common_set_close_response(dialog, -5);

  g_signal_connect_data(G_OBJECT(dialog), "response", (GCallback)response_cb,
                        &plugin_data, 0, 0);
  g_signal_connect_data(G_OBJECT(plugin_data.entry), "activate",
                        (GCallback)activate_cb, dialog, 0, 0);

  gtk_widget_show_all(dialog);

  gtk_widget_grab_focus(GTK_WIDGET(plugin_data.entry));

  return TRUE;
}
