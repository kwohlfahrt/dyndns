{ lib, stdenv, curl, meson, ninja, pkgconfig, systemd ? null }:

stdenv.mkDerivation {
  name = "dyndns";
  src = ./.;

  nativeBuildInputs = [ meson ninja pkgconfig ] ++
    (lib.optional (systemd != null) systemd);
  buildInputs = [ curl ];

  mesonFlags =
    (lib.optional (systemd != null) "-Dwith-systemd=true");
}
