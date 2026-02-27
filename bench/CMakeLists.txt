set(AETHER_REPORTS_DIR "${PROJECT_SOURCE_DIR}/reports")

add_executable(bench_latency bench_latency.cpp)
target_link_libraries(bench_latency PRIVATE aether rt)
target_compile_definitions(bench_latency PRIVATE
    AETHERD_PATH="$<TARGET_FILE:aetherd>"
    AETHER_REPORTS_DIR="${AETHER_REPORTS_DIR}")
add_dependencies(bench_latency aetherd)

add_executable(bench_throughput bench_throughput.cpp)
target_link_libraries(bench_throughput PRIVATE aether rt)
target_compile_definitions(bench_throughput PRIVATE
    AETHERD_PATH="$<TARGET_FILE:aetherd>"
    AETHER_REPORTS_DIR="${AETHER_REPORTS_DIR}")
add_dependencies(bench_throughput aetherd)
