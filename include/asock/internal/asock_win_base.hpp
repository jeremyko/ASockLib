/******************************************************************************
MIT License

Copyright (c) 2025 jung hyun, ko

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 *****************************************************************************/

#ifndef ASOCKWINBASE_HPP
#define ASOCKWINBASE_HPP

#include <functional>
#include "asock_comm.hpp"

namespace asock {
////////////////////////////////////////////////////////////////////////////////
class ASockWinBase {
public :
    virtual ~ASockWinBase() {};
    virtual void SetUsage()=0;
    //-------------------------------------------------------------------------
    bool SetSocketNonBlocking(SOCKET_T sock_fd) {
        // If iMode = 0, blocking is enabled; 
        // If iMode != 0, non-blocking mode is enabled.
        unsigned long nMode = 1;
        int nResult = ioctlsocket(sock_fd, FIONBIO, &nMode);
        if (nResult != NO_ERROR) {
            BuildErrMsgString(WSAGetLastError());
            return  false;
        }
        return true;
    }
    //-------------------------------------------------------------------------
    bool SetBufferCapacity(size_t max_data_len) {
        if(max_data_len<=0) {
            err_msg_ = " length is invalid";
            return false;
        }
        max_data_len_ = max_data_len ; 
        return true;
    }
    //-------------------------------------------------------------------------
    std::string GetLastErrMsg(){
        return err_msg_; 
    }
    //-------------------------------------------------------------------------
    bool SetCbOnRecvedCompletePacket(std::function<bool(Context*,const char* const,size_t)> cb) {
        //for composition usage 
        if(cb != nullptr) {
            cb_on_recved_complete_packet_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }
    HANDLE  handle_completion_port_;

protected :
    size_t  max_data_len_ {0};
    std::string  err_msg_ ;
    ENUM_SOCK_USAGE sock_usage_ {asock::SOCK_USAGE_UNKNOWN};
    std::function<bool(Context*,const char* const,size_t)>cb_on_recved_complete_packet_{nullptr};
    Context* udp_server_ctx_ptr = nullptr;

    //-------------------------------------------------------------------------
    //for udp server, client only
    bool SetSockoptSndRcvBufUdp(SOCKET_T socket) {
        char err_buf[80];
        int opt_cur = 0;
        int opt_len = sizeof(int);
        int opt_cur_max_msg_size = 0;
        //--------------
        if (getsockopt(socket, SOL_SOCKET, SO_MAX_MSG_SIZE, 
                       (char*)&opt_cur_max_msg_size, &opt_len) == -1) {
			strerror_s(err_buf, 80, errno);
            err_msg_ = "gsetsockopt SO_MAX_MSG_SIZE error [" + std::string(err_buf) + "]";
            return false;
        }
        int opt_val = max((int)max_data_len_, opt_cur_max_msg_size );
        DBG_LOG("curr SO_MAX_MSG_SIZE = " << opt_cur_max_msg_size );
        DBG_LOG("max_data_len_ = " << max_data_len_ );
        DBG_LOG("max val = " << opt_val );
        //--------------
        if (getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*) &opt_cur, 
                       (SOCKLEN_T*)&opt_len) == -1) {
			strerror_s(err_buf, 80, errno);
            err_msg_ = "gsetsockopt SO_SNDBUF error [" + std::string(err_buf) + "]";
            return false;
        }
        DBG_LOG("curr SO_SNDBUF = " << opt_cur );
        if (opt_val > opt_cur) {
            if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&opt_val, 
                           sizeof(opt_val)) == -1) {
			    strerror_s(err_buf, 80, errno);
                err_msg_ = "setsockopt SO_SNDBUF error [" + std::string(err_buf) + "]";
                return false;
            }
            DBG_LOG("set SO_SNDBUF = " << opt_val );
        }
        //--------------
        if (getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*) & opt_cur, 
                       (SOCKLEN_T*)&opt_len) == -1) {
			strerror_s(err_buf, 80, errno);
            err_msg_ = "setsockopt SO_RCVBUF error [" + std::string(err_buf) + "]";
            return false;
        }
        DBG_LOG("curr SO_RCVBUF = " << opt_cur);
        if (opt_val > opt_cur) {
            if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&opt_val, 
                           sizeof(opt_val)) == -1) {
			    strerror_s(err_buf, 80, errno);
                err_msg_ = "setsockopt SO_RCVBUF error [" + std::string(err_buf) + "]";
                return false;
            }
            DBG_LOG("set SO_RCVBUF = " << opt_val );
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool InitWinsock() {
        WSADATA wsa_data;
        int nResult = WSAStartup(MAKEWORD(2, 2), & wsa_data);
        if (nResult != 0) {
            BuildErrMsgString(WSAGetLastError());
            WSACleanup();
            return false;
        }
        if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
            err_msg_ = "Could not find a usable version of Winsock.dll";
            WSACleanup();
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    void BuildErrMsgString(int err_no) {
        LPVOID lpMsgBuf;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL, WSAGetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
                      (LPTSTR)&lpMsgBuf, 0, NULL);
        err_msg_ = std::string("(") + std::to_string(err_no) + std::string(") ");
        err_msg_ += std::string((LPCTSTR)lpMsgBuf);
        ELOG(err_msg_);
        LocalFree(lpMsgBuf);
    }

    //-------------------------------------------------------------------------
    bool RecvData(size_t worker_id, Context* ctx_ptr, DWORD bytes_transferred) {
        //DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
		//  " / capacity : " << ctx_ptr->GetBuffer()->GetCapacity() <<
		//  " / total free : " << ctx_ptr->GetBuffer()->GetTotalFreeSpace() <<
		//  " / linear free : " << ctx_ptr->GetBuffer()->GetLinearFreeSpace() <<
		//  " / cumulated : " << ctx_ptr->GetBuffer()->GetCumulatedLen());

        ctx_ptr->GetBuffer()->IncreaseData(bytes_transferred);
        while (ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()) {
            if (!ctx_ptr->per_recv_io_ctx->is_packet_len_calculated){ 
                //only when calculation is necessary
                ctx_ptr->per_recv_io_ctx->complete_recv_len = OnCalculateDataLen(ctx_ptr);
                ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = true;
            }
            if (ctx_ptr->per_recv_io_ctx->complete_recv_len == asock::MORE_TO_COME) {
                ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
                return true; //need to recv more
            } else if (ctx_ptr->per_recv_io_ctx->complete_recv_len > 
                       ctx_ptr->per_recv_io_ctx->cum_buffer.GetCumulatedLen()) {
                return true; //need to recv more
            } else {
                //got complete packet 
                size_t alloc_len = ctx_ptr->per_recv_io_ctx->complete_recv_len;
                //LOG("got complete packet:" << alloc_len - HEADER_SIZE);
                char* complete_packet_data = new (std::nothrow) char [alloc_len] ; //XXX 
                if(complete_packet_data == nullptr) {
                    ELOG("mem alloc failed");
                    exit(EXIT_FAILURE);
                }
                if (cumbuffer::OP_RSLT_OK !=
                    ctx_ptr->per_recv_io_ctx->cum_buffer.GetData (
                        ctx_ptr->per_recv_io_ctx->complete_recv_len, 
                        complete_packet_data)) {
                    //error !
					err_msg_ = ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg();
					ELOG(err_msg_);
                    ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
                    delete[] complete_packet_data; //XXX
                    exit(EXIT_FAILURE);
                }
                if (cb_on_recved_complete_packet_ != nullptr) {
                    //invoke user specific callback
                    cb_on_recved_complete_packet_(ctx_ptr, 
                                                  complete_packet_data+HEADER_SIZE , 
                                                  ctx_ptr->per_recv_io_ctx->complete_recv_len-HEADER_SIZE);
                } else {
                    //invoke user specific implementation
                    OnRecvedCompleteData(ctx_ptr, complete_packet_data+HEADER_SIZE, 
                                         ctx_ptr->per_recv_io_ctx->complete_recv_len-HEADER_SIZE);
                }
                delete[] complete_packet_data; //XXX
                ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
            }
        } //while
        return true;
    }

    //-------------------------------------------------------------------------
    bool RecvfromData(size_t worker_id, Context* ctx_ptr, DWORD bytes_transferred) {
        ctx_ptr->GetBuffer()->IncreaseData(bytes_transferred);
        //DBG_LOG(asock::GetSockUsageName(sock_usage_) << ": worker="<< worker_id 
        //  <<",sock="<<ctx_ptr->socket
        //  <<",GetCumulatedLen = " << ctx_ptr->GetBuffer()->GetCumulatedLen() 
        //  << ",recv_ref_cnt=" << ctx_ptr->recv_ref_cnt 
        //  << ",bytes_transferred=" << bytes_transferred
        //);
        char* complete_packet_data = new (std::nothrow) char[bytes_transferred]; //XXX TODO !!!
        if (complete_packet_data == nullptr) {
            ELOG("mem alloc failed");
            exit(EXIT_FAILURE);
        }
        if (cumbuffer::OP_RSLT_OK != ctx_ptr->per_recv_io_ctx->cum_buffer.
            GetData(bytes_transferred, complete_packet_data)) {
            //error !
            err_msg_ = ctx_ptr->per_recv_io_ctx->cum_buffer.GetErrMsg();
            //ctx_ptr->per_recv_io_ctx->is_packet_len_calculated = false;
            delete[] complete_packet_data; //XXX
            DBG_ELOG("error! ");
            //exit(1);
            return false;
        }
        //XXX UDP 이므로 받는 버퍼를 초기화해서, linear free space를 초기화 상태로 
        ctx_ptr->GetBuffer()->ReSet(); //this is udp. all data has arrived!
        //DBG_LOG("worker=" << worker_id << ",sock=" << ctx_ptr->sock_id_copy 
        //  << ",recv_ref_cnt=" << ctx_ptr->recv_ref_cnt 
        //  << ":got complete data [" << complete_packet_data << "]");

        if (cb_on_recved_complete_packet_ != nullptr) {
            //invoke user specific callback
            cb_on_recved_complete_packet_(ctx_ptr, complete_packet_data+HEADER_SIZE, 
                                          bytes_transferred - HEADER_SIZE ); //udp
        } else {
            //invoke user specific implementation
            OnRecvedCompleteData(ctx_ptr, complete_packet_data+HEADER_SIZE, 
                                 bytes_transferred-HEADER_SIZE); //udp
        }
        delete[] complete_packet_data; //XXX
        return true;
    }

    //-----------------------------------------------------
    size_t  OnCalculateDataLen(Context* ctx_ptr) {
        //DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
        //  " / capacity : " << ctx_ptr->GetBuffer()->GetCapacity() <<
        //  " / total free : " << ctx_ptr->GetBuffer()->GetTotalFreeSpace() <<
        //  " / linear free : " << ctx_ptr->GetBuffer()->GetLinearFreeSpace() <<
        //  " / cumulated : " << ctx_ptr->GetBuffer()->GetCumulatedLen());

        if (ctx_ptr->GetBuffer()->GetCumulatedLen() < (int)HEADER_SIZE) {
            DBG_LOG("more to come");
            return asock::MORE_TO_COME; //more to come 
        }
        ST_HEADER header;
        ctx_ptr->GetBuffer()->PeekData(HEADER_SIZE, (char*)&header);
        size_t supposed_total_len = std::atoi(header.msg_len) + HEADER_SIZE;
        if (supposed_total_len > ctx_ptr->GetBuffer()->GetCapacity()) {
            //DBG_RED_LOG("(usg:" << this->sock_usage_ << ")sock:" 
            //  << ctx_ptr->socket << " / packet length : " << supposed_total_len 
            //  << " / buffer is insufficient => increase buffer");
            
            // If the size of the data to be received is larger than the buffer, 
            // increase the buffer capacity.
            size_t new_buffer_len= supposed_total_len * 2;
            if(!ctx_ptr->GetBuffer()->IncreaseBufferAndCopyExisting(new_buffer_len)){
                ELOG(ctx_ptr->GetBuffer()->GetErrMsg());
                exit(EXIT_FAILURE);
            }
            SetBufferCapacity(new_buffer_len);

            //DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
            //  " / capacity : " << ctx_ptr->GetBuffer()->GetCapacity() <<
            //  " / total free : " << ctx_ptr->GetBuffer()->GetTotalFreeSpace() <<
            //  " / linear free : " << ctx_ptr->GetBuffer()->GetLinearFreeSpace() <<
            //  " / cumulated : " << ctx_ptr->GetBuffer()->GetCumulatedLen());
        };
        DBG_LOG("(usg:" << this->sock_usage_ << ")sock:" << ctx_ptr->socket <<
                " / packet length : " << supposed_total_len);
        return supposed_total_len;
    }

    virtual bool OnRecvedCompleteData(Context* ,const char* const, size_t) {
        std::cerr << "ERROR! OnRecvedCompleteData not implemented!\n";
        return false;
    }

protected :
    std::string  server_ip_ ;
    std::string  server_ipc_socket_path_ = "";
    int server_port_ {-1};
};
} //namaspace
#endif
