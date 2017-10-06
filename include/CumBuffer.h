#ifndef __CUMBUFFER_HPP__
#define __CUMBUFFER_HPP__
/****************************************************************************
 Copyright (c) 2016, ko jung hyun
 
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

// NO THREAD SAFETY HERE !!! 
///////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <string>
#include <string.h>
#include <stdint.h>
#include <cstdlib>

//#define CUMBUFFER_DEBUG

namespace cumbuffer_defines
{
    const int DEFAULT_BUFFER_LEN = 1024 * 4;

    enum OP_RESULT
    {
        OP_RSLT_OK = 0,
        OP_RSLT_NO_DATA,
        OP_RSLT_BUFFER_FULL,
        OP_RSLT_ALLOC_FAILED,
        OP_RSLT_INVALID_LEN,
        OP_RSLT_INVALID_USAGE
    } ;
} ;

///////////////////////////////////////////////////////////////////////////////
class CumBuffer
{
  public:
    CumBuffer() 
    {
        pBuffer_=NULL; 
        nCumulatedLen_=0;
        nCurHead_=0;
        nCurTail_=0;
        nBufferLen_=0;
    }

    virtual ~CumBuffer() { if(pBuffer_) { delete [] pBuffer_; } };

    //------------------------------------------------------------------------
    cumbuffer_defines::OP_RESULT    Init(int nMaxBufferLen = cumbuffer_defines::DEFAULT_BUFFER_LEN)
    {
        nBufferLen_ = nMaxBufferLen;

        try
        {
            pBuffer_ = new char [nBufferLen_];
        }
        catch (std::exception& e)
        {
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] alloc failed :"<<e.what()  <<"\n"; 
            strErrMsg_="alloc failed :";
            strErrMsg_+= e.what();
            return cumbuffer_defines::OP_RSLT_ALLOC_FAILED;
        }

        return cumbuffer_defines::OP_RSLT_OK;
    }

    //------------------------------------------------------------------------
    cumbuffer_defines::OP_RESULT    Append(size_t nLen, char* pData)
    {
#ifdef CUMBUFFER_DEBUG
        std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] nLen="<<nLen<< "["<< pData<<"]\n";  
        DebugPos(__LINE__);
#endif

        if( nBufferLen_ < nLen )
        {
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] invalid len :"<<nLen <<"\n"; 
            strErrMsg_="invalid length";
            return cumbuffer_defines::OP_RSLT_INVALID_LEN;
        }
        else if( nBufferLen_ ==  nCumulatedLen_ )
        {
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] buffer full" <<"\n"; 
            strErrMsg_="buffer full";
            return cumbuffer_defines::OP_RSLT_BUFFER_FULL;
        }

        if(nCurTail_ < nCurHead_)
        {
            //tail 이 버퍼 끝을 지난 경우
            if(nCurHead_ - nCurTail_ >= nLen)
            {
                memcpy(pBuffer_ + nCurTail_, pData, nLen);
                nCurTail_ += nLen;
                nCumulatedLen_ += nLen;
#ifdef CUMBUFFER_DEBUG
                DebugPos(__LINE__);
#endif
                return cumbuffer_defines::OP_RSLT_OK;
            }
            else
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] buffer full" <<"\n"; 
                strErrMsg_="buffer full";
                return cumbuffer_defines::OP_RSLT_BUFFER_FULL;
            }
        }
        else
        {
            if (nBufferLen_ < nCurTail_ + nLen)
            {
                //tail 이후, 남은 버퍼로 모자라는 경우
                if( nCurTail_ > 0 && 
                    nLen - (nBufferLen_ - nCurTail_)  <= nCurHead_ )
                {
                    //2번 나누어서 들어갈 공간이 있는 경우
#ifdef CUMBUFFER_DEBUG
                    DebugPos(__LINE__);
#endif

                    int nFirstBlockLen = nBufferLen_ - nCurTail_;
                    int nSecondBlockLen = nLen - nFirstBlockLen;

#ifdef CUMBUFFER_DEBUG
                    std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] nFirstBlockLen ="<<nFirstBlockLen  
                              << "/nSecondBlockLen="<<nSecondBlockLen<<"\n"; 
#endif

                    if(nFirstBlockLen>0)
                    {
                        memcpy(pBuffer_+ nCurTail_ , pData, nFirstBlockLen); 
                    }

                    memcpy(pBuffer_ , pData+(nFirstBlockLen), nSecondBlockLen); 

                    nCurTail_ = nSecondBlockLen;
                    nCumulatedLen_ += nLen;
#ifdef CUMBUFFER_DEBUG
                    DebugPos(__LINE__);
#endif
                    return cumbuffer_defines::OP_RSLT_OK;
                }
                else
                {
                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] buffer full" <<"\n"; 
                    strErrMsg_="buffer full";
                    return cumbuffer_defines::OP_RSLT_BUFFER_FULL;
                }
            }
            else
            {
                //most general case
                memcpy(pBuffer_+nCurTail_ , pData, nLen); 
                nCurTail_ += nLen;
                nCumulatedLen_ += nLen;
#ifdef CUMBUFFER_DEBUG
                DebugPos(__LINE__);
#endif
                return cumbuffer_defines::OP_RSLT_OK;
            }
        }

        return cumbuffer_defines::OP_RSLT_OK;
    }

    //------------------------------------------------------------------------
    cumbuffer_defines::OP_RESULT    PeekData(size_t nLen, char* pDataOut)
    {
        return GetData(nLen, pDataOut, true, false);
    }

    //------------------------------------------------------------------------
    cumbuffer_defines::OP_RESULT    ConsumeData(size_t nLen)
    {
        //PeekData 사용해서 처리한 data length 만큼 버퍼내 nCurHead_ 를 이동.
        return GetData(nLen, NULL, false, true);
    }

    //------------------------------------------------------------------------
    cumbuffer_defines::OP_RESULT    GetData(size_t  nLen, 
                                            char*   pDataOut, 
                                            bool    bPeek=false, 
                                            bool    bMoveHeaderOnly=false)
    {
        if(bPeek && bMoveHeaderOnly)
        {
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] invalid usage" <<"\n"; 
            strErrMsg_="invalid usage";
            return cumbuffer_defines::OP_RSLT_INVALID_USAGE;
        }

#ifdef CUMBUFFER_DEBUG
        DebugPos(__LINE__);
#endif

        cumbuffer_defines::OP_RESULT nRslt = ValidateBuffer(nLen);
        if(cumbuffer_defines::OP_RSLT_OK!=nRslt )
        {
            std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] invalid buffer :"<<strErrMsg_ <<"\n"; 
            return nRslt;
        }

        if(nCurTail_ > nCurHead_)
        {
            //일반적인 경우
            if (nCurTail_ < nCurHead_ + nLen)
            {
                std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] invalid length :"<<nLen <<"\n"; 
                strErrMsg_="invalid length";
                return cumbuffer_defines:: OP_RSLT_INVALID_LEN;
            }
            else
            {
                if(!bMoveHeaderOnly)
                {
                    memcpy(pDataOut, pBuffer_ + nCurHead_, nLen);
                }
                if(!bPeek)
                {
                    nCurHead_ += nLen;
                }
            }
        }
        else// if(nCurTail_ <= nCurHead_)
        {
            if (nBufferLen_ < nCurHead_ + nLen)
            {
                size_t nFirstBlockLen = nBufferLen_ - nCurHead_;
                size_t nSecondBlockLen = nLen - nFirstBlockLen;
#ifdef CUMBUFFER_DEBUG
                std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] nFirstBlockLen="<<nFirstBlockLen  
                          << "/nSecondBlockLen="<<nSecondBlockLen<<"\n"; 
#endif

                if( nCurTail_ > 0 && 
                    nCurTail_ >= nSecondBlockLen )
                {
                    if(!bMoveHeaderOnly)
                    {
                        memcpy(pDataOut , pBuffer_+nCurHead_, nFirstBlockLen); 
                        memcpy(pDataOut+nFirstBlockLen , pBuffer_, nSecondBlockLen); 
                    }

                    if(!bPeek)
                    {
                        nCurHead_ =nSecondBlockLen ;
                    }
                }
                else
                {
                    std::cerr <<"["<< __func__ <<"-"<<__LINE__ <<"] invalid length :"<<nLen  
                              <<" / nFirstBlockLen ="<<nFirstBlockLen << "/nSecondBlockLen="<<nSecondBlockLen<<"\n"; 
                    strErrMsg_="invalid length";
                    return cumbuffer_defines:: OP_RSLT_INVALID_LEN;
                }
            }
            else
            {
                if(!bMoveHeaderOnly)
                {
                    memcpy(pDataOut, pBuffer_ + nCurHead_, nLen);
                }

                if(!bPeek)
                {
                    nCurHead_ += nLen;
                }
            }
        }

        if(!bPeek)
        {
            nCumulatedLen_ -= nLen;
        }

#ifdef CUMBUFFER_DEBUG
        std::cout <<"["<< __func__ <<"-"<<__LINE__ <<"] out data ["<<pDataOut<<"]\n";
        DebugPos(__LINE__);
#endif

        return cumbuffer_defines::OP_RSLT_OK;
    }

    //------------------------------------------------------------------------
    cumbuffer_defines::OP_RESULT ValidateBuffer(size_t nLen)
    {
        if(nCumulatedLen_ == 0 )
        {
            strErrMsg_="no data";
            return cumbuffer_defines::OP_RSLT_NO_DATA;
        }
        else if(nCumulatedLen_ < nLen)
        {
            strErrMsg_="invalid length";
            return cumbuffer_defines:: OP_RSLT_INVALID_LEN;
        }

        return cumbuffer_defines::OP_RSLT_OK;
    }
        
    //------------------------------------------------------------------------
    size_t GetCumulatedLen()
    {
        return nCumulatedLen_ ;
    }

    //------------------------------------------------------------------------
    size_t GetCapacity()
    {
        return nBufferLen_ ;
    }

    //------------------------------------------------------------------------
    size_t GetTotalFreeSpace()
    {
        return nBufferLen_  - nCumulatedLen_;
    }

    //------------------------------------------------------------------------
    uint64_t GetCurHeadPos()
    {
        return nCurHead_; 
    }

    //------------------------------------------------------------------------
    uint64_t GetCurTailPos()
    {
        return nCurTail_; 
    }

    //------------------------------------------------------------------------
    uint64_t GetLinearFreeSpace() //for direct buffer write
    {
        //current maximun linear buffer size

        if(nCurTail_==nBufferLen_) //nCurTail_ is at last position
        {
            return nBufferLen_ - nCumulatedLen_ ; 
        }
        else if(nCurHead_ < nCurTail_)
        {
            return nBufferLen_- nCurTail_; 
        }
        else if(nCurHead_ > nCurTail_)
        {
            return nCurHead_-nCurTail_; 
        }
        else
        {
            return nBufferLen_- nCurTail_;
        }
    }

    //------------------------------------------------------------------------
    char* GetLinearAppendPtr() //for direct buffer write
    {
        if(nCurTail_==nBufferLen_) //nCurTail_ is at last position
        {
            if(nBufferLen_!= nCumulatedLen_) //and buffer has free space
            {
                //-> append at 0  
                //nCurTail_ -> 버퍼 마지막 위치하고, 버퍼에 공간이 존재. -> 처음에 저장
                //XXX dangerous XXX 
                //this is not a simple get function, nCurTail_ changes !!
                nCurTail_ = 0;
            }
        }

        return (pBuffer_+ nCurTail_);
    }

    //------------------------------------------------------------------------
    void IncreaseData(size_t nLen)
    {
        nCurTail_+= nLen;
        nCumulatedLen_ +=nLen;
    }

    //------------------------------------------------------------------------
    void    DebugPos(int nLine)
    {
        std::cout <<"line=" <<nLine<<"/ nCurHead_=" << nCurHead_  << "/ nCurTail_= "  << nCurTail_ 
                  <<" / nBufferLen_=" << nBufferLen_
                  <<" / nCumulatedLen_=" << nCumulatedLen_
                  <<"\n";
    }

    //------------------------------------------------------------------------
    void ReSet()
    {
        nCumulatedLen_=0;
        nCurHead_=0;
        nCurTail_=0;
    }

    std::string GetErrMsg() { return strErrMsg_ ; }

  private:
    CumBuffer(const CumBuffer&) ; //= delete;
    CumBuffer& operator = (const CumBuffer &) ; //= delete;

    std::string strErrMsg_;
    char*       pBuffer_;
    size_t      nBufferLen_;
    size_t      nCumulatedLen_;

    uint64_t    nCurHead_  ; 
    uint64_t    nCurTail_  ; 
}
;

#endif




