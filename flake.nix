{
  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
    nix-filter.url = "github:numtide/nix-filter";
  };

  outputs = {
    self,
    hyprland,
    nix-filter,
    ...
  }: let
    inherit (hyprland.inputs) nixpkgs;
    forHyprlandSystems = fn: nixpkgs.lib.genAttrs (builtins.attrNames hyprland.packages) (system: fn system nixpkgs.legacyPackages.${system});
  in {
    packages = forHyprlandSystems (system: pkgs: rec {
      split-monitor-workspaces = pkgs.gcc13Stdenv.mkDerivation {
        pname = "split-monitor-workspaces";
        version = "0.1";
        src = nix-filter.lib {
          root = ./.;
          include = [
            "src"
            "include"
            ./Makefile
            ./meson.build
          ];
        };

        # allow overriding xwayland support
        BUILT_WITH_NOXWAYLAND = false;

        nativeBuildInputs = with pkgs; [meson ninja pkg-config];

        buildInputs = with pkgs;
          [
            hyprland.packages.${system}.hyprland.dev
            pango
            cairo
          ]
          ++ hyprland.packages.${system}.hyprland.buildInputs;

        meta = with pkgs.lib; {
          homepage = "https://github.com/Duckonaut/split-monitor-workspaces";
          description = "A small Hyprland plugin to provide awesome-like workspace behavior";
          license = licenses.bsd3;
          platforms = platforms.linux;
        };
      };

      default = split-monitor-workspaces;
    });

    devShells = forHyprlandSystems (system: pkgs: {
      default = pkgs.mkShell {
        name = "split-monitor-workspaces";

        nativeBuildInputs = with pkgs; [
          clang-tools_16
          bear
        ];

        inputsFrom = [self.packages.${system}.split-monitor-workspaces];
      };
    });
  };
}
