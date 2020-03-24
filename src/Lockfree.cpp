#include <atomic>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Constants.hpp"

// FIXME: Actually properly implement this thing for use in <algorithm>
namespace Cutter {
namespace Lockfree {

/********* START HAZARD POINTER *********/

HzdMemPool::HzdMemPool(void):
    head_(std::atomic<Hzd*>(new Hzd())),
    len_(std::atomic<size_t>(0))
{}

HzdMemPool::~HzdMemPool(void) {
    Hzd* cur = head_.load(); // FIXME: CHECK WHETHER THIS SHOULD START AT HEAD???
    do {
        Hzd* next = cur->next();
        delete cur;
        cur = next;
    } while (cur != nullptr);
}

Hzd* HzdMemPool::alloc(void) {
    Hzd* p = head_.load();
    bool False = false;
    do {
        if (p->active_.load() or !std::atomic_compare_exchange_strong(&p->active_, &False, true))
            continue; 
        return p; 
    } while ((p = p->next()) != nullptr);

    size_t old_len;
    do {
        old_len = len_.load(); 
    } while (!std::atomic_compare_exchange_strong(&len_, &old_len, old_len + 1));

    p = new Hzd();
    // Insert p at the head of the list
    Hzd* old;
    do {
        old = head_.load();
        p->next_ = old;
    } while (!std::atomic_compare_exchange_strong(&head_, &old, p));

    return p;
}

void HzdMemPool::free(Hzd* p) {
    // This delete here is potentially problematic.
    **p = nullptr;
    p->active_ = false;
}

Hzd* HzdMemPool::head(void) {
    return head_.load();
}

size_t HzdMemPool::length(void) {
    return len_.load();
}

Hzd::Hzd(void):
    next_(std::atomic<Hzd *>(nullptr)),
    ptr_(std::atomic<void *>(nullptr)),
    active_(std::atomic<bool>(true))
{}


inline Hzd* Hzd::next(void) const {
    return next_.load();
}

inline std::atomic<void*>& Hzd::operator*(void) {
    return ptr_;
}

ThreadLocalHzdWrapper::ThreadLocalHzdWrapper(Hzd*& p):
    ptr(p)
{}

ThreadLocalHzdWrapper::ThreadLocalHzdWrapper(Hzd*&& p):
    ptr(nullptr) {
    std::swap(p, ptr);
}

ThreadLocalHzdWrapper& ThreadLocalHzdWrapper::operator= (Hzd*& p) {
    ptr = p;
    return *this;
}

ThreadLocalHzdWrapper& ThreadLocalHzdWrapper::operator= (Hzd*&& p) {
    std::swap(p, ptr);
    return *this;
}

inline Hzd* ThreadLocalHzdWrapper::operator->(void) {
    return ptr;
}

inline std::atomic<void*>& ThreadLocalHzdWrapper::operator*(void) {
    // First dereference gets us the Hzd object.
    // Second dereference is the overload getting us the void*&
    return **ptr;
}

ThreadLocalHzdWrapper::~ThreadLocalHzdWrapper(void) {
    HzdMemPool::free(ptr); 
}

/********* START NODE  *********/

template<typename T>
Node<T>::Node(void):
    value(T()),
    next(std::atomic<Node<T>*>(nullptr))
{}

template<typename T>
template<typename S>
Node<T>::Node(S&& val): 
    value(std::forward<S>(val)),
    next(std::atomic<Node<T>*>(nullptr))
{}

template<typename T>
inline Node<T>& Node<T>::operator++ (void) {
    return *(next.load());
}

template<typename T>
inline Node<T> Node<T>::operator++ (int) {
    Node<T> temp = *this;
    ++*this;
    return temp;
}

template<typename T>
inline bool Node<T>::operator== (const Node<T>& other) const {
    return &(this->value) == &(other.value);
}

template<typename T>
inline bool Node<T>::operator!= (const Node<T>& other) const {
    return not (other == *this);
}

template<typename T>
inline T& Node<T>::operator* (void) {
    return value;
}

template<typename T>
inline T* Node<T>::operator-> (void) {
    return &value;
}

/********* END NODE *********/

/********* START QUEUE *********/

   
template<typename T>
thread_local ThreadLocalHzdWrapper Queue<T>::hptr_a = mempool->alloc();

template<typename T>
thread_local ThreadLocalHzdWrapper Queue<T>::hptr_b = mempool->alloc();

template<typename T>
thread_local std::vector<Node<T>*> Queue<T>::rlist = std::vector<Node<T>*>();

template<typename T>
size_t Queue<T>::MAX_RLIST_SIZE(void) { 
    return static_cast<size_t>(Cutter::Const::RLIST_SCALE_FACTOR * mempool->length());
}

template<typename T>
Queue<T>::Queue(void):
    head(std::atomic<Node<T>*>(new Node<T>())),
    tail(std::atomic<Node<T>*>(head.load())),
    size_(std::atomic<size_t>(0))
{}

// Destructor: Traverse the list and remove any remaining nodes
template<typename T>
Queue<T>::~Queue(void) {
    Node<T>* cur = head.load(); // FIXME: CHECK WHETHER THIS SHOULD START AT HEAD???
    do {
        Node<T>* next = cur->next.load();
        delete cur;
        cur = next;
    } while (cur != nullptr);
}

template<typename T>
inline bool Queue<T>::empty(void) const noexcept {
    // Check whether the head and tail ptrs both point to the same dummy node
    return size_.load() == 0;
}

// FIXME: SFINAE to enforce that types T and S are related by declval
template<typename T> 
template<typename S>
void Queue<T>::enqueue(S&& value) noexcept {
    // This gets deleted in dequeue.  The only thread owning value should be the one performing
    // the enqueue, so there should not be issues with memory access here.
    auto node = new Node<T>(std::forward<S>(value));  
    
    Node<T>* next, *back;
    while(true) {
        back = tail.load();
        *hptr_a = back;
        if (tail.load() != back) 
            continue;
        // BEGIN HAZARDOUS SECTION // 
        next = back->next.load();
        if (back != tail.load()) 
            continue;
        if (next != nullptr) { 
            std::atomic_compare_exchange_weak(&tail, &back, next);
            continue;
        }
        if (std::atomic_compare_exchange_weak(&back->next, &next, node))
            break;
    }
    std::atomic_compare_exchange_strong(&tail, &back, node);
    // END HAZARDOUS SECTION // 
    ++size_;
    return;
}

template<typename T>
std::optional<T> Queue<T>::dequeue(void) noexcept {
    T out;
    Node<T>* next, *front, *back;
    while(true) {
        front = head.load();
        // BEGIN HAZARDOUS SECTION //
        *hptr_a = front;
        if (front != head.load()) continue;
        back = tail.load();
        next = front->next.load();
        *hptr_b = next;
        if (front != head.load()) continue;
        if (next == nullptr) return {}; // Queue is empty
        if (front == back) {
            std::atomic_compare_exchange_weak(&tail, &back, next);
            continue;
        }
        
        out = next->value;   // TODO: Figure out how to convert this back to move semantics without segfaulting
        if (std::atomic_compare_exchange_weak(&head, &front, next)) {
            break;
        }
    }
    // END HAZARDOUS SECTION //
    Queue<T>::retire(front); // TODO: Look into whether a custom allocator might be useful here so that we can re-use objects
    --size_;
    return out;
}

template<typename T>
size_t Queue<T>::size(void) const {
    return size_.load();
}

template<typename T>
typename Queue<T>::iterator Queue<T>::begin(void) noexcept {
    // Skip the sentinel
    return head.load()->next.load();
}

// FIXME: This needs to be made to agree with STL convention that the end() is 1 element past the last "real" element
template<typename T>
typename Queue<T>::iterator Queue<T>::end(void) noexcept {
    return tail.load();
}

template<typename T>
void Queue<T>::scan(Hzd* head) {
    // Make a set out of all hazard pointers which are currently in use.
    std::unordered_set<void *> active_hazards;
    void* p;
    do {
        if ((p = **head) != nullptr)
            active_hazards.insert(p);
    } while ((head = head->next()) != nullptr);
    
    // For each retired node, check whether it's an active hazard.  
    // If it isn't, then free the node. If is it, then check the next.
    auto it = rlist.begin();
    while (it != rlist.end()) {
        if (active_hazards.find(static_cast<void*>(*it)) == active_hazards.end()) {
            delete *it;
            if (&*it != &rlist.back()) {
                *it = rlist.back();
            }
            rlist.pop_back();
        }
        else {
            ++it;
        }
    }
}

template<typename T>
void Queue<T>::retire(Node<T>* node) {
    rlist.push_back(node);
    if (rlist.size() >= MAX_RLIST_SIZE()) {
        Queue<T>::scan(mempool->head());
    }
}

/*
//
// BEGIN RINGBUFER
//
template<typename T, size_t MAX_SIZE>
RingBuffer<T, MAX_SIZE>::RingBuffer(void) {
    memset(static_cast<void*>(buff), 0, MAX_SIZE * sizeof(T));
}

// To turn % into a bitwise operator, we must have MAX_SIZE being a power of two
template<typename T, size_t MAX_SIZE>
inline RingBuffer<T, MAX_SIZE>::idx(Index i) noexcept {
    return i & (MAX_SIZE - 1);
}

template<typename T, size_t MAX_SIZE>
template<typename S>
inline void RingBuffer<T, MAX_SIZE>::enqueue(S&& item) noexcept {
    Index ticket = eq_ticker_.fetch_add(1);
    buff[idx(ticket)] = std::forward<S>(item);
}

// How do I check whether the buffer is empty?
template<typename T, size_t MAX_SIZE>
inline std::optional<T> RingBuffer<T, MAX_SIZE>::dequeue(void) noexcept {
    Index write_idx = eq_ticker_.load();
    if (std::atomic_compare_exchange_strong(&dq_ticker_, &write_idx, 
    Index read_idx = dq_ticker_.load();
    
    if (write_idx == eq_
    
}
*/
} // end namespace Lockfree
} // end namespace Cutter
