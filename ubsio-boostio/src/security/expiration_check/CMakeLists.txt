file(GLOB_RECURSE EXPIRE_CHECKER_SRCS *.h *.cpp)

add_library(expire_checker SHARED ${EXPIRE_CHECKER_SRCS})

if (${DEBUG_UT} MATCHES "ON")
    set_source_files_properties(expire_checker.cpp PROPERTIES COMPILE_FLAGS -fexceptions)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)$") # arm
    set(OPENSSL_LIB_DIR "${PROJECT_3RDPARTY_BIN_DIR}/openssl/lib")
else()
    set(OPENSSL_LIB_DIR "${PROJECT_3RDPARTY_BIN_DIR}/openssl/lib64")
endif()

TARGET_LINK_LIBRARIES(expire_checker
        -Wl,--start-group
        pthread
        -Wl,--end-group
        )
TARGET_LINK_LIBRARIES(expire_checker
        -Wl,--start-group
        ${OPENSSL_LIB_DIR}/libssl.so
        ${OPENSSL_LIB_DIR}/libcrypto.so
        -Wl,--end-group
)
install(TARGETS expire_checker DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
