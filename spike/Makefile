TOOLCHAIN ?= /opt/mister/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin
CC = $(TOOLCHAIN)/arm-none-linux-gnueabihf-gcc
STRIP = $(TOOLCHAIN)/arm-none-linux-gnueabihf-strip

fbspike: fbspike.c
	$(CC) -O2 -Wall -Wextra -static -o $@ $<
	$(STRIP) $@

clean:
	rm -f fbspike

.PHONY: clean
