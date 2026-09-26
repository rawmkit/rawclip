project(
  'rawclip',
  'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=1.6.0',
  default_options: [
    'c_std=c99',
    'warning_level=3',
    'werror=true',
  ],
)

cc = meson.get_compiler('c')
x11_dep = dependency('x11')
xfixes_dep = dependency('xfixes')
libconfig_dep = dependency('libconfig')

inc = include_directories('include')

core_sources = files(
  'src/history.c',
  'src/ipc.c',
  'src/util.c',
)

rawclipd_sources = core_sources + files(
  'src/config.c',
  'src/x11.c',
  'src/rawclipd.c',
)

rawclip_sources = core_sources + files(
  'src/config.c',
  'src/rawclip.c',
)

rawclipd = executable(
  'rawclipd',
  rawclipd_sources,
  include_directories: inc,
  dependencies: [x11_dep, xfixes_dep, libconfig_dep],
  install: true,
)

rawclip = executable(
  'rawclip',
  rawclip_sources,
  include_directories: inc,
  dependencies: [libconfig_dep],
  install: true,
)

conf = configuration_data()
conf.set_quoted('RAWCLIP_VERSION', meson.project_version())
configure_file(
  input: 'include/version.h.in',
  output: 'version.h',
  configuration: conf,
)

install_data('config/rawclip.conf', install_dir: get_option('sysconfdir') / 'rawclip')
install_data('LICENSE', 'README.md', 'SECURITY.md', 'NEWS.md', install_dir: get_option('datadir') / 'doc' / 'rawclip')

scdoc = find_program('scdoc', required: true)
foreach man : ['rawclip.1', 'rawclipd.1', 'rawclip.conf.5']
  src = 'docs/' + man + '.scd'
  out = custom_target(
    man,
    input: src,
    output: man,
    command: [scdoc],
    feed: true,
    capture: true,
    install: true,
    install_dir: get_option('mandir') / man.split('.')[1],
  )
endforeach

history_test = executable(
  'history-test',
  ['tests/history-test.c', 'src/history.c', 'src/util.c'],
  include_directories: inc,
)
test('history', history_test)

ipc_test = executable(
  'ipc-test',
  ['tests/ipc-test.c', 'src/ipc.c', 'src/util.c'],
  include_directories: inc,
)
test('ipc', ipc_test)

util_test = executable(
  'util-test',
  ['tests/util-test.c', 'src/util.c'],
  include_directories: inc,
)
test('util', util_test)

test(
  'config',
  executable(
    'config-test',
    ['tests/config-test.c', 'src/config.c', 'src/util.c'],
    include_directories: inc,
    dependencies: [libconfig_dep],
  ),
  args: [meson.current_source_dir() / 'config/rawclip.conf'],
)

test(
  'config-invalid',
  find_program('tests/config-invalid.sh'),
  args: [rawclipd],
)

test(
  'cli',
  find_program('tests/cli.sh'),
  args: [rawclip, rawclipd, meson.current_source_dir() / 'config/rawclip.conf', meson.project_version()],
)

x11_helper = executable(
  'x11-helper',
  'tests/x11-helper.c',
  dependencies: [x11_dep],
)

xvfb = find_program('Xvfb', required: false)
if xvfb.found()
  test(
    'integration',
    find_program('tests/integration.sh'),
    args: [rawclipd, rawclip, x11_helper],
    timeout: 30,
  )
endif
