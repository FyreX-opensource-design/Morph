project(
  'morph',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
    'prefix=/usr',
    'sysconfdir=/etc',
  ],
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')
add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

wlroots = dependency(
  ['wlroots-0.20', 'wlroots-0.22', 'wlroots-git', 'wlroots'],
  version: '>=0.20.2'
)
# Needed for compositor-side touchpad defaults such as tap-to-click.
libinput = dependency('libinput')
pixman = dependency('pixman-1')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
libdrm = dependency('libdrm')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
wayland_protocols = dependency('wayland-protocols', version: '>=1.32')
wayland_scanner = find_program('wayland-scanner')
sh_prog = find_program('sh')

morph_runtime_dir = get_option('sysconfdir') / 'morph'
morph_doc_dir = get_option('datadir') / 'doc' / 'morph'
morph_bindir = '/usr/bin'
morph_runtime_dir = '/etc/morph'
morph_sessions_dir = '/usr/share/wayland-sessions'
morph_doc_dir = '/usr/share/doc/morph'
morph_icons_dir = '/usr/share/icons/hicolor/scalable/apps'

xdg_shell_xml = wayland_protocols.get_variable('pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
xdg_shell_server_h = custom_target(
  'xdg-shell-protocol',
  output: 'xdg-shell-protocol.h',
  input: xdg_shell_xml,
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

layer_shell_xml = files('protocols/wlr-layer-shell-unstable-v1.xml')
layer_shell_server_h = custom_target(
  'wlr-layer-shell-protocol',
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  input: layer_shell_xml,
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

output_power_xml = files('protocols/wlr-output-power-management-unstable-v1.xml')
output_power_server_h = custom_target(
  'wlr-output-power-protocol',
  output: 'wlr-output-power-management-unstable-v1-protocol.h',
  input: output_power_xml,
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

ext_workspace_xml = wayland_protocols.get_variable('pkgdatadir') / 'staging/ext-workspace/ext-workspace-v1.xml'
ext_workspace_server_h = custom_target(
  'ext-workspace-v1-protocol-h',
  output: 'ext-workspace-v1-protocol.h',
  input: ext_workspace_xml,
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
ext_workspace_private_c = custom_target(
  'ext-workspace-v1-protocol-c',
  output: 'ext-workspace-v1-protocol.c',
  input: ext_workspace_xml,
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

tablet_v2_xml = wayland_protocols.get_variable('pkgdatadir') / 'stable/tablet/tablet-v2.xml'
tablet_v2_server_h = custom_target(
  'tablet-v2-protocol-h',
  output: 'tablet-v2-protocol.h',
  input: tablet_v2_xml,
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
tablet_v2_private_c = custom_target(
  'tablet-v2-protocol-c',
  output: 'tablet-v2-protocol.c',
  input: tablet_v2_xml,
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

pointer_constraints_xml = wayland_protocols.get_variable('pkgdatadir') / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'
pointer_constraints_server_h = custom_target(
  'pointer-constraints-protocol',
  output: 'pointer-constraints-unstable-v1-protocol.h',
  input: pointer_constraints_xml,
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

executable(
  'morph',
  'src/crash_handler.c',
  'src/main.c',
  'src/config.c',
  'src/ext_workspace.c',
  'src/gamma_control.c',
  'src/image_capture.c',
  'src/output_mgmt.c',
  'src/output_power.c',
  'src/virtual_input.c',
  'src/ui.c',
  xdg_shell_server_h,
  layer_shell_server_h,
  output_power_server_h,
  ext_workspace_server_h,
  ext_workspace_private_c,
  tablet_v2_server_h,
  tablet_v2_private_c,
  pointer_constraints_server_h,
  dependencies: [wlroots, libinput, pixman, cairo, pangocairo, libdrm, wayland_server, xkbcommon, m_dep],
  install: true,
  install_dir: morph_bindir,
)

config_test = executable(
  'morph-test-config',
  'tests/test_config.c',
  'src/config.c',
  xdg_shell_server_h,
  dependencies: [wlroots, pixman, wayland_server, xkbcommon, m_dep],
  include_directories: include_directories('src'),
)

test('config', config_test)
test(
  'shell-runtime',
  sh_prog,
  args: [files('tests/test_shell_runtime.sh'), meson.project_source_root()],
)

install_data(
  files(
    'config/morph.conf',
    'config/environment',
    'config/portals',
  ),
  install_dir: morph_runtime_dir,
)

install_data(
  files(
    'config/startup.sh',
    'config/reload.sh',
    'config/shutdown.sh',
    'scripts/shell-helpers.sh',
    'scripts/system_startup.sh',
    'scripts/system_shutdown.sh',
    'scripts/system_reload.sh',
  ),
  install_dir: morph_runtime_dir,
  install_mode: 'rwxr-xr-x',
)

install_data(
  files('scripts/morph-session'),
  install_dir: morph_bindir,
  rename: 'morph-session',
  install_mode: 'rwxr-xr-x',
)

install_data(
  files('sessions/morph.desktop'),
  install_dir: morph_sessions_dir,
)

install_data(
  files(
    'assets/icons/morph.svg',
    'assets/icons/morph_dbg.svg',
  ),
  install_dir: morph_icons_dir,
)

install_data(
  files('README.md'),
  install_dir: morph_doc_dir,
)

install_data(
  files(
    'docs/OVERVIEW.md',
    'docs/CLI.md',
    'docs/CONFIG.md',
    'docs/COMPOSITOR.md',
    'docs/CRASHING.md',
    'docs/DEVFAQ.md',
    'docs/PROTOCOLS.md',
    'docs/TESTING.md',
    'docs/ENVIRONMENT.md',
    'docs/LAUNCHER.md',
    'docs/morph.conf.example',
    'testing/test-howto_start-variants.nfo',
  ),
  install_dir: morph_doc_dir / 'docs',
)
