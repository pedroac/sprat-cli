#pragma once

#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace sprat::core {

// Runs work(i) for every i in [0, count): worker 0 on the calling thread, the others on new
// threads. A thread that can't be started (thread limit reached) is run on the calling thread
// instead, so no work is lost. Every started thread is joined before returning, and the first
// exception any worker threw (e.g. std::bad_alloc) is rethrown on the calling thread; an
// exception escaping a std::thread, or a joinable thread being destroyed, would otherwise
// call std::terminate.
template <typename Work>
void run_workers(unsigned int count, const Work& work) {
    std::exception_ptr first_error;
    std::mutex error_mutex;
    const auto guarded = [&](unsigned int index) {
        try {
            work(index);
        } catch (...) {
            const std::scoped_lock lock(error_mutex);
            if (!first_error) {
                first_error = std::current_exception();
            }
        }
    };

    std::vector<std::thread> threads;
    std::vector<unsigned int> not_started;
    for (unsigned int index = 1; index < count; ++index) {
        try {
            threads.emplace_back(guarded, index);
        } catch (...) {  // std::system_error (no more threads) or std::bad_alloc
            not_started.push_back(index);
        }
    }
    if (count > 0) {
        guarded(0);
    }
    for (const unsigned int index : not_started) {
        guarded(index);
    }
    for (auto& thread : threads) {
        thread.join();
    }
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

} // namespace sprat::core
