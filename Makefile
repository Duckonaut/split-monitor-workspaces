# compile with HYPRLAND_HEADERS=<path_to_hl> make all
# make sure that the path above is to the root hl repo directory, NOT src/
# and that you have ran `make protocols` in the hl dir.

PLUGIN_NAME=split-monitor-workspaces

SOURCE_FILES=$(wildcard src/*.cpp)

COMPILE_FLAGS=-g -fPIC --no-gnu-unique -std=c++23
COMPILE_FLAGS+=-I "/usr/include/pixman-1"
COMPILE_FLAGS+=-I "/usr/include/libdrm"
COMPILE_FLAGS+=-I "${HYPRLAND_HEADERS}"
COMPILE_FLAGS+=-I "${HYPRLAND_HEADERS}/protocols"
COMPILE_FLAGS+=-I "${HYPRLAND_HEADERS}/subprojects/wlroots/include"
COMPILE_FLAGS+=-I "${HYPRLAND_HEADERS}/subprojects/wlroots/build/include"
COMPILE_FLAGS+=-Iinclude

COMPILE_DEFINES=-DWLR_USE_UNSTABLE

ifeq ($(shell whereis -b jq), "jq:")
$(error "jq not found. Please install jq.")
else
BUILT_WITH_NOXWAYLAND=$(shell hyprctl version -j | jq -r '.flags | .[]' | grep 'no xwayland')
ifneq ($(BUILT_WITH_NOXWAYLAND),)
COMPILE_DEFINES+=-DNO_XWAYLAND
endif
endif

.PHONY: clean clangd

all: check_env $(PLUGIN_NAME).so

install: all
	cp $(PLUGIN_NAME).so ${HOME}/.local/share/hyprload/plugins/bin

check_env:
	@if [ -z "${HYPRLAND_HEADERS}" ]; then \
		echo "HYPRLAND_HEADERS not set. Please set it to the root of the hl repo directory."; \
		exit 1; \
	fi

$(PLUGIN_NAME).so: $(SOURCE_FILES) $(INCLUDE_FILES)
	g++ -shared $(COMPILE_FLAGS) $(COMPILE_DEFINES) $(SOURCE_FILES) -o $(PLUGIN_NAME).so

clean:
	rm -f ./$(PLUGIN_NAME).so

clangd:
	printf "%b" "-I/usr/include/pixman-1\n-I/usr/include/libdrm\n-I${HYPRLAND_HEADERS}\n-Iinclude\n-std=c++2b" > compile_flags.txt
