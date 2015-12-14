CLIBS := libiap_dialog_private_key_pw.so libiap_dialog_gtc_challenge.so
LIBS := libiap_dialog_mschap_change.so libiap_dialog_server_cert.so libiap_dialog_wps.so $(CLIBS)

all: $(LIBS)

install: $(LIBS)
	install -d "$(DESTDIR)/usr/lib/conndlgs/"
	for file in $^; do install -m 644 $$file "$(DESTDIR)/usr/lib/conndlgs/"; done

clean:
	$(RM) $(CLIBS)

lib%.so: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs gconf-2.0 hildon-1 dbus-1 maemosec-certman maemosec-certman-applet libconnui) -W -Wall -O2 -shared -Wl,-soname,$@ $^ -o $@
