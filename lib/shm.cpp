#include "aether/shm.h"

#include <sys/mman.h>   // mmap, munmap, shm_open, shm_unlink
#include <sys/stat.h>   // mode constants (S_IRUSR, S_IWUSR)
#include <fcntl.h>      // O_CREAT, O_RDWR, O_EXCL
#include <unistd.h>     // ftruncate, close
#include <cassert>      // assert
#include <new>          // placement new

namespace aether {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Map a file descriptor into our address space as read+write.
// Returns the mapped pointer, or nullptr if mmap fails.
// fd is closed before returning — mmap keeps the mapping alive independently.
static RingHeader* map_and_close(int fd, std::size_t size) {
    void* ptr = mmap(
        nullptr,            // let the kernel choose the address
        size,               // total bytes to map
        PROT_READ | PROT_WRITE,  // we need both read and write access
        MAP_SHARED,         // changes are visible to all processes that map this segment
        fd,
        0                   // offset into the file
    );

    close(fd); // fd is no longer needed once the mapping exists

    if (ptr == MAP_FAILED) {
        return nullptr;
    }

    return static_cast<RingHeader*>(ptr);
}

// ---------------------------------------------------------------------------
// shm_create
// ---------------------------------------------------------------------------

RingHeader* shm_create(const char* name, uint32_t capacity) {
    assert(name != nullptr);
    assert(capacity > 0);

    // O_EXCL: fail if the segment already exists.
    // This detects leftover segments from a previous crash — the daemon must
    // clean up (shm_destroy) before creating a new one.
    // 0600: only the owning user can read/write this segment.
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        return nullptr; // errno set by shm_open (e.g. EEXIST if already exists)
    }

    const std::size_t size = shm_segment_size(capacity);

    // Set the size of the segment. A newly created shm object has size 0;
    // without this call, any access to the mapped memory would segfault.
    if (ftruncate(fd, static_cast<off_t>(size)) == -1) {
        close(fd);
        shm_unlink(name); // clean up the name we just created
        return nullptr;
    }

    RingHeader* hdr = map_and_close(fd, size);
    if (hdr == nullptr) {
        shm_unlink(name);
        return nullptr;
    }

    // Construct the RingHeader in place using placement new.
    // The memory already exists (mmap'd) — we just need to initialise it.
    new (hdr) RingHeader{
        .magic    = RING_MAGIC,
        .version  = RING_VERSION,
        .capacity = capacity,
        .write_seq = 0,         // producer starts at sequence 0
    };

    // Initialise each slot's sequence number to its index.
    // A subscriber waiting for sequence N polls slot[N % capacity] and checks
    // slot.sequence == N. Setting sequence = i means: "not yet written".
    // If we left it uninitialised, a subscriber might see garbage and think
    // a message is ready when it isn't.
    Slot* slots = reinterpret_cast<Slot*>(hdr + 1); // slots start right after the header
    for (uint32_t i = 0; i < capacity; ++i) {
        new (&slots[i]) Slot{};           // default-construct (zeroes payload_len, data)
        slots[i].sequence.store(i, std::memory_order_relaxed);
    }

    return hdr;
}

// ---------------------------------------------------------------------------
// shm_attach
// ---------------------------------------------------------------------------

RingHeader* shm_attach(const char* name) {
    assert(name != nullptr);

    // Open existing segment — no O_CREAT, no O_EXCL.
    int fd = shm_open(name, O_RDWR, 0);
    if (fd == -1) {
        return nullptr;
    }

    // We need to know the segment size to mmap it correctly.
    // Read it from the file metadata.
    struct stat st{};
    if (fstat(fd, &st) == -1) {
        close(fd);
        return nullptr;
    }

    RingHeader* hdr = map_and_close(fd, static_cast<std::size_t>(st.st_size));
    if (hdr == nullptr) {
        return nullptr;
    }

    // Validate before trusting any of the mapped data.
    // If magic or version doesn't match, this is a stale or incompatible segment.
    if (hdr->magic != RING_MAGIC || hdr->version != RING_VERSION) {
        munmap(hdr, static_cast<std::size_t>(st.st_size));
        return nullptr;
    }

    return hdr;
}

// ---------------------------------------------------------------------------
// shm_detach
// ---------------------------------------------------------------------------

void shm_detach(RingHeader* hdr) {
    assert(hdr != nullptr);

    // Reconstruct the total segment size so we can unmap the right number of bytes.
    const std::size_t size = shm_segment_size(hdr->capacity);
    munmap(hdr, size);
}

// ---------------------------------------------------------------------------
// shm_destroy
// ---------------------------------------------------------------------------

void shm_destroy(const char* name) {
    assert(name != nullptr);

    // Removes the name from /dev/shm. Processes that already have the segment
    // mapped keep their mapping — it stays alive until the last munmap().
    shm_unlink(name);
}

} // namespace aether
