{
  description = "Minimal dynamic DNS client for Linux";

  inputs.nixpkgs.url = "nixpkgs";

  # adapted from https://hoverbear.org/blog/a-flake-for-your-crate/#flake-nix
  outputs = { self, nixpkgs } : let
    systems = [ "x86_64-linux" "aarch64-linux" ];
    forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
  in {
    defaultPackage = forAllSystems (system: (import nixpkgs { inherit system; overlays = [ self.overlay ]; }).dyndns);
    overlay = self: super: { dyndns = self.callPackage ./dyndns.nix {}; };
    nixosModule = import ./module.nix;
  };
}

