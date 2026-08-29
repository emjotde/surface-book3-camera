{
  description = "Linux webcam driver for the Dell XPS 13 7390 2-in-1";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      nixosModules.default = import ./nix/module.nix { inherit self; };

      packages.${system} = {
        driver = pkgs.linuxPackages.callPackage ./nix/driver.nix { src = "${self}/driver"; };
        softisp = pkgs.callPackage ./nix/softisp.nix { src = "${self}/softisp"; };
        default = self.packages.${system}.driver;
      };
    };
}
