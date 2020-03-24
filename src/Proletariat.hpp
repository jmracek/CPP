#ifndef PROLETARIAT_HPP
#define PROLETARIAT_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "Lockfree.hpp"
#include "Constants.hpp"

template<typename T>
using Queue = Cutter::Lockfree::Queue<T>;

namespace Cutter {
namespace Proletariat {

using work_t = std::function<void()>;

class Pool {
public:
    const int size;
    Pool(int num_threads);
    ~Pool(void);

    // Delete copy and assignment operators
    Pool(const Pool&) = delete;
    Pool& operator= (const Pool&) = delete;

    void stop(bool = false);
    void start(void);
     
    template<typename Func, typename... Args>
    bool submit(Func&& f, Args&&... args) noexcept;

private:
    std::atomic<bool> started_;
    char pad1[Cutter::Const::CACHE_LINE_SIZE - sizeof(std::atomic<bool>)];
    std::atomic<bool> stopped_;
    char pad2[Cutter::Const::CACHE_LINE_SIZE - sizeof(std::atomic<bool>)];
    // Here I want to make sure that the queue and the stopped_ controller are on different cache lines
    std::shared_ptr<Queue<work_t>> q;
    std::vector<std::thread> pool_;
};

}
}

#include "Proletariat.cpp"

#endif
