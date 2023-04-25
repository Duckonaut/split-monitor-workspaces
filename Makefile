# compile with HYPRLAND_HEADERS=<path_to_hl> make all
# make sure that the path above is to the root hl repo directory, NOT src/
# and that you have ran `make protocols` in the hl dir.

PLUGIN_NAME=split-monitor-workspaces

SOURCE_FILES=$(wildcard src/*.cpp)

COMPILE_FLAGS=-g -fPIC --no-gnu-unique -std=c++23
COMPILE_FLAGS+=-I "/usr/include/pixman-1"
COMPILE_FLAGS+=-I "/usr/include/libdrm"
COMPILE_FLAGS+=-I "${HYPRLAND_HEADERS}"
COMPILE_FLAGS+=-I "${HYPRLAND_HEADERS}/subprojects/wlroots/include"
COMPILE_FLAGS+=-I "${HYPRLAND_HEADERS}/subprojects/wlroots/build/include"
COMPILE_FLAGS+=-Iinclude

.PHONY: clean clangd

all: check_env $(PLUGIN_NAME).so

install: all
	cp $(PLUGIN_NAME).so ${HOME}/.local/share/hyprload/plugins/bin

check_env:
ifndef HYPRLAND_HEADERS
	$(error HYPRLAND_HEADERS is undefined! Please set it to the path to the root of the configured Hyprland repo)
endif

$(PLUGIN_NAME).so: $(SOURCE_FILES) $(INCLUDE_FILES)
	g++ -shared $(COMPILE_FLAGS) $(SOURCE_FILES) -o $(PLUGIN_NAME).so

clean:
	rm -f ./$(PLUGIN_NAME).so

clangd:
	printf "%b" "-I/usr/include/pixman-1\n-I/usr/include/libdrm\n-I${HYPRLAND_HEADERS}\n-Iinclude\n-std=c++2b" > compile_flags.txt
