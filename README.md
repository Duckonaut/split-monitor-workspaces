# split-monitor-workspaces

[![Luacheck](https://github.com/zjeffer/split-monitor-workspaces/actions/workflows/luacheck.yml/badge.svg?branch=main)](https://github.com/zjeffer/split-monitor-workspaces/actions/workflows/luacheck.yml)

A lua package for Hyprland to provide `awesome`/`dwm`-like behavior with workspaces: split them between monitors and provide independent numbering.

The package features an easy-to-use API to focus or move windows to the `n`th workspace on the active monitor (with for example `SUPER + <N>`, `N` being a number on your keyboard) without having to set up complicated rules yourself. It also provides functionality to create persistent workspaces on each monitor at Hyprland startup, as well as dynamically when new monitors are connected.

It's explicitly compatible with [the hy3 plugin](https://github.com/outfoxxed/hy3): we automatically detect if hy3 is installed, and if it is we'll use hy3's dispatchers instead of Hyprland's.

## Requirements

- Hyprland >= 0.55.0

If you're using an older version of Hyprland, or have not yet switched to the new Lua config, you can use the [cpp plugin](./docs/cpp-plugin.md) instead, which provides the same functionality.
This method will be deprecated when Hyprland 0.57.0 releases.

## Installation

Simply clone the repository into your `~/.config/hypr/plugins` directory, and require the package in your Hyprland config.

```sh
mkdir -p ~/.config/hypr/plugins
cd ~/.config/hypr/plugins
git clone https://github.com/zjeffer/split-monitor-workspaces
```

```lua
-- somewhere in your Hyprland config
package.path = package.path .. ";./?.lua;./?/init.lua"
local smw = require("plugins.split-monitor-workspaces")
```

If you're on the latest Hyprland git commit, simply use the `main` branch. Run `git pull` to get the latest changes every time there is an update.
If you're on a Hyprland release branch, make sure to checkout the corresponding release branch of this plugin, like so:

```sh
# if you're on a 0.55 release, simply run this inside the split-monitor-workspaces repo:
git fetch -Ppft && git checkout release/0.55.x
# if you want to use a specific release instead of a branch, see the https://github.com/zjeffer/split-monitor-workspaces/tags page for a list of all possible tags. For example:
git checkout v0.55.4
```

Make sure you always pull the latest changes with `git pull` when Hyprland has a new minor release, and checkout the new `release/0.XX.x` branch whenever a new major release is created. If there is no release branch yet, create a GitHub issue.

## Usage

Now that the `smw` package is imported, you can call the `setup()` function with your desired configuration.

Minimal example:

```lua
smw.setup({
    workspace_count = 5, -- This will create 5 persistent workspaces on each monitor at startup
})
```

You can set up keybinds like so:

```lua
local mainMod = "SUPER"
for i = 1, smw.get_amount_of_workspaces() do
    local n = tostring(i)
    if n == "10" then n = "0" end -- Optional if you configured 10 workspaces: bind workspace 10 to SUPER + 0
    -- Switch to the Nth workspace on the currently focused monitor.
    hl.bind(mainMod .. " +" .. n, smw.workspace(n))
    -- Move the active window to the Nth workspace on the currently focused monitor silently (no focus change).
    hl.bind(mainMod .. " + SHIFT +" .. n, smw.move_to_workspace_silent(n))
end
```

An example showing the full API can be found in [docs/example.lua](./docs/example.lua).

### Waybar integration

This plugin supports [waybar's](https://github.com/Alexays/Waybar) `hyprland/workspaces` module:

```json
"hyprland/workspaces": {
    "format": "{icon}",
    "format-icons": {
      "urgent": "",
      "active": "", // focused workspace on current monitor
      "visible": "", // focused workspace on other monitors
      "default": "",
      "empty": "" // persistent (created by this plugin)
    },
    "on-scroll-up": "hyprctl dispatch split-cycleworkspaces -1",
    "on-scroll-down": "hyprctl dispatch split-cycleworkspaces +1",
    "all-outputs": false // recommended
  },
```


It is recommended to do this every time you update Hyprland, to ensure compatibility.

# Special thanks

- [@Duckonaut](https://github.com/Duckonaut/): The original creator of this plugin
- [hyprsome](https://github.com/sopa0/hyprsome): An earlier project of similar nature
