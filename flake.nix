{
  description = "Neversnooze loader - Qt6 GUI injector with embedded encrypted payloads";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    # payload sources: Nix-built CS2 / TF2 / Steam modules, inject_memfd, and the
    # shared keydir.
    #
    # NOTE: this must be an absolute path. Relative path inputs ("path:../gamesense")
    # cannot escape the flake's own source snapshot: for a git-backed flake Nix
    # evaluates from a store copy, and "../gamesense" then points outside the store
    # (forbidden in pure eval). The workspace layout is already machine-specific
    # anyway (the CMake tree expects ../cs2 and ../tf2 siblings), so this matches
    # how the project is used.
    gamesense.url = "path:/home/d/dev/neversnooze/gamesense";
  };

  outputs = {
    self,
    nixpkgs,
    gamesense,
  }: let
    system = "x86_64-linux";
    # the loader tree carries no LICENSE file, so its derivation is marked unfree;
    # opt in here so `nix build` just works on the project's own code
    pkgs = import nixpkgs {
      inherit system;
      config.allowUnfree = true;
    };
    gs = gamesense.packages.${system};
  in {
    packages.${system}.default = pkgs.callPackage ./nix/package.nix {
      cs2Module = gs.cs2-module;
      steamModule = gs.steam-module;
      tf2Module = gs.tf2-module;
      injector = gs.injector;
      keyMaterial = gs.key-material;
      mingwGcc = pkgs.pkgsCross.mingwW64.buildPackages.gcc;
      mcfgthreads = pkgs.pkgsCross.mingwW64.windows.mcfgthreads;
    };

    apps.${system}.default = {
      type = "app";
      program = "${self.packages.${system}.default}/bin/Loader";
    };

    devShells.${system}.default = pkgs.mkShell {
      packages = with pkgs; [
        cmake
        ninja
        qt6.qtbase
        openssl
        binutils # ld.bfd for the payload blobs
        gdb # Steam-module injection round
        pkgs.pkgsCross.mingwW64.buildPackages.gcc # ns_inject.exe
      ];
    };
  };
}
