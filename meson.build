project('kvk', 'cpp', default_options: ['cpp_std=c++20'])

sdl3_subproject = subproject('sdl3')

vk = dependency('vulkan', version: '>=1.2.0', required: true)
sdl3 = dependency('sdl3', version: '>=3.0.0', required: true)
# TODO: figure out directx shader compiler library support

library_source = ['src/kvk.cpp']
library_include = include_directories('include')
library = static_library('kvk',
    sources: library_source,
    include_directories: library_include,
    dependencies: [vk],
    install: true,
    override_options: ['cpp_std=c++20'],
)

dxc_executable = find_program('dxc', required: true)

demo_shader_bin = custom_target('compile demo shader',
    output: 'cellular_automata.hlsl.bin',
    input: 'demo/cellular_automata.hlsl',
    command: [
        dxc_executable,
        '-E', 'cellular_automata',
        '-T', 'cs_6_0',
        '@INPUT@',
        '-Fo', '@OUTPUT@',
        '-spirv', '-Zi',
    ]
)

executable('demo',
    sources: ['demo/demo.cpp', demo_shader_bin],
    include_directories: [library_include],
    link_with: [library],
    dependencies: [vk, sdl3],
    override_options: ['cpp_std=c++20'],
)
