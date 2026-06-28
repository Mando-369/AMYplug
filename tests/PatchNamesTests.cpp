// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// Built-in patch-name table — sanity that the build-time codegen (from AMY's
// patches.h comments) produced the expected banks.
#include <catch2/catch_test_macros.hpp>
#include "BuiltinPatchNames.h"
#include <string>

TEST_CASE("Built-in patch names are generated for all 258 patches", "[names]")
{
    using namespace amyplug;
    REQUIRE(kBuiltinPatchCount == 258);
    REQUIRE(std::string(kBuiltinPatchNames[0]).rfind("Juno", 0) == 0);    // Juno bank
    REQUIRE(std::string(kBuiltinPatchNames[127]).rfind("Juno", 0) == 0);
    REQUIRE(std::string(kBuiltinPatchNames[128]).rfind("DX7", 0) == 0);   // DX7 bank
    REQUIRE(std::string(kBuiltinPatchNames[255]).rfind("DX7", 0) == 0);
    REQUIRE(std::string(kBuiltinPatchNames[256]) == "dpwe piano");
    REQUIRE(std::string(kBuiltinPatchNames[257]) == "amyboard default");
}
