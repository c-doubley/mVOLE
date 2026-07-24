cmake_minimum_required(VERSION 3.16)
project(VOLE LANGUAGES CXX) # Specify the languages used in the project

add_executable(main main.cpp)
option(BUILD_SILENT_PROBES "Build silent subfield VOLE API probe executables" OFF)

# Modern way to handle threads and OpenMP
find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)

# Set C++ standard and properties for the executable
set_target_properties(main PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Apply general compile options to the executable
 target_compile_options(main PUBLIC 
        -O3 -march=native 
    )

# Find libOTe and its components.
# Adjust HINTS if libOTe is not in /usr/local or a standard path.
# If libOTe is a submodule, use add_subdirectory instead.
find_package(libOTe REQUIRED 
    COMPONENTS
        sodium
        boost
        openssl
        simplestot
        kos
        silentot
        softspoken_ot
        silent_vole
)

# Link necessary libraries to the executable
target_link_libraries(main 
    PUBLIC
        oc::libOTe
        oc::cryptoTools
        Threads::Threads # Link against the modern Threads target
        OpenMP::OpenMP_CXX   # Link against the modern OpenMP target
)
target_include_directories(main BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libOTe)

if(BUILD_SILENT_PROBES)
    foreach(probe_name
            probe_silent_supported
            probe_silent_module
            probe_silent_toy
            probe_silent_linearity)
        add_executable(${probe_name} probes/${probe_name}.cpp)
        set_target_properties(${probe_name} PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        target_compile_options(${probe_name} PUBLIC -O2 -march=native)
        target_include_directories(${probe_name} BEFORE PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/libOTe
            ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(${probe_name}
            PUBLIC
                oc::libOTe
                oc::cryptoTools
                Threads::Threads
                OpenMP::OpenMP_CXX
        )
    endforeach()
endif()
