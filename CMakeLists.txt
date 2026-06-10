cmake_minimum_required(VERSION 3.16)
project(VOLE LANGUAGES CXX) # Specify the languages used in the project

add_executable(main main.cpp)

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
