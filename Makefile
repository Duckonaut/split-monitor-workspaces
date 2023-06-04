# compile with HYPRLAND_HEADERS=<path_to_hl> make all
# make sure that the path above is to the root hl repo directory, NOT src/
# and that you have ran `make protocols` in the hl dir.

PLUGIN_NAME=split-monitor-workspaces

SOURCE_FILES=$(wildcard src/*.cpp)

COMPILE_FLAGS=-g -fPIC --no-gnu-unique -std=c++23
COMPILE_FLAGS+=`pkg-config --cflags pixman-1 libdrm hyprland`
COMPILE_FLAGS+=-Iinclude

COMPILE_DEFINES=-DWLR_USE_UNSTABLE

INSTALL_LOCATION=${HOME}/.local/share/hyprload/plugins/bin

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
	mkdir -p ${INSTALL_LOCATION}
	cp $(PLUGIN_NAME).so ${INSTALL_LOCATION}

check_env:
	@if pkg-config --exists hyprland; then \
		echo 'Hyprland headers found.'; \
	else \
		echo 'Hyprland headers not available. Run `make pluginenv` in the root Hyprland directory.'; \
		exit 1; \
	fi
	@if [ -z $(BUILT_WITH_NOXWAYLAND) ]; then \
		echo 'Building with XWayland support.'; \
	else \
		echo 'Building without XWayland support.'; \
	fi

$(PLUGIN_NAME).so: $(SOURCE_FILES) $(INCLUDE_FILES)
	g++ -shared $(COMPILE_FLAGS) $(COMPILE_DEFINES) $(SOURCE_FILES) -o $(PLUGIN_NAME).so

clean:
	rm -f ./$(PLUGIN_NAME).so

clangd:
	echo "$(COMPILE_FLAGS) $(COMPILE_DEFINES)" | \
	sed 's/--no-gnu-unique//g' | \
	sed 's/ -/\n-/g' | \
	sed 's/std=c++23/std=c++2b/g' \
	> compile_flags.txt
