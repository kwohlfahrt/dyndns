src = ['dyndns.c', 'filter.c', 'ipaddr.c', 'monitor.c', 'strlcpy.c', 'web_updater.c']
curl = dependency('libcurl')
dependencies = [curl]
if get_option('with-systemd')
  add_project_arguments('-DWITH_SYSTEMD', language : 'c')
  dependencies += dependency('libsystemd', required : true)
endif
executable('dyndns', sources : src, dependencies : dependencies , install : true)
