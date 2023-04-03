# split-monitor-workspaces
A small plugin to provide `awesome`/`dwm`-like behavior with workspaces: split them between monitors and provide independent numbering

# Installing
Since Hyprland plugins don't have ABI guarantees, you *should* download the Hyprland source and compile it if you plan to use plugins.
This ensures the compiler version is the same between the Hyprland build you're running, and the plugins you are using.

The guide on compiling and installing Hyprland manually is on the [wiki](http://wiki.hyprland.org/Getting-Started/Installation/#manual-manual-build)

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


# Usage
The plugin provides drop-in replacements for workspace-related commands
| Normal                | Replacement                   |
|-----------------------|-------------------------------|
| workspace             | split-workspace               |
| movetoworkspace       | split-movetoworkspace         |
| movetoworkspacesilent | split-movetoworkspacesilent   |

It also provides the following config values
| Name                                      | Type      | Default   | Description                                   |
|-------------------------------------------|-----------|-----------|-----------------------------------------------|
| `plugin:split-monitor-workspaces:count`   | int       | 10        | How many workspaces to bind to the monitor    |

Keep in mind that if you're using, for example, the `wlr/workspaces` widgets in [waybar](https://github.com/Alexays/Waybar), this will require a change to your config. You should set `all-outputs` to `false`, and adjust the icon mapping.

If your workspace-per-monitor count is 10, the first monitor will have workspaces 1-10, the second 11-20 and so on. They will be accessed via numbers 1-10 while your mouse is on a given monitor.

# Special thanks
- [hyprsome](https://github.com/sopa0/hyprsome): An earlier project of similar nature
