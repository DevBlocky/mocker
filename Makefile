CC ?= cc
CFLAGS ?= -O2 -pipe -Wall
LDFLAGS ?=
LDLIBS ?=

SRC_DIR := .
ROOTFS := rootfs

SBINS := init getty login adduser gpasswd kill shutdown
BINS := sh echo ls cat mv cp rm mkdir pwd env sleep segfault whoami sudo su gpasswd

.PHONY: all build rootfs run clean

all: rootfs

SBIN_TARGETS := $(addprefix $(ROOTFS)/usr/sbin/,$(SBINS))
$(ROOTFS)/usr/sbin/login: LDLIBS += -lcrypt
$(ROOTFS)/usr/sbin/adduser: LDLIBS += -lcrypt
$(ROOTFS)/usr/sbin/%: %.o
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

BIN_TARGETS := $(addprefix $(ROOTFS)/usr/bin/,$(BINS))
$(ROOTFS)/usr/bin/%: %.o
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

ALL_TARGETS := $(SBIN_TARGETS) $(BIN_TARGETS)
build: $(ALL_TARGETS)

rootfs: $(ALL_TARGETS)
	@for bin in $(ALL_TARGETS); do \
		./scripts/copy-libs.sh $$bin $(ROOTFS); \
	done
	@cp -a ./etc ./$(ROOTFS)/
	@chmod 644 $(ROOTFS)/etc/*
	@chmod 600 $(ROOTFS)/etc/shadow
	@chmod u+s $(ROOTFS)/usr/bin/sudo $(ROOTFS)/usr/bin/su

mocker: mocker.o
	$(CC) $(LDFLAGS) $^ -o $@

run: rootfs mocker
	./mocker rootfs

clean:
	rm -rf $(ROOTFS) mocker *.o
