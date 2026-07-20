settings_sources = [
  'main.cpp',
  'app.cpp',
  'config-backend.cpp',
  'settings-theme.cpp',
  'pages/display-page.cpp',
  'pages/panel-page.cpp',
  'pages/desktop-page.cpp',
  'pages/dock-page.cpp',
  'pages/sound-page.cpp',
  'pages/network-page.cpp',
  'pages/session-page.cpp',
  'pages/mcp-page.cpp',
  'pages/plugin-page.cpp',
]

libxml2 = dependency('libxml-2.0')

wf_settings = executable(
  'wf-settings',
  settings_sources,
  dependencies: [
    gtkmm,
    libutil,
    json,
    wfconfig,
    libxml2,
  ],
  include_directories: [util_includes],
  cpp_args: platform_cpp_args + [
    '-DMETADATA_DIR="@0@"'.format(metadata_dir),
  ],
  install: true,
)

install_data(
  'wf-settings.desktop',
  install_dir: join_paths(get_option('prefix'), 'share', 'applications'),
)
