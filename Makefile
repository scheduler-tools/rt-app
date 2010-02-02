PROGR = rt-app
SOURCES = rt-app.c
HEADERS = rt-app.h

OBJS = $(SOURCES:.c=.o)

LIBS += -lrt -lpthread
OBJOPT += -D_GNU_SOURCE -D_XOPEN_SOURCE=600

LDFLAGS += $(LIBS)
ifeq ($(DEBUG), 1)
CFLAGS += -O0 -g -Wall
else
CFLAGS += -O2
endif

ifeq ($(AQUOSA), 1)
LIBS += -lqreslib
OBJOPT += -D AQUOSA=1
endif

all: $(PROGR)

$(PROGR): $(OBJS)
ifeq ($(VERBOSE), 1)
	$(CC) $(LPATH) $(LDFLAGS) $(OBJS) -o $(PROGR)
else
	@exec echo -n "=> Generating $<: ";
	@$(CC) $(LPATH) $(LDFLAGS) $(OBJS) -o $(PROGR)
	@exec echo "  [OK]"
endif


%.o: %.c $(HEADERS)
ifeq ($(VERBOSE), 1)
	$(CC) $(IPATH) $(OBJOPT) $(CFLAGS) -c $< 
else
	@exec echo -n "=> Compiling $<: ";
	@$(CC) $(IPATH) $(OBJOPT) $(CFLAGS) -c $< 
	@exec echo "  [OK]"
endif

clean:
ifeq ($(VERBOSE), 1)
	-rm -f $(OBJS) $(PROGR)
else
	@exec echo -n "=> Cleaning up $(PROGR): ";
	-@rm -f $(OBJS) $(PROGR)
	@exec echo "  [OK]"
endif

.PHONY: clean

