#include <chrono>
#include <iostream>
#include <cassert>
#include <csignal>
#include <gtest/gtest.h>
#include "asock/asock_ipc_client.hpp"
#include "asock/asock_ipc_server.hpp"

#define DEFAULT_PACKET_SIZE 1024
// NOTE: Not implemented on Windows.
// This file is created in the current directory and automatically deleted.
#define TEST_IPC_PATH "asock.test.ipc"

//////////////////////////////////////////////////////////////////////// server
class Server {
  public:
    Server(){}
    bool RunIpcServer();
    bool IsServerRunning(){
        return ipc_server_.IsServerRunning();
    }
    void StopServer(){ 
        ipc_server_.StopServer();
    }
    std::string GetLastErrMsg(){
        return  ipc_server_.GetLastErrMsg();
    }
    asock::ASockIpcServer ipc_server_ ;
    std::string cli_msg_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context* ctx_ptr, const char* const data_ptr, size_t len) ;
};

static Server* this_instance_ = nullptr;

bool Server::RunIpcServer() {
    this_instance_ = this; 
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    ipc_server_.SetCbOnRecvedCompletePacket(std::bind(
                        &Server::OnRecvedCompleteData, this, _1,_2,_3));

    if(!ipc_server_.RunIpcServer(TEST_IPC_PATH)) {
        std::cerr << ipc_server_.GetLastErrMsg() <<"\n"; 
        return false;
    }
    return true;
}

bool Server::OnRecvedCompleteData(asock::Context* ctx_ptr, const char* const data_ptr, size_t len){
    //user specific : your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE];
    memcpy(&packet, data_ptr, len);
    packet[len] = '\0';
    cli_msg_ = packet;
    //LOG("server get client msg [" << cli_msg_ << "]" << " len="<< cli_msg_.length() );
    //---------------------------------------
    //this is echo server
    if (!ipc_server_.SendData(  ctx_ptr, data_ptr, len)) {
        ELOG( "error! "<< ipc_server_.GetLastErrMsg() ); 
        return false;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////// client 
class Client {
  public:
    bool IntIpcClient();
    bool SendToServer(const char* data, size_t len) ;
    void DisConnect();
    bool IsConnected(){ 
        return ipc_client_.IsConnected();
    }
    std::string  GetLastErrMsg(){
        return  ipc_client_.GetLastErrMsg();
    }
    size_t client_id_;
    std::string svr_res_ = "";
  private:
    asock::ASockIpcClient ipc_client_ ; //composite usage
    bool OnRecvedCompleteData(asock::Context* context_ptr, const char* const data_ptr, size_t len);
};

bool Client::IntIpcClient() {
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    ipc_client_.SetCbOnRecvedCompletePacket(std::bind(
                        &Client::OnRecvedCompleteData, this, _1,_2,_3));
 
    if(!ipc_client_.InitIpcClient(TEST_IPC_PATH) ) {
        ELOG("error : "<< ipc_client_.GetLastErrMsg() ); 
        return false;
    }
    return true;
}

bool Client::SendToServer (const char* data, size_t len) {
    return ipc_client_.SendToServer(data, len);
}

void Client::DisConnect() {
    ipc_client_.Disconnect();
}

bool Client::OnRecvedCompleteData(asock::Context* , const char* const data_ptr, size_t len) {
    //user specific : your whole data has arrived.
    char packet[DEFAULT_PACKET_SIZE]; 
    memcpy(&packet, data_ptr,len);
    packet[len] = '\0';
    svr_res_ = packet;
    //LOG("client get server response [" << packet << "] len=" << len );
    return true;
}

///////////////////////////////////////////////////////////////////////////////
TEST(IpcTest, SendRecv) {
    Server server;
    Client client;

    //--- Run server, client
    EXPECT_TRUE(server.RunIpcServer());
    EXPECT_TRUE(client.IntIpcClient());

    //--- Waiting for initialization to complete.
    while(!server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "==> server started\n";
    while (!client.IsConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "==> client started\n";

    //--- Create a test message and send it to the server.
    std::string test_msg (25, 'x'); 
    EXPECT_TRUE(client.SendToServer(test_msg.c_str(),test_msg.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    std::cout << "==> msg : [" << test_msg << " len : "<< test_msg.length() << "]\n";
    std::cout << "==> svr : [" << server.cli_msg_ << " len : "<< server.cli_msg_.length() << "]\n";
    std::cout << "==> cli : [" << client.svr_res_ << " len : "<< client.svr_res_.length() << "]\n";
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg);
    EXPECT_EQ(client.svr_res_, test_msg);

    std::string test_msg_2 (100, 'y'); 
    EXPECT_TRUE(client.SendToServer(test_msg_2.c_str(),test_msg_2.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    std::cout << "==> msg : [" << test_msg_2 << " len : "<< test_msg_2.length() << "]\n";
    std::cout << "==> svr : [" << server.cli_msg_ << " len : "<< server.cli_msg_.length() << "]\n";
    std::cout << "==> cli : [" << client.svr_res_ << " len : "<< client.svr_res_.length() << "]\n";

    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg_2);
    EXPECT_EQ(client.svr_res_, test_msg_2);

    //--- Start termination procedure
    client.DisConnect();
    server.StopServer();

    std::cout << "==> exiting " << "\n";
}


