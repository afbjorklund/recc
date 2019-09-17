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
#include <grpcretry.h>
#include <logging.h>
#include <merklize.h>
#include <reccdefaults.h>
#include <reccmetrics/durationmetrictimer.h>
#include <reccmetrics/metricguard.h>
#include <remoteexecutionclient.h>
#include <remoteexecutionsignals.h>

#include <signal.h>
#include <utility>

#include <functional>
#include <future>
#include <sstream>

#define TIMER_NAME_FETCH_WRITE_RESULTS "recc.fetch_write_results"

using namespace google::longrunning;

namespace BloombergLP {
namespace recc {

namespace { // Helper functions used by `RemoteExecutionClient`.
/**
 * Add the files from the given directory (and its subdirectories, recursively)
 * to the given outputFiles map.
 *
 * The given prefix will be appended to the file names, and the given digestMap
 * will be used to look up child directories.
 */
void add_from_directory(
    FileInfoMap *outputFiles, const proto::Directory &directory,
    const std::string &prefix,
    const std::unordered_map<proto::Digest, proto::Directory> &digestMap)
{
    for (int i = 0; i < directory.files_size(); ++i) {
        (*outputFiles)[prefix + directory.files(i).name()] =
            std::pair<proto::Digest, bool>(directory.files(i).digest(),
                                           directory.files(i).is_executable());
    }

    for (int i = 0; i < directory.directories_size(); ++i) {
        auto subdirectory = digestMap.at(directory.directories(i).digest());
        add_from_directory(outputFiles, subdirectory,
                           prefix + directory.directories(i).name() + "/",
                           digestMap);
    }
}

void read_operation_async(ReaderPointer reader_ptr,
                          OperationPointer operation_ptr)
{
    bool logged = false;
    while (reader_ptr->Read(operation_ptr.get())) {
        if (!logged && !operation_ptr->name().empty()) {
            RECC_LOG_VERBOSE(
                "Waiting for Operation: " << operation_ptr->name())
            logged = true;
        }
        if (operation_ptr->done()) {
            break;
        }
    }
}
} // namespace

std::atomic_bool RemoteExecutionClient::s_sigint_received(false);

/**
 * Return the ActionResult for the given Operation. Throws an exception
 * if the Operation finished with an error, or if the Operation hasn't
 * finished yet.
 */
proto::ActionResult get_actionresult(const Operation &operation)
{
    if (!operation.done()) {
        throw std::logic_error(
            "Called get_actionresult on an unfinished Operation");
    }
    else if (operation.has_error()) {
        ensure_ok(operation.error());
    }
    else if (!operation.response().Is<proto::ExecuteResponse>()) {
        throw std::runtime_error("Server returned invalid Operation result");
    }

    proto::ExecuteResponse executeResponse;
    if (!operation.response().UnpackTo(&executeResponse)) {
        throw std::runtime_error("Operation response unpacking failed");
    }

    ensure_ok(executeResponse.status());

    const proto::ActionResult actionResult = executeResponse.result();
    if (actionResult.exit_code() == 0) {
        RECC_LOG_VERBOSE("Execute response message: " +
                         executeResponse.message());
    }
    else if (!executeResponse.message().empty()) {
        RECC_LOG("Remote execution message: " + executeResponse.message());
    }

    return actionResult;
}

/**
 * Read the operation into the given pointer using a separate thread so we can
 * properly handle SIGINT.
 *
 * reader_ptr->Read(&operation) is a blocking call that isn't interruptible by
 * a signal like the read() system call. It also doesn't feed its results into
 * a file descriptor, so we can't use select() without some ugly hacks. Since
 * signal handlers are very limited in power in C++, this means we need to use
 * a new thread, busy wait, and check the signal flag on each iteration.
 */
void RemoteExecutionClient::read_operation(ReaderPointer &reader_ptr,
                                           OperationPointer &operation_ptr)
{
    /* We need to block SIGINT so only this main thread catches it. */
    block_sigint();

    auto future = std::async(std::launch::async, read_operation_async,
                             reader_ptr, operation_ptr);
    unblock_sigint();

    /**
     * Wait for the operation to complete, handling the
     * cancellation flag if it was set by SIGINT.
     */
    std::future_status status;
    do {
        status = future.wait_for(DEFAULT_RECC_POLL_WAIT);
        if (RemoteExecutionClient::s_sigint_received) {
            RECC_LOG_WARNING(
                "Cancelling job, operation name: " << operation_ptr->name());
            /* Cancel the operation if the execution service gave it a name */
            if (!operation_ptr->name().empty()) {
                cancel_operation(operation_ptr->name());
            }
            exit(130); // Ctrl+C exit code
        }
    } while (status != std::future_status::ready);
}

bool RemoteExecutionClient::fetch_from_action_cache(
    const proto::Digest &actionDigest, const std::string &instanceName,
    ActionResult *result)
{

    grpc::ClientContext context;

    proto::GetActionResultRequest actionRequest;
    actionRequest.set_instance_name(instanceName);
    *actionRequest.mutable_action_digest() = actionDigest;

    proto::ActionResult actionResult;
    const grpc::Status status = d_actionCacheStub->GetActionResult(
        &context, actionRequest, &actionResult);

    if (!status.ok()) {
        if (status.error_code() == grpc::StatusCode::NOT_FOUND)
            return false;

        throw std::runtime_error("Action cache returned error " +
                                 status.error_message());
    }

    if (result != nullptr) {
        *result = from_proto(actionResult);
    }

    return true;
}

ActionResult
RemoteExecutionClient::execute_action(const proto::Digest &actionDigest,
                                      bool skipCache)
{
    /* Prepare an asynchronous Execute request */
    proto::ExecuteRequest executeRequest;
    executeRequest.set_instance_name(d_instanceName);
    *executeRequest.mutable_action_digest() = actionDigest;
    executeRequest.set_skip_cache_lookup(skipCache);

    setup_signal_handler(SIGINT, RemoteExecutionClient::set_sigint_received);

    ReaderPointer reader_ptr;
    OperationPointer operation_ptr;

    /* Create the lambda to pass to grpc_retry */
    auto execute_lambda = [&](grpc::ClientContext &context) {
        reader_ptr = d_executionStub->Execute(&context, executeRequest);

        /* Read the result of the Execute request into an OperationPointer */
        operation_ptr = std::make_shared<Operation>();
        read_operation(reader_ptr, operation_ptr);

        return reader_ptr->Finish();
    };

    grpc_retry(execute_lambda, d_grpcContext);

    Operation operation = *operation_ptr;
    if (!operation.done()) {
        throw std::runtime_error(
            "Server closed stream before Operation finished");
    }

    proto::ActionResult resultProto = get_actionresult(operation);
    return from_proto(resultProto);
}

void RemoteExecutionClient::cancel_operation(const std::string &operationName)
{
    proto::CancelOperationRequest cancelRequest;
    cancelRequest.set_name(operationName);

    /* Can't use the same context for simultaneous async RPCs */
    std::unique_ptr<grpc::ClientContext> cancelContext =
        d_grpcContext->new_client_context();

    /* Send the cancellation request and report any errors */
    google::protobuf::Empty empty;
    grpc::Status s = d_operationsStub->CancelOperation(cancelContext.get(),
                                                       cancelRequest, &empty);
    if (!s.ok()) {
        RECC_LOG_ERROR("Failed to cancel job " << operationName << ": "
                                               << s.error_message());
    }
    else {
        RECC_LOG("Cancelled job " << operationName);
    }
}

void RemoteExecutionClient::write_files_to_disk(const ActionResult &result,
                                                const char *root)
{
    // Timed function
    reccmetrics::MetricGuard<reccmetrics::DurationMetricTimer> mt(
        TIMER_NAME_FETCH_WRITE_RESULTS, RECC_ENABLE_METRICS);

    for (const auto &fileIter : result.d_outputFiles) {
        const std::string path = std::string(root) + "/" + fileIter.first;
        RECC_LOG_VERBOSE("Writing " << path);
        write_file(path.c_str(), fetch_blob(fileIter.second.first));
        if (fileIter.second.second) {
            make_executable(path.c_str());
        }
    }
}

ActionResult
RemoteExecutionClient::from_proto(const proto::ActionResult &proto)
{
    ActionResult result;

    result.d_exitCode = proto.exit_code();
    result.d_stdOut = OutputBlob(proto.stdout_raw(), proto.stdout_digest());
    result.d_stdErr = OutputBlob(proto.stderr_raw(), proto.stderr_digest());

    for (int i = 0; i < proto.output_files_size(); ++i) {
        auto fileProto = proto.output_files(i);
        result.d_outputFiles[fileProto.path()] =
            std::pair<proto::Digest, bool>(fileProto.digest(),
                                           fileProto.is_executable());
    }

    for (int i = 0; i < proto.output_directories_size(); ++i) {
        const auto outputDirectoryProto = proto.output_directories(i);
        const auto tree =
            fetch_message<proto::Tree>(outputDirectoryProto.tree_digest());

        std::unordered_map<proto::Digest, proto::Directory> digestMap;
        for (int j = 0; j < tree.children_size(); ++j) {
            digestMap[make_digest(tree.children(j))] = tree.children(j);
        }
        add_from_directory(&result.d_outputFiles, tree.root(),
                           outputDirectoryProto.path() + "/", digestMap);
    }

    return result;
}

} // namespace recc
} // namespace BloombergLP
