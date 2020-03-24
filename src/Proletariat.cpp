#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>
#include <unistd.h>

#include "Lockfree.hpp"
#include "Constants.hpp"

template<typename T>
using Queue = Cutter::Lockfree::Queue<T>;

using work_t = std::function<void()>;

namespace Cutter {
namespace Proletariat {

Pool::Pool(int num_threads): 
    size(num_threads), 
    started_(std::atomic<bool>(false)),
    pad1{0},
    stopped_(std::atomic<bool>(false)),
    pad2{0},
    q(std::make_shared<Queue<work_t>>()),
    pool_(std::vector<std::thread>())
{}

Pool::~Pool(void) {
    if (!stopped_.load()) {
        this->stop();
    }
}

void Pool::stop(bool wait_for_complete) {
    if (wait_for_complete)
        while (!q->empty()) continue;

    stopped_ = true; // Send the signal to all the workers to pack it up
    for (auto& worker : pool_) worker.join();
}
    
void Pool::start(void) {
    for (int i = 0; i < size; ++i) {
        pool_.emplace_back([this] (void) noexcept {
            while (!this->stopped_.load()) {
                std::optional<work_t> task = this->q->dequeue();
                if (!this->stopped_.load() && task) {
                    (*task)();
                }
            }
            // Clean up any remaining hzd ptrs
            *Queue<work_t>::hptr_a = nullptr;
            *Queue<work_t>::hptr_b = nullptr;
            Queue<work_t>::scan(this->q->mempool->head());
        });
    }
    started_ = true;
}
    
// We assume that there is no return value from f, or that f itself is handling the results of it's own internal work
template<typename Func, typename... Args>
bool Pool::submit(Func&& f, Args&&... args) noexcept {
    using return_type = typename std::result_of<Func(Args...)>::type;

    // Idiot checks at compile and run-time
    static_assert(
        std::is_invocable<Func, Args...>::value, 
        "Error: Cannot submit non-callable object as work to thread pool."
    );
    if (not started_.load()) {
        std::cout << "Error: Cannot submit work. Threadpool not yet started!" << std::endl;
        return false;
    }
    // Make the task to execute and put it in the queue.
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(
            std::forward<Func>(f), 
            std::forward<Args>(args)...
        )
    );
    q->enqueue(
        [task] (void) noexcept {
            (*task)();
        }
    );
    return true;
}

} // end namespace Proletariat
} // end namespace Cutter
