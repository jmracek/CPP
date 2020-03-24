#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <future>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

#include "../src/Memory.hpp"
#include "../src/Proletariat.hpp"

// UNIT TESTS
// - Block swap (full block + 1 alloc): DONE
// - Subsequent allocs are located contiguously in memory: DONE
// - Alloc when no free: DONE
// - Alloc from free: DONE
// - Alloc with inplace construction: DONE
// - Full multithreaded allocation, starting from empty or full block

// Fixtures:
// - Empty block
// - Full block

namespace Cutter::Memory {

struct TestState {
    int n_allocs;
    TestState(int n): n_allocs(n) { }
};

struct ObjectPoolTest: public testing::TestWithParam<TestState> {
    std::vector<int*> obj_ptrs;
    std::shared_ptr<ObjectPool<int>> q;
    ObjectPoolTest() {
        int n = GetParam().n_allocs;
        q = std::make_shared<ObjectPool<int>>();
        for (int j = 0; j < n; ++j) {
            obj_ptrs.push_back(q->alloc()); 
        }
    }
};

TEST_P(ObjectPoolTest, ContiguousAllocationFromBuffer) {
    int loopsize = std::min(Cutter::Const::DEFAULT_BUFFER_SIZE, obj_ptrs.size());
    bool success = true;
    for (int i = 0; i < loopsize - 1; ++i) {
        success &= std::distance(obj_ptrs[i], obj_ptrs[i + 1]) == 1;
    } 
    ASSERT_TRUE(success);
}

TEST(ObjectPoolTest, BlockSwap) {
    ObjectPool<int> pool;
    std::vector<int *> ptrs;

    // Record where the block end is at the start.
    int* last = pool.last_;
    // Allocate the whole block.
    for (int i = 0; i < Cutter::Const::DEFAULT_BUFFER_SIZE; ++i) {
        // The pointer to the last element should not change while we're allocating within a block
        ASSERT_EQ(last, pool.last_);
        ptrs.emplace_back(pool.alloc()); 
    }
    
    // We're now pulling from memory in a new block.
    ASSERT_NE(last, pool.last_);
    // Check that the new block holds space for the correct number of objects.
    ASSERT_TRUE(std::next(pool.current_.load(), Cutter::Const::DEFAULT_BUFFER_SIZE - 1) == pool.last_);
}

TEST(ObjectPoolTest, CanAllocFromFree) {
    ObjectPool<int> pool;
    
    int* ptr = pool.alloc();
    pool.free(ptr);
    int* ptr2 = pool.alloc();
    ASSERT_EQ(ptr, ptr2);
}

TEST(ObjectPoolTest, AllocWithInPlaceConstruction) {
    ObjectPool<int> int_pool;
    ObjectPool<std::string> str_pool;
    
    int* ptr = int_pool.alloc(150);
    std::string* ptr2 = str_pool.alloc("The quick brown fox jumped over the lazy dog");
    ASSERT_EQ(*ptr, 150);
    ASSERT_EQ(*ptr2, "The quick brown fox jumped over the lazy dog");
}

TEST(ObjectPoolTest, MultithreadedAllocFromOneBuffer) {
    auto obj_mgr = std::make_shared<ObjectPool<int>>();
    auto q = std::make_shared<Cutter::Lockfree::Queue<int *>>();
    Cutter::Proletariat::Pool pool(16);
    
    std::vector<int*> expected_results;
    std::vector<int*> results;

    int* first = obj_mgr->current_.load();
    for (int i = 0; i < Cutter::Const::DEFAULT_BUFFER_SIZE; ++i) {
        expected_results.push_back(std::next(first, i));
    }

    auto thread_task = [q, obj_mgr](void) {
        q->enqueue(obj_mgr->alloc());
    };
    
    // Use the pool to allocate an entire buffer's worth of objects
    pool.start();
    for (int i = 0; i < Cutter::Const::DEFAULT_BUFFER_SIZE; ++i) {
        pool.submit(thread_task);
    }
    // Sit back and collect results.
    while (results.size() < Cutter::Const::DEFAULT_BUFFER_SIZE) {
        auto maybe_ptr = q->dequeue();
        if (maybe_ptr.has_value()) 
            results.push_back(*maybe_ptr);
    }
    pool.stop();
    
    std::sort(results.begin(), results.end());

    for (int i = 0; i < Cutter::Const::DEFAULT_BUFFER_SIZE; ++i) {
        if (results[i] != expected_results[i]) {
            std::cout << results[i] << std::endl;
            std::cout << expected_results[i] << std::endl;
        }
    }
    
    ASSERT_EQ(results, expected_results);
}

TEST(ObjectPoolTest, MultithreadedAllocMultiBuffer) {
    auto obj_mgr = std::make_shared<ObjectPool<int>>();
    auto q = std::make_shared<Cutter::Lockfree::Queue<int *>>();
    auto thread_task = [q, obj_mgr](void) {
        q->enqueue(obj_mgr->alloc());
    };
    
    Cutter::Proletariat::Pool pool(16);
    
    std::vector<int*> expected_results;
    std::vector<int*> results;
     
    // Use the pool to allocate an entire buffer's worth of objects
    pool.start();
    for (int i = 0; i < 2 * Cutter::Const::DEFAULT_BUFFER_SIZE; ++i) {
        pool.submit(thread_task);
    }
    // Sit back and collect results.
    while (results.size() < 2 * Cutter::Const::DEFAULT_BUFFER_SIZE) {
        auto maybe_ptr = q->dequeue();
        if (maybe_ptr.has_value()) 
            results.push_back(*maybe_ptr);
    }
    pool.stop();
    
    for (int j = 0; j < 2; ++j) {
        int* first =  obj_mgr->blocks_[j];
        for (int i = 0; i < Cutter::Const::DEFAULT_BUFFER_SIZE; ++i) {
            expected_results.push_back(std::next(first, i));
        }
    }
    

    std::sort(results.begin(), results.end());
    std::sort(expected_results.begin(), expected_results.end());
    std::cout << "Results size: " << results.size() << std::endl; 
    std::cout << "Expected results size: " << expected_results.size() << std::endl; 
    for (int i = 0; i < 2 * Cutter::Const::DEFAULT_BUFFER_SIZE; ++i) {
        if (results[i] != expected_results[i]) {
            std::cout << "Result differs from expected at index: " << i << std::endl;
            std::cout << "Result Ptr: " << results[i] << std::endl;
            std::cout << "Expected Ptr: " << expected_results[i] << std::endl;
        }
    }
    ASSERT_EQ(results, expected_results);
}
INSTANTIATE_TEST_SUITE_P(
    Default, 
    ObjectPoolTest,
    testing::Values(
        TestState{0},
        TestState{5},
        TestState{Cutter::Const::DEFAULT_BUFFER_SIZE},
        TestState{Cutter::Const::DEFAULT_BUFFER_SIZE + 1},
        TestState{Cutter::Const::DEFAULT_BUFFER_SIZE + 100}
    )
);

}
