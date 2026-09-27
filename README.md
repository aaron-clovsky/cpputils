# C++ Utilities

General purpose libraries with potential applicability across projects.

## sane_binary_semaphore.h
**Compatibility: C++20**

Like C++20's std::binary_semaphore but an actual binary semaphore that enforces the maximum value of one, because it's binary...

This should be superseded by P2643's additions to atomic_flag whenever C++26 becomes a thing
 
## publisher.h
**Compatibility: C++11**

Lock free, retry free, no heap allocation and optionally zero copy mechanism to safely publish data from one producing thread to one consuming thread while replacing previously published data with the latest data if it has not yet been consumed.

## dispatch_thread.h
**Compatibility: C++20**

Yet Another Way to Thread Functions<sup>TM</sup>. Uses [publisher.h](https://github.com/aaron-clovsky/cpputils/blob/main/publisher.h) and [sane_binary_semaphore.h](https://github.com/aaron-clovsky/cpputils/blob/main/sane_binary_semaphore.h).

## LICENSE
This software is licensed under the
[MIT License](https://opensource.org/licenses/MIT).
