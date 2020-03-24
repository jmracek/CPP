# CPP
Random C++ utilities

This repository contains various utilities I've written in C++.  You'll find a threadsafe object pool implementation (Memory.\*), a threadpool implementation (Proletariat.\*), as well as an implementation of a lockfree queue which utilizes hazard pointers (Lockfree.\*).

Currently the implementation of the lockfree queue I've written is not performant.  You're free to use it, but do so under the assumption that it will be much slower than a regular queue with a simple lock.  It needs to be improved by coalescing memory allocations.  

There is one additional file here called Pipeline.\*.  The code in that file defines a template library for machine learning ETL tasks.  There are much better ways to do ML ETL than what you'll find there, which is basically only a SFINAE/template flex.
