CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local

ipu4-softisp: ipu4-softisp.c
	$(CC) $(CFLAGS) -o $@ $< -lm

ipu4-softisp-surface-rear: ipu4-softisp.c
	$(CC) $(CFLAGS) -DIN_W=3264 -DIN_H=2448 -DIN_STRIDE=4096 -o $@ $< -lm

ipu4-softisp-surface-front: ipu4-softisp.c
	$(CC) $(CFLAGS) -DIN_W=2592 -DIN_H=1944 -DIN_STRIDE=3264 -o $@ $< -lm

libgstsurfaceisp.so: gstsurfaceisp.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< \
		$(shell pkg-config --cflags --libs gstreamer-base-1.0)

install: ipu4-softisp
	install -D -m755 ipu4-softisp $(DESTDIR)$(PREFIX)/bin/ipu4-softisp

clean:
	rm -f ipu4-softisp ipu4-softisp-surface-rear ipu4-softisp-surface-front libgstsurfaceisp.so
