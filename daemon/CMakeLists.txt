# aetherd — broker daemon

add_executable(aetherd main.cpp acceptor.cpp topic_registry.cpp)
target_link_libraries(aetherd PRIVATE aether rt)
