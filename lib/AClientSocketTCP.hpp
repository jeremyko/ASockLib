
#ifndef __A_CLIENT_SOCKET_TCP_HPP__
#define __A_CLIENT_SOCKET_TCP_HPP__

#include "ASockBase.hpp"
#include "CumBuffer.h"
#include <atomic>

#define ROLE_CLIENT

class AClientSocketTCP : public ASockBase
{

    public :
        AClientSocketTCP() ;
        virtual ~AClientSocketTCP() ;

        bool            Connect(const char* connIP, int nPort, int nConnectTimeoutSecs=10 );
        bool            SendToServer (const char* packet, int sendSize) ; 
        int             GetSocket () { return  context_.socket_ ; }
        void            Disconnect() ;
        bool            IsConnected() { return bConnected_;}

    private :
        std::atomic<bool>    bClientThreadRunning_ {false};
        bool            bCumBufferInit_ {false};
        SOCKADDR_IN     connAddr_ ;
        bool            bConnected_ {false};
        Context         context_;

    private :
        void            ClientThreadRoutine();

        //socket events
        virtual void    OnDisConnected() =0; 
};

#endif

