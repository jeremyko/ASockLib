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

#define TEST_IPC_PATH "asock.test.ipc"
#define BUFFER_SIZE 1024
// make buffer insufficient for test
#define INSUFFICIENT_BUFFER_SIZE (10 + 20) // HEADER_SIZE(10) + user_data(20)

////////////////////////////////////////////////////////////////////////////////
template <typename Base>
class Server : public Base {
  public:
    std::string cli_msg_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context* ctx_ptr,
                              const char* const data_ptr, size_t len) override {
        char packet[BUFFER_SIZE];
        memcpy(&packet, data_ptr, len);
        packet[len] = '\0';
        cli_msg_ = packet;
        if (!this->SendData(  ctx_ptr, data_ptr, len)) {
            ELOG("error! "<< this->GetLastErrMsg());
            return false;
        }
        return true;
    }
};

////////////////////////////////////////////////////////////////////////////////
template <typename Base>
class Client : public Base {
  public:
    std::string svr_res_ = "";
  private:
    bool OnRecvedCompleteData(asock::Context*,
                              const char* const data_ptr, size_t len) override {
        char packet[BUFFER_SIZE];
        memcpy(&packet, data_ptr,len);
        packet[len] = '\0';
        svr_res_ = packet;
        return true;
    }
};

///////////////////////////////////////////////////////////////////////////////
TEST(AllTest, SendRecv) {
	std::string test_msg(25, 'x');
	//----------------------------------- buffer test
    Server<asock::ASockTcpServer> serverTcp1;
    Client<asock::ASockTcpClient> clientTcp1;
	EXPECT_TRUE(serverTcp1.RunTcpServer("127.0.0.1", 9990, INSUFFICIENT_BUFFER_SIZE));
	EXPECT_TRUE(clientTcp1.InitTcpClient("127.0.0.1", 9990, 3, INSUFFICIENT_BUFFER_SIZE));
    EXPECT_TRUE(clientTcp1.SendToServer(test_msg.c_str(), test_msg.length()));
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	LOG("BUF ==> svr : [" << serverTcp1.cli_msg_ << " len : " << serverTcp1.cli_msg_.length()<<"]");
	LOG("    ==> cli : [" << clientTcp1.svr_res_ << " len : " << clientTcp1.svr_res_.length()<<"]");
    EXPECT_EQ(serverTcp1.cli_msg_, test_msg);
    EXPECT_EQ(clientTcp1.svr_res_, test_msg);
	clientTcp1.Disconnect();
	serverTcp1.StopServer();
	//----------------------------------- tcp test
    Server<asock::ASockTcpServer> serverTcp2;
    Client<asock::ASockTcpClient> clientTcp2;
	EXPECT_TRUE(serverTcp2.RunTcpServer("127.0.0.1", 9990));
	EXPECT_TRUE(clientTcp2.InitTcpClient("127.0.0.1", 9990));
	EXPECT_TRUE(clientTcp2.SendToServer(test_msg.c_str(),test_msg.length()));
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	LOG( "TCP ==> svr : [" << serverTcp2.cli_msg_ << " len : " << serverTcp2.cli_msg_.length() <<"]");
	LOG( "    ==> cli : [" << clientTcp2.svr_res_ << " len : " << clientTcp2.svr_res_.length() <<"]");
    EXPECT_EQ(serverTcp2.cli_msg_, test_msg);
    EXPECT_EQ(clientTcp2.svr_res_, test_msg);
	clientTcp2.Disconnect();
	serverTcp2.StopServer();
	//----------------------------------- udp test
    Server<asock::ASockUdpServer> serverUdp;
    Client<asock::ASockUdpClient> clientUdp;
    EXPECT_TRUE(serverUdp.RunUdpServer("127.0.0.1", 9990, BUFFER_SIZE));
	EXPECT_TRUE(clientUdp.InitUdpClient("127.0.0.1", 9990, BUFFER_SIZE));
	EXPECT_TRUE(clientUdp.SendToServer(test_msg.c_str(), test_msg.length()));
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	LOG("UDP ==> svr : [" << serverUdp.cli_msg_ << " len : " << serverUdp.cli_msg_.length()<<"]");
	LOG("    ==> cli : [" << clientUdp.svr_res_ << " len : " << clientUdp.svr_res_.length()<<"]");
    EXPECT_EQ(serverUdp.cli_msg_, test_msg);
    EXPECT_EQ(clientUdp.svr_res_, test_msg);
	clientUdp.Disconnect();
	serverUdp.StopServer();
    //----------------------------------- ipc test
    Server<asock::ASockIpcServer> serverIpc;
    Client<asock::ASockIpcClient> clientIpc;
    EXPECT_TRUE(serverIpc.RunIpcServer(TEST_IPC_PATH));
	EXPECT_TRUE(clientIpc.InitIpcClient(TEST_IPC_PATH));
    EXPECT_TRUE(clientIpc.SendToServer(test_msg.c_str(), test_msg.length()));
	std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
	LOG( "IPC ==> svr : [" << serverIpc.cli_msg_ << " len : "<< serverIpc.cli_msg_.length() <<"]");
	LOG( "    ==> cli : [" << clientIpc.svr_res_ << " len : "<< clientIpc.svr_res_.length() <<"]");
    EXPECT_EQ(serverIpc.cli_msg_, test_msg);
    EXPECT_EQ(clientIpc.svr_res_, test_msg);
	clientIpc.Disconnect();
	serverIpc.StopServer();
}

