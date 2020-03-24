#include <atomic>

#include "Lockfree.hpp"
#include "Memory.hpp"

namespace Cutter {
namespace Plumbing {

////// PIPE ///////
template<typename T>
Pipe<T>::Pipe(void):
    flow(Cutter::Lockfree::Queue<T*>()),
    obj_mgr(Cutter::Memory::ObjectPool<T>()) 
{}

////// JOINT ///////
template<typename Derived>
Joint<Derived>::Joint(void):
    counter_(std::atomic<size_t>(0))
{}

template<typename Derived>
Joint<Derived>::Joint(const Joint<Derived>& other) {
    counter_ = other.counter_.load();
}

template<typename Derived>
Joint<Derived>::Joint(Joint<Derived>&& other) {
    counter_ = other.counter_.load();
}

template<typename Derived>
auto& Joint<Derived>::operator= (const Joint& other) {
    counter_ = other.counter_.load();
    return *this;
}

template<typename Derived>
auto& Joint<Derived>::operator= (Joint&& temp) {
    if (this != &temp) {
        counter_ = temp.counter_.load();
    }
    return *this;
}

template<typename Derived>
inline bool Joint<Derived>::ready(void) {
    return static_cast<Derived *>(this)->ready_impl();
}

template<typename Derived>
inline size_t Joint<Derived>::size(void) {
    return counter_.load();
}

// Returns true if derived class successfully found work to do
template<typename Derived>
inline bool Joint<Derived>::work(void) {
    return static_cast<Derived *>(this)->work_impl();
}

////// SOURCE ///////
template<template<class> class Derived, class out_t>
Source<Derived, out_t>::Source(const std::vector<std::string>& file_names):
    Joint<Derived<out_t>>(),
    downstream_(nullptr),
    files_(file_names),
    name("Source") {
    for (const auto& fname : file_names) {
        fnames_.enqueue(fname);
    }
}

template<template<class> class Derived, class out_t>
Source<Derived, out_t>::Source(const Source<Derived, out_t>& other):
  Joint<Derived<out_t>>(other) {
    name = other.name;
    downstream_ = other.downstream_;
    files_ = other.files_;
    for (const auto& fname : files_) {
        fnames_.enqueue(fname);
    }
}

template<template<class> class Derived, class out_t>
Source<Derived, out_t>::Source(Source<Derived, out_t>&& other):
  Joint<Derived<out_t>>(std::move(other)) {
    downstream_.reset();
    downstream_ = std::move(other.downstream_);
    name = std::move(other.name);
    while (!other.fnames_.empty()) {
        fnames_.enqueue(*other.fnames_.dequeue());
    }
}

template<template<class> class Derived, class out_t>
auto& Source<Derived, out_t>::operator= (const Source<Derived, out_t>& other) {
    downstream_ = other.downstream_;
    name = other.name;
    files_ = other.files_;
    for (const auto& fname : files_) {
        fnames_.enqueue(fname);
    }
    return *this;
}

template<template<class> class Derived, class out_t>
auto& Source<Derived, out_t>::operator= (Source<Derived, out_t>&& temp) {
    if (this != &temp) {
        downstream_.reset();
        downstream_ = std::move(temp.downstream_);
        name = std::move(temp.name);
        while (!temp.fnames_.empty()) {
            fnames_.enqueue(*temp.fnames_.dequeue());
        }
    }
    return *this;
}

template<template<class> class Derived, class out_t>
inline bool Source<Derived, out_t>::ready_impl(void) {
    return !fnames_.empty();
}

template<template<class> class Derived, class out_t>
inline bool Source<Derived, out_t>::work_impl(void) {
    return static_cast<Derived<out_t>*>(this)->extract();
}

// TEMPORARY FIXME REMOVE
template<template<class> class Derived, class out_t>
inline void Source<Derived, out_t>::setDownstream(std::shared_ptr<Pipe<out_t>> ds) {
    downstream_ = ds;
}

////// TRANSFORM ///////
template<typename F, typename Condition>
Transform<F, Condition>::Transform(F t_func): 
    Joint<Transform<F>>(),
    task_(t_func),
    upstream_(nullptr),
    downstream_(nullptr)
{}

template<typename F, typename Condition>
Transform<F, Condition>::Transform(const Transform<F, void>& other):
  Joint<Transform<F>>(other) {
    downstream_ = other.downstream_;
    upstream_ =  other.upstream_;
    task_ = other.task_;
}

template<typename F, typename Condition>
Transform<F, Condition>::Transform(Transform<F, void>&& other):
    task_(std::move(other.task_))
{}

template<typename F, typename Condition>
inline bool Transform<F, Condition>::ready_impl() {
    return !upstream_->empty();
}

template<typename F, typename Condition>
auto& Transform<F, Condition>::operator= (Transform<F, Condition>&& temp) {
    if (this != &temp) {
        task_ = std::move(temp.task_);
        upstream_.reset();
        downstream_.reset();
        upstream_ = std::move(temp.upstream_);
        downstream_ = std::move(temp.downstream_);
    }
    return *this;
}

template<typename F, typename Condition>
inline bool Transform<F, Condition>::work_impl() {
    if (upstream_ == nullptr) {
        std::cout << "Error: Cannot transform from null upstream queue!" << std::endl;
        exit(1);
    }
    if (downstream_ == nullptr) {
        std::cout << "Error: Cannot transform with no downstream queue!" << std::endl;
        exit(1);
    }
/* FIXME
    out_t* out = mgr_.alloc(task_(*input));
    out_->enqueue(out);
    prev_->mgr_.free(input);
*/
    return true;
}

template<typename F, typename Condition>
inline void Transform<F, Condition>::setDownstream(std::shared_ptr<Pipe<output_type>> ds) {
    downstream_ = ds;
}

template<typename F, typename Condition>
inline void Transform<F, Condition>::setUpstream(std::shared_ptr<Pipe<input_type>> us) {
    upstream_ = us;
}


template<typename T>
Transform<T, std::enable_if_t<has_call_operator<T>::value>>::Transform(T t_func): 
    Joint<Transform<T>>(),
    task_(t_func),
    upstream_(nullptr),
    downstream_(nullptr)
{}

template<typename T>
Transform<T, std::enable_if_t<has_call_operator<T>::value>>::Transform(const Transform<T, void>& other):
  Joint<Transform<T>>(other) {
    downstream_ = other.downstream_;
    upstream_ =  other.upstream_;
    task_ = other.task_;
}

template<typename T>
Transform<T, std::enable_if_t<has_call_operator<T>::value>>::Transform(Transform<T, void>&& other):
    task_(std::move(other.task_))
{}

template<typename T>
inline bool Transform<T, std::enable_if_t<has_call_operator<T>::value>>::ready_impl() {
    return !upstream_->flow.empty();
}

template<typename T>
auto& Transform<T, std::enable_if_t<has_call_operator<T>::value>>::operator= (Transform<T, void>&& temp) {
    if (this != &temp) {
        task_ = std::move(temp.task_);
        upstream_.reset();
        downstream_.reset();
        upstream_ = std::move(temp.upstream_);
        downstream_ = std::move(temp.downstream_);
    }
    return *this;
}

template<typename T>
inline void Transform<T, std::enable_if_t<has_call_operator<T>::value>>::work_impl() {
    if (upstream_ == nullptr) {
        std::cout << "Error: Cannot transform from null upstream queue!" << std::endl;
        exit(1);
    }
    if (downstream_ == nullptr) {
        std::cout << "Error: Cannot transform with no downstream queue!" << std::endl;
        exit(1);
    }
/* FIXME
    out_t* out = mgr_.alloc(task_(*input));
    out_->enqueue(out);
    prev_->mgr_.free(input);
*/
    return;
}

template<typename T>
inline void Transform<T, std::enable_if_t<has_call_operator<T>::value>>::setDownstream(std::shared_ptr<Pipe<output_type>> ds) {
    downstream_ = ds;
}

template<typename T>
inline void Transform<T, std::enable_if_t<has_call_operator<T>::value>>::setUpstream(std::shared_ptr<Pipe<input_type>> us) {
    upstream_ = us;
}

////// SINK ///////
template<template<class> typename Derived, class in_t>
Sink<Derived, in_t>::Sink(void):
    Joint<Derived<in_t>>(),
    upstream_(nullptr),
    name("Sink")
{}

template<template<class> typename Derived, class in_t>
Sink<Derived, in_t>::Sink(const Sink<Derived, in_t>& other) {
    upstream_ = other.upstream_;
    name = other.name;
}

template<template<class> typename Derived, class in_t>
Sink<Derived, in_t>::Sink(Sink<Derived, in_t>&& other) {
    upstream_ = std::move(other.upstream_);
    name = std::move(other.name);
}

template<template<class> typename Derived, class in_t>
auto& Sink<Derived, in_t>::operator= (Sink<Derived, in_t>&& temp) {
    if (this != &temp) {
        upstream_.reset();
        upstream_ = std::move(temp.upstream_);
        name = std::move(temp.name);
    }
    return *this;
}
    
template<template<class> typename Derived, class in_t>
inline void Sink<Derived, in_t>::work_impl(void) {
    return static_cast<Derived<in_t>*>(this)->load();
}

template<template<class> typename Derived, class in_t>
inline bool Sink<Derived, in_t>::ready_impl(void) {
    return !upstream_->flow.empty(); 
}

template<template<class> class Derived, class in_t>
inline void Sink<Derived, in_t>::setUpstream(std::shared_ptr<Pipe<in_t>> ds) {
    upstream_ = ds;
}

} // end namespace Plumbing 
} // end namespace Cutter
