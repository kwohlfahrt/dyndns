import os

src = ['dyndns.c', 'web_updater.c', 'filter.c', 'ipaddr.c', 'monitor.c']

env = Environment(**os.environ)
env.Append(CCFLAGS='-std=gnu11') #-std=c11 fails on checking for resolv
env.Append(CCFLAGS='-Wall -Wextra -Wpedantic')

opts = Variables()
opts.Add(PathVariable('DESTDIR', 'Directory to install to', '/usr', PathVariable.PathIsDirCreate))
Help(opts.GenerateHelpText(env))
opts.Update(env)

conf = Configure(env)
if not conf.CheckLibWithHeader('curl', 'curl/curl.h', 'c'):
	print('Missing libcurl')
	Exit(1)

if not conf.CheckLibWithHeader('resolv', 'resolv.h', 'c'):
	print('Missing resolver')
	Exit(1)

if not conf.CheckFunc('strlcpy'):
	src.append('strlcpy.c')
else:
	env.Append(CDEFINES='HAVE_strlcpy')
env = conf.Finish()

dyndns = env.Program('dyndns', src)

bindir = os.path.join('$DESTDIR', 'bin')
install_bin = env.Install(bindir, dyndns)
env.Alias('install', [install_bin])
