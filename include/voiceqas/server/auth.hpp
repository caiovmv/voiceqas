#pragma once

#include <string>

#include <httplib.h>

namespace voiceqas {

bool ops_auth_required();
bool check_ops_token(const std::string& token);
bool check_ops_read_auth(const httplib::Request& req);
bool check_ops_write_auth(const httplib::Request& req);

}  // namespace voiceqas
