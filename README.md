dyndns
======

Non-polling dynamic DNS utility for linux.

`dyndns` is a utility for linux that will listen for IP address changes on an interface, and display them
or use them to fetch a website.

Requirements
------------

The only requirement is [`libcurl`](http://curl.haxx.se/libcurl/).

Usage
-----

    dyndns [-v] [-46] [--allow-private] <interface> [URL]
    
If called with one option, `dyndns` will print new IP addresses to `stdout`.

If called with two it will `GET` the address specified by `URL`, substituting the pattern `<ipaddr>`
for the new IP address. The pattern may occur zero or more times.

If neither `-4` nor `-6` is specified, it will listen for both IPv4 and IPv6 addresses, otherwise it will
react only to the specified one. Both may be explicitly specified.

If `--allow-private` is specified, it will also react to private IPv4 addresses (10.0.0.0/8, 172.16.0.0/12,
192.168.0.0/16) and IPv6 Unique Local Addresses (fc00::/7), otherwise these will be ignored.

`-v` adds some additional verbosity, but doesn't really do much.
