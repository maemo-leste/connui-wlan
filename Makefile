LIBS := libiap_dialog_gtc_challenge.so libiap_dialog_mschap_change.so libiap_dialog_private_key_pw.so libiap_dialog_server_cert.so libiap_dialog_wps.so

all: $(LIBS)

install: $(LIBS)
	install -d "$(DESTDIR)/usr/lib/conndlgs/"
	for file in $^; do install -m 644 $$file "$(DESTDIR)/usr/lib/conndlgs/"; done

clean:
