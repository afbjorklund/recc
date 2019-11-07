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

#include <fileutils.h>

#include <env.h>
#include <logging.h>
#include <subprocess.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <unordered_map>

namespace BloombergLP {
namespace recc {

TemporaryDirectory::TemporaryDirectory(const char *prefix)
{
    d_name = TMPDIR + "/" + std::string(prefix) + "XXXXXX";
    if (mkdtemp(&d_name[0]) == nullptr) {
        throw std::system_error(errno, std::system_category());
    }
}

TemporaryDirectory::~TemporaryDirectory()
{
    const std::vector<std::string> rmCommand = {"rm", "-rf", d_name};
    // TODO: catch here so as to not throw from destructor
    execute(rmCommand);
}

void FileUtils::create_directory_recursive(const char *path)
{
    RECC_LOG_VERBOSE("Creating directory at " << path);
    if (mkdir(path, 0777) != 0) {
        if (errno == EEXIST) {
            // The directory already exists, so return.
            return;
        }
        else if (errno == ENOENT) {
            auto lastSlash = strrchr(path, '/');
            if (lastSlash == nullptr) {
                throw std::system_error(errno, std::system_category());
            }
            std::string parent(
                path, static_cast<std::string::size_type>(lastSlash - path));
            create_directory_recursive(parent.c_str());
            if (mkdir(path, 0777) != 0) {
                throw std::system_error(errno, std::system_category());
            }
        }
        else {
            throw std::system_error(errno, std::system_category());
        }
    }
}

bool FileUtils::isRegularFileOrSymlink(const struct stat &s)
{
    return (S_ISREG(s.st_mode) || S_ISLNK(s.st_mode));
}

struct stat FileUtils::get_stat(const char *path, const bool followSymlinks)
{
    if (path == nullptr || *path == 0) {
        const std::string error = "invalid args: path is either null or empty";
        RECC_LOG_ERROR(error);
        throw std::runtime_error(error);
    }

    struct stat statResult;
    const int rc =
        (followSymlinks ? stat(path, &statResult) : lstat(path, &statResult));
    if (rc < 0) {
        RECC_LOG_ERROR("error in " << (followSymlinks ? "stat()" : "lstat()")
                                   << ", rc = " << rc << ", errno = [" << errno
                                   << ":" << strerror(errno) << "]");
        throw std::system_error(errno, std::system_category());
    }

    return statResult;
}

bool FileUtils::is_executable(const struct stat &s)
{
    return s.st_mode & S_IXUSR;
}

bool FileUtils::is_executable(const char *path)
{
    if (path == nullptr || *path == 0) {
        std::ostringstream oss;
        oss << "invalid args: path is either null or empty";
        throw std::runtime_error(oss.str());
    }

    struct stat statResult;
    if (stat(path, &statResult) == 0) {
        return statResult.st_mode & S_IXUSR;
    }
    throw std::system_error(errno, std::system_category());
}

bool FileUtils::is_symlink(const struct stat &s) { return S_ISLNK(s.st_mode); }

void FileUtils::make_executable(const char *path)
{
    struct stat statResult;
    if (stat(path, &statResult) == 0) {
        if (chmod(path, statResult.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) ==
            0) {
            return;
        }
    }
    throw std::system_error(errno, std::system_category());
}

std::string FileUtils::get_file_contents(const char *path,
                                         const bool followSymlinks)
{
    struct stat statResult = FileUtils::get_stat(path, followSymlinks);
    return FileUtils::get_file_contents(path, statResult);
}

std::string FileUtils::get_file_contents(const char *path,
                                         const struct stat &statResult)
{
    std::string contents(statResult.st_size, '\0');
    if (S_ISREG(statResult.st_mode)) {
        std::ifstream fileStream;
        fileStream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fileStream.open(path, std::ios::in | std::ios::binary);

        auto start = fileStream.tellg();
        fileStream.seekg(0, std::ios::end);
        auto size = fileStream.tellg() - start;

        contents.resize(static_cast<std::string::size_type>(size));
        fileStream.seekg(start);

        if (fileStream) {
            fileStream.read(&contents[0],
                            static_cast<std::streamsize>(contents.length()));
        }
    }
    else if (S_ISLNK(statResult.st_mode)) {
        const int rc = readlink(path, &contents[0], contents.size());
        if (rc < 0) {
            std::ostringstream oss;
            oss << "readlink failed for \"" << std::string(path)
                << "\", rc = " << rc << ", errno = [" << errno << ":"
                << strerror(errno) << "]";
            RECC_LOG_ERROR(oss.str());
            throw std::runtime_error(oss.str());
        }
    }
    else {
        throw std::runtime_error("\"" + std::string(path) +
                                 "\" is not a regular file or a symlink");
    }

    return contents;
}

void FileUtils::write_file(const char *path, const std::string &contents)
{
    std::ofstream fileStream(path, std::ios::trunc | std::ios::binary);
    if (!fileStream) {
        const auto slash = strrchr(path, '/');
        if (slash != nullptr) {
            std::string slashPath(
                path, static_cast<std::string::size_type>(slash - path));
            slashPath = normalize_path(slashPath.c_str());
            create_directory_recursive(slashPath.c_str());
            fileStream.open(path, std::ios::trunc | std::ios::binary);
        }
    }
    fileStream.exceptions(std::ofstream::failbit | std::ofstream::badbit);
    fileStream << contents << std::flush;
}

std::string FileUtils::normalize_path(const char *path)
{
    std::vector<std::string> segments;
    const bool global = path[0] == '/';

    while (path[0] != '\0') {
        const char *slash = strchr(path, '/');
        std::string segment;

        if (slash == nullptr) {
            segment = std::string(path);
        }
        else {
            segment = std::string(
                path, static_cast<std::string::size_type>(slash - path));
        }

        if (segment == ".." && !segments.empty() && segments.back() != "..") {
            segments.pop_back();
        }
        else if (segment != "." && segment != "") {
            segments.push_back(segment);
        }

        if (slash == nullptr) {
            break;
        }
        else {
            path = slash + 1;
        }
    }

    std::string result(global ? "/" : "");
    if (segments.size() > 0) {
        for (const auto &segment : segments) {
            result += segment + "/";
        }
        result.pop_back();
    }
    return result;
}

bool FileUtils::has_path_prefix(const std::string &path, std::string prefix)
{
    /* A path can never have the empty path as a prefix */
    if (prefix.empty()) {
        return false;
    }
    if (path == prefix) {
        return true;
    }

    /*
     * Make sure prefix ends in a slash.
     * This is so we don't return true if path = /foo and prefix = /foobar
     */
    if (prefix.back() != '/') {
        prefix.push_back('/');
    }

    return path.substr(0, prefix.length()) == prefix;
}

std::string FileUtils::make_path_relative(std::string path,
                                          const char *workingDirectory)
{
    if (workingDirectory == nullptr || workingDirectory[0] == 0 ||
        path.length() == 0 || path[0] != '/' ||
        !has_path_prefix(path, RECC_PROJECT_ROOT)) {
        return path;
    }
    if (workingDirectory[0] != '/') {
        throw std::logic_error(
            "Working directory must be null or an absolute path");
    }

    unsigned int i = 0;
    unsigned int lastMatchingSegmentEnd = 0;
    while (i < path.length() && path[i] == workingDirectory[i]) {
        if (workingDirectory[i + 1] == 0) {
            // Working directory is prefix of path, so if the last
            // segment matches, we're done.
            if (path.length() == i + 1) {
                return std::string(path[i] == '/' ? "./" : ".");
            }
            else if (path.length() == i + 2 && path[i + 1] == '/') {
                return std::string("./");
            }
            else if (path[i] == '/') {
                return path.substr(i + 1);
            }
            else if (path[i + 1] == '/') {
                return path.substr(i + 2);
            }
        }
        else if (path[i] == '/') {
            lastMatchingSegmentEnd = i;
        }
        ++i;
    }

    if (i == path.length() && workingDirectory[i] == '/') {
        // Path is prefix of working directory.
        if (workingDirectory[i + 1] == 0) {
            return std::string(".");
        }
        else {
            lastMatchingSegmentEnd = i;
            ++i;
        }
    }

    unsigned int dotdotsNeeded = 1;
    while (workingDirectory[i] != 0) {
        if (workingDirectory[i] == '/' && workingDirectory[i + 1] != 0) {
            ++dotdotsNeeded;
        }
        ++i;
    }

    auto result =
        path.replace(0, lastMatchingSegmentEnd, dotdotsNeeded * 3 - 1, '.');

    for (unsigned int j = 0; j < dotdotsNeeded - 1; ++j) {
        result[j * 3 + 2] = '/';
    }
    return result;
}

std::string FileUtils::make_path_absolute(const std::string &path,
                                          const std::string &cwd)
{
    if (path.empty() || path.front() == '/') {
        return path;
    }

    const std::string fullPath = cwd + '/' + path;
    std::string normalizedPath = FileUtils::normalize_path(fullPath.c_str());

    /* normalize_path removes trailing slashes, so let's preserve them here
     */
    if (path.back() == '/' && normalizedPath.back() != '/') {
        normalizedPath.push_back('/');
    }
    return normalizedPath;
}

std::string FileUtils::join_normalize_path(const std::string &base,
                                           const std::string &extension)
{
    std::ostringstream catPath;
    catPath << base;
    const char SLASH = '/';
    const bool extStartSlash = (extension.front() == SLASH);
    const bool baseEndSlash = (base.back() == SLASH);

    // "/a/" + "b" -> nothing
    // "/a" + "b" -> << add "/"
    // "/a/" + "/b"-> << remove "/"
    // "/a" + "/b" ->  nothing
    size_t index = 0;
    // add slash if neither base nor extension have slash
    if (!base.empty() && !baseEndSlash && !extStartSlash)
        catPath << SLASH;
    // remove slash if both base and extension have slash
    else if (!extension.empty() && baseEndSlash && extStartSlash)
        index = 1;

    catPath << extension.substr(index);
    std::string catPathStr = catPath.str();
    return normalize_path(catPathStr.c_str());
}

std::string FileUtils::expand_path(const std::string &path)
{
    std::string home = "";
    std::string newPath = path;
    if (!path.empty() && path[0] == '~') {
        home = getenv("HOME");
        if (home.c_str() == nullptr or home[0] == '\0') {
            std::ostringstream errorMsg;
            errorMsg << "Could not expand path: " << path << " $HOME not set";
            throw std::runtime_error(errorMsg.str());
        }
        newPath = path.substr(1);
    }
    std::string expandPath = join_normalize_path(home, newPath);
    return expandPath;
}

std::string FileUtils::get_current_working_directory()
{
    unsigned int bufferSize = 1024;
    while (true) {
        std::unique_ptr<char[]> buffer(new char[bufferSize]);
        char *cwd = getcwd(buffer.get(), bufferSize);

        if (cwd != nullptr) {
            return std::string(cwd);
        }
        else if (errno == ERANGE) {
            bufferSize *= 2;
        }
        else {
            RECC_LOG_PERROR(
                "Warning: could not get current working directory");
            return std::string();
        }
    }
}

int FileUtils::parent_directory_levels(const char *path)
{
    int currentLevel = 0;
    int lowestLevel = 0;

    while (*path != 0) {
        const char *slash = strchr(path, '/');
        if (!slash) {
            break;
        }

        const auto segmentLength = slash - path;
        if (segmentLength == 0 || (segmentLength == 1 && path[0] == '.')) {
            // Empty or dot segments don't change the level.
        }
        else if (segmentLength == 2 && path[0] == '.' && path[1] == '.') {
            currentLevel--;
            lowestLevel = std::min(lowestLevel, currentLevel);
        }
        else {
            currentLevel++;
        }

        path = slash + 1;
    }
    if (strcmp(path, "..") == 0) {
        currentLevel--;
        lowestLevel = std::min(lowestLevel, currentLevel);
    }
    return -lowestLevel;
}

std::string FileUtils::last_n_segments(const char *path, int n)
{
    if (n == 0) {
        return "";
    }

    const auto pathLength = strlen(path);
    const char *substringStart = path + pathLength - 1;
    unsigned int substringLength = 1;
    int slashesSeen = 0;

    if (path[pathLength - 1] == '/') {
        substringLength = 0;
    }

    while (substringStart != path) {
        if (*(substringStart - 1) == '/') {
            slashesSeen++;
            if (slashesSeen == n) {
                return std::string(substringStart, substringLength);
            }
        }
        substringStart--;
        substringLength++;
    }

    // The path might only be one segment (no slashes)
    if (slashesSeen == 0 && n == 1) {
        return std::string(path, pathLength);
    }
    throw std::logic_error("Not enough segments in path");
}

bool FileUtils::is_absolute_path(const char *path)
{
    return path != nullptr && path[0] == '/';
}

std::string FileUtils::resolve_path_from_prefix_map(const std::string &path)
{
    if (RECC_PREFIX_REPLACEMENT.empty()) {
        return path;
    }

    // Iterate through dictionary, replacing path if it includes key, with
    // value.
    for (const auto &pair : RECC_PREFIX_REPLACEMENT) {
        // Check if prefix is found in the path, and that it is a prefix.
        if (FileUtils::has_path_prefix(path, pair.first)) {
            // Append a trailing slash to the replacement, in cases of
            // replacing `/` Double slashes will get removed during
            // normalization.
            const std::string replaced_path =
                pair.second + '/' + path.substr(pair.first.length());
            const std::string newPath =
                FileUtils::normalize_path(replaced_path.c_str());
            RECC_LOG_VERBOSE("Replacing and normalized path: ["
                             << path << "] with newpath: [" << newPath << "]");
            return newPath;
        }
    }
    return path;
}

std::string FileUtils::path_basename(const char *path)
{
    return FileUtils::last_n_segments(path, 1);
}

std::vector<std::string> FileUtils::parseDirectories(const std::string &path)
{
    std::vector<std::string> result;
    char *token = std::strtok((char *)path.c_str(), "/");
    while (token != nullptr) {
        result.emplace_back(token);
        token = std::strtok(nullptr, "/");
    }

    return result;
}

} // namespace recc
} // namespace BloombergLP
