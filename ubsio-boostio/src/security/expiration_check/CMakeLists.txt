file(GLOB_RECURSE EXPIRE_CHECKER_SRCS *.h *.cpp)
add_library(expire_checker STATIC ${EXPIRE_CHECKER_SRCS})

TARGET_LINK_LIBRARIES(expire_checker
        -Wl,--start-group
        pthread
        bio_security
        -Wl,--end-group
        )
