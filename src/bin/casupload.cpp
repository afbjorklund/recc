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

#include <casclient.h>
#include <env.h>
#include <logging.h>
#include <merklize.h>

#include <cstdlib>
#include <cstring>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <iostream>
#include <reccdefaults.h>

using namespace BloombergLP::recc;

const std::string HELP(
    "USAGE: casupload <paths>\n"
    "Uploads the given files to CAS, then prints the digest hash and size of\n"
    "the corresponding Directory message.\n"
    "\n"
    "The files are placed in CAS subdirectories corresponding to their\n"
    "paths. For example, 'casupload file1.txt subdir/file2.txt' would create\n"
    "a CAS directory containing file1.txt and a subdirectory called 'subdir'\n"
    "containing file2.txt.\n"
    "\n"
    "The server and instance to write to are controlled by the RECC_SERVER\n"
    "and RECC_INSTANCE environment variables.");

int main(int argc, char *argv[])
{
    if (argc <= 1) {
        RECC_LOG_ERROR("USAGE: casupload <paths>");
        RECC_LOG_ERROR("(run \"casupload --help\" for details)");
        return 1;
    }
    else if (argc == 2 &&
             (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        RECC_LOG_WARNING(HELP);
        return 1;
    }

    set_config_locations();
    parse_config_variables();

    NestedDirectory nestedDirectory;
    std::unordered_map<proto::Digest, std::string> blobs;
    std::unordered_map<proto::Digest, std::string> filenames;

    for (int i = 1; i < argc; ++i) {
        File file(argv[i]);
        nestedDirectory.add(file, argv[i]);
        filenames[file.d_digest] = argv[i];
    }

    auto directoryDigest = nestedDirectory.to_digest(&blobs);

    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (RECC_SERVER_AUTH_GOOGLEAPI) {
        creds = grpc::GoogleDefaultCredentials();
    }
    else {
        creds = grpc::InsecureChannelCredentials();
    }
    auto channel = grpc::CreateChannel(RECC_CAS_SERVER, creds);
    CASClient(channel, RECC_INSTANCE).upload_resources(blobs, filenames);

    RECC_LOG(directoryDigest.hash());
    RECC_LOG(directoryDigest.size_bytes());

    return 0;
}
