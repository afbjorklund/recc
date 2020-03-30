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

#ifndef INCLUDED_PARSEDCOMMAND
#define INCLUDED_PARSEDCOMMAND

#include <buildboxcommon_temporaryfile.h>
#include <initializer_list>
#include <logging.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace BloombergLP {
namespace recc {

/**
 * Represents the result of parsing a compiler command.
 */
class ParsedCommand {
  public:
    /**
     * Parses the given command. If workingDirectory is non-null, replace
     * absolute paths with paths relative to the given working directory.
     */
    ParsedCommand(std::vector<std::string> command,
                  const char *workingDirectory);
    ParsedCommand(char **argv, const char *workingDirectory)
        : ParsedCommand(vector_from_argv(argv), workingDirectory)
    {
    }
    ParsedCommand(std::initializer_list<std::string> command)
        : ParsedCommand(std::vector<std::string>(command), nullptr)
    {
    }

    /**
     * Returns true if the given command is a supported compiler command.
     */
    bool is_compiler_command() const { return d_compilerCommand; }

    /**
     * Returns true if this is a clang command.
     */
    bool is_clang() const { return d_isClang; }

    /**
     * Returns true if the command contains a AIX compiler.
     */
    bool is_AIX() const { return d_dependencyFileAIX != nullptr; }

    /**
     * Returns the original command that was passed to the constructor, with
     * absolute paths replaced with equivalent relative paths.
     */
    std::vector<std::string> get_command() const { return d_command; }

    /**
     * Return a command that prints this command's dependencies in Makefile
     * format. If this command is not a supported compiler command, the result
     * is undefined.
     */
    std::vector<std::string> get_dependencies_command() const
    {
        return d_dependenciesCommand;
    }

    /**
     * Return compiler basename specified from the command.
     */
    std::string get_compiler() const { return d_compiler; }

    /**
     * Return the name of the file the compiler will write the source
     * dependencies to on AIX.
     *
     * If the compiler command doesn't include a AIX compiler, return empty.
     */
    std::string get_aix_dependency_file_name() const
    {
        if (d_dependencyFileAIX != nullptr) {
            return d_dependencyFileAIX->strname();
        }
        return "";
    }

    /**
     * Return the output files specified in the command arguments.
     *
     * This is not necessarily all of the files the command will create. (For
     * example, if no output files are specified, many compilers will write to
     * a.out by default.)
     */
    std::set<std::string> get_products() const { return d_commandProducts; }

    /**
     * If true, the dependencies command will produce nonstandard Sun-style
     * make rules where one dependency is listed per line and spaces aren't
     * escaped.
     */
    bool produces_sun_make_rules() const { return d_producesSunMakeRules; }

    /**
     * Converts a command path (e.g. "/usr/bin/gcc-4.7") to a command name
     * (e.g. "gcc")
     */
    static std::string command_basename(const std::string &path);

    /**
     * Convert a null-terminated list of C strings to a vector of C++ strings.
     */
    static std::vector<std::string> vector_from_argv(const char *const *argv);

  private:
    bool d_compilerCommand;
    bool d_isClang;
    bool d_producesSunMakeRules;
    std::string d_compiler;
    std::vector<std::string> d_command;
    std::vector<std::string> d_dependenciesCommand;
    std::set<std::string> d_commandProducts;
    std::unique_ptr<buildboxcommon::TemporaryFile> d_dependencyFileAIX;
};

} // namespace recc
} // namespace BloombergLP

#endif
