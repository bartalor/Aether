# Smoke tests for the ring buffer and shared memory layer

add_executable(test_ring test_ring.cpp)
target_link_libraries(test_ring PRIVATE aether rt)
