CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local

ipu4-softisp: ipu4-softisp.c
	$(CC) $(CFLAGS) -o $@ $< -lm

install: ipu4-softisp
	install -D -m755 ipu4-softisp $(DESTDIR)$(PREFIX)/bin/ipu4-softisp

clean:
	rm -f ipu4-softisp
