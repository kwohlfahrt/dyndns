{ stdenv, curl, meson, ninja, pkgconfig }:

stdenv.mkDerivation {
  name = "dyndns";
  src = ./.;

  nativeBuildInputs = [ meson ninja pkgconfig ];
  buildInputs = [ curl ];

  mesonFlags = [ "-Dwith-systemd=true" ];
}
