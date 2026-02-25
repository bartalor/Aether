#pragma once

#include <cstdint>

namespace aether {

constexpr char DAEMON_SOCKET_PATH[] = "/tmp/aetherd.sock";

constexpr uint32_t MAX_TOPIC_LEN = 64;
constexpr uint32_t MAX_SHM_NAME_LEN = 64;

enum class ControlStatus : uint8_t {
    Ok           = 0,
    TopicNotFound = 1,
    InternalError = 2,
};

struct SubscribeRequest {
    uint32_t topic_len;
    char     topic[MAX_TOPIC_LEN];
};

struct SubscribeResponse {
    ControlStatus status;
    uint32_t      capacity;
    char          shm_name[MAX_SHM_NAME_LEN];
};

} // namespace aether
