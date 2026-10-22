#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>

extern "C" {
std::uint64_t APS5_VABI SSLeay_nid_postfix(void);
}

static void Require(bool condition, std::source_location location = std::source_location::current()) {
    if (condition) return;
    std::fprintf(stderr, "LibreSSL version check failed at line %u\n", location.line());
    std::abort();
}

int main() {
    Require(SSLeay_nid_postfix() == std::uint64_t{0x20000000});
    Require(SSLeay_nid_postfix() == SSLeay_nid_postfix());
    return 0;
}
