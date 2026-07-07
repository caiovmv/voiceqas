#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include "voiceqas/ops/config.hpp"
#include "voiceqas/ops/event_store.hpp"

namespace voiceqas {
namespace {

class OpsEventStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        saved_ = global_ops_config();
        path_ = std::filesystem::temp_directory_path() /
                ("voiceqas_ops_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".jsonl");
        std::filesystem::remove(path_);

        auto cfg = saved_;
        cfg.persistence_enabled = true;
        cfg.storage_path = path_.string();
        global_ops_config() = cfg;
    }

    void TearDown() override {
        global_ops_config() = saved_;
        std::filesystem::remove(path_);
    }

    void seed_fixture() const {
        std::ofstream out(path_, std::ios::trunc);
        ASSERT_TRUE(out);
        out << R"({"session_id":"s1","type":"vqa_window","ts_ms":1})" << '\n';
        out << R"({"session_id":"s2","type":"alert","ts_ms":2})" << '\n';
        out << R"({"session_id":"s1","type":"stt_final","ts_ms":3})" << '\n';
    }

    ops::OpsConfig saved_{};
    std::filesystem::path path_;
};

TEST_F(OpsEventStoreTest, AppendsRowWithTimestamp) {
    ops::OpsEventStore::instance().append({{"session_id", "call-a"}, {"type", "alert"}});
    ASSERT_TRUE(std::filesystem::exists(path_));

    std::ifstream in(path_);
    std::string line;
    ASSERT_TRUE(std::getline(in, line));
    const auto row = nlohmann::json::parse(line);
    EXPECT_EQ(row["session_id"], "call-a");
    EXPECT_EQ(row["type"], "alert");
    EXPECT_TRUE(row.contains("ts_ms"));
}

TEST_F(OpsEventStoreTest, QueryFiltersBySessionAndType) {
    seed_fixture();

    const auto s1 = ops::OpsEventStore::instance().query("s1", std::nullopt, 10);
    ASSERT_EQ(s1.size(), 2u);
    EXPECT_EQ(s1[0]["type"], "vqa_window");
    EXPECT_EQ(s1[1]["type"], "stt_final");

    const auto alerts = ops::OpsEventStore::instance().query(std::nullopt, "alert", 10);
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0]["session_id"], "s2");
}

TEST_F(OpsEventStoreTest, QueryRespectsLimit) {
    seed_fixture();
    const auto rows = ops::OpsEventStore::instance().query(std::nullopt, std::nullopt, 2);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0]["type"], "alert");
    EXPECT_EQ(rows[1]["type"], "stt_final");
}

TEST_F(OpsEventStoreTest, NoOpWhenPersistenceDisabled) {
    auto cfg = global_ops_config();
    cfg.persistence_enabled = false;
    global_ops_config() = cfg;

    ops::OpsEventStore::instance().append({{"session_id", "x"}, {"type", "alert"}});
    EXPECT_FALSE(std::filesystem::exists(path_));
}

}  // namespace
}  // namespace voiceqas
