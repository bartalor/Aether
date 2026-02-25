# aetherd — broker daemon

add_executable(aetherd main.cpp acceptor.cpp)
target_link_libraries(aetherd PRIVATE aether rt)
