/****************************************************************************
 Copyright (c) 2022 ko jung hyun
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#ifndef _KOCONDVAR_HPP_
#define _KOCONDVAR_HPP_

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include <queue>
#include <condition_variable>
#include <chrono>

typedef enum __ENUM_COND_VAR_RSLT__
{
    COND_VAR_RSLT_TIMEOUT = 0,
    COND_VAR_RSLT_SIGNALED
} ENUM_COND_VAR_RSLT ;

///////////////////////////////////////////////////////////////////////////////
class CondVar
{
    public:
        CondVar()  = default;
        ~CondVar() = default;

        void NotifyOne() {
            std::unique_lock<std::mutex> lock (cond_var_lock_);
            notified_cnt_++ ;
            cond_var_.notify_one();
        }

        void NotifyAll() {
            std::unique_lock<std::mutex> lock (cond_var_lock_);
            notified_cnt_++ ;
            cond_var_.notify_all();
        }

        void WaitForSignal() {
            std::unique_lock<std::mutex> lock (cond_var_lock_);
            while (!notified_cnt_) {
                cond_var_.wait(lock );
            }    
            if(!is_all_waiting_end_) {
                notified_cnt_-- ;
            }
        }

        ENUM_COND_VAR_RSLT WaitForSignalTimeout(int timeout_secs) {
            std::unique_lock<std::mutex> lock (cond_var_lock_);
            std::cv_status ret = std::cv_status::no_timeout;
            auto duration_sec = std::chrono::seconds(timeout_secs);

            while(!notified_cnt_ && std::cv_status::timeout !=ret) { 
                ret=cond_var_.wait_for(lock, duration_sec);
            }
            if(!is_all_waiting_end_) {
                notified_cnt_-- ;
            }
            if(std::cv_status::timeout ==ret) {
                return COND_VAR_RSLT_TIMEOUT;
            }
            return COND_VAR_RSLT_SIGNALED;
        }

        void SetAllWaitingEnd() {
            is_all_waiting_end_ = true;
        }

    private:    
        std::mutex              cond_var_lock_ ;
        std::condition_variable cond_var_ ;
        size_t notified_cnt_ {0}; 
        //XXX use count. no boolean. 
        //NotifyOne, NotifyAll can be called multiple times before WaitForSignal is called.
        bool is_all_waiting_end_ {false};
};


#endif // _KOCONDVAR_HPP_

