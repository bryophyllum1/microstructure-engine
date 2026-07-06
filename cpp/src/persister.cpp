#include "msengine/persister.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace msengine {

namespace {
constexpr std::size_t kBatchSize = 256;  // rows buffered before a write
}

FeatureWriter::FeatureWriter(std::string path, std::size_t queue_capacity)
    : path_(std::move(path)), queue_(queue_capacity) {
    running_.store(true, std::memory_order_release);
    writer_ = std::thread([this] { run(); });
}

FeatureWriter::~FeatureWriter() { stop(); }

void FeatureWriter::submit(const FeatureRow& row) {
    FeatureRow copy = row;
    if (!queue_.try_push(std::move(copy))) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void FeatureWriter::stop() {
    if (!running_.exchange(false)) return;
    if (writer_.joinable()) writer_.join();
}

void FeatureWriter::run() {
    std::FILE* f = std::fopen(path_.c_str(), "wb");
    if (!f) {
        // Can't throw across a thread boundary; count everything as dropped.
        FeatureRow discard;
        while (running_.load(std::memory_order_acquire)) {
            while (queue_.try_pop(discard))
                dropped_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return;
    }

    FeatureFileHeader header{};
    std::memcpy(header.magic, FEATURE_MAGIC, 4);
    header.version = FEATURE_VERSION;
    header.record_size = sizeof(FeatureRow);
    std::fwrite(&header, sizeof(header), 1, f);

    std::vector<FeatureRow> batch;
    batch.reserve(kBatchSize);
    FeatureRow row;

    const auto flush = [&] {
        if (batch.empty()) return;
        std::fwrite(batch.data(), sizeof(FeatureRow), batch.size(), f);
        written_.fetch_add(batch.size(), std::memory_order_relaxed);
        batch.clear();
    };

    // Drain until stopped AND empty, so no submitted row is lost on shutdown.
    while (true) {
        if (queue_.try_pop(row)) {
            batch.push_back(row);
            if (batch.size() >= kBatchSize) flush();
        } else {
            flush();  // idle: get whatever we have onto disk
            if (!running_.load(std::memory_order_acquire)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    flush();
    std::fclose(f);
}

std::vector<FeatureRow> read_feature_file(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open feature file: " + path);

    FeatureFileHeader header{};
    if (std::fread(&header, sizeof(header), 1, f) != 1 ||
        std::memcmp(header.magic, FEATURE_MAGIC, 4) != 0 ||
        header.version != FEATURE_VERSION ||
        header.record_size != sizeof(FeatureRow)) {
        std::fclose(f);
        throw std::runtime_error("bad feature file header: " + path);
    }

    std::vector<FeatureRow> rows;
    FeatureRow row;
    while (std::fread(&row, sizeof(row), 1, f) == 1) rows.push_back(row);
    std::fclose(f);
    return rows;
}

}  // namespace msengine
