#pragma once

#define _DOT4_IFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DOT4_MAX_CHANNELS                 128
#define NO_TIMEOUT                          0
#define STREAM_TYPE_CHANNEL                 1
#define PACKET_TYPE_CHANNEL                 2
#define DOT4_STREAM_RECEIVED            0x100
#define DOT4_STREAM_CREDITS             0x101
#define DOT4_MESSAGE_RECEIVED           0x102
#define DOT4_DISCONNECT                 0x103
#define DOT4_CHANNEL_CLOSED             0x105
#define DOT4_CHANNEL                    0
#define HP_MESSAGE_PROCESSOR            1
#define PRINTER_CHANNEL                 2
#define SCANNER_CHANNEL                 4
#define MIO_COMMAND_PROCESSOR           5
#define ECHO_CHANNEL                    6
#define FAX_SEND_CHANNEL                7
#define FAX_RECV_CHANNEL                8
#define DIAGNOSTIC_CHANNEL              9
#define HP_RESERVED                     10
#define IMAGE_DOWNLOAD                  11
#define HOST_DATASTORE_UPLOAD           12
#define HOST_DATASTORE_DOWNLOAD         13
#define CONFIG_UPLOAD                   14
#define CONFIG_DOWNLOAD                 15

typedef unsigned long CHANNEL_HANDLE, *PCHANNEL_HANDLE;

typedef struct _DOT4_ACTIVITY {
  ULONG ulMessage;
  ULONG ulByteCount;
  CHANNEL_HANDLE hChannel;
} DOT4_ACTIVITY, *PDOT4_ACTIVITY;

#ifdef __cplusplus
}
#endif
