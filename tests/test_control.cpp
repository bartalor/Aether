
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aether/control.h"
#include "aether/shm.h"
#include "aether/ring.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "daemon_fixture.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Sends a SubscribeRequest directly over a raw Unix socket and returns the
// daemon's SubscribeResponse. Bypasses aether::subscribe() entirely — no shm
// attach, no RingHeader mapping. Use this to test the control-plane protocol
// in isolation, independent of the client library.
static aether::SubscribeResponse raw_subscribe(const char* topic) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); std::abort(); }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, aether::DAEMON_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("connect"); std::abort();
    }

    aether::SubscribeRequest req{};
    req.topic_len = static_cast<uint32_t>(strlen(topic));
    strncpy(req.topic, topic, aether::MAX_TOPIC_LEN - 1);
    write(fd, &req, sizeof(req));

    aether::SubscribeResponse resp{};
    read(fd, &resp, sizeof(resp));
    close(fd);
    return resp;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE_FIXTURE(DaemonFixture, "subscribe returns Ok for new topic") {
    auto resp = raw_subscribe("prices");
    CHECK(resp.status == aether::ControlStatus::Ok);
    CHECK(resp.capacity == 1024);
    CHECK(strcmp(resp.shm_name, "/aether_prices") == 0);
}

TEST_CASE_FIXTURE(DaemonFixture, "subscribe twice returns same shm_name") {
    auto resp1 = raw_subscribe("prices");
    auto resp2 = raw_subscribe("prices");
    CHECK(resp1.status == aether::ControlStatus::Ok);
    CHECK(resp2.status == aether::ControlStatus::Ok);
    CHECK(strcmp(resp1.shm_name, resp2.shm_name) == 0);
}

TEST_CASE_FIXTURE(DaemonFixture, "different topics get different shm segments") {
    auto resp1 = raw_subscribe("prices");
    auto resp2 = raw_subscribe("orders");
    CHECK(strcmp(resp1.shm_name, "/aether_prices") == 0);
    CHECK(strcmp(resp2.shm_name, "/aether_orders") == 0);
    CHECK(strcmp(resp1.shm_name, resp2.shm_name) != 0);
}

TEST_CASE_FIXTURE(DaemonFixture, "shm_attach succeeds with returned shm_name") {
    auto resp = raw_subscribe("prices");
    REQUIRE(resp.status == aether::ControlStatus::Ok);

    aether::RingHeader* hdr = aether::shm_attach(resp.shm_name);
    REQUIRE(hdr != nullptr);
    CHECK(hdr->capacity == 1024);
    aether::shm_detach(hdr);
}
