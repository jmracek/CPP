#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#ifndef LOCKFREEQUEUE_HPP
#define LOCKFREEQUEUE_HPP

namespace Cutter {
namespace Lockfree {

constexpr int CACHE_LINE_SIZE = 64;

class Hzd;

class HzdMemPool {
private:
    std::atomic<Hzd*> head_;
    std::atomic<size_t> len_;
public:
    Hzd* alloc(void);
    static void free(Hzd *);
    Hzd* head(void);
    size_t length(void);
    
    HzdMemPool(void);
    ~HzdMemPool(void);
};

class Hzd {
private:
    // TODO: Separate these onto different cache lines
    std::atomic<Hzd*> next_;
    std::atomic<void*> ptr_;
    std::atomic<bool> active_;
    friend class HzdMemPool;
public:
    Hzd(void);
    inline Hzd* next(void) const;
    inline std::atomic<void*>& operator*(void);
};

// This class simply takes care of thread local cleanup of hazard pointers.  
class ThreadLocalHzdWrapper {
    Hzd* ptr;
public:
    ThreadLocalHzdWrapper(Hzd*&);
    ThreadLocalHzdWrapper(Hzd*&&);
    ~ThreadLocalHzdWrapper(void);
    ThreadLocalHzdWrapper& operator= (Hzd*&);
    ThreadLocalHzdWrapper& operator= (Hzd*&&);
    inline Hzd* operator->(void);
    inline std::atomic<void*>& operator*(void);
};

template<typename T>
struct Node {
    typedef T value_type;
    
    // Consider adding a "taken" flag to enable move semantics
    T value;
    std::atomic<Node<T> *> next;
    
    Node(void);
    template<typename S> Node(S&&);

    inline Node<T>& operator++ (void);
    inline Node<T> operator++ (int);
    inline bool operator== (const Node<T>&) const;
    inline bool operator!= (const Node<T>&) const;
    inline T& operator* (void);
    inline T* operator-> (void);
};

// This class is a multi producer, multi consumer lock free queue.  It is a C++ adaptation of the Michael-Scott queue (Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms)
template<typename T>
class Queue {
private:
    // The padding is to put the head and tail on different cache lines so that there is no contention between producers and consumers.
    std::atomic<Node<T>*> head;
    char paddingOne[CACHE_LINE_SIZE - sizeof(std::atomic<Node<T> *>)];
    
    std::atomic<Node<T>*> tail;
    char paddingTwo[CACHE_LINE_SIZE - sizeof(std::atomic<Node<T> *>)];
    
    std::atomic<size_t> size_;

    static void retire(Node<T>*);
public:
    using iterator = Node<T>*;
    Queue(void);
    ~Queue(void);
    
    inline bool empty(void) const noexcept;
    template<typename S>
    void enqueue(S&& value) noexcept;
    std::optional<T> dequeue(void) noexcept;
    iterator begin(void) noexcept;
    iterator end(void) noexcept;
    size_t size(void) const;
    static inline std::unique_ptr<HzdMemPool> mempool = std::make_unique<HzdMemPool>();
    thread_local static ThreadLocalHzdWrapper hptr_a;
    thread_local static ThreadLocalHzdWrapper hptr_b;
    thread_local static std::vector<Node<T>*> rlist;
    static size_t MAX_RLIST_SIZE(void);
    static void scan(Hzd*);
};

constexpr bool isPowerOfTwo(size_t v) {
    return v && ((v & (v - 1)) == 0);
}

// MAX SIZE MUST BE A POWER OF TWO
template<typename T, size_t MAX_SIZE = 4096>
class RingBuffer {
    static_assert(
        isPowerOfTwo(MAX_SIZE), 
        "Ring buffer must have max size a power of two."
    ); 
private:
    using Index = uint64_t;
    T buff[MAX_SIZE];
    std::atomic<Index> eq_ticker_;
    std::atomic<Index> dq_ticker_;

    inline uint64_t idx(Index) noexcept;
public:
    static constexpr size_t max_size = MAX_SIZE;
    RingBuffer(void);
    template<typename S> inline void enqueue(S&&) noexcept;
    inline std::optional<T> dequeue(void) noexcept;
    bool empty(void) const;
};

} // end namespace Lockfree
} // end namespace Cutter

#include "Lockfree.cpp"

#endif
