#pragma once
#include "BuildQueueEngine.hpp"
namespace bq {
class HttpServer {
  public:
    static int serve(BuildQueueEngine &, int, const std::string &root);
};
} // namespace bq
