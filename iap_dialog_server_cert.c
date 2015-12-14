#include <libconnui.h>
#include <connui-conndlgs.h>
#include <osso-applet-certman.h>

IAP_DIALOGS_PLUGIN_DEFINE(server_cert, ICD_UI_SHOW_SERVER_CERT_REQ);

struct iap_dialog_server_cert_data_t
{
  DBusMessage *dbus_request;
  void (*done_cb)(void *, gboolean);
  void *iap_id;
};

typedef struct iap_dialog_server_cert_data_t iap_dialog_server_cert_data;

static iap_dialog_server_cert_data plugin_data;

static gboolean
iap_dialog_server_cert_send_reply(gboolean accepted, const char *sender)
{
  DBusMessage *signal = dbus_message_new_signal(ICD_UI_DBUS_PATH,
                                                ICD_UI_DBUS_INTERFACE,
                                                ICD_UI_SERVER_CERT_SIG);
  if (!signal)
    return FALSE;

  if (!dbus_message_append_args(signal,
                                DBUS_TYPE_BOOLEAN, &accepted,
                                DBUS_TYPE_INVALID))
  {
    syslog(11, "could not append args to server cert reply");
    dbus_message_unref(signal);

    return FALSE;
  }

  dbus_message_set_destination(signal, sender);
  connui_dbus_send_system_msg(signal);
  dbus_message_unref(signal);

  return TRUE;
}

static void expired_cb(gboolean expired, gpointer user_data)
{
  iap_dialog_server_cert_data *data =
      (iap_dialog_server_cert_data *)user_data;

  data->done_cb(data->iap_id, 0);
  iap_dialog_server_cert_send_reply(!expired,
                                    dbus_message_get_sender(data->dbus_request));
  dbus_message_unref(data->dbus_request);
  data->dbus_request = NULL;
}

static gboolean
iap_dialog_server_cert_show(void *iap_id, DBusMessage *message,
                            void (*showing)(DBusMessage *),
                            void (*done)(void *, gboolean),
                            void *libosso G_GNUC_UNUSED)
{
  CertmanUIExpiredDialogType dialog_type;
  DBusError error;
  gboolean self_signed;
  gboolean expired;
  const gchar *cert_serial;
  const gchar *cert_name;

  dbus_error_init(&error);

  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_STRING, &cert_name,
                             DBUS_TYPE_STRING, &cert_serial,
                             DBUS_TYPE_BOOLEAN, &expired,
                             DBUS_TYPE_BOOLEAN, &self_signed,
                             DBUS_TYPE_INVALID))
  {
    syslog(11, "iap_dialog_server_cert_show(): could not get arguments : %s",
           error.message);
    dbus_error_free(&error);

    return FALSE;
  }

  plugin_data.dbus_request = message;
  plugin_data.done_cb = done;
  plugin_data.iap_id = iap_id;

  showing(dbus_message_ref(message));

  if (expired && self_signed)
    dialog_type = CERTMANUI_EXPIRED_DIALOG_NOCA_EXPIRED;
  else if (!expired && self_signed)
    dialog_type = CERTMANUI_EXPIRED_DIALOG_NOCA;
  else if (expired && !self_signed)
    dialog_type = CERTMANUI_EXPIRED_DIALOG_EXPIRED;
  else
  {
    certmanui_show_error_with_name_and_serial(NULL,
                                              CERTMANUI_ERROR_NOT_VALID_SERVER_CERT,
                                              cert_name, cert_serial, 0,
                                              expired_cb, &plugin_data);

    return TRUE;
  }

  certmanui_certificate_expired_with_name(NULL, dialog_type, cert_name,
                                          expired_cb, &plugin_data);

  return TRUE;
}

static gboolean
iap_dialog_server_cert_cancel(DBusMessage *message)
{
  const char *sender = dbus_message_get_sender(message);

  return iap_dialog_server_cert_send_reply(FALSE, sender);
}
