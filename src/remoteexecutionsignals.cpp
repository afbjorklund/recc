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

#include <remoteexecutionsignals.h>

#include <cerrno>
#include <cstdio>
#include <pthread.h>
#include <signal.h>

using namespace std;

namespace BloombergLP {
namespace recc {

void setup_signal_handler(int signal, void (*handler)(int))
{
    /* Set up signal handling for the Execute() request */
    struct sigaction sa;
    sa.sa_handler = handler;
    if (sigemptyset(&sa.sa_mask) != 0) {
        perror("Unable to clear signal set");
        return;
    }
    if (sigaction(signal, &sa, NULL) != 0) {
        perror("Unable to register cancellation handler");
    }
}

void block_sigint()
{
    sigset_t signal_set;
    if (sigaddset(&signal_set, SIGINT) != 0) {
        perror("Unable to block SIGINT");
        return;
    }
    errno = pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
    if (errno != 0) {
        perror("Unable to block SIGINT");
    }
}

void unblock_sigint()
{
    sigset_t signal_set;
    if (sigaddset(&signal_set, SIGINT) != 0) {
        perror("Unable to unblock SIGINT");
        return;
    }
    errno = pthread_sigmask(SIG_UNBLOCK, &signal_set, NULL);
    if (errno != 0) {
        perror("Unable to block SIGINT");
    }
}

} // namespace recc
} // namespace BloombergLP