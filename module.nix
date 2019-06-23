{ config, pkgs, lib, ... }:

let
  cfg = config.services.dyndns;
  inherit (lib) types mkOption mkIf;
in {
  options.services.dyndns = {
    enable = mkOption {
      type = types.bool;
      default = false;
      description = ''
        Whether to enable the Dyndns dynamic DNS service.
      '';
    };
    interface = mkOption {
      type = types.string;
      description = ''
        Which interface to monitor for IP address changes.
      '';
    };
    url = mkOption {
      type = types.string;
      description = ''
        Which URL to GET in response to changes.
      '';
    };
    class = mkOption {
      type = types.nullOr (types.enum ["4" "6"]);
      description = ''
        Whether to listen only for IPv4 or IPv6 address chantges (default both).
      '';
    };
    allow_private = mkOption {
      type = types.bool;
      default = false;
      description = ''
        Whether to update when a private-range IP address is detected.
      '';
    };
  };

  config = mkIf cfg.enable {
    environment.systemPackages = with pkgs; [ dyndns ];

    systemd.services.dyndns = {
      description = "Small Dynamic DNS Client";
      after = [ "network.target" ];
      wantedBy = [ "multi-user.target" ];
      serviceConfig = {
        Type = "notify";
        ExecStart = let
          flags = lib.concatStringsSep " "
            (["-${cfg.class or "46"}"] ++ lib.optional cfg.allow_private "--allow-private");
          in "${pkgs.dyndns}/bin/dyndns ${flags} ${cfg.interface} ${cfg.url}";
      };
    };
  };
}
