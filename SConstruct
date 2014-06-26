import os

src = ['dyndns.c', 'web_updater.c', 'filter.c', 'ipaddr.c', 'monitor.c']

env = Environment(CC='clang')
env['ENV']['TERM'] = os.environ['TERM']
env.Append(CCFLAGS='-Wall -Wextra')

conf = Configure(env)

if not conf.CheckLibWithHeader('curl', 'curl/curl.h', 'c'):
	print('Missing libcurl')
	Exit(1)

if not conf.CheckLibWithHeader('pthread', 'pthread.h', 'c'):
	print('Missing pthread')
	Exit(1)

if not conf.CheckLibWithHeader('resolv', 'resolv.h', 'c'):
	print('Missing resolver')
	Exit(1)

if not conf.CheckFunc('strlcpy'):
	src.append('strlcpy.c')
else:
	env.Append(CDEFINES='HAVE_strlcpy')

env = conf.Finish()
env.Program('dyndns', src)
