.PHONY: build run clean
.SECONDARY:

CFLAGS += -std=gnu11 -MMD
CFLAGS += -g
CFLAGS += -flto -ffunction-sections -fdata-sections -Wl,--gc-sections
CFLAGS += -O2 -march=native -mtune=native
CFLAGS += -L lib/
CFLAGS += -D_GNU_SOURCE

sources := $(wildcard *.c)
objects := $(sources:%.c=%.o)
libs := m pthread ps2000a jpeg

san ?= 0
ifeq ($(san),1)
sanflags += -fsanitize=address -fno-omit-frame-pointer
sanflags += -fsanitize=undefined
endif

CFLAGS += $(sanflags)
LDFLAGS += $(sanflags)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

decoder: $(objects)
	$(CC) $(CFLAGS) -o $@ $^ $(libs:%=-l%)

clean:
	rm -f -- *.o *.d decoder

build: decoder

run: decoder
	./decoder

debug: decoder
	gdb --quiet --ex run --args ./decoder

video_preview:
	ffplay -loglevel warning -autoexit -nostats -analyzeduration 0 -strict experimental -probesize 32 -flags low_delay -fflags nobuffer -f mjpeg -vcodec mjpeg -an -max_delay 0 -max_probe_packets 0 -sync ext -framedrop -i pipe:

play: decoder
	./decoder | make -s video_preview

record: decoder
	mkdir -p recordings
	./decoder | tee recordings/$(shell date +%Y%m%d-%H%M%S).mjpg | make -s video_preview

-include $(wildcard *.d)
