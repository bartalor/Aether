#include "topic_registry.h"
#include "aether/shm.h"

#include <sys/mman.h> // shm_unlink
#include <cstdio>    // fprintf, snprintf
#include <cstring>   // (transitively needed)
#include <mutex>
#include <string>
#include <unordered_map>

static constexpr uint32_t DEFAULT_TOPIC_CAPACITY = 1024;

static std::mutex                               g_mutex;
static std::unordered_map<std::string, TopicInfo> g_topics;

const TopicInfo* get_or_create_topic(const char* name, uint32_t name_len) {
    std::string key(name, name_len);

    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_topics.find(key);
    if (it != g_topics.end()) {
        return &it->second;
    }

    // Construct shm_name = "/aether_<topic>"
    TopicInfo info{};
    int written = snprintf(info.shm_name, aether::MAX_SHM_NAME_LEN,
                           "/aether_%.*s", static_cast<int>(name_len), name);
    if (written < 0 || written >= static_cast<int>(aether::MAX_SHM_NAME_LEN)) {
        fprintf(stderr, "[topic_registry] topic name too long: %.*s\n",
                static_cast<int>(name_len), name);
        return nullptr;
    }

    shm_unlink(info.shm_name); // remove any stale segment from a previous crash
    info.hdr = aether::shm_create(info.shm_name, DEFAULT_TOPIC_CAPACITY);
    if (info.hdr == nullptr) {
        fprintf(stderr, "[topic_registry] failed to create shm for topic: %.*s\n",
                static_cast<int>(name_len), name);
        return nullptr;
    }

    fprintf(stderr, "[topic_registry] created topic '%.*s' -> %s\n",
            static_cast<int>(name_len), name, info.shm_name);

    auto [iter, inserted] = g_topics.emplace(key, info);
    (void)inserted;
    return &iter->second;
}

void destroy_all_topics() {
    std::lock_guard<std::mutex> lock(g_mutex);

    for (auto& [name, info] : g_topics) {
        aether::shm_detach(info.hdr);
        aether::shm_destroy(info.shm_name);
        fprintf(stderr, "[topic_registry] destroyed topic '%s'\n", name.c_str());
    }

    g_topics.clear();
}

void dump_all_topic_stats() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_topics.empty()) {
        fprintf(stderr, "[aetherd] stats: no topics\n");
        return;
    }

    for (auto& [name, info] : g_topics) {
        const uint64_t total =
            info.hdr->write_seq.load(std::memory_order_relaxed) - 1;
        fprintf(stderr, "[aetherd] stats: topic='%s' capacity=%u messages_published=%llu\n",
                name.c_str(),
                info.hdr->capacity,
                static_cast<unsigned long long>(total));
    }
}
