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

#ifndef ASOCKWINCLIENT_HPP
#define ASOCKWINCLIENT_HPP

#include <atomic>
#include <thread>
#include <queue>
#include <chrono>
#include "asock_win_base.hpp"

namespace asock {
////////////////////////////////////////////////////////////////////////////////
class ASockClientBase : public ASockWinBase {
public :
    virtual ~ASockClientBase() {
		Disconnect();
        WSACleanup();
    }

    //-------------------------------------------------------------------------
    bool SendToServer(const char* data, size_t len) {
        std::lock_guard<std::mutex> lock(client_ctx_.ctx_lock); // XXX lock
        size_t total_sent = 0;           
        size_t total_len = len + HEADER_SIZE;
        if ( !is_connected_ ) {
            err_msg_ = "not connected";
            return false;
        }
        if (client_ctx_.socket == INVALID_SOCKET) {
            err_msg_ = "not connected";
            return false;
        }

        ST_HEADER header;
        snprintf(header.msg_len, sizeof(header.msg_len), "%d", (int)len);

        //if sent is pending, just push to queue. 
        if(client_ctx_.is_sent_pending){
            DBG_LOG("pending. just queue.");
            PENDING_SENT pending_sent;
            pending_sent.pending_sent_data = new char [len+HEADER_SIZE]; //xxx C6386 warning
            pending_sent.pending_sent_len  = len+HEADER_SIZE;
            memcpy(pending_sent.pending_sent_data, (char*)&header, HEADER_SIZE);
            memcpy(pending_sent.pending_sent_data+HEADER_SIZE, data, len);
            client_ctx_.pending_send_deque.push_back(pending_sent);
            return true;
        }

		char* data_buffer = new char[len+HEADER_SIZE];
		char* data_position_ptr = data_buffer;
		memcpy(data_position_ptr, (char*)&header, HEADER_SIZE);
		memcpy(data_position_ptr + HEADER_SIZE, data, len);

        if (sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
			int sent_len=sendto(client_ctx_.socket, data_position_ptr, (int)total_len, 0,
							  (SOCKADDR*)&udp_server_addr_, sizeof(udp_server_addr_)); 
            if (sent_len != total_len) {
				ELOG("udp partial sent error :" << sent_len << "/" << total_len);
				shutdown(client_ctx_.socket, SD_BOTH);
				if (0 != closesocket(client_ctx_.socket)) {
					ELOG("close socket error! : " << WSAGetLastError());
				}
				client_ctx_.socket = INVALID_SOCKET;
				delete[] data_buffer;
				return false;
            }
        } else {
            while( total_sent < total_len ) {
                int sent_len =0;
                size_t remained_len = total_len - total_sent;
                sent_len = send(client_ctx_.socket, data_position_ptr, (int)(remained_len), 0);
                if(sent_len > 0) {
               	    total_sent += sent_len ;  
               	    data_position_ptr += sent_len ;      
                } else if( sent_len == SOCKET_ERROR ) {
                	if (WSAEWOULDBLOCK == WSAGetLastError()) {
                		//send later
                		PENDING_SENT pending_sent;
                		pending_sent.pending_sent_data = new char [remained_len]; 
                		pending_sent.pending_sent_len = remained_len;
                		memcpy(pending_sent.pending_sent_data,data_position_ptr, remained_len);
                		client_ctx_.pending_send_deque.push_back(pending_sent);
                		client_ctx_.is_sent_pending = true;
                		delete[] data_buffer;
                		return true;
                	} else {
                		BuildErrMsgString(WSAGetLastError());
                		shutdown(client_ctx_.socket, SD_BOTH);
                		if (0 != closesocket(client_ctx_.socket)) {
                			DBG_ELOG("close socket error! : " << WSAGetLastError());
                		}
                		client_ctx_.socket = INVALID_SOCKET;
                		delete[] data_buffer;
                		return false;
                	}
                }
            }//while
        }
		delete[] data_buffer;
        return true;
    }
    //-------------------------------------------------------------------------
    bool SetCbOnDisconnectedFromServer(std::function<void()> cb) {
        //for composition usage 
        if(cb != nullptr) {
            cb_on_disconnected_from_server_ = cb;
        } else {
            err_msg_ = "callback is null";
            return false;
        }
        return true;
    }
    //-------------------------------------------------------------------------
    SOCKET_T  GetSocket () { 
        return  client_ctx_.socket ; 
    }
    //-------------------------------------------------------------------------
    bool IsConnected() { 
        if (is_connected_ && is_client_thread_running_) {
            return true;
        }
        return false;
    }
    //-------------------------------------------------------------------------
    void Disconnect() {
        is_connected_ = false;
        if(client_ctx_.socket != INVALID_SOCKET ) {
            shutdown(client_ctx_.socket, SD_BOTH);
            closesocket(client_ctx_.socket);
        }
        client_ctx_.socket = INVALID_SOCKET;
        //wait thread exit
        if (client_thread_.joinable()) {
            client_thread_.join();
        }
        if (client_ctx_.per_recv_io_ctx) {
            delete client_ctx_.per_recv_io_ctx;
            client_ctx_.per_recv_io_ctx = NULL; // 소멸자에서도 호출되기 때문
        }
        while(!client_ctx_.pending_send_deque.empty() ) {
            PENDING_SENT pending_sent= client_ctx_.pending_send_deque.front();
            delete [] pending_sent.pending_sent_data;
            client_ctx_.pending_send_deque.pop_front();
        }
    }

protected :
    int  connect_timeout_secs_{ 10 };
    bool is_buffer_init_ {false};
    std::atomic<bool> is_connected_ {false};
    Context     client_ctx_; 
    SOCKADDR_IN tcp_server_addr_ ;
    SOCKADDR_IN udp_server_addr_ ;
    SOCKADDR_UN ipc_conn_addr_;
    std::thread client_thread_;
    std::atomic<bool> is_client_thread_running_ {false};
    std::string server_ip_ ;
    std::string server_ipc_socket_path_ = "";
    int         server_port_ {-1};

    //-------------------------------------------------------------------------
	bool ClientSendPendingData() {
		std::lock_guard<std::mutex> guard(client_ctx_.ctx_lock); 
		while(!client_ctx_.pending_send_deque.empty()) {
			DBG_LOG("pending exists");
			PENDING_SENT pending_sent = client_ctx_.pending_send_deque.front();
			int sent_len = 0;
			 if ( sock_usage_ == SOCK_USAGE_UDP_CLIENT ) {
				//XXX UDP : all or nothing . no partial sent!
				sent_len = sendto(client_ctx_.socket,  pending_sent.pending_sent_data, 
								  (int)pending_sent.pending_sent_len,
								  0, (SOCKADDR*)&udp_server_addr_, 
								  sizeof(udp_server_addr_));
			} else {
				sent_len = send(client_ctx_.socket, pending_sent.pending_sent_data, 
								(int)pending_sent.pending_sent_len, 0) ;
			}
			if( sent_len > 0 ) {
				if(sent_len == pending_sent.pending_sent_len) {
					delete [] pending_sent.pending_sent_data;
					client_ctx_.pending_send_deque.pop_front();
					if(client_ctx_.pending_send_deque.empty()) {
						//sent all data
						client_ctx_.is_sent_pending = false;
						DBG_LOG("all pending is sent");
						break;
					}
				} else {
					//TCP : partial sent ---> 남은 부분을 다시 제일 처음으로
					DBG_LOG("partial pending is sent, retry next time");
					PENDING_SENT partial_pending_sent;
					size_t alloc_len = pending_sent.pending_sent_len - sent_len;
					partial_pending_sent.pending_sent_data = new char [alloc_len]; 
					partial_pending_sent.pending_sent_len  = alloc_len;
					memcpy( partial_pending_sent.pending_sent_data, 
							pending_sent.pending_sent_data+sent_len, alloc_len);
					//remove first.
					delete [] pending_sent.pending_sent_data;
					client_ctx_.pending_send_deque.pop_front();
					//push_front
					client_ctx_.pending_send_deque.push_front(partial_pending_sent);
					break; //next time
				}
			} else if( sent_len == SOCKET_ERROR ) {
				if (WSAEWOULDBLOCK == WSAGetLastError()) {
					break; //next time
				} else {
					char err_buf[80];
					strerror_s(err_buf, 80, errno);
					err_msg_ = "send error ["  + std::string(err_buf) + "]";
					BuildErrMsgString(WSAGetLastError());
					InvokeServerDisconnectedHandler();
					shutdown(client_ctx_.socket, SD_BOTH);
					if (0 != closesocket(client_ctx_.socket)) {
						DBG_ELOG("close socket error! : " << WSAGetLastError());
					}
					client_ctx_.socket = INVALID_SOCKET;
					return false; 
				} 
			} 
		} //while
		return true;
	}

    //-------------------------------------------------------------------------
    bool ConnectToServer() {
        if(is_connected_ ){
            return true;
        }
		if (!SetSocketNonBlocking(client_ctx_.socket)) {
			closesocket(client_ctx_.socket);
			return  false;
		}
        if (!is_buffer_init_) {
            if (cumbuffer::OP_RSLT_OK != client_ctx_.per_recv_io_ctx->cum_buffer.Init(max_data_len_)) {
                err_msg_ = std::string("cumBuffer Init error :") + 
                            client_ctx_.per_recv_io_ctx->cum_buffer.GetErrMsg();
                closesocket(client_ctx_.socket);
                client_ctx_.socket = INVALID_SOCKET;
                ELOG(err_msg_);
                return false;
            }
            is_buffer_init_ = true;
        } else {
            //in case of reconnect
            client_ctx_.per_recv_io_ctx->cum_buffer.ReSet();
        }
        if (sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
            if (!SetSockoptSndRcvBufUdp(client_ctx_.socket)) {
                closesocket(client_ctx_.socket);
                return false;
            }
			is_connected_ = true;
			return RunClientThread();
        }

        struct timeval timeout_val;
        timeout_val.tv_sec  = connect_timeout_secs_;
        timeout_val.tv_usec = 0;

        int result = -1;
        if ( sock_usage_ == SOCK_USAGE_IPC_CLIENT ) {
            result = connect(client_ctx_.socket, (SOCKADDR*)&ipc_conn_addr_,
                             (SOCKLEN_T)sizeof(SOCKADDR_UN));
        } else if ( sock_usage_ == SOCK_USAGE_TCP_CLIENT ) {
            result = connect(client_ctx_.socket, (SOCKADDR*)&tcp_server_addr_, 
                             (SOCKLEN_T)sizeof(SOCKADDR_IN));
        }
		if (result == SOCKET_ERROR) {
			int last_err = WSAGetLastError();
			if (last_err != WSAEWOULDBLOCK) {
				BuildErrMsgString(last_err);
				return false;
			}
		}
		WSASetLastError(0);
		//wait for connected
		int wait_timeout_ms = connect_timeout_secs_ * 1000;
		WSAPOLLFD fdArray = { 0 };
		fdArray.fd = client_ctx_.socket;
		fdArray.events = POLLWRNORM;
		result = WSAPoll(&fdArray, 1, wait_timeout_ms);
		//LOG("connect WSAPoll returns....: result =" << result);
		if (SOCKET_ERROR == result) {
			BuildErrMsgString(WSAGetLastError());
			return false;
		}
		if (result) {
			if (fdArray.revents & POLLWRNORM) {
				//LOG("Established connection");
			} else {
				err_msg_ = "connect timeout";
				LOG(err_msg_);
				return false;
			}
		}
		is_connected_ = true;
		return RunClientThread();
    }
    //-------------------------------------------------------------------------
    bool RunClientThread() {
        if (!is_client_thread_running_) {
            client_thread_ = std::thread (&ASockClientBase::ClientThreadRoutine, this);
            is_client_thread_running_ = true;  //todo 이거 없애야함
        }
        return true;
    }
    //-------------------------------------------------------------------------
    void ClientThreadRoutine() {
        //is_client_thread_running_ = true; //todo 이렇게 되게 수정해야 함
        int wait_timeout_ms =  10 ;
        while (is_connected_) { 
            WSAPOLLFD fdArray = { 0 };
            if (client_ctx_.socket == INVALID_SOCKET) {
                ELOG( "INVALID_SOCKET" ) ;
                is_client_thread_running_ = false;
                return;
            }
            fdArray.fd = client_ctx_.socket;
            fdArray.revents = 0;
            int result = -1;
            //================================== 
            //fdArray.events = POLLRDNORM | POLLWRNORM; // XXX 이런식으로 하면 안됨? 
            fdArray.events = POLLRDNORM ;
            result = WSAPoll(&fdArray, 1, wait_timeout_ms);
            if (SOCKET_ERROR == result) {
                is_client_thread_running_ = false;
                if (!is_connected_) { 
					//LOG("client thread exiting...");
                    return;//no error.
                }
                BuildErrMsgString(WSAGetLastError());
                return ;
            }
            if (result == 0) {
                //timeout
            }else{
                if (!is_connected_) {
                    is_client_thread_running_ = false;
					//LOG("client thread exiting...");
                    return;//no error.
                }
                if (fdArray.revents & POLLRDNORM) {  
                    //============================
                    size_t want_recv_len = max_data_len_;
                    //-------------------------------------
                    size_t free_linear_len = client_ctx_.GetBuffer()->GetLinearFreeSpace();
                    if (max_data_len_ > free_linear_len) { 
                        want_recv_len = free_linear_len;
                    }
                    if (want_recv_len == 0) {
                        err_msg_ = "no linear free space left ";
                        ELOG(err_msg_);
                        is_client_thread_running_ = false;
                        return ;
                    }
                    //============================
                    if (client_ctx_.socket == INVALID_SOCKET) {
                        ELOG("INVALID_SOCKET");
                        is_client_thread_running_ = false;
                        return;
                    }
                    int ret = -1;
                    if (sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
                        SOCKLEN_T addrlen = sizeof(client_ctx_.udp_remote_addr);
                        ret = recvfrom(client_ctx_.socket, //--> is listen_socket_
                                        client_ctx_.GetBuffer()->GetLinearAppendPtr(),
                                        int(max_data_len_),
                                        0,
                                        (struct sockaddr*)&client_ctx_.udp_remote_addr,
                                        &addrlen);
                    } else {
                        ret = recv(client_ctx_.socket, 
                                   client_ctx_.GetBuffer()->GetLinearAppendPtr(), 
                                   (int) want_recv_len, 0);
                    }
                    if (SOCKET_ERROR == ret) {
                        BuildErrMsgString(WSAGetLastError());
                        is_client_thread_running_ = false;
                        return;
                    } else {
                        DBG_LOG("client recved : " << ret <<" bytes");
                        if (ret == 0) {
                            shutdown(client_ctx_.socket, SD_BOTH);
                            closesocket(client_ctx_.socket);
                            is_connected_ = false;
							InvokeServerDisconnectedHandler();
                            client_ctx_.socket = INVALID_SOCKET; //XXX
							//LOG("client thread exiting...");
                            is_client_thread_running_ = false;
                            return;
                        }
                        bool isOk = false;
                        if (sock_usage_ == SOCK_USAGE_UDP_CLIENT) {
                            isOk = RecvfromData(0, &client_ctx_, ret);
                        } else {
                            isOk = RecvData(0, &client_ctx_, ret);
                        }
                        if (!isOk) {
                            ELOG("client exit");
                            shutdown(client_ctx_.socket, SD_BOTH);
                            if (0 != closesocket(client_ctx_.socket)) {
                                DBG_ELOG("close socket error! : " << WSAGetLastError());
                            }
                            is_connected_ = false;
                            client_ctx_.socket = INVALID_SOCKET;
							InvokeServerDisconnectedHandler();
                            is_client_thread_running_ = false;
                            return;
                        }
                    }
                } else if (fdArray.revents & POLLHUP) {
                    DBG_LOG("POLLHUP");
                    shutdown(client_ctx_.socket, SD_BOTH);
                    if (0 != closesocket(client_ctx_.socket)) {
                        DBG_ELOG("close socket error! : " << WSAGetLastError());
                    }
                    is_connected_ = false;
                    client_ctx_.socket = INVALID_SOCKET;
					InvokeServerDisconnectedHandler();
                    is_client_thread_running_ = false;
                    return;
                }
            }
            //================================== send pending if any
            fdArray.fd = client_ctx_.socket;
            fdArray.revents = 0;
            fdArray.events = POLLWRNORM;
            result = WSAPoll(&fdArray, 1, 0);
            if (SOCKET_ERROR == result) {
                is_client_thread_running_ = false;
                if (!is_connected_) { 
                    return;//no error.
                }
                BuildErrMsgString(WSAGetLastError());
                return ;
            }
            if (result == 0) {
                //timeout
            } else{
                 if (fdArray.revents & POLLWRNORM) {
                    if (!ClientSendPendingData()) {
                        is_client_thread_running_ = false;
                        return; //error!
                    }
                }
            }
            //================================== 
        } //while(true)
        is_client_thread_running_ = false;
    }
    //-------------------------------------------------------------------------
    void InvokeServerDisconnectedHandler() {
        if (cb_on_disconnected_from_server_ != nullptr) {
            cb_on_disconnected_from_server_();
        } else {
            OnDisconnectedFromServer();
        }
    }
    //-------------------------------------------------------------------------
    std::function<void()> cb_on_disconnected_from_server_ {nullptr} ;
    virtual void  OnDisconnectedFromServer() {}; 
};
} //namaspace
#endif
