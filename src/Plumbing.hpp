// Take care, all who wander inward; for here, be dragons.

#ifndef PLUMBING_HPP
#define PLUMBING_HPP

#include <memory>
#include <string>
#include <type_traits> // Hoo boy.  Here we go...
#include <tuple>
#include <utility>
#include <vector>

#include "Constants.hpp"
#include "IO.hpp"
#include "Lockfree.hpp"
#include "Memory.hpp"

namespace Cutter {
namespace Plumbing {

template<typename R>
struct function_traits: function_traits<decltype(&R::operator())> {};

// Specialization for plain old functions
template<typename R, typename... Args>
struct function_traits<R(Args...)> {
    using return_type = R;
    static constexpr size_t nargs = sizeof...(Args);

    template<size_t N>
    struct arg {
        static_assert(N < nargs, "Error: Invalid argument index");
        using type = typename std::tuple_element<N, std::tuple<Args...>>::type;
    };
};

// function pointer
template<class R, class... Args>
struct function_traits<R(*)(Args...)> : public function_traits<R(Args...)>
{};

// member function pointer
template<class C, class R, class... Args>
struct function_traits<R(C::*)(Args...)> : public function_traits<R(C&,Args...)>
{};
 
// const member function pointer
template<class C, class R, class... Args>
struct function_traits<R(C::*)(Args...) const> : public function_traits<R(C&,Args...)>
{};
 
// member object pointer
template<class C, class R>
struct function_traits<R(C::*)> : public function_traits<R(C&)>
{};

template<typename T, typename Condition = void>
struct has_call_operator: std::false_type {};

template<typename T>
struct has_call_operator<T, std::void_t< decltype(&T::operator()) >>: std::true_type {};

// A "Pipe" will connect joints.  It controls the flow of data between each task.
template<typename T>
struct Pipe {
    using type = T;
    Cutter::Lockfree::Queue<T*> flow;
    Cutter::Memory::ObjectPool<T> obj_mgr;
    Pipe(void);
};

// This will serve as a base class template for all the types of segments we'll deal with (Source, Transform, Sink).
// The polymorphism is necessary because we need to build objects into a common pipeline and retrieve their
// work while knowing only the base class.  We will choose static polymorphism over runtime polymorpishm in
// order to avoid the runtime overhead of vtable lookups. The static polymorphism is implemented using the 
// curiously recurring template pattern. For more details, see here: 
// https://eli.thegreenplace.net/2011/05/17/the-curiously-recurring-template-pattern-in-c
template<typename Derived>
class Joint {
protected:
    // For tracking work done and data throughput
    std::atomic<size_t> counter_; 
public:
    Joint(void);

    Joint(const Joint<Derived>& other);
    Joint(Joint<Derived>&& other);
    auto& operator= (const Joint<Derived>& other);
    auto& operator= (Joint<Derived>&& temp);

    inline bool ready(void);
    inline size_t size(void);
    inline bool work(void); // This is the function that threads will call in order to compute the work at a joint.
};

// A "Source" is a Joint that produces data.  The specific way that data is produced is not determined by
// this particular class, but instead by classes which derive Source.  We accept a template template "Derived"
// as a template argument to Source.  The reason for this is the particular manner in which the derived class
// produces elements of type out_t depends on the output type.  For example, we will support a template
// Derived = AWS that can stream elements of various types from files stored on S3.
// Types of Sources that will derive this class:
// 1) Local: For streaming files from disk.
// 2) AWS: For streaming files from AWS.
// 3) Kubernetes? Azure (Ew.)? Kafka? Kinesis? Directly over TCP?
template<template<class> class Derived, class out_t>
class Source: public Joint<Derived<out_t>> {
protected:
    using P = Cutter::IO::Parser<out_t>;
    std::shared_ptr<Pipe<out_t>> downstream_;
    std::vector<std::string> files_;
    Cutter::Lockfree::Queue<std::string> fnames_; 
public:
    std::string name;
    using output_type = out_t;
    Source(const std::vector<std::string>&);
    
    Source(const Source<Derived, out_t>&);
    Source(Source<Derived, out_t>&&);

    auto& operator= (const Source<Derived, out_t>& other);
    auto& operator= (Source<Derived, out_t>&& temp);

    // FIXME: Allow different batch sizes!
    inline bool ready_impl(void);
    inline bool work_impl(void);
    inline void setDownstream(std::shared_ptr<Pipe<out_t>>);
};

// Apply a function to objects of some type, producing objects of a new type.  The general template supports
// the case where F is a general function or function pointer.  A specialization will handle the case that
// F is a class with operator() defined.
template <typename F, typename Condition = void>
class Transform: public Joint<Transform<F>> {
private:
    F task_;
public: 
    using output_type = typename function_traits<F>::return_type;
    using input_type = typename function_traits<F>::template arg<0>::type;
private:
    std::shared_ptr<Pipe<input_type>> upstream_;
    std::shared_ptr<Pipe<output_type>> downstream_;
public:
    Transform(F t_func);
    
    Transform(const Transform<F, void>& other);
    Transform(Transform<F, void>&& other);
    auto& operator= (Transform<F, Condition>&& temp);

    inline bool work_impl(void); 
    inline bool ready_impl(void); 

    inline void setDownstream(std::shared_ptr<Pipe<output_type>>);
    inline void setUpstream(std::shared_ptr<Pipe<input_type>>);
};

// This specialization deals with the case that T is a type withoperator().  The only difference between
// this specialization and the general template is the arg<1> (c.f. arg<0>)
template <typename T>
class Transform<T, std::enable_if_t<has_call_operator<T>::value>>: public Joint<Transform<T>> {
private:
    T task_;
public: 
    using output_type = typename function_traits<T>::return_type;
    using input_type = typename function_traits<T>::template arg<1>::type;
private:
    std::shared_ptr<Pipe<input_type>> upstream_;
    std::shared_ptr<Pipe<output_type>> downstream_;
public:
    Transform(T);

    Transform(const Transform<T, void>& other);
    Transform(Transform<T>&& other);
    auto& operator= (Transform<T, void>&& temp);

    inline void work_impl(void); 
    inline bool ready_impl(void); 

    inline void setDownstream(std::shared_ptr<Pipe<output_type>>);
    inline void setUpstream(std::shared_ptr<Pipe<input_type>>);
};

// A "Sink" is a Joint that consumes data and does something with it.  For example, write elements to a file,
// train an ML model on a batch of data, etc.
// Types of sinks that will derive this class:
// 1) FileIO
// 2) AwsIO
// 3) Stdout
// 4) MXNet neural network training!
template<template<class> class Derived, class in_t>
class Sink: public Joint<Derived<in_t>> {
protected:
    std::shared_ptr<Pipe<in_t>> upstream_;
public:
    std::string name;
    using input_type = in_t;

    Sink(void);
    Sink(const Sink<Derived, in_t>&);
    Sink(Sink<Derived, in_t>&&);

    auto& operator= (Sink<Derived, in_t>&& temp);
    // FIXME: Allow different batch sizes!
    inline void work_impl(void);
    inline bool ready_impl(void);
    void setUpstream(std::shared_ptr<Pipe<in_t>>);
};

//
// TRAITS
//
// These structures that follow are used when constructing pipelines in order to determine
// which type of Joint needs to be added next.
template<typename T>
struct is_source {
    // This is a hacky way of pattern matching T := Derived<out_t>.  If the below compiles, this means that
    // T* is convertible to Source<Derived, out_t>* AND T* is not identically equal to a Source<Derived, out_t> -
    // i.e. is a proper derived class.
    template<template<class> typename Derived, typename out_t>
    static auto test(Source<Derived, out_t>*) -> 
        typename std::integral_constant<bool, !std::is_same<T, Source<Derived, out_t>>::value>;

    static std::false_type test(void*);
    
    using type = decltype(test(std::declval<T*>()));
    static constexpr bool value = decltype(test(std::declval<T*>()))::value;
};

template<typename T>
using is_source_t = typename is_source<T>::type;

template<typename T>
constexpr bool is_source_v = is_source<T>::value;

template<typename T>
struct is_transform {
    template<typename Func>
    
    // If T* can be converted to Transform<Func>* then test yields true type, else it can always be casted to void*
    // and the result type of test is false.
    static auto test(Transform<Func>*) -> typename std::true_type;
    static std::false_type test(void*);
    
    using type = decltype(test(std::declval<T*>()));
    static constexpr bool value = decltype(test(std::declval<T*>()))::value;
};

template<typename T>
constexpr bool is_transform_v = is_transform<T>::value;


template<typename T>
struct is_sink {
    template<template<class> typename Derived, typename out_t>
    static auto test(Sink<Derived, out_t>*) -> 
        typename std::integral_constant<bool, !std::is_same<T, Source<Derived, out_t>>::value>;

    static std::false_type test(void*);
    using type = decltype(test(std::declval<T*>()));
    static constexpr bool value = decltype(test(std::declval<T*>()))::value;
};

template<typename T>
using is_sink_t = typename is_sink<T>::type;

template<typename T>
constexpr bool is_sink_v = is_sink<T>::value;

//
// PIPELINE
//
// This is the main structure relevant to Cutter.  A pipeline is hard to define exactly, but roughly is
// a series of manipulations performed on data in some order.  For example, in machine learning applications,
// one is required to load raw training data from a file, transform the training data into feature vectors,
// and then use those feature vectors to train a machine learning model.  One can simply think of a pipeline
// as a directed graph, where each node corresponds to some operation to be performed on data, and edges represent
// input/output relationships.  Right now, Cutter only supports linear pipelines.  Perhaps in the future we will
// expand support to more general directed graphs.

// Base class.  The template argument "Condition" is used to control template specializations
// for each of Source, Sink, and Transform.  For technical reasons, we must separate the public
// interface from the private implementation (for anyone reading this who cares, the specific
// reason is that both the argument pack and the default argument want to be the final argument.
// If we don't add the "void" template argument in by hand, then when someone goes to declare
// Pipeline<Src, Trf, Snk>, the compiler will simply match to the base template definition and
// and nothing will work).
template<typename Condition = void, typename... Args>
class PipelineImpl {
public:
    PipelineImpl(void) { std::cout << "Hello from base constructor" << std::endl; } 
};

template<typename... Args>
using Pipeline = PipelineImpl<void, Args...>;

// These functions are necessary for getting the type of some pipeline stage.
template<size_t, typename, typename...> struct GetStageType {};

template<typename Jnt, typename... Rest>
struct GetStageType<0, Pipeline<Jnt, Rest...>> {
    using type = Pipeline<Jnt, Rest...>;
};

template<size_t k, typename Jnt, typename... Rest>
struct GetStageType<k, Pipeline<Jnt, Rest...>> {
    using type = typename GetStageType<k - 1, Pipeline<Rest...>>::type;
};

// The getStage function will return a given pipeline stage.  The stage has information about the Joint type, 
// as well as the destination queue for the work done by the pipe.  
template<size_t k, typename... Rest>
typename std::enable_if<k == 0, typename GetStageType<0, Pipeline<Rest...>>::type&>::type&
getStage(Pipeline<Rest...>& p) {
    return p;
}

template<size_t k, typename Jnt, typename... Rest>
typename std::enable_if<k != 0, typename GetStageType<k, Pipeline<Jnt, Rest...>>::type&>::type&
getStage(Pipeline<Jnt, Rest...>& pl) {
    Pipeline<Rest...>& peel = pl;
    return getStage<k - 1>(peel);
}


// These next two function templates are used to look for available work in a pipeline from the
// sink end towards the source end.
template<int I = 0, typename... Args>
inline std::enable_if_t<I == -1> searchForWork(Pipeline<Args...>&) {} // noop

template<int I = 0, typename... Args>
inline std::enable_if_t<(I > -1)> searchForWork(Pipeline<Args...>& p) {
    bool found_work = getStage<I>(p).getJoint().work();
    if (found_work) {
       return;
    }
    else {
        searchForWork<I - 1, Args...>(p);
    }
}

template<std::size_t I = 0, typename Func, typename... Args>
inline std::enable_if_t<I == sizeof...(Args)> apply_stage(int, Pipeline<Args...> &, Func) {}

template<std::size_t I = 0, typename Func, typename... Args>
inline std::enable_if_t<I < sizeof...(Args)>  apply_stage(int index, Pipeline<Args...>& p, Func f) {
    if (index == 0) f(getStage<I>(p));
    apply_stage<I + 1, Func, Args...>(index - 1, p, f);
}


// Partial specialization for when Derived inherits from Source<Derived, out_t>.  This is the most complicated stage type because it is the terminal one.  All the pipeline functionality is defined here.
template<
    typename Src,
    typename... Args
>
class PipelineImpl<
    std::enable_if_t<is_source_v<Src>>,
    Src,
    Args...
>: public PipelineImpl<void, Args...> {
private:
    std::atomic<bool> started_;
    char pad1[Cutter::Const::CACHE_LINE_SIZE - sizeof(std::atomic<bool>)];

    std::atomic<bool> stopped_;
    char pad2[Cutter::Const::CACHE_LINE_SIZE - sizeof(std::atomic<bool>)];

    std::vector<std::thread> milpool_;
    Src joint_;
    std::shared_ptr<Pipe<typename Src::output_type>> pipe_;

    static constexpr size_t n_joints = sizeof...(Args) + 1;

    // ready() is defined by checking whether the upstream queue is empty or not (in the case of Transform or Sink).
    // in the case of Source, we check if there are any files remaining.
    inline bool workAvailableAtStage(size_t stage_id) {
        bool result;
        apply_stage(stage_id, *this, [&result](auto& s) { result = s.getJoint().ready(); });
        return result;
    }

    inline bool workAvailableForThread(size_t tid) {
        size_t stage_id = tid % n_joints;
        return workAvailableAtStage(stage_id);
    }
    
    inline void doWorkAtStage(size_t stage_id) {
        apply_stage(stage_id, *this, [](auto& s) { s.getJoint().work(); });
    }

    inline void doWorkForThread(size_t tid) {
        size_t stage_id = tid % n_joints;
        doWorkAtStage(stage_id);
    }

public:
    PipelineImpl(Src&& src, Args&&... args): 
      PipelineImpl<void, Args...>(std::forward<Args>(args)...),
      started_(std::atomic<bool>(false)),
      pad1{0},
      stopped_(std::atomic<bool>(false)),
      pad2{0},
      joint_(std::forward<Src>(src)),
      pipe_(std::make_shared<Pipe<typename Src::output_type>>()) {
        joint_.setDownstream(pipe_);
        // Cast *this to the downstream pipeline type and set the upstream pipe of the next segment
        PipelineImpl<void, Args...>& next = *this;
        next.getJoint().setUpstream(pipe_);
    }

    ~PipelineImpl(void) {
        if (!stopped_.load()) {
            this->stop();
        }
    }

    Src& getJoint(void) {
        return joint_;
    }

    auto getPipe(void) {
        return pipe_;
    }
     
    inline void threadCleanUp(void);
    /*
    *Queue<work_t>::hptr_a = nullptr;
    *Queue<work_t>::hptr_b = nullptr;
    Queue<work_t>::scan(this->q->mempool->head());
    */
    inline void monitor(void) {}

    inline void run(void) {
        for (size_t tid = 0; tid < Cutter::Const::THREAD_COUNT; ++tid) {
            milpool_.emplace_back(
                [this] (size_t tid) noexcept {
                    while (!this->stopped_.load()) {
                        if (workAvailableForThread(tid)) {
                            doWorkForThread(tid);
                            continue;
                        }
                        searchForWork<std::remove_reference<decltype(*this)>::type::n_joints>(*this);
                    }
                    /*
                    // Clean up hzd ptrs for each pipeline stage
                    for (size_t k = n_joints - 1; k >= 0; ++k) {
                        auto stage = getStage<k>(*this);
                        stage.threadCleanUp();
                    }
                    */
                },
                tid
            );
        }
    }

    inline void stop(void) {}
};

// Partial specialization for when Derived inherits from Sink<Derived, out_t>
template<
    typename Snk, 
    typename... Args
>
class PipelineImpl<
    std::enable_if_t<is_sink_v<Snk>>,
    Snk, 
    Args...
>: public PipelineImpl<void, Args...> {
protected:
    Snk joint_;
public:
    PipelineImpl(Snk&& snk, Args&&... args): 
      PipelineImpl<void, Args...>(std::forward<Args>(args)...),
      joint_(std::forward<Snk>(snk))
      {
       // joint_.setDownstream(pipe_);
       // PipelineImpl<void, Args...>& next = *this;
       // next.getJoint().setUpstream(pipe_);
    }

    
    /*
    PipelineImpl<void, Snk, Args...>& operator= (PipelineImpl<void, Snk, Args...>&& temp) {
        if (this != &temp) {
            joint_ = std::move(temp.getJoint());
            // Recurse through the remaining layers of the pipeline...
            getStage<1>(*this) = std::move(getStage<1>(temp));
        }
        return *this;
    }
    */

    Snk& getJoint(void) {
        return joint_;
    }
};

template<
    typename Trf,
    typename... Args
>
class PipelineImpl<std::enable_if_t<is_transform_v<Trf>>, Trf, Args...>: public PipelineImpl<void, Args...> {
private:
    Trf joint_;
    std::shared_ptr<Pipe<typename Trf::output_type>> pipe_;
public:
    PipelineImpl(Trf&& trf, Args&&... args): 
      PipelineImpl<void, Args...>(std::forward<Args>(args)...),
      joint_(std::forward<Trf>(trf))
      {
        joint_.setDownstream(pipe_);
        PipelineImpl<void, Args...>& next = *this;
        next.getJoint().setUpstream(pipe_);
    }
    /* 

    PipelineImpl<void, Trf, Args...>& operator= (PipelineImpl<void, Trf, Args...>&& temp) {
        if (this != &temp) {
            joint_ = std::move(temp.getJoint());
            pipe_.reset();
            pipe_ = temp.getPipe();
           
            // Recurse through the remaining layers of the pipeline...
            getStage<1>(*this) = std::move(getStage<1>(temp));
        }
        return *this;
    }
    */

    Trf& getJoint(void) {
        return joint_;
    }
};

// How to perform throughput monitoring?
//  - Every second, poll the counters at each stage.
//  - Take a delta with the last time a poll was done.

//
//  API
//
// This overload of operator>> works by accumulating all the joint segments until we reach a sink.  When
// we get to a sink, we unpack the accumulator tuple as arguments into the constructor for Pipeline.
template<typename S, typename T>
std::enable_if_t<is_source_v<S> && !is_source_v<T>, std::tuple<S, T>> operator>> (S src, T next) {
    static_assert(
        std::is_same<typename S::output_type, typename T::input_type>::value, 
        "Error: Output type of source must equal input type of transform."
    );
    return {src, next};
}

template<typename Trf, typename... Args>
std::enable_if_t<is_transform_v<Trf>, std::tuple<Args..., Trf>> operator>> (std::tuple<Args...> acc, Trf trf) {
    return std::tuple_cat(acc, std::make_tuple(trf));
}

template<typename Snk, typename... Args>
std::enable_if_t<is_sink_v<Snk>, Pipeline<Args..., Snk>> operator>> (std::tuple<Args...> acc, Snk snk) {
    return std::make_from_tuple<Pipeline<Args..., Snk>>( std::tuple_cat(acc, std::make_tuple(snk)) );
}

/*
// Overload for  Source() >> Transform() >> ... >> (Transform() || Sink())
template<typename Jnt, typename... Args>
std::enable_if_t<!is_source_v<Jnt>, Pipeline<Args..., Jnt>> operator>> (Pipeline<Args...>&& p, Jnt&& next) {
    return Pipeline<Args..., Jnt>(
        getJoints<Pipeline<Args>>(std::forward<Pipeline<Args>>(p)), 
        std::forward<Jnt>(next)
    );
}
*/

} // end namespace Plumbing
} // end namespace Cutter

#include "Plumbing.cpp"

#endif
