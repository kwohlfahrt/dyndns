src = ['dyndns.c', 'filter.c', 'ipaddr.c', 'monitor.c', 'strlcpy.c', 'web_updater.c']
curl = dependency('libcurl')
if get_option('with-systemd')
  add_project_arguments('-DWITH_SYSTEMD', language : 'c')
endif
executable('dyndns', sources : src, dependencies : [curl] , install : true)
