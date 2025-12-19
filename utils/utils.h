#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <gtk/gtk.h>

#include "protocol.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#ifndef ssize_t
typedef SSIZE_T ssize_t;
#endif
#define close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

// openssl...
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <sqlite3.h>

#define UNUSED(x) (void)(x)

#define LOG_INFO(format, ...)  g_print("[INFO]: " format "\n", ##__VA_ARGS__)
#define LOG_ERROR(format, ...) fprintf(stderr, "[ERROR]: " format "\n", ##__VA_ARGS__)
#define LOG_SUCCESS(format, ...) g_print("[SUCCESS]: " format "\n", ##__VA_ARGS__)

#define CARO_BOARD_MAX 15

int set_workdir_to_project_root(void);

typedef struct CaroState{
    GtkBuilder *builder;
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *scroller;
    GtkWidget *status_label;
    GtkWidget *opponent_label;
    GtkWidget *turn_label;
    GtkWidget *play_again_btn;
    GtkWidget *overlay;
    GtkWidget *win_label;
    GtkWidget *cells[CARO_BOARD_MAX][CARO_BOARD_MAX];
    char board[CARO_BOARD_MAX][CARO_BOARD_MAX];
    int board_size;
    int win_length;
    char opponent[MAX_USERNAME_LEN];
    gboolean in_game;
    gboolean waiting_accept;
    gboolean my_turn;
    char my_symbol;
    char opp_symbol;
}CaroState;

typedef struct clientDetails{
    int clientSocketFD;
    char *clientName;
    struct sockaddr *serverAddress;
    RSA *public_key;
    const unsigned char *aes_key;
    char active_target[CLIENT_NAME_INPUT_MAX];
    gboolean active_target_is_group;
    gboolean group_joined;
    CaroState caro;
}clientDetails;


typedef struct SecurityKeys{
    RSA *private_key; // RSA format
    char* public_key; // string format
}SecurityKeys;

typedef struct DbContext{
    sqlite3 *db;
    pthread_mutex_t lock;
}DbContext;


typedef struct RMWGUI {
    clientDetails *clientD;
    GtkBuilder* builder;
}RMWGUI;


typedef struct CDBHData{
    clientDetails* data;
    GtkBuilder* builder;
    GtkWidget* connection_dialog;
    gboolean connection_status;
}CDBHData;


typedef struct SMData{
    clientDetails* data;
    GtkBuilder* builder;
}SMData;


typedef struct SMHPack{
    clientDetails* data;
    GtkBuilder* builder;
    gboolean status;
}SMHPack;


typedef struct serverDetails{
    int serverSocketFD;
    struct sockaddr *serverAddress;
    int *clientFDStore;
    unsigned char *client_aes_keyStore[MAX_CLIENTS];
    char *clientNames[MAX_CLIENTS];
    SecurityKeys *keys;
    DbContext db;
    char *groupNames[MAX_GROUPS];
    int groupMembers[MAX_GROUPS][MAX_CLIENTS];
    int groupCounts[MAX_GROUPS];
    
}serverDetails;


typedef struct HNAC{
    int *clientSocketFD;
    serverDetails *serverD;
}HNAC;


int setupClient(clientDetails *clientD);
int setupClientFromGUI(clientDetails *clientD, GtkBuilder* builder);
int setupServer(serverDetails *serverD);
void *handleOtherOperationsOnSeperateThread(void*);
void *handleNewlyAcceptedClient(void *);
void broadcastMessage(char *clientUsername, char *receivedMessage, int currentClientFD, int *clientFDStore, unsigned char **client_aes_keyStore);
void cleanup(clientDetails *clientD);
void process_public_key(char *received_key_str, RSA **client_public_key);
void broadcast_presence(serverDetails *serverD);

void *sendMessages(void *clientD_ptr);
void *sendMessagesWithGUI(void *pack_ptr);
void *receiveMessages(void *clientD_ptr);
void *receiveMessagesWithGUI(void *clientD_ptr);

int get_socket();
// struct sockaddr *get_address();
struct sockaddr *get_address(int *ui_port, const char* ui_ip);
char *get_client_name(const char* ui_client_name);
// char *get_client_name();


void connection_dialog_button_handler(GtkWidget* button, CDBHData *pack);
void send_message_handler(GtkWidget *button, SMHPack* pack);
void add_to_messages_interface(GtkBuilder* builder, const char* message, gboolean is_sent, const char* sender_username, const char *open_path);
int send_file_base64(clientDetails *clientD, const char *filepath, const char *target, gboolean is_group);
void send_file_button_handler(GtkWidget *button, SMHPack* pack);

int file_exists(const char *filename);
char *bytes_to_base64_encode(const unsigned char *data, size_t len);
unsigned char *base64_to_bytes_decode(const char *b64_data, size_t *out_len);
unsigned char* decrypt_aes_key(RSA* rsa_private_key, const char* encrypted_aes_key_str);
unsigned char *generate_aes_key(size_t key_size);
char* encrypt_with_aes(const char* plaintext, const unsigned char* aes_key, const unsigned char* iv);
char* decrypt_with_aes(const char* encoded_ciphertext, const unsigned char* aes_key, const unsigned char* iv);
char *sanitize_base64(const char *input);
int aes_encrypt_bytes(const unsigned char *plaintext, size_t plaintext_len,
                      const unsigned char *aes_key, const unsigned char *iv,
                      unsigned char **out_cipher, size_t *out_len);
int aes_decrypt_bytes(const unsigned char *cipher, size_t cipher_len,
                      const unsigned char *aes_key, const unsigned char *iv,
                      unsigned char **out_plain, size_t *out_len);

int db_init(DbContext *ctx, const char *path);
void db_close(DbContext *ctx);
int db_set_user_online(DbContext *ctx, const char *username);
int db_set_user_offline(DbContext *ctx, const char *username);
int db_save_message(DbContext *ctx, const char *sender, const char *receiver, const char *body);
int db_group_join(DbContext *ctx, const char *username, const char *groupname);
int db_group_leave(DbContext *ctx, const char *username, const char *groupname);
int db_group_leave_all(DbContext *ctx, const char *username);

void refresh_online_users(GtkBuilder* builder, clientDetails *clientD, const char* csv_users);
void on_online_user_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
void refresh_groups(GtkBuilder* builder, clientDetails *clientD, const char* csv_groups);
void on_group_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
void on_group_create(GtkButton *button, gpointer user_data);
void on_group_join(GtkButton *button, gpointer user_data);
void caro_invite_button_handler(GtkWidget *button, SMData *pack);
void caro_init_state(CaroState *state, GtkBuilder *builder);
void caro_handle_invite(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload);
void caro_handle_accept(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload);
void caro_handle_move(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload);
void caro_handle_end(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload);

int send_framed_packet(int fd, const unsigned char *payload, size_t len);
int recv_framed_packet(int fd, unsigned char **out, size_t *out_len);
int perform_client_handshake(clientDetails *clientD);
int send_protocol_packet(int fd, PacketHeader *hdr, const unsigned char *payload, size_t payload_len, const unsigned char *aes_key);
int recv_protocol_packet(int fd, PacketHeader *hdr_out, unsigned char **payload_out, size_t *payload_len_out, const unsigned char *aes_key);
