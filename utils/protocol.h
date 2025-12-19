#pragma once

/* Shared constants */
#define MAX_CLIENTS 200
#define MAX_GROUPS 50
#define IP_INPUT_MAX 40
#define PORT_INPUT_MAX 7
#define CLIENT_NAME_INPUT_MAX 62
#define NETWORK_MESSAGE_BUFFER_SIZE 8192
#define AES_KEY_SIZE 32
#define AES_BLOCK_SIZE 16
#define FILE_CHUNK_RAW 2048
#define FILE_TRANSFER_MAX_BYTES (1024 * 1024) /* ~1 MB */

#define DB_FILE_PATH "chat_app.db"
#define UI_CONNECTION_PATH "gui/connection_dialog.glade"
#define UI_MAIN_PATH "gui/main_window.glade"

/* Legacy channel tags kept for compatibility */
#define MESSAGE_TYPE_BROADCAST "ALL"
#define MESSAGE_TYPE_DM_PREFIX "DM:"
#define MESSAGE_TYPE_GROUP_PREFIX "GROUP:"
#define MESSAGE_TYPE_FILE "FILE"
#define MESSAGE_FORMAT "%s %s"

/* New protocol layout */
#include <stdint.h>
#include <string.h>

#define DEFAULT_PORT            8080
#define MAX_BUFFER_SIZE         4096
#define MAX_TOPIC_LEN           32
#define MAX_USERNAME_LEN        32

typedef enum {
    MSG_LOGIN = 1,
    MSG_LOGOUT,
    MSG_SUBSCRIBE,
    MSG_UNSUBSCRIBE,
    MSG_PUBLISH_TEXT,
    MSG_PUBLISH_FILE,
    MSG_FILE_DATA,
    MSG_ERROR,
    MSG_ACK,
    MSG_GAME_INVITE,
    MSG_GAME_ACCEPT,
    MSG_GAME_MOVE,
    MSG_GAME_END
} MessageType;

#if defined(_MSC_VER)
#  define PACKED_STRUCT(name) __pragma(pack(push, 1)) struct name __pragma(pack(pop))
#else
#  define PACKED_STRUCT(name) struct __attribute__((packed)) name
#endif

PACKED_STRUCT(PacketHeader) {
    uint32_t msgType;
    uint32_t payloadLength;
    uint32_t messageId;
    uint64_t timestamp;
    uint8_t  version;
    uint8_t  flags;
    char     sender[MAX_USERNAME_LEN];
    char     topic[MAX_TOPIC_LEN];
    uint32_t checksum;
};
typedef struct PacketHeader PacketHeader;
