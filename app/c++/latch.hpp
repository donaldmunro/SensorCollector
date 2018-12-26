// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// (C) Copyright 2013 Vicente J. Botet Escriba

// Hacked/Kludged to use std C++ 11 primitives instead of the boost ones by Donald Munro (2018)

#ifndef _THREAD_LATCH_
#define _THREAD_LATCH_

#include <chrono>
#include <mutex>
#include <condition_variable>

#ifndef ANDROID_LOG_TAG
#define ANDROID_LOG_TAG "latch"
#endif

#include "android-logging.h"

namespace stdex
{
   class latch
   {
      bool count_down(std::unique_lock<std::mutex> &)
      {
         if (count_ <= 0)
         {
            ALOGE("expected count > 0");
            return false;
         }
         if (--count_ == 0)
         {
            ++generation_;
            //lk.unlock();
            cond_.notify_all();
            return true;
         }
         return false;
      }

      /// Effect: Decrement the count is > 0. Unlocks the lock notify anyone waiting if we reached zero.
      /// Returns: true if count_ is 0.
      /// @ThreadSafe ensured by the @c lk parameter
      bool try_count_down(std::unique_lock<std::mutex> &lk)
      {
         if (count_ > 0)
         {
            return count_down(lk);
         }
         return true;
      }

      struct not_equal
      {
         not_equal(std::size_t &x, std::size_t &y) : x_(x), y_(y)
         {}

         bool operator()() const
         {return x_ != y_;}

         std::size_t &x_;
         std::size_t &y_;
      };

   public:

      /// Constructs a latch with a given count.
      latch(std::size_t count) : count_(count), generation_(0)
      {}

      /// Blocks until the latch has counted down to zero.
      void wait()
      {
         std::unique_lock<std::mutex> lk(mutex_);
         if (count_ == 0) return;
         std::size_t generation(generation_);
         cond_.wait(lk, not_equal(generation, generation_));
      }

      /// @return true if the internal counter is already 0, false otherwise
      bool try_wait()
      {
         std::unique_lock<std::mutex> lk(mutex_);
         return (count_ == 0);
      }

      /// try to wait for a specified amount of time is elapsed.
      /// @return whether there is a timeout or not.
      template<class Rep, class Period>
      std::cv_status wait_for(const std::chrono::duration<Rep, Period> &rel_time)
      {
         std::unique_lock<std::mutex> lk(mutex_);
         if (count_ == 0) return std::cv_status::no_timeout;
         std::size_t generation(generation_);
         return cond_.wait_for(lk, rel_time, not_equal(generation, generation_))
                ? std::cv_status::no_timeout
                : std::cv_status::timeout;
      }

      /// try to wait until the specified time_point is reached
      /// @return whether there were a timeout or not.
      template<class Clock, class Duration>
      std::cv_status wait_until(const std::chrono::time_point<Clock, Duration> &abs_time)
      {
         std::unique_lock<std::mutex> lk(mutex_);
         if (count_ == 0) return std::cv_status::no_timeout;
         std::size_t generation(generation_);
         return cond_.wait_until(lk, abs_time, not_equal(generation, generation_))
                ? std::cv_status::no_timeout
                : std::cv_status::timeout;
      }

      /// Decrement the count and notify anyone waiting if we reach zero.
      /// @Requires count must be greater than 0
      void count_down()
      {
         std::unique_lock<std::mutex> lk(mutex_);
         count_down(lk);
      }

      /// Effect: Decrement the count if it is > 0 and notify anyone waiting if we reached zero.
      /// Returns: true if count_ was 0 or reached 0.
      bool try_count_down()
      {
         std::unique_lock<std::mutex> lk(mutex_);
         return try_count_down(lk);
      }

      void signal()
      {
         count_down();
      }

      /// Decrement the count and notify anyone waiting if we reach zero.
      /// Blocks until the latch has counted down to zero.
      /// @Requires count must be greater than 0
      void count_down_and_wait()
      {
         std::unique_lock<std::mutex> lk(mutex_);
         std::size_t generation(generation_);
         if (count_down(lk))
         {
            return;
         }
         cond_.wait(lk, not_equal(generation, generation_));
      }

      void sync()
      {
         count_down_and_wait();
      }

      /// Reset the counter
      /// #Requires This method may only be invoked when there are no other threads currently inside the count_down_and_wait() method.
      void reset(std::size_t count)
      {
         std::lock_guard<std::mutex> lk(mutex_);
         //BOOST_ASSERT(count_ == 0);
         count_ = count;
      }

   private:
      std::mutex mutex_;
      std::condition_variable cond_;
      std::size_t count_;
      std::size_t generation_;
   };
}

#endif
