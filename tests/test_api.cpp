#include "Api.hpp"
#include "Test.hpp"
using namespace bq;
TEST(REST_validation_and_real_engine_operations) {
    BuildQueueEngine e(BUILDQUEUE_ROOT, "");
    Api api(e);
    CHECK(api.request("GET", "/api/health").status == 200);
    CHECK(api.request("POST", "/api/submit", "{").status == 400);
    CHECK(api.request("POST", "/api/submit", "[]").status == 400);
    CHECK(api.request("POST", "/api/submit", R"({"burst":0})").status == 400);
    CHECK(api.request("POST", "/api/submit", R"({"burst":1.5})").status == 400);
    CHECK(api.request("POST", "/api/submit", R"({"memory":1025})").status == 400);
    CHECK(api.request("POST", "/api/workers", R"({"workers":0})").status == 400);
    CHECK(api.request("POST", "/api/load-demo", R"({"name":"../outside"})").status == 400);
    CHECK(e.snapshot()["jobs"].empty());
    CHECK(api.request("POST", "/api/submit", R"({"name":"API task","burst":2})").status == 201);
    CHECK(api.request("GET", "/api/jobs/1").status == 200);
    CHECK(api.request("GET", "/api/jobs/999").status == 404);
    CHECK(api.request("POST", "/api/step", "{}").status == 200);
    CHECK(e.snapshot()["stats"]["tick"] == 1);
    CHECK(api.request("POST", "/api/cancel", R"({"id":1})").status == 400);
    CHECK(api.request("GET", "/api/unknown").status == 404);
    CHECK(api.request("POST", "/api/policy", R"({"policy":"mlfq"})").status == 400);
}
