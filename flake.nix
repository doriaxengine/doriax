{
  description = "Game engine for 2D and 3D projects with entity component system (ECS) and data-oriented design.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = (import nixpkgs { inherit system; });
          doriax = pkgs.callPackage misc/nix/package.nix { };
        in
        {
          inherit doriax;
          default = doriax;
          devShell = pkgs.mkShell { inputsFrom = [ doriax ]; };
        }
      );
    };
}
