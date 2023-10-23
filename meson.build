project('modbusgw', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++14'])

libdeps = [dependency('libmodbus'),
           dependency('jsoncpp'), dependency('libuci')]

executable('mgd',
           'server.cpp',
           dependencies : libdeps)

executable('mgdc',
           'client.cpp',
           dependencies : libdeps)

executable('mgt', 'tool.cpp', dependencies: libdeps)
executable('mgw', 'writer.cpp', dependencies: libdeps)