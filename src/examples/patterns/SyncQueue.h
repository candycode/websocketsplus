#pragma once
//
// Created by Ugo Varetto on 6/29/16.
//

#include <deque>
#include <mutex>
#include <condition_variable>
#include <cassert>

template < typename T >
class SyncQueue {
public:
    void Push(const T& e) {
        std::lock_guard< std::mutex > guard(mutex_);
        queue_.push_back(e);
        cond_.notify_one(); //notify
    }
    //used to add a high piority message, normally to signal
    //end of operations
    void PushFront(const T& e) {
        std::lock_guard< std::mutex > guard(mutex_);
        queue_.push_front(e);
        cond_.notify_one(); //notify
    }
    template < typename FwdT >
    void Buffer(FwdT begin, FwdT end) {
        std::lock_guard< std::mutex > guard(mutex_);
        while(begin++ != end) queue_.push_back(*begin);
        cond_.notify_one();
    }
    T Pop() {
        std::unique_lock< std::mutex > lock(mutex_);
        //stop and wait for notification if condition is false;
        //continue otherwise
        cond_.wait(lock, [this]{ return !queue_.empty();});
        T e(std::move(queue_.front()));
        queue_.pop_front();
        return e;
    }
    bool Empty() const {
        //std::lock_guard< std::mutex > guard(mutex_);
        return queue_.empty();
    }
private:
    std::deque< T > queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
};
