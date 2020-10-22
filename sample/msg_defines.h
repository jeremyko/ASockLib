
#ifndef __SAMPLE_MSG_DEFINES_H__
#define __SAMPLE_MSG_DEFINES_H__

//user specific define
typedef struct _ST_MY_CHAT_HEADER_
{
    char msg_len[10+1];

} ST_MY_HEADER ;
#define CHAT_HEADER_SIZE sizeof(ST_MY_HEADER)

#endif

