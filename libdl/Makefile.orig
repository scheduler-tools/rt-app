# go on and adjust here if you don't like those flags
CFLAGS=-Os -s -pipe -DDEBUG
#CFLAGS=-Wall -Os -fomit-frame-pointer -s -pipe -DDEBUG
CC=$(CROSS_COMPILE)gcc
# likewise, if you want to change the destination prefix
DESTPREFIX=/usr/local
DESTDIR=lib
TARGET=libdl
DOCS=COPYING README
RELEASE=$(shell basename `pwd`)

all: static shared

clean:
	rm -f *.o $(TARGET).so* $(TARGET).a

distclean: clean
	rm -f *~ *.s

install: all
	install -d $(DESTPREFIX)/$(DESTDIR)
	install -c $(TARGET) $(DESTPREFIX)/$(DESTDIR)

install-doc:
	install -d $(DESTPREFIX)/share/doc/libdl
	install -c $(DOCS) $(DESTPREFIX)/share/doc/libdl

release: distclean release_gz release_bz2
	@echo --- $(RELEASE) released ---

release_gz: distclean
	@echo Building tar.gz
	( cd .. ; tar czf $(RELEASE).tar.gz $(RELEASE) )

release_bz2: distclean
	@echo Building tar.bz2
	( cd .. ; tar cjf $(RELEASE).tar.bz2 $(RELEASE) )

static: $(TARGET).o
	$(AR) rcs $(TARGET).a $(TARGET).o

shared: $(TARGET).o
	$(CC) -shared -Wl,-soname,$(TARGET).so.1 -o $(TARGET).so.1.0.0 $(TARGET).o

$(TARGET).o: dl_syscalls.h dl_syscalls.c
	$(CC) -fPIC $(CFLAGS) -c dl_syscalls.c -o $(TARGET).o

