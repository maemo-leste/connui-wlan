AM_CFLAGS = -Wall -Werror $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
	    -DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1
AM_LDFLAGS = -Wl,--as-needed $(HILDON_LIBS) $(CONNUI_LIBS) \
	     -module -avoid-version -Wl, no-undefined

conndlgslib_LTLIBRARIES = \
	libiap_dialog_private_key_pw.la \
	libiap_dialog_gtc_challenge.la \
	libiap_dialog_server_cert.la \
	libiap_dialog_mschap_change.la \
	libiap_dialog_wps.la

libiap_dialog_private_key_pw_la_SOURCES = iap_dialog_private_key_pw.c
libiap_dialog_private_key_pw_la_CFLAGS = $(AM_CFLAGS) $(MAEMOSEC_CFLAGS)
libiap_dialog_private_key_pw_la_LDFLAGS = $(AM_LDFLAGS) $(MAEMOSEC_LIBS)

libiap_dialog_gtc_challenge_la_SOURCES = iap_dialog_gtc_challenge.c
libiap_dialog_gtc_challenge_la_CFLAGS = $(AM_CFLAGS) $(IAPSETTINGS_CFLAGS)
libiap_dialog_gtc_challenge_la_LDFLAGS = $(AM_LDFLAGS) $(IAPSETTINGS_LIBS)

libiap_dialog_server_cert_la_SOURCES = iap_dialog_server_cert.c

libiap_dialog_mschap_change_la_SOURCES = iap_dialog_mschap_change.c

libiap_dialog_wps_la_SOURCES = iap_dialog_wps.c
libiap_dialog_wps_la_CFLAGS = $(AM_CFLAGS) $(ICD_CFLAGS) $(CONBTUI_CFLAGS)
libiap_dialog_wps_la_LDFLAGS = $(AM_LDFLAGS) $(ICD_LIBS) $(CONBTUI_LIBS)
