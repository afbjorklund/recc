// Copyright 2018 Bloomberg Finance L.P
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_DEPS
#define INCLUDED_DEPS

#include <parsedcommand.h>
#include <parsedcommandfactory.h>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace BloombergLP {
namespace recc {

/**
 * Exception reporting that a subprocess has terminated with a non-zero status
 * code.
 */
class subprocess_failed_error : public std::runtime_error {
  public:
    const int d_error_code;
    subprocess_failed_error(int code)
        : std::runtime_error("Subprocess failed"), d_error_code(code){};
};

/**
 * Contains the location of a command's dependencies, as well as its possible
 * output file locations.
 */
struct CommandFileInfo {
    std::set<std::string> d_dependencies;
    std::set<std::string> d_possibleProducts;
};

struct Deps {
    /**
     * Returns the names of the files needed to run the command.
     *
     * The command must be a supported compiler command; if is_compiler_command
     * returns false, the result of calling get_file_info is undefined.
     *
     * Only paths local to the build directory are returned.
     */
    static CommandFileInfo get_file_info(const ParsedCommand &command);

    /**
     * Parse the given Make rules and return a set containing their
     * dependencies.
     */
    static std::set<std::string>
    dependencies_from_make_rules(const std::string &rules,
                                 bool is_sun_format = false,
                                 bool include_global_paths = false);

    /**
     * Given a set of dependencies, return a set of possible compilation
     * outputs.
     */
    static std::set<std::string>
    guess_products(const std::set<std::string> &dependencies);

    /**
     * Determine the location of crtbegin.o that Clang has selected as its
     * GCC installation marker, from the stderr output of `clang -v`.
     * Returns an empty string if something went wrong.
     */
    static std::string crtbegin_from_clang_v(const std::string &str);
};

} // namespace recc
} // namespace BloombergLP

#endif
