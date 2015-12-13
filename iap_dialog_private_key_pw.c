#include <glib.h>
#include <dbus/dbus.h>
#include <icd/osso-ic-ui-dbus.h>
#include <libconnui.h>
#include <maemosec_certman.h>
#include <osso-applet-certman.h>

void iap_dialog_register_service(const char *, const char *);
void iap_dialog_unregister_service(const char *, const char *);

struct iap_dialog_private_key_pw_data_t
{
  DBusMessage *dbus_request;
  void (*done_cb)(void *, gboolean);
  void *iap_id;
};

typedef struct iap_dialog_private_key_pw_data_t iap_dialog_private_key_pw_data;

iap_dialog_private_key_pw_data plugin_data;

G_MODULE_EXPORT void
g_module_unload(GModule *module G_GNUC_UNUSED)
{
  iap_dialog_unregister_service(ICD_UI_DBUS_INTERFACE, ICD_UI_DBUS_PATH);
}

G_MODULE_EXPORT int
g_module_check_init(GModule *module G_GNUC_UNUSED)
{
  iap_dialog_register_service(ICD_UI_DBUS_INTERFACE, ICD_UI_DBUS_PATH);

  return 0;
}

G_MODULE_EXPORT gboolean
iap_dialogs_plugin_match(DBusMessage *message)
{
  return dbus_message_is_method_call(message, ICD_UI_DBUS_INTERFACE,
                                     ICD_UI_SHOW_PRIVATE_KEY_PASSWD_REQ);
}

static gboolean
iap_dialog_private_key_pw_send_reply(gboolean ok, const char *destination,
                                     maemosec_key_id key_id,
                                     const char *password)
{
  DBusMessage *signal;
  char buf[41];

  signal = dbus_message_new_signal(ICD_UI_DBUS_PATH, ICD_UI_DBUS_INTERFACE,
                                   ICD_UI_PRIVATE_KEY_PASSWD_SIG);

  if (!signal)
    return FALSE;

  if (maemosec_certman_key_id_to_str(key_id, buf, sizeof(buf)))
  {
    syslog(11, "Unable to convert key ID to string!");

    goto error;
  }

  if (!dbus_message_append_args(signal,
                                DBUS_TYPE_STRING, &buf,
                                DBUS_TYPE_STRING, &password,
                                DBUS_TYPE_BOOLEAN, &ok,
                                NULL))
  {
    syslog(11, "could not append args to priv key reply");
    dbus_message_unref(signal);

    goto error;
  }

  dbus_message_set_destination(signal, destination);
  connui_dbus_send_system_msg(signal);
  dbus_message_unref(signal);

  return TRUE;

error:
  dbus_message_unref(signal);

  return FALSE;
}

static gboolean
iap_dialog_private_key_pw_cancel(DBusMessage *message)
{
  maemosec_key_id key_id;
  DBusError error;
  char *from_str = NULL;

  dbus_error_init(&error);

  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_STRING, &from_str,
                             NULL))
  {
    syslog(11, "iap_dialog_private_key_pw_cancel(): could not get arguments : %s", error.message);
    dbus_error_free(&error);

    return FALSE;
  }


  if (maemosec_certman_str_to_key_id(from_str, key_id))
  {
    syslog(11, "Unable to convert string '%s' to cert_id!", from_str);

    return FALSE;
  }

  return iap_dialog_private_key_pw_send_reply(FALSE,
                                              dbus_message_get_sender(message),
                                              key_id, "");
}

G_MODULE_EXPORT gboolean
iap_dialogs_plugin_cancel(DBusMessage *message)
{
  return iap_dialog_private_key_pw_cancel(message);
}

static void
iap_dialog_private_key_pw_response(maemosec_key_id cert_id, EVP_PKEY *key,
                                   gchar *password, gpointer user_data)
{
  iap_dialog_private_key_pw_data *data =
      (iap_dialog_private_key_pw_data *)user_data;

  g_free(key);
  g_return_if_fail(data != NULL);

  data->done_cb(data->iap_id, 0);

  iap_dialog_private_key_pw_send_reply(
        key && password, dbus_message_get_sender(data->dbus_request), cert_id,
        password ? password : "");

  dbus_message_unref(data->dbus_request);
  data->dbus_request = NULL;
}

static gboolean
iap_dialog_private_key_pw_show(void *iap_id, DBusMessage *message,
                               void (*showing)(DBusMessage *),
                               void (*done)(void *, gboolean), void *libosso)
{
  maemosec_key_id key_id;
  DBusError error;
  char *from_str = NULL;

  g_return_val_if_fail(showing != NULL, FALSE);
  g_return_val_if_fail(done != NULL, FALSE);
  g_return_val_if_fail(libosso != NULL, FALSE);

  dbus_error_init(&error);

  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_STRING, &from_str,
                             NULL))
  {
    syslog(11, "iap_dialog_private_key_pw_show(): could not get arguments : %s",
           error.message);
    dbus_error_free(&error);

    return FALSE;
  }

  plugin_data.dbus_request = message;
  plugin_data.done_cb = done;
  plugin_data.iap_id = iap_id;

  showing(dbus_message_ref(message));

  if (maemosec_certman_str_to_key_id(from_str, key_id))
  {
    syslog(11, "Unable to convert string '%s' to cert_id!", from_str);
    dbus_message_unref(plugin_data.dbus_request);
    plugin_data.dbus_request = NULL;
    done(iap_id, 0);

    return FALSE;
  }

  certmanui_get_privatekey(0, key_id, 0, iap_dialog_private_key_pw_response,
                           &plugin_data);

  return TRUE;
}

G_MODULE_EXPORT gboolean
iap_dialogs_plugin_show(void *iap_id, DBusMessage *message,
                        void (*showing)(DBusMessage *),
                        void (*done)(void *, gboolean), void *libosso)
{
  return
      iap_dialog_private_key_pw_show(iap_id, message, showing, done, libosso);
}
