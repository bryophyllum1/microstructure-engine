#include "msengine/persister.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace msengine {
namespace {

namespace fs = std::filesystem;

class PersisterTest : public ::testing::Test {
protected:
    std::string path_;

    void SetUp() override {
        path_ = (fs::temp_directory_path() /
                 ("msengine_test_" +
                  std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                  "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name() +
                  ".msef"))
                    .string();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(path_, ec);
    }

    static FeatureRow make_row(std::int64_t seq) {
        FeatureRow r{};
        r.ts_ns = seq * 1'000'000;
        r.exchange_seq = seq;
        r.mid = 100.0 + static_cast<double>(seq);
        r.spread = 0.5;
        r.imbalance_top5 = 0.1;
        return r;
    }
};

TEST_F(PersisterTest, RoundTripPreservesRows) {
    {
        FeatureWriter writer(path_);
        for (std::int64_t i = 1; i <= 10; ++i) writer.submit(make_row(i));
        writer.stop();  // drains queue before closing
        EXPECT_EQ(writer.rows_written(), 10u);
        EXPECT_EQ(writer.rows_dropped(), 0u);
    }

    const auto rows = read_feature_file(path_);
    ASSERT_EQ(rows.size(), 10u);
    for (std::int64_t i = 1; i <= 10; ++i) {
        EXPECT_EQ(rows[i - 1].exchange_seq, i);
        EXPECT_DOUBLE_EQ(rows[i - 1].mid, 100.0 + static_cast<double>(i));
    }
}

TEST_F(PersisterTest, ManyRowsExceedingBatchSize) {
    constexpr int kRows = 10'000;  // many batches of 256
    {
        FeatureWriter writer(path_);
        for (std::int64_t i = 0; i < kRows; ++i) {
            writer.submit(make_row(i));
            if (i % 512 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        writer.stop();
        EXPECT_EQ(writer.rows_written() + writer.rows_dropped(),
                  static_cast<std::uint64_t>(kRows));
    }

    const auto rows = read_feature_file(path_);
    EXPECT_FALSE(rows.empty());
    // Whatever was written must be in order with no duplicates.
    for (std::size_t i = 1; i < rows.size(); ++i) {
        EXPECT_LT(rows[i - 1].exchange_seq, rows[i].exchange_seq);
    }
}

TEST_F(PersisterTest, ReadMissingFileThrows) {
    EXPECT_THROW(read_feature_file(path_ + ".does_not_exist"), std::runtime_error);
}

TEST_F(PersisterTest, ReadRejectsCorruptHeader) {
    std::FILE* f = std::fopen(path_.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const char garbage[] = "this is not a feature file at all.....";
    std::fwrite(garbage, 1, sizeof(garbage), f);
    std::fclose(f);

    EXPECT_THROW(read_feature_file(path_), std::runtime_error);
}

TEST_F(PersisterTest, EmptyWriterProducesValidEmptyFile) {
    {
        FeatureWriter writer(path_);
        writer.stop();
    }
    const auto rows = read_feature_file(path_);
    EXPECT_TRUE(rows.empty());
}

}  // namespace
}  // namespace msengine
