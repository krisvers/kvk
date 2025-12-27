project('kvk', 'cpp')

vk = dependency('vulkan', version: '>=1.2.0')

library_source = ['src/kvk.cpp']
library_include = include_directories('include')
library = shared_library('kvk',
    sources: library_source,
    include_directories: library_include,
    dependencies: [vk],
)

executable('demo',
    sources: 'demo/demo.cpp',
    include_directories: library_include,
    link_with: [library],
)