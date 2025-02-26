PLUGIN_NAME=split-monitor-workspaces

SOURCE_FILES=$(wildcard src/*.cpp)

COMPILE_FLAGS=-g -fPIC --no-gnu-unique -std=c++23
COMPILE_FLAGS+=`pkg-config --cflags pixman-1 libdrm`
COMPILE_FLAGS+=-Iinclude

# use HYPRLAND_HEADERS environment variable to find Hyprland headers
# usage: HYPRLAND_HEADERS=/path/to/hyprland-sources/ make all,
#        where /path/to/hyprland-sources/ should include a directory 'hyprland' which contains the repository root.
ifneq ($(HYPRLAND_HEADERS),)
COMPILE_FLAGS+=-I"$(HYPRLAND_HEADERS)"
COMPILE_FLAGS+=-I"$(HYPRLAND_HEADERS)/protocols/"
endif
# fallback to installed headers in case some headers are missing in HYPRLAND_HEADERS
# this happens with the installed protocol headers
COMPILE_FLAGS+=`pkg-config --cflags hyprland`

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

check_env:
	@if [ -n "$(HYPRLAND_HEADERS)" ]; then \
		echo 'Using HYPRLAND_HEADERS enviroment variable to find Hyprland headers'; \
		if [ ! -d "$(HYPRLAND_HEADERS)/hyprland/protocols" ]; then \
			echo 'Hyprland headers not found. The hyprland sources should be in HYPRLAND_HEADERS/hyprland for this to work!'; \
			exit 1; \
		fi; \
	elif pkg-config --exists hyprland; then \
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
