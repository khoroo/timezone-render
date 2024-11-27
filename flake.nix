{
  description = "GeoJSON parser and visualizer using json-c and raylib with timezone data processing";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        
        timezonesUrl = "https://github.com/evansiroky/timezone-boundary-builder/releases/download/2024b/timezones-now.geojson.zip";
        
        processingScript = pkgs.writeScriptBin "process-timezones" ''
          #!${pkgs.bash}/bin/bash
          set -euo pipefail
          
          mkdir -p output
          cd output
          
          if [ ! -f timezones-now.geojson.zip ]; then
            ${pkgs.curl}/bin/curl -L "${timezonesUrl}" -o timezones-now.geojson.zip
          fi
          
          if [ ! -f combined-now.json ]; then
            ${pkgs.unzip}/bin/unzip -o timezones-now.geojson.zip combined-now.json
          fi
          
          ${self.packages.${system}.default}/bin/geojson-visualizer combined-now.json
        '';
      in
      {
        packages = {
          default = pkgs.stdenv.mkDerivation {
            name = "geojson-visualizer";
            src = ./.;

            nativeBuildInputs = with pkgs; [
              pkg-config
            ];

            buildInputs = with pkgs; [
              json_c
              raylib
            ];

            buildPhase = ''
              $CC -std=c17 -Wall -Wextra -o geojson-visualizer src/main.c $(pkg-config --cflags --libs json-c raylib) -lm
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp geojson-visualizer $out/bin/
            '';
          };
          
          process-timezones = processingScript;
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            json_c
            raylib
            pkg-config
            gcc
            jq
          ];
        };
      });
}