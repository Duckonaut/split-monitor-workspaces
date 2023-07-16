{
  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
  };

  outputs = { self, hyprland, ... }: let
    inherit (hyprland.inputs) nixpkgs;
    hyprlandSystems = fn: nixpkgs.lib.genAttrs (builtins.attrNames hyprland.packages) (system: fn system nixpkgs.legacyPackages.${system});
  in {
    packages = hyprlandSystems (system: pkgs: rec {
      split-monitor-workspaces = pkgs.stdenv.mkDerivation {
        pname = "split-monitor-workspaces";
        version = "0.1";
        src = ./src;

        nativeBuildInputs = with pkgs; [ meson pkg-config ];

        buildInputs = with pkgs; [
          hyprland.packages.${system}.hyprland.dev
          pango
          cairo
        ] ++ hyprland.packages.${system}.hyprland.buildInputs;

        meta = with pkgs.lib; {
          homepage = "https://github.com/Duckonaut/split-monitor-workspaces";
          description = "A small Hyprland plugin to provide awesome-like workspace behavior";
          license = licenses.bsd3;
          platforms = platforms.linux;
        };
      };

      default = split-monitor-workspaces;
    });

    devShells = hyprlandSystems (system: pkgs: {
      default = pkgs.mkShell {
        name = "split-monitor-workspaces";

        nativeBuildInputs = with pkgs; [
          clang-tools_16
          bear
        ];

        inputsFrom = [ self.packages.${system}.split-monitor-workspaces ];
      };
    });
  };
}
