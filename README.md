# split-monitor-workspaces
[![Build](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/main.yml/badge.svg?branch=main)](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/main.yml)
[![Build on latest Hyprland release](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/release.yml/badge.svg)](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/release.yml)


A small plugin to provide `awesome`/`dwm`-like behavior with workspaces: split them between monitors and provide independent numbering

# Requirements
- Hyprland >= v0.38.1

# Installing
Since Hyprland plugins don't have ABI guarantees, you *should* download the Hyprland source and compile it if you plan to use plugins.
This ensures the compiler version is the same between the Hyprland build you're running, and the plugins you are using.

The guide on compiling and installing Hyprland manually is on the [wiki](http://wiki.hyprland.org/Getting-Started/Installation/#manual-manual-build)

## Using [hyprpm](https://wiki.hyprland.org/Plugins/Using-Plugins/#hyprpm)
Hyprpm is a tool integrated with the latest Hyprland version, to use it first you'll need to add the repository and then enable the plugin
```BASH
hyprpm add https://github.com/Duckonaut/split-monitor-workspaces # Add the plugin repository
hyprpm enable split-monitor-workspaces # Enable the plugin
hyprpm reload # Reload the plugins
```

## Using [hyprload](https://github.com/Duckonaut/hyprload)
Add the line `"Duckonaut/split-monitor-workspaces",` to your `hyprload.toml` config, like this
```toml
plugins = [
    "Duckonaut/split-monitor-workspaces",
]
```
Then update via the `hyprload,update` dispatcher

## Manual installation

1. Export the `HYPRLAND_HEADERS` variable to point to the root directory of the Hyprland repo
    - `export HYPRLAND_HEADERS="$HOME/repos/Hyprland"`
2. Compile
    - `make all`
3. Add this line to the bottom of your hyprland config
    - `exec-once=hyprctl plugin load <ABSOLUTE PATH TO split-monitor-workspaces.so>`

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
      url = "github:Duckonaut/split-monitor-workspaces";
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
                    split-monitor-workspaces.packages.${pkgs.system}.split-monitor-workspaces
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

You will need to have home-manager installed and configured. You use `wayland.windowManager.hyprland.plugins = [];` to add the plugin. The home-manager
module will handle the rest.

# Usage

The plugin provides drop-in replacements for workspace-related commands

| Normal                | Replacement                   |
|-----------------------|-------------------------------|
| workspace             | split-workspace               |
| movetoworkspace       | split-movetoworkspace         |
| movetoworkspacesilent | split-movetoworkspacesilent   |

And two new ones, to move windows between monitors

| Normal                    | Arguments         |
|---------------------------|-------------------|
| split-changemonitor       | next/prev/+1/-1    |
| split-changemonitorsilent | next/prev/+1/-1    |

It also provides the following config values
| Name                                                    | Type      | Default   | Description                                           |
|---------------------------------------------------------|-----------|-----------|-------------------------------------------------------|
| `plugin:split-monitor-workspaces:count`                 | int       | 10        | How many workspaces to bind to the monitor            |
| `plugin:split-monitor-workspaces:keep_focused`          | boolean   | 0         | Keep current workspaces focused on plugin init/reload |
| `plugin:split-monitor-workspaces:enable_notifications`  | boolean   | 0         | Enable notifications                                  |

Keep in mind that if you're using, for example, the `wlr/workspaces` widgets in [waybar](https://github.com/Alexays/Waybar), this will require a change to your config. You should set `all-outputs` to `false`, and adjust the icon mapping.

If your workspace-per-monitor count is 10, the first monitor will have workspaces 1-10, the second 11-20 and so on. They will be accessed via numbers 1-10 while your mouse is on a given monitor.

### Example

```
# in your hyprland config file:

plugin {
    split-monitor-workspaces {
        count = 5
        keep_focused = 0
        enable_notifications = 0
    }
}

$mainMod = SUPER
# Switch workspaces with mainMod + [0-5]
bind = $mainMod, 1, split-workspace, 1
bind = $mainMod, 2, split-workspace, 2
bind = $mainMod, 3, split-workspace, 3
bind = $mainMod, 4, split-workspace, 4
bind = $mainMod, 5, split-workspace, 5

# Move active window to a workspace with mainMod + SHIFT + [0-5]
bind = $mainMod SHIFT, 1, split-movetoworkspacesilent, 1
bind = $mainMod SHIFT, 2, split-movetoworkspacesilent, 2
bind = $mainMod SHIFT, 3, split-movetoworkspacesilent, 3
bind = $mainMod SHIFT, 4, split-movetoworkspacesilent, 4
bind = $mainMod SHIFT, 5, split-movetoworkspacesilent, 5
```

# Special thanks
- [hyprsome](https://github.com/sopa0/hyprsome): An earlier project of similar nature
