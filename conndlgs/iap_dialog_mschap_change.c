#include <connui/connui.h>
#include <connui/connui-conndlgs.h>
#include <connui/connui-dbus.h>
#include <connui/connui-log.h>

#include <string.h>
#include <libintl.h>

#include "config.h"

IAP_DIALOGS_PLUGIN_DEFINE(mschap_change, ICD_UI_MSCHAP_CHANGE_REQ);

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

struct iap_dialog_mschap_change_data_t
{
  gchar *username;
  gchar *old_password;
  gchar *iap_name;
  GtkEntry *old_pwd_entry;
  GtkEntry *new_pwd_entry;
  GtkEntry *confirm_pwd_entry;
  DBusMessage *message;
  iap_dialogs_done_fn done_cb;
  int iap_id;
};

typedef struct iap_dialog_mschap_change_data_t iap_dialog_mschap_change_data;

static iap_dialog_mschap_change_data plugin_data;

static gboolean
iap_dialog_mschap_change_send_reply(dbus_bool_t ok, const char *dest,
                                    const gchar *username,
                                    const gchar *new_password,
                                    const gchar *iap_name)
{
  DBusMessage *reply = dbus_message_new_signal(ICD_UI_DBUS_PATH,
                                               ICD_UI_DBUS_INTERFACE,
                                               ICD_UI_MSCHAP_CHANGE_SIG);
  if (!reply)
    return FALSE;

  if (dbus_message_append_args(reply,
                               DBUS_TYPE_STRING, &username,
                               DBUS_TYPE_STRING, &new_password,
                               DBUS_TYPE_STRING, &iap_name,
                               DBUS_TYPE_BOOLEAN, &ok,
                               DBUS_TYPE_INVALID))
  {
    dbus_message_set_destination(reply, dest);
    connui_dbus_send_system_msg(reply);
    dbus_message_unref(reply);
    return TRUE;
  }

  CONNUI_ERR("could not append args to gtc reply");
  dbus_message_unref(reply);

  return FALSE;
}

static void
iap_dialog_mschap_change_dialog_response_cb(GtkDialog *dialog, gint response_id,
                                            iap_dialog_mschap_change_data *data)
{
  dbus_bool_t ok = response_id == GTK_RESPONSE_OK;
  const char *new_pwd;

  if (ok)
  {
    const char *msgid = NULL;
    const char *old_pwd = gtk_entry_get_text(data->old_pwd_entry);

    if (!old_pwd || !data->old_password ||
        strcmp(data->old_password, old_pwd))
    {
      msgid = "conn_ib_net_eap_msc_pw_error2";
    }

    if (!msgid)
    {
      const char *confirm_pwd = gtk_entry_get_text(data->confirm_pwd_entry);

      new_pwd = gtk_entry_get_text(data->new_pwd_entry);

      if (!confirm_pwd || !new_pwd || strcmp(new_pwd, confirm_pwd))
        msgid = "conn_ib_net_eap_msc_pw_error1";
    }

    if (msgid)
    {
      hildon_banner_show_information(GTK_WIDGET(dialog), NULL, _(msgid));
      return;
    }
  }
  else
    new_pwd = "";

  if (iap_dialog_mschap_change_send_reply(
        ok, dbus_message_get_sender(data->message), data->username,
        new_pwd, data->iap_name))
  {
    gtk_widget_hide_all(GTK_WIDGET(dialog));
    gtk_widget_destroy(GTK_WIDGET(dialog));
    data->done_cb(data->iap_id, FALSE);
    dbus_message_unref(data->message);
    g_free(data->username);
    g_free(data->old_password);
    g_free(data->iap_name);
  }
}

static gboolean
iap_dialog_mschap_change_show(int iap_id, DBusMessage *message,
                              iap_dialogs_showing_fn showing,
                              iap_dialogs_done_fn done,
                              osso_context_t *libosso /*G_GNUC_UNUSED*/)
{
  GtkSizeGroup *size_group;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  HildonGtkInputMode im;
  GtkWidget *caption;
  GtkWidget *dialog;
  DBusError error;
  gchar *iap_name;
  gchar *old_password;
  gchar *username;

  dbus_error_init(&error);

  if (!dbus_message_get_args(message, &error,
                            DBUS_TYPE_STRING, &username,
                            DBUS_TYPE_STRING, &old_password,
                            DBUS_TYPE_STRING, &iap_name,
                            DBUS_TYPE_INVALID))
  {
    CONNUI_ERR("could not get arguments: %s", error.message);
    dbus_error_free(&error);
    return FALSE;
  }

  dbus_message_ref(message);

  plugin_data.username = g_strdup(username);
  plugin_data.old_password = g_strdup(old_password);
  plugin_data.iap_name = g_strdup(iap_name);
  plugin_data.message = message;
  plugin_data.done_cb = done;
  plugin_data.iap_id = iap_id;

  showing();

  size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  vbox = gtk_vbox_new(0, 0);
  hbox = gtk_hbox_new(0, 16);

  /* connection label */
  label = gtk_label_new(_("conn_fi_change_eap_msc_pw_conn"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_size_group_add_widget(size_group, label);
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  gtk_box_pack_start(
        GTK_BOX(hbox),
        iap_common_make_connection_entry_with_type(plugin_data.iap_name,
                                                   NULL, NULL),
        TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  /* username label */
  hbox = gtk_hbox_new(0, 16);
  label = gtk_label_new(_("conn_fi_change_eap_msc_pw_username"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_size_group_add_widget(size_group, label);
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
  label = gtk_label_new(plugin_data.username);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  /* old password */
  plugin_data.old_pwd_entry = GTK_ENTRY(gtk_entry_new());
  im = hildon_gtk_entry_get_input_mode(plugin_data.old_pwd_entry);
  im &= ~HILDON_GTK_INPUT_MODE_AUTOCAP;
  im |= HILDON_GTK_INPUT_MODE_INVISIBLE;
  hildon_gtk_entry_set_input_mode(plugin_data.old_pwd_entry, im);

  caption = hildon_caption_new(size_group,
                               _("conn_fi_change_eap_msc_pw_oldpw"),
                               GTK_WIDGET(plugin_data.old_pwd_entry), NULL,
                               HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  /* new password */
  plugin_data.new_pwd_entry = GTK_ENTRY(gtk_entry_new());
  hildon_gtk_entry_set_input_mode(plugin_data.new_pwd_entry, im);
  caption = hildon_caption_new(size_group,
                               _("conn_fi_change_eap_msc_pw_newpw"),
                               GTK_WIDGET(plugin_data.new_pwd_entry), NULL,
                               HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  /* confirm password */
  plugin_data.confirm_pwd_entry = GTK_ENTRY(gtk_entry_new());
  hildon_gtk_entry_set_input_mode(plugin_data.confirm_pwd_entry, im);

  caption = hildon_caption_new(size_group,
                               _("conn_fi_change_eap_msc_pw_confirmpw"),
                               GTK_WIDGET(plugin_data.confirm_pwd_entry), NULL,
                               HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start(GTK_BOX(vbox), caption, FALSE, FALSE, 0);

  g_object_unref(size_group);

  dialog = gtk_dialog_new_with_buttons(
        _("conn_ti_change_eap_msc_pw"), NULL,
        GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_MODAL,
        dgettext("hildon-libs", "wdgt_bd_done"), GTK_RESPONSE_OK, NULL);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), vbox);
  iap_common_set_close_response(dialog, GTK_RESPONSE_CANCEL);
  g_signal_connect(G_OBJECT(dialog), "response",
                   G_CALLBACK(iap_dialog_mschap_change_dialog_response_cb),
                   &plugin_data);
  gtk_widget_show_all(dialog);

  return TRUE;
}

static gboolean
iap_dialog_mschap_change_cancel(DBusMessage *message)
{
  DBusError error;
  gchar *iap_name;
  gchar *password;
  gchar *username;

  dbus_error_init(&error);

  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_STRING, &username,
                             DBUS_TYPE_STRING, &password,
                             DBUS_TYPE_STRING, &iap_name,
                             DBUS_TYPE_INVALID))
  {
    CONNUI_ERR("could not get arguments: %s", error.message);
    dbus_error_free(&error);
    return FALSE;
  }

  return iap_dialog_mschap_change_send_reply(FALSE,
                                             dbus_message_get_sender(message),
                                             username, "", iap_name);
}
