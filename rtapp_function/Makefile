SOURCE = $(wildcard */*.c)
LIBRARIES = $(SOURCE:.c=.so)

.PHONY: all clean

all: $(LIBRARIES)

clean:
	echo "cleaning libraries"
	rm -f */*.so

%.so : %.c
	@echo "*** building external workload $(<:.c=)"
	$(CC) $^  -rdynamic -shared -fPIC -o $@
