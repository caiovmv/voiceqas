#include <gtest/gtest.h>

#include <httplib.h>

#include "voiceqas/config.hpp"
#include "voiceqas/server/auth.hpp"

namespace voiceqas {

TEST(AuthTest, AllowsWhenNoTokensConfigured) {
    auto cfg = load_app_config_from_file("/nonexistent/voiceqas_auth_test.yaml");
    cfg.ops.admin_token.clear();
    cfg.ops.read_token.clear();
    cfg.ops.write_token.clear();
    global_ops_config() = cfg.ops;

    httplib::Request req;
    EXPECT_TRUE(check_ops_read_auth(req));
    EXPECT_TRUE(check_ops_write_auth(req));
}

TEST(AuthTest, EnforcesReadAndWriteTokens) {
    auto cfg = load_app_config_from_file("/nonexistent/voiceqas_auth_test2.yaml");
    cfg.ops.read_token = "read-secret";
    cfg.ops.write_token = "write-secret";
    global_ops_config() = cfg.ops;

    httplib::Request no_token;
    EXPECT_FALSE(check_ops_read_auth(no_token));

    httplib::Request read_req;
    read_req.set_header("X-Ops-Token", "read-secret");
    EXPECT_TRUE(check_ops_read_auth(read_req));
    EXPECT_FALSE(check_ops_write_auth(read_req));

    httplib::Request write_req;
    write_req.set_header("X-Ops-Token", "write-secret");
    EXPECT_TRUE(check_ops_write_auth(write_req));
}

}  // namespace voiceqas
