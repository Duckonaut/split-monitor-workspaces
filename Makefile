PLUGIN_NAME=split-monitor-workspaces

SOURCE_FILES=$(wildcard src/*.cpp)

COMPILE_FLAGS=-g -fPIC --no-gnu-unique -std=c++23
COMPILE_FLAGS+=`pkg-config --cflags pixman-1 libdrm`
COMPILE_FLAGS+=-Iinclude

ifeq ($(HYPRLAND_HEADERS),)
COMPILE_FLAGS+=`pkg-config --cflags hyprland`
else
COMPILE_FLAGS+=-I"$(HYPRLAND_HEADERS)"
COMPILE_FLAGS+=-I"$(HYPRLAND_HEADERS)/protocols/"
endif

COMPILE_DEFINES=-DWLR_USE_UNSTABLE

.PHONY: clean clangd

all: check_env $(PLUGIN_NAME).so

check_env:
	@if [ -n "$(HYPRLAND_HEADERS)" ]; then \
		echo 'Using HYPRLAND_HEADERS enviroment variable to find Hyprland headers'; \
		if [ ! -d "$(HYPRLAND_HEADERS)/protocols" ]; then \
			echo 'Hyprland headers not found'; \
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
