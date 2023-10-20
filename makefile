CC=gcc
CFLAGS=-I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -Wall
LIBS=-lglib-2.0
CC_FRITZ=/usr/src/freetz-ng/toolchain/target/bin/i686-linux-uclibc-gcc-5.5.0
CFLAGS_FRITZ=-I/usr/src/freetz-ng/toolchain/target/i686-linux-uclibc/include/glib-2.0 -I/usr/src/freetz-ng/toolchain/target/i686-linux-uclibc/lib/glib-2.0/include -Wall --static -s
LIBS_FRITZ=-L/usr/src/freetz-ng/toolchain/target/i686-linux-uclibc/lib -lglib-2.0
APPNAME=solarlife


all: 
	$(CC) $(CFLAGS) *.c -o $(APPNAME) $(LIBS)


fritz: 
	$(CC_FRITZ) $(CFLAGS_FRITZ) *.c -o $(APPNAME) $(LIBS_FRITZ)

.PHONY: clean

clean:
	rm -f $(APPNAME)

