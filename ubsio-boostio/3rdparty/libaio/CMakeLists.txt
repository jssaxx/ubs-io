# --- Path setup ---
set(LIBAIO_INSTALL_DIR "${PROJECT_3RDPARTY_BIN_DIR}/libaio")
message(STATUS "LIBAIO_INSTALL_DIR: ${LIBAIO_INSTALL_DIR}")

# --- Try system-installed libaio first ---
find_path(LIBAIO_INCLUDE_DIR
    NAMES libaio.h
    PATHS /usr/include
    NO_DEFAULT_PATH
)

find_library(LIBAIO_LIBRARY
    NAMES libaio.so aio
    PATHS /usr/lib64 /usr/lib /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
    NO_DEFAULT_PATH
)

if(LIBAIO_INCLUDE_DIR AND LIBAIO_LIBRARY)
    message(STATUS "Found system libaio at ${LIBAIO_LIBRARY} (include: ${LIBAIO_INCLUDE_DIR})")

    if(NOT TARGET aio)
        get_filename_component(AIO_LIB_DIR "${LIBAIO_LIBRARY}" DIRECTORY)
        add_library(aio INTERFACE IMPORTED GLOBAL)
        set_target_properties(aio PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBAIO_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "-L${AIO_LIB_DIR} -laio"
        )
    endif()
else()
    if(LIBAIO_INCLUDE_DIR)
        message(STATUS "libaio header found but library missing. Building from source...")
    elseif(LIBAIO_LIBRARY)
        message(STATUS "libaio library found but header missing. Building from source...")
    else()
        message(STATUS "System libaio not found. Building from source via FetchContent...")
    endif()

    include(FetchContent)

    # Fetch libaio 0.3.113 source from deepin-community GitHub mirror
    set(LIBAIO_SRC_DIR "${PROJECT_3RDPARTY_SRC_DIR}/libaio/libaio")
    FetchContent_Declare(
        libaio_src
        GIT_REPOSITORY https://github.com/deepin-community/libaio.git
        GIT_TAG        0.3.113-8deepin2
        SOURCE_DIR     ${LIBAIO_SRC_DIR}
    )
    FetchContent_MakeAvailable(libaio_src)

    # Build libaio using its native Makefile (handles arch-specific syscall headers
    # and links with --version-script=libaio.map for proper symbol versioning)
    execute_process(
        COMMAND make CC=gcc -j8
        WORKING_DIRECTORY ${LIBAIO_SRC_DIR}
        RESULT_VARIABLE MAKE_RESULT
    )

    if(NOT MAKE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build libaio from source!")
    endif()

    # Install to 3rdparty directory
    file(MAKE_DIRECTORY ${LIBAIO_INSTALL_DIR}/lib)
    file(MAKE_DIRECTORY ${LIBAIO_INSTALL_DIR}/include)

    file(COPY ${LIBAIO_SRC_DIR}/src/libaio.h DESTINATION ${LIBAIO_INSTALL_DIR}/include)
    file(COPY ${LIBAIO_SRC_DIR}/src/libaio.so.1.0.2 DESTINATION ${LIBAIO_INSTALL_DIR}/lib)
    file(COPY ${LIBAIO_SRC_DIR}/src/libaio.a DESTINATION ${LIBAIO_INSTALL_DIR}/lib)

    # Create symlinks: libaio.so -> libaio.so.1 -> libaio.so.1.0.2
    execute_process(
        COMMAND ln -sf libaio.so.1.0.2 libaio.so.1
        WORKING_DIRECTORY ${LIBAIO_INSTALL_DIR}/lib
    )
    execute_process(
        COMMAND ln -sf libaio.so.1 libaio.so
        WORKING_DIRECTORY ${LIBAIO_INSTALL_DIR}/lib
    )

    # Create imported shared library target with symbol versioning intact
    if(NOT TARGET aio)
        add_library(aio SHARED IMPORTED GLOBAL)
        set_target_properties(aio PROPERTIES
            IMPORTED_LOCATION "${LIBAIO_INSTALL_DIR}/lib/libaio.so.1.0.2"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBAIO_INSTALL_DIR}/include"
        )
    endif()

    set(LIBAIO_INCLUDE_DIR "${LIBAIO_INSTALL_DIR}/include")
endif()

set(LIBAIO_LIBRARIES aio CACHE INTERNAL "libaio target")
set(LIBAIO_INCLUDE_DIRS "${LIBAIO_INCLUDE_DIR}" CACHE INTERNAL "libaio include directories")
message(STATUS "libaio target defined: aio")
