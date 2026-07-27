# C++ plugin for Hyprland (deprecated)

[![Build](https://github.com/zjeffer/split-monitor-workspaces/actions/workflows/main.yml/badge.svg?branch=main)](https://github.com/zjeffer/split-monitor-workspaces/actions/workflows/main.yml) [![Build on latest Hyprland release](https://github.com/zjeffer/split-monitor-workspaces/actions/workflows/release.yml/badge.svg)](https://github.com/zjeffer/split-monitor-workspaces/actions/workflows/release.yml)

> [!CAUTION] 
> This plugin has been deprecated. It is recommended to use the Lua package instead. 
> Below is the old documentation for the C++ plugin.

A small plugin to provide `awesome`/`dwm`-like behavior with workspaces: split them between monitors and provide independent numbering

# Requirements

- Hyprland >= v0.38.1, <= v0.56.x. The latest git of Hyprland is not supported anymore. Use the lua plugin instead.
- `meson` and `ninja` to compile the plugin

# Installing

## Using [hyprpm](https://wiki.hyprland.org/Plugins/Using-Plugins/#hyprpm)

Hyprpm is a tool integrated with the latest Hyprland version, to use it first you'll need to add the repository and then enable the plugin.

```BASH
hyprpm add https://github.com/zjeffer/split-monitor-workspaces # Add the plugin repository
hyprpm enable split-monitor-workspaces # Enable the plugin
hyprpm reload # Reload the plugins
```

Add the following in your `hyprland.conf` file to automatically load the plugin at startup:

```
exec-once = hyprpm reload -n
```

## NixOS installation

With flakes enabled, a sample installation will look like this:

```nix
# flake.nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    home-manager = {
      url = "github:nix-community/home-manager";
      inputs.nixpkgs.follows = "nixpkgs";
    };

    hyprland.url = "github:hyprwm/Hyprland";
    split-monitor-workspaces = {
      url = "github:zjeffer/split-monitor-workspaces";
      inputs.hyprland.follows = "hyprland"; # <- make sure this line is present for the plugin to work as intended
    };
  };

  outputs = {
    self,
    nixpkgs,
    home-manager,
    split-monitor-workspaces,
    ...
  }: let
    system = "x86_64-linux";
    #        ↑ Swap it for your system if needed
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    nixosConfigurations = {
      yourHostname = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [
          # ...
          home-manager.nixosModules.home-manager
          {
            home-manager = {
              useGlobalPkgs = true;
              useUserPackages = true;
              users.yourUsername = {
                wayland.windowManager.hyprland = {
                  # ...
                  plugins = [
                    split-monitor-workspaces.packages.${pkgs.stdenv.hostPlatform.system}.split-monitor-workspaces
                  ];
                  # ...
                };
              };
            };
          }
        ];
        # ...
      };
    };
  };
}
```

You will need to have home-manager installed and configured. You use `wayland.windowManager.hyprland.plugins = [];` to add the plugin. The home-manager module will handle the rest.

## Manual installation

Since Hyprland plugins don't have ABI guarantees, you _should_ download the Hyprland source and compile it if you plan to install plugins manually. This ensures the compiler version is the same between the Hyprland build you're running, and the plugins you are using.

The guide on compiling and installing Hyprland manually is on the [wiki](http://wiki.hyprland.org/Getting-Started/Installation/#manual-manual-build)

1. Export the `HYPRLAND_HEADERS` variable to point to the root directory of the Hyprland repo, for example:
    - `export HYPRLAND_HEADERS="$HOME/repos/Hyprland"`
2. Compile
    - `make all`
3. Add this line to the bottom of your hyprland config
    - `exec-once=hyprctl plugin load <ABSOLUTE PATH TO split-monitor-workspaces.so>`

# Usage

## Dispatchers

The plugin provides drop-in replacements for workspace-related commands, to be able to easily specify the `n`th workspace on the focused monitor:

| Description                   | Legacy config dispatcher      | Lua function                   |
| ----------------------------- | ----------------------------- | ------------------------------ |
| Switch to workspace           | `split-workspace`             | `smw.workspace`                |
| Move window to workspace      | `split-movetoworkspace`       | `smw.move_to_workspace`        |
| Move window (no focus change) | `split-movetoworkspacesilent` | `smw.move_to_workspace_silent` |

The lua function example assumes you set `local smw = hl.plugin.split_monitor_workspaces` in your config.

These commands also support passing `+x`/`-x` (`x: int`) to specify a workspace relative to the currently focused one, with the extra feature that they will wrap around. So when you get to the end and use `+1`, it will go to the first workspace. When you're at the first workspace and use `-1`, it will go to the last workspace. Special relative arguments like `r+1`, `m+1`, ... will simply be passed to Hyprland as if they were used with the normal commands. If you want to use those arguments, there's no need to use the split- variants.

If you set `enable_wrapping = false` in the plugin config, it will not wrap around and will just stop at the first or last workspace. So in this case if you specify +100, but you only have 10 workspaces, it will just go to the last workspace (10).

#### Additional commands

| Description | Arguments | Legacy config dispatcher | Lua function |
| --- | --- | --- | --- |
| Cycle through workspaces on current monitor | `next/prev/+x/-x` | `split-cycleworkspaces` | `smw.cycle_workspaces` |
| Move window to next/previous monitor | `next/prev/+x/-x` | `split-changemonitor` | `smw.change_monitor` |
| Move window to next/previous monitor (silent) | `next/prev/+x/-x` | `split-changemonitorsilent` | `smw.change_monitor_silent` |
| Move rogue windows to current monitor |  | `split-grabroguewindows` | `smw.grab_rogue_windows` |

> **Deprecated:** `split-cycleworkspacesnowrap` - set `enable_wrapping = false` in the plugin config instead.

## Configuration

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `count` | int | 10 | How many workspaces to bind to the monitor |
| `keep_focused` | boolean | 0 | Keep current workspaces focused on plugin init/reload |
| `enable_notifications` | boolean | 0 | Enable notifications |
| `enable_persistent_workspaces` | boolean | 1 | Create `$count` persistent workspaces on each monitor at initialization |
| `enable_wrapping` | boolean | 1 | Wrap around when cycling workspaces or moving windows to prev/next workspace |
| `monitor_priority` | list of strings | - | Set per-monitor workspace assignment order. The first monitor gets the lowest workspace numbers. |
| `max_workspaces` | string + int | - | Set a maximum number of workspaces per monitor. Should be called multiple times for each monitor. |
| `link_monitors` | boolean | 0 | Gnome-like workspace switching: switching on one monitor switches all monitors to the same workspace number |
| `enable_hy3` | boolean | 1 | When the [hy3](https://github.com/outfoxxed/hy3) plugin is loaded, move operations will use hy3 dispatchers to move entire node groups across workspaces |

If your workspace-per-monitor count is 10, the first monitor will have workspaces 1-10, the second 11-20 and so on. They will be accessed via numbers 1-10 while your mouse is on a given monitor.

### Lua config

Hyprland supports a [Lua-based config system](https://wiki.hyprland.org/Configuring/Lua-Config/). When using Lua, the plugin config goes inside `hl.config(...)` under `plugin.split_monitor_workspaces` (note the underscores). Dispatchers and other plugin functions are accessed via `hl.plugin.split_monitor_workspaces`.

```lua
-- Example lua config
hl.config({
    plugin = {
        split_monitor_workspaces = {
            count                        = 5,
            keep_focused                 = 0,
            enable_notifications         = 0,
            enable_persistent_workspaces = 1,
            enable_wrapping              = 1,
            link_monitors                = 0,
            -- enable_hy3                = 1,
        },
    },
})

-- Set monitor priority (first entry = lowest workspace numbers)
local smw = hl.plugin.split_monitor_workspaces
smw.monitor_priority({ "DP-1", "DP-2" })

-- Set max workspaces per monitor (call once per monitor)
smw.max_workspaces({ monitor = "DP-1", max = 9 })
smw.max_workspaces({ monitor = "DP-2", max = 5 })
```

Keybindings using the plugin's Lua dispatchers:

```lua
-- keybinds.lua
local smw = hl.plugin.split_monitor_workspaces

-- Switch workspaces with mainMod + [1-5]
for i = 1, 5 do
    local key = tostring(i)
    hl.bind(mainMod .. " + " .. key, function() return smw.workspace(i) end)
    hl.bind(mainMod .. " + SHIFT + " .. key, function() return smw.move_to_workspace_silent(i) end)
end
```

### Legacy config (.conf)

```
# in your hyprland.conf:

plugin {
    split-monitor-workspaces {
        count = 5
        keep_focused = 0
        enable_notifications = 0
        enable_persistent_workspaces = 1
        enable_wrapping = 1

        # set this to 1 for gnome-like workspace switching
        link_monitors = 0

        # set this to 0 to disable hy3 support
        # enable_hy3 = 1

        # set monitor priority (first = lowest workspace numbers)
        monitor_priority = DP-1, DP-2

        # set max workspaces per monitor
        max_workspaces = DP-1, 9
        max_workspaces = DP-2, 5
    }
}

$mainMod = SUPER
# Switch workspaces with mainMod + [1-5]
bind = $mainMod, 1, split-workspace, 1
bind = $mainMod, 2, split-workspace, 2
bind = $mainMod, 3, split-workspace, 3
bind = $mainMod, 4, split-workspace, 4
bind = $mainMod, 5, split-workspace, 5

# Move active window to a workspace with mainMod + SHIFT + [1-5]
bind = $mainMod SHIFT, 1, split-movetoworkspacesilent, 1
bind = $mainMod SHIFT, 2, split-movetoworkspacesilent, 2
bind = $mainMod SHIFT, 3, split-movetoworkspacesilent, 3
bind = $mainMod SHIFT, 4, split-movetoworkspacesilent, 4
bind = $mainMod SHIFT, 5, split-movetoworkspacesilent, 5
```

#### Omarchy compatibility

For Omarchy installations, make sure you first unbind Omarchy's keybindings by putting the following above your own keybindings (see [this comment](https://github.com/zjeffer/split-monitor-workspaces/issues/203#issuecomment-3426554922) for more details):

```
# Unbind Omarchy workspace bindings
unbind = SUPER, code:10
unbind = SUPER, code:11
unbind = SUPER, code:12
unbind = SUPER, code:13
unbind = SUPER, code:14
unbind = SUPER, code:15
unbind = SUPER, code:16
unbind = SUPER, code:17
unbind = SUPER, code:18
unbind = SUPER, code:19
unbind = SUPER SHIFT, code:10
unbind = SUPER SHIFT, code:11
unbind = SUPER SHIFT, code:12
unbind = SUPER SHIFT, code:13
unbind = SUPER SHIFT, code:14
unbind = SUPER SHIFT, code:15
unbind = SUPER SHIFT, code:16
unbind = SUPER SHIFT, code:17
unbind = SUPER SHIFT, code:18
unbind = SUPER SHIFT, code:19
```
