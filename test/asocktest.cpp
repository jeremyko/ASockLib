#include <chrono>
#include <cassert>
#include <csignal>
#include <gtest/gtest.h>
#include "asock/asock_tcp_client.hpp"
#include "asock/asock_tcp_server.hpp"
#include "asock/asock_ipc_client.hpp"
#include "asock/asock_ipc_server.hpp"
#include "asock/asock_udp_server.hpp"
#include "asock/asock_udp_client.hpp"

#define BUFFER_SIZE 1024
#define TEST_IPC_PATH "asock.test.ipc"

// !!! make buffer insufficient for test !!! 
#define INSUFFICIENT_BUFFER_SIZE (10 + 20) // HEADER_SIZE(10) + user_data(20)

/////////////////////////////////////////////////////////////////////////// TCP
class ServerTcpTest : public asock::ASockTcpServer  {
  public:
    std::string cli_msg_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context* ctx_ptr,
                              const char* const data_ptr, size_t len) override {
        char packet[BUFFER_SIZE];
        memcpy(&packet, data_ptr, len);
        packet[len] = '\0';
        cli_msg_ = packet;
        // LOG("server get client msg [" << cli_msg_ << "]" << " len="<< cli_msg_.length() );
        if (!SendData(  ctx_ptr, data_ptr, len)) {
            ELOG("error! "<< GetLastErrMsg());
            return false;
        }
        return true;
    }
};

class ClientTcpTest : public asock::ASockTcpClient  {
  public:
    std::string svr_res_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context*,
                              const char* const data_ptr, size_t len) override {
        char packet[BUFFER_SIZE];
        memcpy(&packet, data_ptr,len);
        packet[len] = '\0';
        svr_res_ = packet;
        //LOG("client get server response [" << packet << "] len=" << len );
        return true;
    }
};

//////////////////////////////////////////////////////////////////////// IPC
class ServerIpcTest : public asock::ASockIpcServer {
  public:
    std::string cli_msg_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context* ctx_ptr,
                              const char* const data_ptr, size_t len) override {
        char packet[BUFFER_SIZE];
        memcpy(&packet, data_ptr, len);
        packet[len] = '\0';
        cli_msg_ = packet;
        //LOG("server get client msg [" << cli_msg_ << "]" << " len="<< cli_msg_.length() );
        //---------------------------------------
        if (!SendData(ctx_ptr, data_ptr, len)) {
            ELOG("error! "<< GetLastErrMsg());
            return false;
        }
        return true;
    }
};

class ClientIpcTest : public asock::ASockIpcClient {
  public:
    std::string svr_res_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context*,
                              const char* const data_ptr, size_t len) override {
        char packet[BUFFER_SIZE]; 
        memcpy(&packet, data_ptr,len);
        packet[len] = '\0';
        svr_res_ = packet;
        //LOG("client get server response [" << packet << "] len=" << len );
        return true;
    }
};

//////////////////////////////////////////////////////////////////////// UDP
class ServerUdpTest : public asock::ASockUdpServer {
  public:
    std::string cli_msg_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context* ctx_ptr,
                              const char* const data_ptr, size_t len) override {
        // note : this buffer must be large enough to receive the data sent.
        char packet[BUFFER_SIZE];
        memcpy(&packet, data_ptr, len);
        packet[len] = '\0';
        cli_msg_ = packet;
        //LOG("server get client msg [" << cli_msg_ << "]" << " len="<< cli_msg_.length() );
        if (!SendData(ctx_ptr, data_ptr, len)) {
            ELOG( "error! "<< GetLastErrMsg());
            return false;
        }
        return true;
    }
};

class ClientUdpTest : public asock::ASockUdpClient {
  public:
    std::string svr_res_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context*,
                              const char* const data_ptr, size_t len) override {
        // note : this buffer must be large enough to receive the data sent.
        char packet[BUFFER_SIZE];
        memcpy(&packet, data_ptr,len);
        packet[len] = '\0';
        svr_res_ = packet;
        //LOG("client get server response [" << packet << "] len=" << len );
        return true;
    }
};

///////////////////////////////////////////////////////////////////////////////
// To ensure that buffer reallocation occurs internally and is handled normally
// when sending or receiving a message that exceeds the allocated buffer size.
TEST(BufferTestTcp, IncreaseCapacity) {
    ServerTcpTest server;
    ClientTcpTest client;
    //--- Run server, client
    EXPECT_TRUE(server.RunTcpServer("127.0.0.1", 9990, INSUFFICIENT_BUFFER_SIZE));
    EXPECT_TRUE(client.InitTcpClient("127.0.0.1", 9990, 3, INSUFFICIENT_BUFFER_SIZE));
    //--- Waiting for initialization to complete.
    while(!server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!client.IsConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
    }
    //--- Create a test message and send it to the server.
    // note : 
    // The size of this message(25) exceeds the allocated buffer size(20),
    // so a buffer reallocation occurs internally. And that's the purpose of this test.
    std::string test_msg (25, 'a'); 
    EXPECT_TRUE(client.SendToServer(test_msg.c_str(),test_msg.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg);
    EXPECT_EQ(client.svr_res_, test_msg);
    std::string test_msg_2 (100, 'b'); 
    EXPECT_TRUE(client.SendToServer(test_msg_2.c_str(),test_msg_2.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    // std::cout << "==> msg : [" << test_msg_2 << " len : "<< test_msg_2.length() << "]\n";
    // std::cout << "==> svr : [" << server.cli_msg_ << " len : "<< server.cli_msg_.length() << "]\n";
    // std::cout << "==> cli : [" << client.svr_res_ << " len : "<< client.svr_res_.length() << "]\n";
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg_2);
    EXPECT_EQ(client.svr_res_, test_msg_2);
    //--- Start termination procedure
    client.Disconnect();
    server.StopServer();
}

///////////////////////////////////////////////////////////////////////////////
TEST(IpcTest, SendRecv) {
    ServerIpcTest server;
    ClientIpcTest client;
    //--- Run server, client
    EXPECT_TRUE(server.RunIpcServer(TEST_IPC_PATH));
    EXPECT_TRUE(client.InitIpcClient(TEST_IPC_PATH));
    //--- Waiting for initialization to complete.
    while(!server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!client.IsConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    //--- Create a test message and send it to the server.
    std::string test_msg (25, 'c'); 
    EXPECT_TRUE(client.SendToServer(test_msg.c_str(),test_msg.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg);
    EXPECT_EQ(client.svr_res_, test_msg);
    std::string test_msg_2 (100, 'd'); 
    EXPECT_TRUE(client.SendToServer(test_msg_2.c_str(),test_msg_2.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    // std::cout << "==> msg : [" << test_msg_2 << " len : "<< test_msg_2.length() << "]\n";
    // std::cout << "==> svr : [" << server.cli_msg_ << " len : "<< server.cli_msg_.length() << "]\n";
    // std::cout << "==> cli : [" << client.svr_res_ << " len : "<< client.svr_res_.length() << "]\n";
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg_2);
    EXPECT_EQ(client.svr_res_, test_msg_2);
    //--- Start termination procedure
    client.Disconnect();
    server.StopServer();
}


///////////////////////////////////////////////////////////////////////////////
TEST(UdpTest, SendRecv) {
    ServerUdpTest server;
    ClientUdpTest client;
    //--- Run server, client
    EXPECT_TRUE(server.RunUdpServer("127.0.0.1", 9990, BUFFER_SIZE));
    EXPECT_TRUE(client.InitUdpClient("127.0.0.1", 9990, BUFFER_SIZE));
    //--- Waiting for initialization to complete.
    while(!server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!client.IsConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    //--- Create a test message and send it to the server.
    std::string test_msg (25, 'g'); 
    EXPECT_TRUE(client.SendToServer(test_msg.c_str(),test_msg.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg);
    EXPECT_EQ(client.svr_res_, test_msg);

    std::string test_msg_2 (100, 'h'); 
    EXPECT_TRUE(client.SendToServer(test_msg_2.c_str(),test_msg_2.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg_2);
    EXPECT_EQ(client.svr_res_, test_msg_2);
    //--- Start termination procedure
    client.Disconnect();
    server.StopServer();
}

///////////////////////////////////////////////////////////////////////////////
TEST(TcpTest, SendRecv) {
    ServerTcpTest server;
    ClientTcpTest client;
    //--- Run server, client
    EXPECT_TRUE(server.RunTcpServer("127.0.0.1", 9990));
    EXPECT_TRUE(client.InitTcpClient("127.0.0.1", 9990));
    //--- Waiting for initialization to complete.
    while(!server.IsServerRunning() ) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!client.IsConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    //--- Create a test message and send it to the server.
    std::string test_msg (25, 'e'); 
    EXPECT_TRUE(client.SendToServer(test_msg.c_str(),test_msg.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg);
    EXPECT_EQ(client.svr_res_, test_msg);

    std::string test_msg_2 (100, 'f'); 
    EXPECT_TRUE(client.SendToServer(test_msg_2.c_str(),test_msg_2.length()));
    //--- Waiting to confirm receipt of message.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    //--- Verify that the sent and received messages are identical.
    EXPECT_EQ(server.cli_msg_, test_msg_2);
    EXPECT_EQ(client.svr_res_, test_msg_2);

    //--- Start termination procedure
    client.Disconnect();
    server.StopServer();
}


