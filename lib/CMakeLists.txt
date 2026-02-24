# libaether — client library (publisher + subscriber API)

add_library(aether SHARED
    shm.cpp
    publish.cpp
    consume.cpp
)

# -lrt is required on Linux for shm_open() and shm_unlink().
# These live in the POSIX realtime extensions library, not libc.
target_link_libraries(aether PRIVATE rt)
