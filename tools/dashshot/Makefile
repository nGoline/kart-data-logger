# Host build of the dashboard renderer. Compiles the real lib/DashV2/ui_dash2.c
# and the real lib/DashFonts faces against LVGL's software renderer, so the PNGs
# are the firmware's layout rather than an impression of it.
#
#   make && ./dashshot out/      -> out/dash_*.png
#
# LVGL comes from the PlatformIO dependency tree, so `pio run` must have been
# done at least once. Its objects are cached in build/, so only ui_dash2.c and
# dashshot.c recompile on a normal edit (about a second).

ROOT   := ../..
BUILD  := build

# Whichever env has been built: pinning one meant building only the other left
# LVGL_SRC empty and the link failed with undefined lv_* rather than a reason.
LVGL   := $(firstword $(wildcard $(ROOT)/.pio/libdeps/*/lvgl))
ifeq ($(LVGL),)
$(error no LVGL in $(ROOT)/.pio/libdeps — run a firmware build first)
endif

# lv_conf.h lives in include/display and is found by name, matching the way the
# firmware build puts it on the include path.
CFLAGS := -O1 -std=gnu11 -Wall \
          -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
          -I$(ROOT)/include/display -I$(LVGL) -I$(LVGL)/src \
          -I$(ROOT)/lib/DashV2 -I$(ROOT)/lib/DashFonts

# LVGL's tree is guarded by LV_USE_* throughout, so unused subsystems compile to
# nothing and the whole of src/ can be swept in without picking a file list that
# would rot at the next version bump.
LVGL_SRC  := $(shell find $(LVGL)/src -name '*.c')
LOCAL_SRC := $(ROOT)/lib/DashV2/ui_dash2.c $(wildcard $(ROOT)/lib/DashFonts/*.c) dashshot.c

# Sources live above this directory, so their paths start with ../, and joining
# those onto $(BUILD) unchanged resolves back out of it. Flatten the leading
# ../ away so every object lands under $(BUILD).
obj = $(BUILD)/$(subst ../,,$(1:.c=.o))

LVGL_OBJ  := $(foreach s,$(LVGL_SRC),$(call obj,$(s)))
LOCAL_OBJ := $(foreach s,$(LOCAL_SRC),$(call obj,$(s)))

# Flattened stems are repo-relative (lib/DashV2/ui_dash2), so the search path
# is the repo root; dashshot.c has no ../ and resolves in place.
vpath %.c $(ROOT)

DEPS := $(ROOT)/include/display/lv_conf.h $(ROOT)/lib/DashV2/ui_dash2.h

all: dashshot

dashshot: $(LVGL_OBJ) $(LOCAL_OBJ)
	$(CC) -o $@ $^ -lm

# Header changes must force a rebuild; see the same note in tools/lapreplay.
$(BUILD)/%.o: %.c $(DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

shots: dashshot
	@mkdir -p out
	./dashshot out

clean:
	rm -rf $(BUILD) dashshot out

# LVGL objects survive this, so iterating on the dash stays fast.
clean-local:
	rm -f $(LOCAL_OBJ) dashshot

.PHONY: all clean clean-local shots
