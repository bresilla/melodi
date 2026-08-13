{
  description = "Melodi radio firmware development shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs?rev=4c1018dae018162ec878d42fec712642d214fdfa";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            platformio-core
            picotool
            python3
            gcc
            gnumake
            git
            jq
            gum
            minicom
            usbutils
            udev
          ];

          shellHook = ''
            echo "melodi firmware: pio $(pio --version 2>/dev/null || echo unavailable)"
            echo "melodi firmware: host test with sh test/run.sh"
          '';
        };
      }
    );
}
