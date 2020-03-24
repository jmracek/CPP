#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <future>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

#include "../src/Lockfree.hpp"

struct TestState {
    std::vector<int> elements_to_insert;
    bool empty;
    TestState(std::vector<int> elts, bool empt): elements_to_insert(elts), empty(empt) { }
};

struct EmptyQueue : public testing::TestWithParam<TestState> {
    std::shared_ptr<Cutter::Lockfree::Queue<int>> q;
    EmptyQueue() {
        q = std::make_shared<Cutter::Lockfree::Queue<int>>();
    }
};

struct QueueTest: public testing::TestWithParam<TestState> {
    
    std::shared_ptr<Cutter::Lockfree::Queue<int>> q;

    QueueTest() {
        auto elts = GetParam().elements_to_insert;
        q = std::make_shared<Cutter::Lockfree::Queue<int>>();
        for (auto& x : elts) {
            q->enqueue(x); 
        }
    }
};

TEST_P(QueueTest, CanEnqueueSingleThread) {
    auto state = GetParam();
    
    bool success = true;
    // Grab the first node after the sentinel
    Cutter::Lockfree::Node<int>* cur = q->begin();
    for (auto& x : state.elements_to_insert) {
        success &= (x == cur->value);
        cur = cur->next.load();
    }
    ASSERT_TRUE(success);
}

TEST_P(QueueTest, QueueIsEmpty) {
    auto state = GetParam();
    ASSERT_TRUE(state.empty == q->empty());
}

TEST_P(QueueTest, CanDequeueSingleThread) {
    auto state = GetParam();
    
    std::vector<int> output;
    while (!q->empty()) {
        auto elt = q->dequeue();
        if (elt != std::nullopt)
            output.push_back(*elt);
    }
    ASSERT_TRUE(std::equal(output.begin(), output.end(), state.elements_to_insert.begin()));
}

INSTANTIATE_TEST_SUITE_P(
    Default, 
    QueueTest,
    testing::Values(
        TestState{{}, true},
        TestState{{1, 2, 3}, false},
        TestState{{5,-2,4,8,9,10}, false},
        TestState{std::vector<int>(100000), false}
    )
);
