// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/fuzz/fuzz.h>

#include <netaddress.h>
#include <netbase.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <util/sock.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <signal.h>
#include <unistd.h>
#include <vector>

const std::function<void(const std::string&)> G_TEST_LOG_FUN{};

std::map<std::string_view, std::tuple<TypeTestOneInput, TypeInitialize, TypeHidden>>& FuzzTargets()
{
    static std::map<std::string_view, std::tuple<TypeTestOneInput, TypeInitialize, TypeHidden>> g_fuzz_targets;
    return g_fuzz_targets;
}

void FuzzFrameworkRegisterTarget(std::string_view name, TypeTestOneInput target, TypeInitialize init, TypeHidden hidden)
{
    const auto it_ins = FuzzTargets().try_emplace(name, std::move(target), std::move(init), hidden);
    Assert(it_ins.second);
}

static TypeTestOneInput* g_test_one_input{nullptr};

void initialize()
{
    // Terminate immediately if a fuzzing harness ever tries to create a TCP socket.
    CreateSock = [](const CService&) -> std::unique_ptr<Sock> { std::terminate(); };

    // Terminate immediately if a fuzzing harness ever tries to perform a DNS lookup.
    g_dns_lookup = [](const std::string& name, bool allow_lookup) {
        if (allow_lookup) {
            std::terminate();
        }
        return WrappedGetAddrInfo(name, false);
    };

    bool should_abort{false};
    if (std::getenv("PRINT_ALL_FUZZ_TARGETS_AND_ABORT")) {
        for (const auto& t : FuzzTargets()) {
            if (std::get<2>(t.second)) continue;
            std::cout << t.first << std::endl;
        }
        should_abort = true;
    }
    if (const char* out_path = std::getenv("WRITE_ALL_FUZZ_TARGETS_AND_ABORT")) {
        std::cout << "Writing all fuzz target names to '" << out_path << "'." << std::endl;
        std::ofstream out_stream(out_path, std::ios::binary);
        for (const auto& t : FuzzTargets()) {
            if (std::get<2>(t.second)) continue;
            out_stream << t.first << std::endl;
        }
        should_abort = true;
    }
    Assert(!should_abort);
    std::string_view fuzz_target{Assert(std::getenv("FUZZ"))};
    const auto it = FuzzTargets().find(fuzz_target);
    Assert(it != FuzzTargets().end());
    Assert(!g_test_one_input);
    g_test_one_input = &std::get<0>(it->second);
    std::get<1>(it->second)();
}

#if defined(PROVIDE_FUZZ_MAIN_FUNCTION)
static bool read_stdin(std::vector<uint8_t>& data)
{
    uint8_t buffer[1024];
    ssize_t length = 0;
    while ((length = read(STDIN_FILENO, buffer, 1024)) > 0) {
        data.insert(data.end(), buffer, buffer + length);
    }
    return length == 0;
}
#endif

#if defined(PROVIDE_FUZZ_MAIN_FUNCTION) && !defined(__AFL_LOOP)
static bool read_file(fs::path p, std::vector<uint8_t>& data)
{
    uint8_t buffer[1024];
    FILE* f = fsbridge::fopen(p.string(), "rb");
    if (f == nullptr) return false;
    do {
        const size_t length = fread(buffer, sizeof(uint8_t), sizeof(buffer), f);
        if (ferror(f)) return false;
        data.insert(data.end(), buffer, buffer + length);
    } while (!feof(f));
    fclose(f);
    return true;
}
#endif

#if defined(PROVIDE_FUZZ_MAIN_FUNCTION) && !defined(__AFL_LOOP)
fs::path g_seed_path;
void signal_handler(int signal)
{
    if (signal == SIGABRT) {
        std::cerr << "Error processing seed " << g_seed_path << std::endl;
    } else {
        std::cerr << "Unexpected signal " << signal << " received\n";
    }
    std::_Exit(EXIT_FAILURE);
}
#endif

// This function is used by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static const auto& test_one_input = *Assert(g_test_one_input);
    test_one_input({data, size});
    return 0;
}

// This function is used by libFuzzer
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    initialize();
    return 0;
}

#if defined(PROVIDE_FUZZ_MAIN_FUNCTION)
int main(int argc, char** argv)
{
    initialize();
    static const auto& test_one_input = *Assert(g_test_one_input);
#ifdef __AFL_INIT
    // Enable AFL deferred forkserver mode. Requires compilation using
    // afl-clang-fast++. See fuzzing.md for details.
    __AFL_INIT();
#endif

#ifdef __AFL_LOOP
    // Enable AFL persistent mode. Requires compilation using afl-clang-fast++.
    // See fuzzing.md for details.
    while (__AFL_LOOP(1000)) {
        std::vector<uint8_t> buffer;
        if (!read_stdin(buffer)) {
            continue;
        }
        test_one_input(buffer);
    }
#else
    std::vector<uint8_t> buffer;
    if (argc <= 1) {
        if (!read_stdin(buffer)) {
            return 0;
        }
        test_one_input(buffer);
        return 0;
    }
    signal(SIGABRT, signal_handler);
    int tested = 0;
    for (int i = 1; i < argc; ++i) {
        fs::path seed_path(*(argv + i));
        if (fs::is_directory(seed_path)) {
            for (fs::directory_iterator it(seed_path); it != fs::directory_iterator(); ++it) {
                if (!fs::is_regular_file(it->path())) continue;
                g_seed_path = it->path();
                Assert(read_file(it->path(), buffer));
                test_one_input(buffer);
                ++tested;
                buffer.clear();
            }
        } else {
            g_seed_path = seed_path;
            Assert(read_file(seed_path, buffer));
            test_one_input(buffer);
            ++tested;
            buffer.clear();
        }
    }
    std::cout << "tested " << tested << " files\n";
#endif
    return 0;
}
#endif
