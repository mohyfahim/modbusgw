project('modbusgw', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++14'])

libdeps = [dependency('libmodbus'), dependency('jsoncpp')]

executable('modbusgw',
           'main.cpp',
           dependencies : libdeps)
