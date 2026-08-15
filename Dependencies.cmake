include(FetchContent)

# SHA-2 Library - Modern FetchContent approach
FetchContent_Declare(
    sha2
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    GIT_REPOSITORY https://github.com/amosnier/sha-2.git
    GIT_TAG master
)

FetchContent_MakeAvailable(sha2)

# Create a proper CMake target for sha2 (since the original doesn't have CMakeLists.txt)
if(NOT TARGET sha2::sha2)
    add_library(sha2 STATIC ${sha2_SOURCE_DIR}/sha-256.c)
    target_include_directories(sha2 PUBLIC ${sha2_SOURCE_DIR})

    # Set C99 standard for this library
    target_compile_features(sha2 PUBLIC c_std_99)

    # Create an alias for clean linking
    add_library(sha2::sha2 ALIAS sha2)

    # Organize in IDE
    set_target_properties(sha2 PROPERTIES FOLDER "Dependencies/SHA2")
endif()

# cxxopts - header-only CLI option parser, provides its own CMake target
FetchContent_Declare(
    cxxopts
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.3.1
)

FetchContent_MakeAvailable(cxxopts)