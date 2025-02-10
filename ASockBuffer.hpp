/****************************************************************************
 Copyright (c) 2025, ko jung hyun
 
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

// https://github.com/jeremyko/CumBuffer

#ifndef CUMBUFFER_HPP
#define CUMBUFFER_HPP
// NO THREAD SAFETY HERE !!! 
///////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <string>
#include <string.h>
#include <stdint.h>
#include <cstdlib>
#include <cassert>

namespace cumbuffer
{
    const size_t DEFAULT_BUFFER_LEN = 1024 * 4;

    enum OP_RESULT {
        OP_RSLT_OK = 0,
        OP_RSLT_NO_DATA,
        OP_RSLT_BUFFER_FULL,
        OP_RSLT_ALLOC_FAILED,
        OP_RSLT_INVALID_LEN,
        OP_RSLT_INVALID_USAGE
    } ;
} ;

///////////////////////////////////////////////////////////////////////////////
class CumBuffer {
  public:
    CumBuffer() {
        buffer_ptr_=NULL; 
        cumulated_len_=0;
        cur_read_=0;
        cur_write_=0;
        buffer_len_=0;
        is_infinite_buffer_ = false;
    }

    virtual ~CumBuffer() { 
        if(buffer_ptr_) { 
            delete [] buffer_ptr_; 
        } 
    };
    //-------------------------------------------------------------------------
    // Fixed length buffer. If there is insufficient space, an OP_RSLT_BUFFER_FULL error is returned.
    cumbuffer::OP_RESULT Init(size_t max_buffer_len = cumbuffer::DEFAULT_BUFFER_LEN) {
        //fixed buffer size mode.
        is_infinite_buffer_ = false;
        buffer_len_ = max_buffer_len;
        buffer_ptr_ = new (std::nothrow) char [buffer_len_];
        if(buffer_ptr_ == nullptr) {
            err_msg_="alloc failed :";
            return cumbuffer::OP_RSLT_ALLOC_FAILED;
        }
        return cumbuffer::OP_RSLT_OK;
    }
    //-------------------------------------------------------------------------
    // If the buffer space runs out, it automatically doubles in size.
    cumbuffer::OP_RESULT InitAutoGrowing(size_t default_len = cumbuffer::DEFAULT_BUFFER_LEN) {
        //infinite buffer size mode
        is_infinite_buffer_ = true;
        buffer_len_ = default_len ;
        buffer_ptr_ = new (std::nothrow) char [buffer_len_];
        if(buffer_ptr_ == nullptr) {
            err_msg_="alloc failed :";
            return cumbuffer::OP_RSLT_ALLOC_FAILED;
        }
        return cumbuffer::OP_RSLT_OK;
    }
    //-------------------------------------------------------------------------
    cumbuffer::OP_RESULT    Append(size_t len, char* pData)
    {
#ifdef CUMBUFFER_DEBUG
        std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] len="<<len<< "["<< pData<<"]\n";  
#endif
        if( buffer_len_ < len ) {
            err_msg_="invalid length";
            return cumbuffer::OP_RSLT_INVALID_LEN;
        } else if( buffer_len_ ==  cumulated_len_ ) {
            DebugPos(__LINE__);
            if (is_infinite_buffer_){
                if(cur_read_ ==0 && cur_write_ == buffer_len_){ 
                    //std::cout <<"["<< __func__ <<"-"<<__LINE__<<"] AUTO GROW 1\n";  
                    char* buffer_tmp = new (std::nothrow)  char [buffer_len_];
                    if(buffer_tmp == nullptr) {
                        err_msg_="alloc failed :";
                        return cumbuffer::OP_RSLT_ALLOC_FAILED;
                    }
                    memcpy(buffer_tmp, buffer_ptr_, buffer_len_);
                    delete [] buffer_ptr_; 
                    buffer_ptr_ = new (std::nothrow) char [buffer_len_*2];
                    if(buffer_ptr_ == nullptr) {
                        err_msg_="alloc failed :";
                        return cumbuffer::OP_RSLT_ALLOC_FAILED;
                    }
                    memcpy(buffer_ptr_ , buffer_tmp, buffer_len_);
                    memcpy(buffer_ptr_+cur_write_ , pData, len); 
                    cur_write_ += len;
                    cumulated_len_ += len;
                    buffer_len_ = buffer_len_ * 2 ;
                    DebugPos(__LINE__);
                    return cumbuffer::OP_RSLT_OK;
                }else if(  cur_write_ == cur_read_ ) { 
                    // 중간에 있는 경우
                    // |0123456789|
                    // ------------
                    // |bcdef6789a|
                    // |     w    |
                    // |     r    |
                    //std::cout <<"["<< __func__ <<"-"<<__LINE__<<"] AUTO GROW 2 \n";  
                    char* buffer_tmp = new (std::nothrow)  char [buffer_len_];
                    if(buffer_tmp == nullptr) {
                        err_msg_="alloc failed :";
                        return cumbuffer::OP_RSLT_ALLOC_FAILED;
                    }
                    size_t first_block_len  = buffer_len_ - cur_write_;
                    size_t second_block_len = cur_read_ ;
                    memcpy(buffer_tmp, buffer_ptr_+cur_write_, first_block_len);
                    memcpy(buffer_tmp+ first_block_len, buffer_ptr_, second_block_len);
                    delete [] buffer_ptr_; 
                    buffer_ptr_ = new (std::nothrow) char [buffer_len_*2];
                    if(buffer_ptr_ == nullptr) {
                        err_msg_="alloc failed :";
                        return cumbuffer::OP_RSLT_ALLOC_FAILED;
                    }
                    memcpy(buffer_ptr_ , buffer_tmp, buffer_len_);
                    memcpy(buffer_ptr_+cumulated_len_ , pData, len); 
                    // |01234567890123456789|
                    // ----------------------
                    // |6789abcdefxxxxx     |
                    // |               w    |
                    // |r                   |
                    cur_read_  = 0; //XXX reset
                    cur_write_ = (buffer_len_+len);//XXX reset
                    cumulated_len_ += len;
                    buffer_len_ = buffer_len_ * 2 ;
                    DebugPos(__LINE__);
                    return cumbuffer::OP_RSLT_OK;
                } 
            }else{
                //std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] *** buffer full ***\n"; 
                DebugPos(__LINE__);
                err_msg_="buffer full";
                return cumbuffer::OP_RSLT_BUFFER_FULL;
            }
        }
        if(cur_write_ < cur_read_) {
            //write 이 버퍼 끝을 지난 경우
            if(cur_read_ - cur_write_ >= len) {
                memcpy(buffer_ptr_ + cur_write_, pData, len);
                cur_write_ += len;
                cumulated_len_ += len;
                //DebugPos(__LINE__);
                return cumbuffer::OP_RSLT_OK;
            } else {
                // |0123456789|
                // ------------
                // |bc   6789a|
                // |  w       |
                // |     r    |
                if (is_infinite_buffer_){
                    //XXX
                    //std::cout <<"["<< __func__ <<"-"<<__LINE__<<"] AUTO GROW 3 \n";  
                    char* buffer_tmp = new (std::nothrow)  char [buffer_len_];
                    if(buffer_tmp == nullptr) {
                        err_msg_="alloc failed :";
                        return cumbuffer::OP_RSLT_ALLOC_FAILED;
                    }
                    size_t first_block_len  = buffer_len_ - cur_read_;
                    size_t second_block_len = cur_write_ ;
                    memcpy(buffer_tmp, buffer_ptr_+cur_read_, first_block_len);
                    memcpy(buffer_tmp+ first_block_len, buffer_ptr_, second_block_len);
                    delete [] buffer_ptr_; 
                    buffer_ptr_ = new (std::nothrow) char [buffer_len_*2];
                    if(buffer_ptr_ == nullptr) {
                        err_msg_="alloc failed :";
                        return cumbuffer::OP_RSLT_ALLOC_FAILED;
                    }
                    memcpy(buffer_ptr_ , buffer_tmp, buffer_len_);
                    memcpy(buffer_ptr_+cumulated_len_ , pData, len); 
                    // |01234567890123456789|
                    // ----------------------
                    // |6789abcxxxxx        |
                    // |            w       |
                    // |r                   |
                    cur_write_ = (buffer_len_+len)-(cur_read_ - cur_write_);//XXX reset
                    cur_read_  = 0; //XXX reset
                    cumulated_len_ += len;
                    buffer_len_ = buffer_len_ * 2 ;
                    DebugPos(__LINE__);
                    return cumbuffer::OP_RSLT_OK;
                }else{
                    //std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] *** buffer full ***\n" ;
                    DebugPos(__LINE__);
                    err_msg_="buffer full";
                    return cumbuffer::OP_RSLT_BUFFER_FULL;
                }
            }
        } else {
            //write 이 버퍼 끝을 지나지 않음
            if (buffer_len_ < cur_write_ + len) {
                //tail 이후, 남은 버퍼로 모자라는 경우
                if( cur_write_ > 0 && 
                    len - (buffer_len_ - cur_write_)  <= cur_read_ ) {
                    //2번 나누어서 들어갈 공간이 있는 경우
                    //DebugPos(__LINE__);
                    size_t first_block_len  = buffer_len_ - cur_write_;
                    size_t second_block_len = len - first_block_len;
#ifdef CUMBUFFER_DEBUG
                    std::cout <<"["<< __func__ <<"-"<<__LINE__ 
                        <<"] first_block_len ="<<first_block_len  << ", second_block_len="<<second_block_len<<"\n"; 
#endif
                    if(first_block_len>0) {
                        memcpy(buffer_ptr_+ cur_write_ , pData, first_block_len); 
                    }
                    memcpy(buffer_ptr_ , pData+(first_block_len), second_block_len); 
                    cur_write_ = second_block_len;
                    cumulated_len_ += len;
                    //DebugPos(__LINE__);
                    return cumbuffer::OP_RSLT_OK;
                } else {
                    //2번 나누어서 들어갈 공간 없는 경우
                    // |0123456789|
                    // ------------
                    // |abcdef    |
                    // |      w   |
                    // |  r       |
                    if (is_infinite_buffer_){
                        //XXX 
                        //std::cout <<"["<< __func__ <<"-"<<__LINE__<<"] AUTO GROW 4\n";  
                        char* buffer_tmp = new (std::nothrow)  char [buffer_len_];
                        if(buffer_tmp == nullptr) {
                            err_msg_="alloc failed :";
                            return cumbuffer::OP_RSLT_ALLOC_FAILED;
                        }
                        memcpy(buffer_tmp, buffer_ptr_, buffer_len_);
                        delete [] buffer_ptr_; 
                        buffer_ptr_ = new (std::nothrow) char [buffer_len_*2];
                        if(buffer_ptr_ == nullptr) {
                            err_msg_="alloc failed :";
                            return cumbuffer::OP_RSLT_ALLOC_FAILED;
                        }
                        memcpy(buffer_ptr_ , buffer_tmp, buffer_len_);
                        // the write and read positions do not change.
                        // |01234567890123456789|
                        // ----------------------
                        // |abcdef              |
                        // |      w             |
                        // |  r                 |
                        memcpy(buffer_ptr_+cur_write_ , pData, len); 
                        cur_write_ += len;
                        cumulated_len_ += len;
                        buffer_len_ = buffer_len_ * 2 ;
                        DebugPos(__LINE__);
                        return cumbuffer::OP_RSLT_OK;
                    }else{
                        //std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] *** buffer full ***" <<"\n"; 
                        DebugPos(__LINE__);
                        err_msg_="buffer full";
                        return cumbuffer::OP_RSLT_BUFFER_FULL;
                    }
                }
            } else {
                //write 이후, 남은 버퍼로 가능한 경우
                //most general case
                memcpy(buffer_ptr_+cur_write_ , pData, len); 
                cur_write_ += len;
                cumulated_len_ += len;
                //DebugPos(__LINE__);
                return cumbuffer::OP_RSLT_OK;
            }
        }
        return cumbuffer::OP_RSLT_OK;
    }

    //-------------------------------------------------------------------------
    cumbuffer::OP_RESULT    PeekData(size_t len, char* data_out) {
        cumbuffer::OP_RESULT nRslt = ValidateBuffer(len);
        if(cumbuffer::OP_RSLT_OK!=nRslt ) {
            return nRslt;
        }
        if(cur_write_ > cur_read_) {
            //일반적인 경우
            if (cur_write_ < cur_read_ + len) {
                err_msg_="invalid length";
                return cumbuffer:: OP_RSLT_INVALID_LEN;
            } else {
                memcpy(data_out, buffer_ptr_ + cur_read_, len);
            }
        } else {
            if (buffer_len_ < cur_read_ + len) {
                size_t first_block_len = buffer_len_ - cur_read_;
                size_t second_block_len = len - first_block_len;
#ifdef CUMBUFFER_DEBUG
                std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] first_block_len="
                        <<first_block_len  << "/second_block_len="<<second_block_len<<"\n"; 
#endif
                if( cur_write_ > 0 && cur_write_ >= second_block_len ) {
                    memcpy(data_out , buffer_ptr_+cur_read_, first_block_len); 
                    memcpy(data_out+first_block_len , buffer_ptr_, second_block_len); 
                } else {
                    err_msg_="invalid length";
                    return cumbuffer:: OP_RSLT_INVALID_LEN;
                }
            } else {
                memcpy(data_out, buffer_ptr_ + cur_read_, len);
            }
        }
#ifdef CUMBUFFER_DEBUG
        std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] out data ["<<data_out<<"]\n";
#endif
        return cumbuffer::OP_RSLT_OK;
    }

    //-------------------------------------------------------------------------
    cumbuffer::OP_RESULT    ConsumeData(size_t len) {
        //버퍼내 cur_read_ 만 이동.
        cumbuffer::OP_RESULT nRslt = ValidateBuffer(len);
        if(cumbuffer::OP_RSLT_OK!=nRslt ) {
            return nRslt;
        }
        if(cur_write_ > cur_read_) {
            //일반적인 경우
            if (cur_write_ < cur_read_ + len) {
                err_msg_="invalid length";
                return cumbuffer:: OP_RSLT_INVALID_LEN;
            } else {
                cur_read_ += len;
            }
        } else {
            if (buffer_len_ < cur_read_ + len) {
                size_t first_block_len = buffer_len_ - cur_read_;
                size_t second_block_len = len - first_block_len;
#ifdef CUMBUFFER_DEBUG
                std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] first_block_len="
                        <<first_block_len  << "/second_block_len="<<second_block_len<<"\n"; 
#endif
                if( cur_write_ > 0 && cur_write_ >= second_block_len ) {
                    cur_read_ =second_block_len ;
                } else {
                    err_msg_="invalid length";
                    return cumbuffer:: OP_RSLT_INVALID_LEN;
                }
            } else {
                cur_read_ += len;
            }
        }
        cumulated_len_ -= len;
#ifdef CUMBUFFER_DEBUG
        std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] out data ["<<data_out<<"]\n";
#endif
        return cumbuffer::OP_RSLT_OK;
    }

    //-------------------------------------------------------------------------
    cumbuffer::OP_RESULT    GetData(size_t len, char* data_out )
    {
        cumbuffer::OP_RESULT nRslt = ValidateBuffer(len);
        if(cumbuffer::OP_RSLT_OK!=nRslt ) {
            return nRslt;
        }
        if(cur_write_ > cur_read_) {
            //일반적인 경우
            if (cur_write_ < cur_read_ + len) {
                err_msg_="invalid length";
                return cumbuffer:: OP_RSLT_INVALID_LEN;
            } else {
                memcpy(data_out, buffer_ptr_ + cur_read_, len);
                cur_read_ += len;
            }
        } else {
            if (buffer_len_ < cur_read_ + len) {
                size_t first_block_len = buffer_len_ - cur_read_;
                size_t second_block_len = len - first_block_len;
#ifdef CUMBUFFER_DEBUG
                std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] first_block_len="
                        <<first_block_len  << "/second_block_len="<<second_block_len<<"\n"; 
#endif
                if( cur_write_ > 0 && cur_write_ >= second_block_len ) {
                    memcpy(data_out , buffer_ptr_+cur_read_, first_block_len); 
                    memcpy(data_out+first_block_len , buffer_ptr_, second_block_len); 
                    cur_read_ =second_block_len ;
                } else {
                    err_msg_="invalid length";
                    return cumbuffer:: OP_RSLT_INVALID_LEN;
                }
            } else {
                memcpy(data_out, buffer_ptr_ + cur_read_, len);
                cur_read_ += len;
            }
        }
        cumulated_len_ -= len;
#ifdef CUMBUFFER_DEBUG
        std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] out data ["<<data_out<<"]\n";
#endif
        return cumbuffer::OP_RSLT_OK;
    }

    //-------------------------------------------------------------------------
    cumbuffer::OP_RESULT ValidateBuffer(size_t len) {
        if(cumulated_len_ == 0 ) {
            err_msg_="no data";
            return cumbuffer::OP_RSLT_NO_DATA;
        } else if(cumulated_len_ < len) {
            err_msg_="invalid length";
            return cumbuffer:: OP_RSLT_INVALID_LEN;
        }
        return cumbuffer::OP_RSLT_OK;
    }
    //-------------------------------------------------------------------------
    size_t GetCumulatedLen() {
        return cumulated_len_ ;
    }
    //-------------------------------------------------------------------------
    size_t GetCapacity() {
        return buffer_len_ ;
    }
    //-------------------------------------------------------------------------
    size_t GetTotalFreeSpace() {
        return buffer_len_  - cumulated_len_;
    }
    //-------------------------------------------------------------------------
    uint64_t GetCurReadPos() {
        return cur_read_; 
    }
    //-------------------------------------------------------------------------
    uint64_t GetCurWritePos() {
        return cur_write_; 
    }
    //-------------------------------------------------------------------------
    char* GetUnReadDataPtr() {
        return buffer_ptr_+cur_read_ ;
    }
    //-------------------------------------------------------------------------
    //for direct buffer write
    uint64_t GetLinearFreeSpace() {
        //current maximun linear buffer size
        if(cur_write_==buffer_len_) {
            //cur_write_ is at last position
            return buffer_len_ - cumulated_len_ ; 
        } else if(cur_read_ < cur_write_) {
            return buffer_len_- cur_write_; 
        } else if(cur_read_ > cur_write_) {
            return cur_read_-cur_write_; 
        } else {
            return buffer_len_- cur_write_;
        }
    }
    //------------------------------------------------------------------------
    //for direct buffer write
    char* GetLinearAppendPtr() {
        if(cur_write_==buffer_len_) {
            //cur_write_ is at last position
            if(buffer_len_!= cumulated_len_) {
                //and buffer has free space
                //-> append at 0  
                //cur_write_ -> 버퍼 마지막 위치하고, 버퍼에 공간이 존재. -> 처음에 저장
                //XXX dangerous XXX 
                //this is not a simple get function, cur_write_ changes !!
                cur_write_ = 0;
            }
        }
        return (buffer_ptr_+ cur_write_);
    }
    //-------------------------------------------------------------------------
    void IncreaseData(size_t len) {
        cur_write_+= len;
        cumulated_len_ +=len;
        if (cumulated_len_ > buffer_len_) {
            std::cerr << "invalid len error!\n";
        }
        assert(cumulated_len_ <= buffer_len_);//XXX
    }
    //-------------------------------------------------------------------------
#ifdef CUMBUFFER_DEBUG
    void    DebugPos(int line) {
        std::cout <<"line [" <<line
            <<"] cur_read_=" << cur_read_  
            <<", cur_write_="  << cur_write_ 
            <<", cumulated_len_=" << cumulated_len_ 
            <<", buffer_len_=" << buffer_len_
            <<"\n";
    }
#else
    void DebugPos(int) {}
#endif
    //-------------------------------------------------------------------------
    void ReSet() {
        cumulated_len_=0;
        cur_read_ =0;
        cur_write_=0;
    }

    // Increases the buffer size by the specified size and then copies existing data.
    bool IncreaseBufferAndCopyExisting(size_t len) {
        size_t old_buffer_len = buffer_len_ ;
        char* old_buffer_ptr = new (std::nothrow) char [old_buffer_len];
        if(old_buffer_ptr == nullptr) {
            err_msg_="alloc failed :";
            return false;
        }
        memcpy(old_buffer_ptr, buffer_ptr_, old_buffer_len);

        if(buffer_ptr_) { 
            delete [] buffer_ptr_; 
        } 
        buffer_len_ = len ;
        buffer_ptr_ = new (std::nothrow) char [buffer_len_];
        if(buffer_ptr_ == nullptr) {
            err_msg_="alloc failed :";
            return false;
        }
        memset(buffer_ptr_, '\0', buffer_len_);
        memcpy(buffer_ptr_, old_buffer_ptr, old_buffer_len);

        return true;
    }


    //-------------------------------------------------------------------------
    const char* GetErrMsg() { 
        return err_msg_.c_str() ; 
    }

  private:
    CumBuffer(const CumBuffer&) ; //= delete;
    CumBuffer& operator = (const CumBuffer &) ; //= delete;

    std::string err_msg_;
    char*       buffer_ptr_;
    size_t      buffer_len_;
    size_t      cumulated_len_;
    uint64_t    cur_read_  ; 
    uint64_t    cur_write_  ; 
    bool        is_infinite_buffer_ ;
} ;

#endif // CUMBUFFER_HPP

