#
# flake.nix for https://github.com/maxsharabayko/srt-xtransmit
#
# nix build .
# or
# nix build .#srt-xtransmit
#
# nix shell .#srt-xtransmit -c srt-xtransmit --version
#
# ls -la ./result/bin | grep xtransmit
#
# ./result/bin/srt-xtransmit generate \
#  "srt://SERVER_IP:PORT?mode=caller&latency=200&maxbw=120000000" \
#  --bitrate 100M
#
{
  description = "srt-xtransmit (SRT performance / traffic generator)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      lib = nixpkgs.lib;
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: lib.genAttrs systems (system: f system);
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        rec {
          srt-xtransmit = pkgs.stdenv.mkDerivation {
            pname = "srt-xtransmit";
            version = "0.2.0";

            src = pkgs.fetchFromGitHub {
              owner = "maxsharabayko";
              repo = "srt-xtransmit";
              rev = "v0.2.0";
              fetchSubmodules = true;
              hash = "sha256-AEqVJr7TLH+MV4SntZhFFXTttnmcywda/P1EoD2px6E=";
            };

            nativeBuildInputs = [
              pkgs.cmake
              pkgs.pkg-config
            ];

            buildInputs = [
              pkgs.openssl
            ];

            cmakeFlags = [
              "-DENABLE_CXX17=OFF"
              "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
            ];

            # Upstream installs the SRT apps, but not srt-xtransmit.
            # CMake builds it at build/xtransmit/bin/srt-xtransmit (seen in your log).
            postInstall = ''
              candidate=""
              for p in \
                build/xtransmit/bin/srt-xtransmit \
                build/bin/srt-xtransmit \
                build/xtransmit/srt-xtransmit \
                bin/srt-xtransmit \
              ; do
                if [ -x "$p" ]; then
                  candidate="$p"
                  break
                fi
              done

              if [ -z "$candidate" ]; then
                # Fallback: locate it (should find something like ./build/xtransmit/bin/srt-xtransmit)
                candidate="$(find . -type f -name srt-xtransmit -perm -0100 | head -n1 || true)"
              fi

              if [ -z "$candidate" ] || [ ! -x "$candidate" ]; then
                echo "ERROR: srt-xtransmit binary not found in build tree" >&2
                find . -type f -name srt-xtransmit -print >&2 || true
                exit 1
              fi

              install -Dm755 "$candidate" "$out/bin/srt-xtransmit"
            '';

            # Nix fixup fails if .pc files contain ${prefix}//nix/store/...
            postFixup = ''
              for pc in "$out"/lib/pkgconfig/*.pc; do
                [ -f "$pc" ] || continue
                sed -i 's#//#/#g' "$pc"
              done
            '';

            meta = with lib; {
              description = "SRT xtransmit performance / traffic generator";
              homepage = "https://github.com/maxsharabayko/srt-xtransmit";
              license = licenses.mit;
              platforms = platforms.linux;
            };
          };

          default = srt-xtransmit;
        }
      );

      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.pkg-config
              pkgs.openssl
            ];
          };
        });
    };
}

