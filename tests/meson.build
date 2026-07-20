gtest_dep = dependency('gtest_main')
gtkmm_dep = dependency('gtkmm-4.0')
libxml2_dep = dependency('libxml-2.0')

# Headers live in src/util; add that to the include path for tests.
test_includes = include_directories('../src/util')

test_exec_power = executable(
    'power-controller-test',
    'power-controller-test.cpp',
    dependencies: [gtest_dep, gtkmm_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'power-controller-test',
    test_exec_power,
    suite: 'unit',
)

test_exec_network = executable(
    'network-backend-test',
    'network-backend-test.cpp',
    dependencies: [gtest_dep, gtkmm_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'network-backend-test',
    test_exec_network,
    suite: 'unit',
)

# Full audio backend + pure parse + volume-logic coverage
test_exec_audio = executable(
    'audio-backend-test',
    'audio-backend-test.cpp',
    dependencies: [gtest_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'audio-backend-test',
    test_exec_audio,
    suite: ['unit', 'audio'],
)

test_exec_theme = executable(
    'theme-defaults-test',
    'theme-defaults-test.cpp',
    dependencies: [gtest_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'theme-defaults-test',
    test_exec_theme,
    suite: ['unit', 'theme'],
)

# Never present widgets/themes that are not actually available (integration rules)
test_exec_caps = executable(
    'panel-capabilities-test',
    'panel-capabilities-test.cpp',
    dependencies: [gtest_dep, libutil, gtkmm_dep],
    include_directories: test_includes,
    link_with: util,
    cpp_args: platform_cpp_args,
)

test(
    'panel-capabilities-test',
    test_exec_caps,
    suite: ['unit', 'theme', 'integration', 'present-only-available'],
)

# CSS path preflight + safe load (no dependency on data/themes content)
# Own main() for Gtk::Application — use gtest without gtest_main
gtest_only = dependency('gtest')
test_exec_css = executable(
    'css-load-test',
    'css-load-test.cpp',
    dependencies: [gtest_only, gtkmm_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'css-load-test',
    test_exec_css,
    suite: ['unit', 'theme', 'fault-tolerance'],
)

test_exec_session_env = executable(
    'session-env-test',
    'session-env-test.cpp',
    dependencies: [gtest_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'session-env-test',
    test_exec_session_env,
    suite: ['unit', 'session-env'],
)

test_exec_display = executable(
    'display-config-test',
    'display-config-test.cpp',
    dependencies: [gtest_dep, libutil, json],
    include_directories: test_includes,
    link_with: util,
)

test(
    'display-config-test',
    test_exec_display,
    suite: ['unit', 'display'],
)

test_exec_ini = executable(
    'ini-file-test',
    'ini-file-test.cpp',
    dependencies: [gtest_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'ini-file-test',
    test_exec_ini,
    suite: ['unit', 'config'],
)

# ConfigBackend dirty-save safety (never full-dump wayfire.ini)
test_exec_cfg_backend = executable(
    'config-backend-test',
    'config-backend-test.cpp',
    '../src/settings/config-backend.cpp',
    dependencies: [gtest_dep, libutil, wfconfig, json, libxml2_dep],
    include_directories: [
        test_includes,
        include_directories('../src/settings'),
    ],
    cpp_args: [
        '-DMETADATA_DIR="@0@"'.format(metadata_dir),
    ],
)

test(
    'config-backend-test',
    test_exec_cfg_backend,
    suite: ['unit', 'config', 'fault-tolerance'],
)

# Validate-before-apply gates (must refuse bad theme/session combinations)
test_exec_apply_gate = executable(
    'apply-gate-test',
    'apply-gate-test.cpp',
    dependencies: [gtest_dep, gtkmm_dep, libutil],
    include_directories: test_includes,
    link_with: util,
)

test(
    'apply-gate-test',
    test_exec_apply_gate,
    suite: ['unit', 'gate', 'fault-tolerance'],
)

# Settings startup gate — never invent compositor; fail closed offline
test_exec_startup_gate = executable(
    'startup-gate-test',
    'startup-gate-test.cpp',
    '../src/settings/startup-gate.cpp',
    dependencies: [gtest_dep, libutil],
    include_directories: [
        test_includes,
        include_directories('../src/settings'),
    ],
)

test(
    'startup-gate-test',
    test_exec_startup_gate,
    suite: ['unit', 'gate', 'fault-tolerance'],
)

# Docs / config red-green (python)
py3 = find_program('python3')
audio_defaults_py = join_paths(meson.current_source_dir(),
    '..', 'docs', 'audio-control', 'tests', 'test_defaults.py')
test(
    'audio-defaults-py',
    py3,
    args: [audio_defaults_py],
    suite: ['audio', 'docs'],
    workdir: join_paths(meson.current_source_dir(), '..'),
)
