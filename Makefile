# ******************************************************************
# CS-370 term project: Source-vs-System signal processor
#
# Build:         make                    (native, all daemons with sources)
# Tests:         make test
# Sanitizers:    make asan
# Valgrind:      make memcheck
# Soak sanity:   make soakcheck          (1-hour miniature of the 48h run)
# Cross/deploy:  make deploy PI=pi@<host> (cross-builds, rsyncs bin/ + os/ +
#                systemd/ to the Pi; copy-only -- it prints the install/
#                restart commands to run yourself over ssh, it does not run
#                them for you)
# Clean:         make clean
#
# Until a given directory has its first .c file, the daemon that depends on
# it is skipped (not an error) so `make`/`make test`/etc. all succeed on the
# empty tree. Build logic that needs unit testing (biquads, the ring, the
# delay estimator) belongs in src/common/ or analysis/, never behind ALSA or
# sd_notify calls -- those stay in each daemon's own main.c. That split is
# what lets `make test`/`make asan`/`make memcheck` run with no Pi attached.
# ******************************************************************

# --- Toolchains ---
CC           ?= gcc
CROSS_PREFIX ?= aarch64-linux-gnu-
CROSS_CC     = $(CROSS_PREFIX)gcc
# NOTE: cross-linking against -lasound/-lsystemd requires an aarch64 sysroot
# that actually has those libs; not set up/verified yet.

CSTD       = -std=c17 -Wall -Wextra -Werror
CFLAGS     = $(CSTD) -g -Isrc/common -MMD -MP
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer

LDLIBS_AUDIOD    = -lasound -lsystemd -lpthread -lm
LDLIBS_CTLD      = -lsystemd -lpthread
LDLIBS_ANALYZERD = -lsystemd -lpthread -lm

NATIVE_DIR = build/native
ASAN_DIR   = build/asan
CROSS_DIR  = build/cross

BIN_NATIVE = $(NATIVE_DIR)/bin
BIN_CROSS  = $(CROSS_DIR)/bin

# --- Sources ---
COMMON_SRCS   = $(wildcard src/common/*.c)
AUDIOD_OWN    = $(wildcard src/audio/*.c)
CTLD_OWN      = $(wildcard src/ctl/*.c)
ANALYZERD_OWN = $(wildcard analysis/*.c)

AUDIOD_SRCS    = $(AUDIOD_OWN) $(COMMON_SRCS)
CTLD_SRCS      = $(CTLD_OWN) $(COMMON_SRCS)
ANALYZERD_SRCS = $(ANALYZERD_OWN) $(COMMON_SRCS)

# Testable logic only -- never a daemon's own main.c, so test binaries don't
# fight over which main() wins.
TESTABLE_SRCS = $(COMMON_SRCS) $(ANALYZERD_OWN)
TEST_SRCS     = $(wildcard tests/test_*.c)

native_obj = $(patsubst %.c,$(NATIVE_DIR)/obj/%.o,$(1))
asan_obj   = $(patsubst %.c,$(ASAN_DIR)/obj/%.o,$(1))
cross_obj  = $(patsubst %.c,$(CROSS_DIR)/obj/%.o,$(1))

AUDIOD_OBJS_NATIVE    = $(call native_obj,$(AUDIOD_SRCS))
CTLD_OBJS_NATIVE      = $(call native_obj,$(CTLD_SRCS))
ANALYZERD_OBJS_NATIVE = $(call native_obj,$(ANALYZERD_SRCS))
TEST_OBJS_SHARED      = $(call native_obj,$(TESTABLE_SRCS))

ASAN_TEST_OBJS_SHARED = $(call asan_obj,$(TESTABLE_SRCS))

AUDIOD_OBJS_CROSS    = $(call cross_obj,$(AUDIOD_SRCS))
CTLD_OBJS_CROSS      = $(call cross_obj,$(CTLD_SRCS))
ANALYZERD_OBJS_CROSS = $(call cross_obj,$(ANALYZERD_SRCS))

ALL_NATIVE_DEPS = $(AUDIOD_OBJS_NATIVE:.o=.d) $(CTLD_OBJS_NATIVE:.o=.d) \
                   $(ANALYZERD_OBJS_NATIVE:.o=.d) $(TEST_OBJS_SHARED:.o=.d)
ALL_ASAN_DEPS   = $(ASAN_TEST_OBJS_SHARED:.o=.d)
ALL_CROSS_DEPS  = $(AUDIOD_OBJS_CROSS:.o=.d) $(CTLD_OBJS_CROSS:.o=.d) \
                   $(ANALYZERD_OBJS_CROSS:.o=.d)
-include $(ALL_NATIVE_DEPS) $(ALL_ASAN_DEPS) $(ALL_CROSS_DEPS)

# --- Which daemons currently have sources ---
ALL_TARGETS   :=
CROSS_TARGETS :=

ifneq ($(strip $(AUDIOD_OWN)),)
ALL_TARGETS   += $(BIN_NATIVE)/audiod
CROSS_TARGETS += $(BIN_CROSS)/audiod
else
$(info skipping audiod: no sources in src/audio/ yet)
endif

ifneq ($(strip $(CTLD_OWN)),)
ALL_TARGETS   += $(BIN_NATIVE)/ctld
CROSS_TARGETS += $(BIN_CROSS)/ctld
else
$(info skipping ctld: no sources in src/ctl/ yet)
endif

ifneq ($(strip $(ANALYZERD_OWN)),)
ALL_TARGETS   += $(BIN_NATIVE)/analyzerd
CROSS_TARGETS += $(BIN_CROSS)/analyzerd
else
$(info skipping analyzerd: no sources in analysis/ yet)
endif

.PHONY: all test asan memcheck soakcheck deploy cross check-pi clean \
        audiod ctld analyzerd

all: $(ALL_TARGETS)
	@if [ -z "$(strip $(ALL_TARGETS))" ]; then echo "nothing to build yet"; fi

audiod: $(BIN_NATIVE)/audiod
ctld: $(BIN_NATIVE)/ctld
analyzerd: $(BIN_NATIVE)/analyzerd

# --- Native pattern rules ---
$(NATIVE_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_NATIVE)/audiod: $(AUDIOD_OBJS_NATIVE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS_AUDIOD)

$(BIN_NATIVE)/ctld: $(CTLD_OBJS_NATIVE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS_CTLD)

$(BIN_NATIVE)/analyzerd: $(ANALYZERD_OBJS_NATIVE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS_ANALYZERD)

# --- Tests (dependency-free runner: each tests/test_*.c is standalone) ---
TEST_BIN_DIR = $(NATIVE_DIR)/tests
TEST_BINS    = $(patsubst tests/%.c,$(TEST_BIN_DIR)/%,$(TEST_SRCS))

$(TEST_BIN_DIR)/%: tests/%.c $(TEST_OBJS_SHARED)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(TEST_OBJS_SHARED) -o $@ -lm

test: $(TEST_BINS)
	@if [ -z "$(strip $(TEST_SRCS))" ]; then \
		echo "no tests yet"; \
	else \
		status=0; \
		for t in $(TEST_BINS); do \
			echo "-- $$t --"; \
			$$t || status=1; \
		done; \
		exit $$status; \
	fi

# --- AddressSanitizer build of the same tests ---
ASAN_CFLAGS      = $(CFLAGS) $(ASAN_FLAGS)
ASAN_TEST_BIN_DIR = $(ASAN_DIR)/tests
ASAN_TEST_BINS    = $(patsubst tests/%.c,$(ASAN_TEST_BIN_DIR)/%,$(TEST_SRCS))

$(ASAN_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ASAN_CFLAGS) -c $< -o $@

$(ASAN_TEST_BIN_DIR)/%: tests/%.c $(ASAN_TEST_OBJS_SHARED)
	@mkdir -p $(dir $@)
	$(CC) $(ASAN_CFLAGS) $< $(ASAN_TEST_OBJS_SHARED) -o $@ -lm

asan: $(ASAN_TEST_BINS)
	@if [ -z "$(strip $(TEST_SRCS))" ]; then \
		echo "no tests yet"; \
	else \
		status=0; \
		for t in $(ASAN_TEST_BINS); do \
			echo "-- $$t --"; \
			$$t || status=1; \
		done; \
		exit $$status; \
	fi

# --- Valgrind over the native tests ---
memcheck: $(TEST_BINS)
	@if [ -z "$(strip $(TEST_SRCS))" ]; then \
		echo "no tests yet"; \
	else \
		status=0; \
		for t in $(TEST_BINS); do \
			echo "-- valgrind $$t --"; \
			valgrind --leak-check=full --error-exitcode=1 --quiet $$t || status=1; \
		done; \
		exit $$status; \
	fi

# --- 1-hour soak sanity (needs real daemons + a status-block reader; stub
# until audiod/analyzerd exist) ---
soakcheck: all
	@if [ -z "$(strip $(ALL_TARGETS))" ]; then \
		echo "nothing to soak yet"; \
	else \
		python3 tools/soakcheck.py --duration 3600 --bin-dir $(BIN_NATIVE); \
	fi

# --- Cross build for the Pi (aarch64) ---
$(CROSS_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CROSS_CC) $(CFLAGS) -c $< -o $@

$(BIN_CROSS)/audiod: $(AUDIOD_OBJS_CROSS)
	@mkdir -p $(dir $@)
	$(CROSS_CC) $(CFLAGS) $^ -o $@ $(LDLIBS_AUDIOD)

$(BIN_CROSS)/ctld: $(CTLD_OBJS_CROSS)
	@mkdir -p $(dir $@)
	$(CROSS_CC) $(CFLAGS) $^ -o $@ $(LDLIBS_CTLD)

$(BIN_CROSS)/analyzerd: $(ANALYZERD_OBJS_CROSS)
	@mkdir -p $(dir $@)
	$(CROSS_CC) $(CFLAGS) $^ -o $@ $(LDLIBS_ANALYZERD)

cross: $(CROSS_TARGETS)

# --- Deploy: copy-only. Builds, rsyncs, then tells you the install steps;
# never runs systemctl on the Pi itself. ---
DEPLOY_STAGING = ~/dspunit-deploy

check-pi:
	@if [ -z "$(PI)" ]; then \
		echo "PI is not set -- usage: make deploy PI=pi@<host>" >&2; \
		exit 1; \
	fi

deploy: check-pi cross
	@if [ -z "$(strip $(CROSS_TARGETS))" ]; then \
		echo "nothing to deploy yet -- no daemon sources exist" >&2; \
		exit 1; \
	fi
	ssh $(PI) 'mkdir -p $(DEPLOY_STAGING)/bin $(DEPLOY_STAGING)/os $(DEPLOY_STAGING)/systemd'
	rsync -av $(CROSS_TARGETS) $(PI):$(DEPLOY_STAGING)/bin/
	rsync -av os/ $(PI):$(DEPLOY_STAGING)/os/
	rsync -av systemd/ $(PI):$(DEPLOY_STAGING)/systemd/
	@echo ""
	@echo "Copied to $(PI):$(DEPLOY_STAGING)/. Nothing on the Pi was restarted."
	@echo "To install and activate, run on the Pi (or: ssh $(PI) '...'):"
	@echo "  sudo install -m755 $(DEPLOY_STAGING)/os/rt-tune.sh /usr/local/sbin/rt-tune.sh"
	@echo "  sudo cp $(DEPLOY_STAGING)/bin/* /usr/local/bin/"
	@echo "  sudo cp $(DEPLOY_STAGING)/systemd/*.service /etc/systemd/system/"
	@echo "  sudo systemctl daemon-reload"
	@echo "  # TODO: rt-tune.service doesn't exist in systemd/ yet -- add it once it lands"
	@echo "  sudo systemctl enable --now rt-tune.service audiod analyzerd ctld"
	@echo ""
	@echo "  sudo cp $(DEPLOY_STAGING)/os/sshd_config.d/*.conf /etc/ssh/sshd_config.d/"
	@echo "  sudo systemctl reload sshd"
	@echo ""
	@echo "  # TODO: fill in LAN_SUBNET/UI_PORT CHANGEME placeholders in os/nftables.conf"
	@echo "  # before applying -- keep a console/physical session open, a bad rule can"
	@echo "  # lock out the only SSH path (see os/VERIFY.md)"
	@echo "  # Only enable this after the RT kernel is already installed+held via apt --"
	@echo "  # this firewall drops outbound, so apt won't work once it's on"
	@echo "  # (bootstrap-before-firewall ordering: ProposedStructure.md §2, issues.md C7)"
	@echo "  sudo cp $(DEPLOY_STAGING)/os/nftables.conf /etc/nftables.conf"
	@echo "  sudo systemctl enable --now nftables"
	@echo ""
	@echo "  # cmdline.txt/isolcpus and config.txt/kernel= -> /boot/firmware/: deploy is"
	@echo "  # copy-only and never edits the Pi's live boot config itself, so run this by"
	@echo "  # hand. It's idempotent (checks before writing, won't duplicate entries on a"
	@echo "  # repeat run) -- see os/merge-boot-config.sh for what it does and why."
	@echo "  sudo sh $(DEPLOY_STAGING)/os/merge-boot-config.sh"
	@echo "  sudo reboot"

# --- Cleanup ---
clean:
	rm -rf build
