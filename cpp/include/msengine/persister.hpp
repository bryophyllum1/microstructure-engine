#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "msengine/signal_engine.hpp"
#include "msengine/spsc_queue.hpp"

namespace msengine {

// On-disk format: fixed 16-byte header, then contiguous raw FeatureRow
// records. Dead simple on purpose — the writer thread does nothing but
// memcpy-append, and Python reads it with a single numpy.fromfile call.
//
//   [0..3]  magic "MSEF"
//   [4..7]  uint32 version (1)
//   [8..11] uint32 record size in bytes (rejects layout drift on read)
//   [12..15] reserved (0)
struct FeatureFileHeader {
    char magic[4];
    std::uint32_t version;
    std::uint32_t record_size;
    std::uint32_t reserved;
};
static_assert(sizeof(FeatureFileHeader) == 16);

inline constexpr char FEATURE_MAGIC[4] = {'M', 'S', 'E', 'F'};
inline constexpr std::uint32_t FEATURE_VERSION = 1;

// Asynchronous feature persister: the compute thread submits rows via a
// lock-free SPSC queue; a dedicated writer thread batches them to disk.
// Slow disk I/O therefore never backpressures signal computation — if the
// queue fills, rows are dropped and counted (research data loses a row;
// the live book and signals are unaffected).
class FeatureWriter {
public:
    explicit FeatureWriter(std::string path, std::size_t queue_capacity = 8192);
    ~FeatureWriter();

    FeatureWriter(const FeatureWriter&) = delete;
    FeatureWriter& operator=(const FeatureWriter&) = delete;

    // Producer side (compute thread). Non-blocking.
    void submit(const FeatureRow& row);

    // Stops the writer thread, draining anything still queued.
    void stop();

    std::uint64_t rows_written() const { return written_.load(std::memory_order_relaxed); }
    std::uint64_t rows_dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    void run();

    std::string path_;
    SpscQueue<FeatureRow> queue_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::thread writer_;
};

// Blocking reader for tests and tools (Python reads the file directly).
// Throws std::runtime_error on missing file or header mismatch.
std::vector<FeatureRow> read_feature_file(const std::string& path);

}  // namespace msengine
