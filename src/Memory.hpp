#ifndef MEMORY_HPP 
#define MEMORY_HPP 

#include <atomic>
#include <future>
#include <memory>
#include <vector>

#include <gtest/gtest_prod.h>

#include "Lockfree.hpp"
#include "Constants.hpp"

namespace Cutter {
namespace Memory {

template<typename T>
class ObjectPool {
private:
    FRIEND_TEST(ObjectPoolTest, BlockSwap);
    FRIEND_TEST(ObjectPoolTest, MultithreadedAllocFromOneBuffer);
    FRIEND_TEST(ObjectPoolTest, MultithreadedAllocMultiBuffer);

    std::vector<T*> blocks_;
    alignas(64) std::atomic<T*> current_;
    char pad[Cutter::Const::CACHE_LINE_SIZE - sizeof(std::atomic<T*>)];
    
    T* last_;
    std::future<T*> next_;
    Cutter::Lockfree::Queue<T*> free_;

    inline T* getPtrFromBuffer(void);
    inline bool noFreePtrsAvail(void);

public:
    ObjectPool(void);
    ~ObjectPool(void);
    T* alloc(void);
    template<typename... Args> T* alloc(Args&&...);
    void free(T* obj);
    void clean(T* obj);
};

}
}

#include "Memory.cpp"

#endif

