#pragma once

#include "aether/control.h"
#include "aether/ring.h"

struct TopicInfo {
    char               shm_name[aether::MAX_SHM_NAME_LEN];
    aether::RingHeader* hdr;
};

// Returns the TopicInfo for the given topic name, creating the shm segment
// if it doesn't exist yet. Returns nullptr if creation fails.
// Thread-safe.
const TopicInfo* get_or_create_topic(const char* name, uint32_t name_len);

// Detach and destroy all topic shm segments. Call once on daemon shutdown.
void destroy_all_topics();

// Print stats for all live topics to stderr.
void dump_all_topic_stats();
