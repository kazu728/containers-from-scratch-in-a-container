CC := gcc
CFLAGS := -Wall -Wextra -O2
TARGET := container
SRCS := src/main.c
ROOTFS_TAR := ubuntu-rootfs.tar
ROOTFS_DIR := ubuntu-rootfs

RUN_SUDO ?=

.PHONY: all build run local clean local-clean tar

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

run:
	docker run --rm -it \
		--cap-add=SYS_ADMIN \
		container-from-scratch

local: $(TARGET)
	$(RUN_SUDO) ./$(TARGET) run /bin/bash

build: tar
	docker build --platform=linux/amd64 -t container-from-scratch .

local-clean:
	rm -f $(TARGET)
	rm -rf $(ROOTFS_DIR)

clean: local-clean
	docker rmi -f container-from-scratch 2>/dev/null || true

tar:
	rm -rf $(ROOTFS_DIR)
	mkdir -p $(ROOTFS_DIR)
	tar -xpf $(ROOTFS_TAR) -C $(ROOTFS_DIR)
