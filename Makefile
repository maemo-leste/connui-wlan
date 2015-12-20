CLIBS := libiap_dialog_private_key_pw.so libiap_dialog_gtc_challenge.so libiap_dialog_server_cert.so
LIBS := libiap_dialog_mschap_change.so libiap_dialog_wps.so $(CLIBS)

all: $(LIBS)

install: $(LIBS)
	install -d "$(DESTDIR)/usr/lib/conndlgs/"
	for file in $^; do install -m 644 $$file "$(DESTDIR)/usr/lib/conndlgs/"; done

clean:
	$(RM) $(CLIBS)

libiap_dialog_private_key_pw.so: iap_dialog_private_key_pw.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs dbus-1 maemosec-certman maemosec-certman-applet libconnui) -W -Wall -O2 -shared -Wl,-soname,$@ $^ -o $@

libiap_dialog_gtc_challenge.so: iap_dialog_gtc_challenge.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs dbus-1 hildon-1 libconnui_iapsettings) -W -Wall -O2 -shared -Wl,-soname,$@ $^ -o $@

libiap_dialog_server_cert.so: iap_dialog_server_cert.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs dbus-1 maemosec-certman-applet libconnui) -W -Wall -O2 -shared -Wl,-soname,$@ $^ -o $@
