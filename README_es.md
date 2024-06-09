# split-monitor-workspaces
[![Build](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/main.yml/badge.svg?branch=main)](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/main.yml)
[![Build on latest Hyprland release](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/release.yml/badge.svg)](https://github.com/Duckonaut/split-monitor-workspaces/actions/workflows/release.yml)


Un pequeño plugin para tener un comportamiento similar a `awesome`/`dwm` en los espacios de trabajo: dividirlos entre los monitores y tener numeración independiente.

# Requisitos
- Hyprland >= v0.38.1

# Instalación
Como los plugins de Hyprland no tienen garantías de ABI, *deberías* descargar el código fuente de Hyprland y compilarlo si piensas estar utilizando plugins.

Esto asegura que la versión del compilador es la misma entre la build de Hyprland que estás usando y la de los plugins que estás usando.

La guía de cómo compilar e instalar Hyprland manualmente está en la [wiki](http://wiki.hyprland.org/Getting-Started/Installation/#manual-manual-build)

## Usando [hyprpm](https://wiki.hyprland.org/Plugins/Using-Plugins/#hyprpm)
Hyprpm es una herramienta que está integrada en la versión más reciente de Hyprland, para utilizarla primero tendrás que agregar el repositorio y después habilitar el plugin. 
```BASH
hyprpm add https://github.com/Duckonaut/split-monitor-workspaces # Agrega el repositorio del plugin
hyprpm enable split-monitor-workspaces # Habilita el plugin
hyprpm reload # Recarga los plugins
```

## Usando [hyprload](https://github.com/Duckonaut/hyprload)
Agrega la línea `"Duckonaut/split-monitor-workspaces",` a tu `hyprload.toml` de esta forma
```toml
plugins = [
    "Duckonaut/split-monitor-workspaces",
]
```
Después actualiza a través del dispatcher `hyprload,update`

## Instalación manual

1. Exporta la variable `HYPRLAND_HEADERS` para que apunte al directorio raíz del repositorio de Hyprland
    - `export HYPRLAND_HEADERS="$HOME/repos/Hyprland"`
2. Compila
    - `make all`
3. Agrega esta línea al final de tu configuración de Hyprland
    - `exec-once=hyprctl plugin load <RUTA ABSOLUTA A split-monitor-workspaces.so>`

## Instalación en NixOS

Ejemplo de una instalación con los flakes habilitados:

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

Deberás tener home-manager instalado y configurado. Usa `wayland.windowManager.hyprland.plugins = [];` para agregar el plugin, el módulo de home-manager se encargará del resto.

# Uso

El complemento tiene reemplazos directos para los comandos relacionados con los espacios de trabajo.

| Normal                | Reemplazo                   |
|-----------------------|-----------------------------|
| workspace             | split-workspace             |
| movetoworkspace       | split-movetoworkspace       |
| movetoworkspacesilent | split-movetoworkspacesilent |

Y dos nuevos, para mover ventanas entre monitores:

| Normal                    | Argumentos         |
|---------------------------|--------------------|
| split-changemonitor       | next/prev/+1/-1    |
| split-changemonitorsilent | next/prev/+1/-1    |

También proporciona los siguientes valores de configuración:

| Nombre                                                  | Tipo      | Predeterminado   | Descripción                                                                                      |
|---------------------------------------------------------|-----------|------------------|--------------------------------------------------------------------------------------------------|
| `plugin:split-monitor-workspaces:count`                 | int       | 10               | Cuántos espacios de trabajo asignar al monitor                                                   |
| `plugin:split-monitor-workspaces:keep_focused`          | boolean   | 0                | Mantener los espacios de trabajo actuales enfocados en el momento de iniciar/reiniciar el plugin |
| `plugin:split-monitor-workspaces:enable_notifications`  | boolean   | 0                | Habilitar notificaciones                                                                         |

Recuerda que si estás usando, por ejemplo, el widget `wlr/workspaces` en [waybar](https://github.com/Alexays/Waybar), esto hará que necesites cambiar tu configuración. Debes configurar `all-outputs` a `false`, y ajustar el mapeo de íconos..

Si tu cuenta de espacios-de-trabajo-por-monitor es 10, el primer monitor tendrá los espacios de trabajo del 1 al 10, el segundo del 11 al 20 y así sucesivamente. Se accederá a ellos mediante los números del 1 al 10 mientras tu ratón esté en un monitor dado.

### Ejemplo

```
# en tu archivo de configuración de hyprland:

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

# Agradecimiento especial
- [hyprsome](https://github.com/sopa0/hyprsome): Un proyecto anterior de naturaleza similar.
