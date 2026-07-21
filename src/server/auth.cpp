#include "voiceqas/server/auth.hpp"

#include "voiceqas/ops/config.hpp"

#include <cstdlib>

namespace voiceqas {

namespace {

std::string token_from_request(const httplib::Request& req) {
    const auto header = req.get_header_value("X-Ops-Token");
    if (!header.empty()) {
        return header;
    }
    const auto bearer = req.get_header_value("Authorization");
    if (bearer.size() > 7 && bearer.substr(0, 7) == "Bearer ") {
        return bearer.substr(7);
    }
    return {};
}

bool token_matches_any(const std::string& token) {
    if (token.empty()) {
        return false;
    }
    const auto& cfg = ops::global_ops_config();
    if (!cfg.admin_token.empty() && token == cfg.admin_token) {
        return true;
    }
    if (!cfg.write_token.empty() && token == cfg.write_token) {
        return true;
    }
    if (!cfg.read_token.empty() && token == cfg.read_token) {
        return true;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_TOKEN")) {
        return token == v;
    }
    return false;
}

}  // namespace

bool ops_auth_required() {
    const auto& cfg = ops::global_ops_config();
    if (!cfg.admin_token.empty() || !cfg.read_token.empty() || !cfg.write_token.empty()) {
        return true;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_TOKEN")) {
        return v[0] != '\0';
    }
    return false;
}

bool check_ops_token(const std::string& token) {
    if (!ops_auth_required()) {
        return true;
    }
    return token_matches_any(token);
}

bool check_ops_read_auth(const httplib::Request& req) {
    if (!ops_auth_required()) {
        return true;
    }
    const auto token = token_from_request(req);
    if (token.empty()) {
        return false;
    }
    const auto& cfg = ops::global_ops_config();
    if (!cfg.admin_token.empty() && token == cfg.admin_token) {
        return true;
    }
    if (!cfg.write_token.empty() && token == cfg.write_token) {
        return true;
    }
    if (!cfg.read_token.empty() && token == cfg.read_token) {
        return true;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_TOKEN")) {
        return token == v;
    }
    return false;
}

bool check_ops_write_auth(const httplib::Request& req) {
    if (!ops_auth_required()) {
        return true;
    }
    const auto token = token_from_request(req);
    if (token.empty()) {
        return false;
    }
    const auto& cfg = ops::global_ops_config();
    if (!cfg.admin_token.empty() && token == cfg.admin_token) {
        return true;
    }
    if (!cfg.write_token.empty() && token == cfg.write_token) {
        return true;
    }
    if (const char* v = std::getenv("VOICEQAS_OPS_TOKEN")) {
        return token == v;
    }
    return false;
}

}  // namespace voiceqas
