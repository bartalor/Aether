# doctest header is in this directory
set(DOCTEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Smoke tests for the ring buffer and shared memory layer
add_executable(test_ring test_ring.cpp)
target_link_libraries(test_ring PRIVATE aether rt)

# Integration tests for the control plane (protocol correctness, no shm attach)
add_executable(test_control test_control.cpp)
target_include_directories(test_control PRIVATE ${DOCTEST_INCLUDE_DIR})
target_link_libraries(test_control PRIVATE aether rt)
target_compile_definitions(test_control PRIVATE AETHERD_PATH="$<TARGET_FILE:aetherd>")
add_dependencies(test_control aetherd)

# Integration tests for pub/sub data plane (full stack: subscribe + publish + consume)
add_executable(test_pubsub test_pubsub.cpp)
target_include_directories(test_pubsub PRIVATE ${DOCTEST_INCLUDE_DIR})
target_link_libraries(test_pubsub PRIVATE aether rt)
target_compile_definitions(test_pubsub PRIVATE AETHERD_PATH="$<TARGET_FILE:aetherd>")
add_dependencies(test_pubsub aetherd)

# Stress test: concurrent publishers, single subscriber
add_executable(test_stress test_stress.cpp)
target_include_directories(test_stress PRIVATE ${DOCTEST_INCLUDE_DIR})
target_link_libraries(test_stress PRIVATE aether rt)
target_compile_definitions(test_stress PRIVATE AETHERD_PATH="$<TARGET_FILE:aetherd>")
add_dependencies(test_stress aetherd)
