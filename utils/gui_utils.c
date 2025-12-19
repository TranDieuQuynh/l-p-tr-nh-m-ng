#include "utils.h"
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <shellapi.h>
#endif

typedef struct {
    GtkBuilder *builder;
    char *message;
    char *sender;
    gboolean is_sent;
    char *open_path;
} UiMsgPayload;

typedef struct {
    GtkBuilder *builder;
    clientDetails *clientD;
    char *csv;
} UiPresencePayload;

static gboolean idle_add_message_cb(gpointer data) {
    UiMsgPayload *p = (UiMsgPayload *)data;
    add_to_messages_interface(p->builder, p->message, p->is_sent, p->sender, p->open_path);
    g_object_unref(p->builder);
    free(p->message);
    free(p->sender);
    if (p->open_path) free(p->open_path);
    free(p);
    return FALSE;
}

static gboolean idle_refresh_presence_cb(gpointer data) {
    UiPresencePayload *p = (UiPresencePayload *)data;
    refresh_online_users(p->builder, p->clientD, p->csv);
    g_object_unref(p->builder);
    free(p->csv);
    free(p);
    return FALSE;
}

static gboolean idle_refresh_groups_cb(gpointer data) {
    UiPresencePayload *p = (UiPresencePayload *)data;
    refresh_groups(p->builder, p->clientD, p->csv);
    g_object_unref(p->builder);
    free(p->csv);
    free(p);
    return FALSE;
}

typedef struct {
    clientDetails *clientD;
    GtkBuilder *builder;
    char *from;
    char *payload;
    MessageType type;
} UiGamePayload;

static gboolean idle_game_dispatch_cb(gpointer data) {
    UiGamePayload *p = (UiGamePayload *)data;
    switch (p->type) {
        case MSG_GAME_INVITE:
            caro_handle_invite(p->clientD, p->builder, p->from, p->payload ? p->payload : "");
            break;
        case MSG_GAME_ACCEPT:
            caro_handle_accept(p->clientD, p->builder, p->from, p->payload ? p->payload : "");
            break;
        case MSG_GAME_MOVE:
            caro_handle_move(p->clientD, p->builder, p->from, p->payload ? p->payload : "");
            break;
        case MSG_GAME_END:
            caro_handle_end(p->clientD, p->builder, p->from, p->payload ? p->payload : "");
            break;
        default:
            break;
    }
    g_object_unref(p->builder);
    if (p->from) free(p->from);
    if (p->payload) free(p->payload);
    free(p);
    return FALSE;
}

int setupClientFromGUI(clientDetails *clientD, GtkBuilder* builder) {
    clientD->clientSocketFD = get_socket();
    if (clientD->clientSocketFD == -1) {
        LOG_ERROR(" [ creating Client Socket Process Failed ]\n\n");
        return -1;
    }

    GtkWidget* connection_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "connection_dialog"));
    GtkWidget* connection_dialog_button = GTK_WIDGET(gtk_builder_get_object(builder, "connection_dialog_button"));

    CDBHData *pack = (CDBHData *)malloc(sizeof(CDBHData));
    pack->data = clientD;
    pack->builder = builder;
    pack->connection_dialog = connection_dialog;

    g_signal_connect(connection_dialog_button, "clicked", G_CALLBACK(connection_dialog_button_handler), pack);

    gint response = gtk_dialog_run(GTK_DIALOG(connection_dialog));
    UNUSED(response);
    gtk_widget_hide(connection_dialog);

    return 0;
}

void connection_dialog_button_handler(GtkWidget* button, CDBHData *pack) {
    UNUSED(button);

    GtkWidget* ip_entry;
    GtkWidget* port_entry;
    GtkWidget* user_name_entry;

    ip_entry = GTK_WIDGET(gtk_builder_get_object(pack->builder, "connection_dialog_ip_entry"));
    port_entry = GTK_WIDGET(gtk_builder_get_object(pack->builder, "connection_dialog_port_entry"));
    user_name_entry = GTK_WIDGET(gtk_builder_get_object(pack->builder, "connection_dialog_username_entry"));

    int port;

    const char* ip = gtk_entry_get_text(GTK_ENTRY(ip_entry));
    const char* port_str = gtk_entry_get_text(GTK_ENTRY(port_entry));
    const char* username = gtk_entry_get_text(GTK_ENTRY(user_name_entry));

    if (strlen(ip) < 1 || strlen(port_str) < 1 || strlen(username) < 1) {
        LOG_ERROR("All fields are required.");
        return;
    }

    char* endptr;
    errno = 0;

    port = (int)strtol(port_str, &endptr, 10);
    if (*endptr != '\0' && *endptr != '\n') {
        LOG_ERROR("Please input a correct PORT no. eg [ 2000 ]\n");
        return;
    }

    pack->data->serverAddress = get_address(&port, ip);
    pack->data->clientName = get_client_name(username);
    memset(pack->data->active_target, 0, sizeof(pack->data->active_target));
    pack->data->active_target_is_group = FALSE;

    if (connect(pack->data->clientSocketFD, pack->data->serverAddress, sizeof(*(pack->data->serverAddress))) < 0) {
        LOG_ERROR("Failed to connect to server: %s", strerror(errno));
    } else {
        LOG_SUCCESS("Successfully connected to the server.");
        pack->connection_status = TRUE;
    }

    gtk_dialog_response(GTK_DIALOG(pack->connection_dialog), GTK_RESPONSE_OK);
}

void *sendMessagesWithGUI(void *pack_ptr) {
    SMData *pack = (SMData *)pack_ptr;

    GtkWidget* send_button = GTK_WIDGET(gtk_builder_get_object(pack->builder, "send_button"));
    GtkWidget* send_file_button = GTK_WIDGET(gtk_builder_get_object(pack->builder, "send_file_button"));
    GtkWidget* group_button = GTK_WIDGET(gtk_builder_get_object(pack->builder, "group_create_button"));
    GtkWidget* group_join_button = GTK_WIDGET(gtk_builder_get_object(pack->builder, "group_join_button"));
    GtkWidget* group_list = GTK_WIDGET(gtk_builder_get_object(pack->builder, "group_list"));
    GtkWidget* caro_invite_button = GTK_WIDGET(gtk_builder_get_object(pack->builder, "caro_invite_button"));

    SMHPack *smh_pack = malloc(sizeof(SMHPack));
    smh_pack->data = pack->data;
    smh_pack->builder = pack->builder;

    g_signal_connect(send_button, "clicked", G_CALLBACK(send_message_handler), smh_pack);
    if (send_file_button) {
        g_signal_connect(send_file_button, "clicked", G_CALLBACK(send_file_button_handler), smh_pack);
    }
    if (group_button) {
        g_signal_connect(group_button, "clicked", G_CALLBACK(on_group_create), pack);
    }
    if (group_join_button) {
        g_signal_connect(group_join_button, "clicked", G_CALLBACK(on_group_join), pack);
    }
    if (group_list) {
        g_signal_connect(group_list, "row-selected", G_CALLBACK(on_group_selected), pack);
    }
    if (caro_invite_button) {
        g_signal_connect(caro_invite_button, "clicked", G_CALLBACK(caro_invite_button_handler), pack);
    }

    return NULL;
}

static const char *basename_from_path(const char *path) {
    if (!path) return NULL;
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && backslash) base = (slash > backslash) ? slash + 1 : backslash + 1;
    else if (slash) base = slash + 1;
    else if (backslash) base = backslash + 1;
    return base;
}

int send_file_base64(clientDetails *clientD, const char *filepath, const char *target, gboolean is_group) {
    if (!clientD || !clientD->aes_key || !filepath || !target) return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        LOG_ERROR("Cannot open file: %s", filepath);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    if (sz > (long)FILE_TRANSFER_MAX_BYTES) {
        LOG_ERROR("File too large for transfer limit (~1MB).");
        fclose(f);
        return -1;
    }
    rewind(f);
    unsigned char *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    size_t readn = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (readn != (size_t)sz) { free(buf); return -1; }

    char *b64 = bytes_to_base64_encode(buf, (size_t)sz);
    free(buf);
    if (!b64) return -1;

    const char *fname = basename_from_path(filepath);
    if (!fname || strlen(fname) == 0) fname = "file.bin";

    size_t payload_cap = strlen(fname) + 1 + strlen(b64) + 1;
    char *payload = malloc(payload_cap);
    if (!payload) { free(b64); return -1; }
    snprintf(payload, payload_cap, "%s|%s", fname, b64);
    free(b64);

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_PUBLISH_FILE;
    hdr.version = 1;
    static uint32_t g_file_msg_id = 30000;
    hdr.messageId = g_file_msg_id++;
    hdr.timestamp = (uint64_t)time(NULL);
    strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, target, MAX_TOPIC_LEN - 1);
    hdr.flags = is_group ? 0x1 : 0x0;

    int rc = send_protocol_packet(clientD->clientSocketFD, &hdr, (unsigned char *)payload, strlen(payload), clientD->aes_key);
    free(payload);
    return rc;
}

void send_file_button_handler(GtkWidget *button, SMHPack* pack) {
    UNUSED(button);
    if (!pack || !pack->data || !pack->builder) return;
    clientDetails *clientD = pack->data;

    if (strlen(clientD->active_target) == 0) {
        LOG_ERROR("Select a user or group before sending a file.");
        return;
    }
    if (clientD->active_target_is_group && !clientD->group_joined) {
        LOG_ERROR("Join the group before sending a file.");
        return;
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select file",
                                                    NULL,
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    if (!dialog) return;

    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    if (resp == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            if (send_file_base64(clientD, filename, clientD->active_target, clientD->active_target_is_group) != 0) {
                LOG_ERROR("Send file failed.");
            } else {
                char note[512];
                snprintf(note, sizeof(note), "You sent file: %s", filename);
                add_to_messages_interface(pack->builder, note, TRUE, "File", filename);
            }
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

void send_message_handler(GtkWidget *button, SMHPack* pack) {
    UNUSED(button);

    GtkWidget* message_entry = GTK_WIDGET(gtk_builder_get_object(pack->builder, "message_entry"));

    const char* message = gtk_entry_get_text(GTK_ENTRY(message_entry));

    if (strlen(message) >= 1) {
        const char *target = NULL;
        if (strlen(pack->data->active_target) > 0) {
            target = pack->data->active_target;
        }

        // File send command: /sendfile <path>
        if (g_str_has_prefix(message, "/sendfile ")) {
            const char *path = message + strlen("/sendfile ");
            if (!target) {
                LOG_ERROR("Select a user or group before sending a file.");
                gtk_entry_set_text(GTK_ENTRY(message_entry), "");
                return;
            }
            if (pack->data->active_target_is_group && !pack->data->group_joined) {
                LOG_ERROR("Join the group before sending a file.");
                gtk_entry_set_text(GTK_ENTRY(message_entry), "");
                return;
            }
            if (send_file_base64(pack->data, path, target, pack->data->active_target_is_group) != 0) {
                LOG_ERROR("Send file failed.");
            } else {
                char note[CLIENT_NAME_INPUT_MAX * 2 + 128];
                snprintf(note, sizeof(note), "You sent file: %s", path);
                add_to_messages_interface(pack->builder, note, TRUE, "File", path);
            }
            gtk_entry_set_text(GTK_ENTRY(message_entry), "");
            return;
        }

        char header[CLIENT_NAME_INPUT_MAX * 2];
        if (target) {
            if (pack->data->active_target_is_group) {
                if (!pack->data->group_joined) {
                    LOG_ERROR("Please join the group before sending messages.");
                    gtk_entry_set_text(GTK_ENTRY(message_entry), "");
                    return;
                }
                snprintf(header, sizeof(header), "You -> #%s", target);
            } else {
                snprintf(header, sizeof(header), "You -> %s", target);
            }
        } else {
            snprintf(header, sizeof(header), "You");
        }
        add_to_messages_interface(pack->builder, message, TRUE, header, NULL);

        PacketHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.msgType = MSG_PUBLISH_TEXT;
        hdr.version = 1;
        static uint32_t g_msg_id = 1;
        hdr.messageId = g_msg_id++;
        hdr.timestamp = (uint64_t)time(NULL);
        strncpy(hdr.sender, pack->data->clientName, MAX_USERNAME_LEN - 1);

        if (target) {
            strncpy(hdr.topic, target, MAX_TOPIC_LEN - 1);
            hdr.flags = pack->data->active_target_is_group ? 0x1 : 0x0;
        } else {
            strncpy(hdr.topic, MESSAGE_TYPE_BROADCAST, MAX_TOPIC_LEN - 1);
            hdr.flags = 0x0;
        }

        size_t payload_len = strlen(message);
        if (send_protocol_packet(pack->data->clientSocketFD, &hdr, (const unsigned char *)message, payload_len, pack->data->aes_key) != 0) {
            pack->status = FALSE;
            LOG_ERROR("Send failed");
        } else {
            pack->status = TRUE;
        }
    }
    gtk_entry_set_text(GTK_ENTRY(message_entry), "");
}

static void on_open_file_clicked(GtkButton *button, gpointer user_data) {
    UNUSED(user_data);
    const char *path = g_object_get_data(G_OBJECT(button), "open_path");
    if (!path) return;
#ifdef _WIN32
    ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
#else
    gchar *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri) {
        GError *err = NULL;
        if (!g_app_info_launch_default_for_uri(uri, NULL, &err)) {
            if (err) {
                g_printerr("Failed to open file: %s\n", err->message);
                g_error_free(err);
            }
        }
        g_free(uri);
    }
#endif
}

void add_to_messages_interface(GtkBuilder* builder, const char* message, gboolean is_sent, const char* sender_username, const char *open_path) {
    GtkWidget* messages_interface = GTK_WIDGET(gtk_builder_get_object(builder, "messages_interface"));
    if (!messages_interface || !GTK_IS_LIST_BOX(messages_interface)) {
        g_error("Invalid messages_interface!");
        return;
    }

    GtkWidget* row = gtk_list_box_row_new();
    if (!row) {
        g_error("Failed to create GtkListBoxRow!");
        return;
    }

    GtkWidget* message_node = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    if (!message_node) {
        g_error("Failed to create message_node!");
        return;
    }

    GtkWidget* message_label = gtk_label_new(message);
    GtkWidget* username_label = gtk_label_new(sender_username);

    if (!message_label || !username_label) {
        g_error("Failed to create labels!");
        return;
    }

    gtk_widget_set_halign(message_label, GTK_ALIGN_START);
    gtk_widget_set_halign(username_label, GTK_ALIGN_END);

    gtk_box_pack_start(GTK_BOX(message_node), message_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(message_node), username_label, FALSE, FALSE, 0);

    if (open_path && strlen(open_path) > 0) {
        GtkWidget *open_btn = gtk_button_new_with_label("Open");
        if (open_btn) {
            g_object_set_data_full(G_OBJECT(open_btn), "open_path", g_strdup(open_path), g_free);
            g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_file_clicked), NULL);
            gtk_box_pack_start(GTK_BOX(message_node), open_btn, FALSE, FALSE, 2);
        }
    }

    if (is_sent) {
        gtk_widget_set_halign(message_node, GTK_ALIGN_END);
        gtk_widget_set_margin_start(message_node, 50);
    } else {
        gtk_widget_set_halign(message_node, GTK_ALIGN_START);
        gtk_widget_set_margin_end(message_node, 50);
    }

    gtk_container_add(GTK_CONTAINER(row), message_node);
    gtk_list_box_insert(GTK_LIST_BOX(messages_interface), row, -1);

    g_print("About to show all widgets...\n");

    if (GTK_IS_WIDGET(row)) {
        gtk_widget_show_all(row);
    } else {
        g_error("row is invalid before gtk_widget_show_all!");
    }
}

void *receiveMessagesWithGUI(void *pack) {
    clientDetails *clientD = ((RMWGUI *)pack)->clientD;
    GtkBuilder* builder = ((RMWGUI *)pack)->builder;

    clientD->group_joined = FALSE;

    if (perform_client_handshake(clientD) != 0) {
        LOG_ERROR("Handshake failed");
        return NULL;
    }

    while (1) {
        PacketHeader hdr;
        unsigned char *payload = NULL;
        size_t payload_len = 0;
        if (recv_protocol_packet(clientD->clientSocketFD, &hdr, &payload, &payload_len, clientD->aes_key) != 0) {
            LOG_ERROR("Receive failed");
            break;
        }

        if (hdr.msgType == MSG_PUBLISH_TEXT) {
            const char *sender = hdr.sender;
            const char *topic = hdr.topic;
            gboolean is_group = (hdr.flags & 0x1) != 0;
            gboolean is_sent = sender && clientD->clientName && strcmp(sender, clientD->clientName) == 0;

            char header[CLIENT_NAME_INPUT_MAX * 2];
            if (topic && strlen(topic) > 0 && strcmp(topic, MESSAGE_TYPE_BROADCAST) != 0) {
                if (is_group) {
                    if (is_sent) snprintf(header, sizeof(header), "[#%s] You", topic);
                    else snprintf(header, sizeof(header), "[#%s] %s", topic, sender ? sender : "Unknown");
                } else {
                    if (is_sent) snprintf(header, sizeof(header), "You -> %s", topic);
                    else snprintf(header, sizeof(header), "%s -> you", sender ? sender : "Unknown");
                }
            } else {
                snprintf(header, sizeof(header), "%s", sender ? sender : "Unknown");
            }

            UiMsgPayload *p = malloc(sizeof(UiMsgPayload));
            p->builder = g_object_ref(builder);
            p->message = g_strdup((const char *)payload);
            p->sender = g_strdup(header);
            p->is_sent = is_sent;
            p->open_path = NULL;
            g_idle_add(idle_add_message_cb, p);
        } else if (hdr.msgType == MSG_PUBLISH_FILE) {
            const char *sender = hdr.sender;
            const char *topic = hdr.topic;
            gboolean is_group = (hdr.flags & 0x1) != 0;
            gboolean is_sent = sender && clientD->clientName && strcmp(sender, clientD->clientName) == 0;

            char *payload_str = (char *)payload;
            char *sep = strchr(payload_str, '|');
            if (!sep) { free(payload); continue; }
            *sep = '\0';
            const char *filename = payload_str;
            const char *b64 = sep + 1;

            size_t decoded_len = 0;
            unsigned char *decoded = base64_to_bytes_decode(b64, &decoded_len);
            if (!decoded) { free(payload); continue; }

            char save_path[512];
            snprintf(save_path, sizeof(save_path), "received_%s", filename);
            FILE *f = fopen(save_path, "wb");
            if (f) {
                fwrite(decoded, 1, decoded_len, f);
                fclose(f);
            }
            free(decoded);

            char header[CLIENT_NAME_INPUT_MAX * 2 + 32];
            if (topic && strlen(topic) > 0 && strcmp(topic, MESSAGE_TYPE_BROADCAST) != 0) {
                if (is_group) {
                    if (is_sent) snprintf(header, sizeof(header), "[#%s] You (file)", topic);
                    else snprintf(header, sizeof(header), "[#%s] %s (file)", topic, sender ? sender : "Unknown");
                } else {
                    if (is_sent) snprintf(header, sizeof(header), "You -> %s (file)", topic);
                    else snprintf(header, sizeof(header), "%s -> you (file)", sender ? sender : "Unknown");
                }
            } else {
                snprintf(header, sizeof(header), "%s (file)", sender ? sender : "Unknown");
            }

            char notice[512];
            snprintf(notice, sizeof(notice), "Received file %s saved to %s", filename, save_path);

            UiMsgPayload *p = malloc(sizeof(UiMsgPayload));
            p->builder = g_object_ref(builder);
            p->message = g_strdup(notice);
            p->sender = g_strdup(header);
            p->is_sent = is_sent;
            p->open_path = g_strdup(save_path);
            g_idle_add(idle_add_message_cb, p);
        } else if (hdr.msgType == MSG_ACK) {
            if (strcmp(hdr.topic, "PRESENCE") == 0) {
                UiPresencePayload *p = malloc(sizeof(UiPresencePayload));
                p->builder = g_object_ref(builder);
                p->clientD = clientD;
                p->csv = g_strdup((const char *)payload);
                g_idle_add(idle_refresh_presence_cb, p);
            } else if (strcmp(hdr.topic, "GROUPS") == 0) {
                UiPresencePayload *p = malloc(sizeof(UiPresencePayload));
                p->builder = g_object_ref(builder);
                p->clientD = clientD;
                p->csv = g_strdup((const char *)payload);
                g_idle_add(idle_refresh_groups_cb, p);
            }
        } else if (hdr.msgType == MSG_GAME_INVITE) {
            UiGamePayload *p = malloc(sizeof(UiGamePayload));
            p->clientD = clientD;
            p->builder = g_object_ref(builder);
            p->from = g_strdup(hdr.sender);
            p->payload = g_strdup((const char *)payload);
            p->type = hdr.msgType;
            g_idle_add(idle_game_dispatch_cb, p);
        } else if (hdr.msgType == MSG_GAME_ACCEPT) {
            UiGamePayload *p = malloc(sizeof(UiGamePayload));
            p->clientD = clientD;
            p->builder = g_object_ref(builder);
            p->from = g_strdup(hdr.sender);
            p->payload = g_strdup((const char *)payload);
            p->type = hdr.msgType;
            g_idle_add(idle_game_dispatch_cb, p);
        } else if (hdr.msgType == MSG_GAME_MOVE) {
            UiGamePayload *p = malloc(sizeof(UiGamePayload));
            p->clientD = clientD;
            p->builder = g_object_ref(builder);
            p->from = g_strdup(hdr.sender);
            p->payload = g_strdup((const char *)payload);
            p->type = hdr.msgType;
            g_idle_add(idle_game_dispatch_cb, p);
        } else if (hdr.msgType == MSG_GAME_END) {
            UiGamePayload *p = malloc(sizeof(UiGamePayload));
            p->clientD = clientD;
            p->builder = g_object_ref(builder);
            p->from = g_strdup(hdr.sender);
            p->payload = g_strdup((const char *)payload);
            p->type = hdr.msgType;
            g_idle_add(idle_game_dispatch_cb, p);
        }

        free(payload);
    }

    return NULL;
}

void refresh_online_users(GtkBuilder* builder, clientDetails *clientD, const char* csv_users) {
    GtkWidget* list = GTK_WIDGET(gtk_builder_get_object(builder, "online_user_list"));
    if (!list || !GTK_IS_LIST_BOX(list)) return;

    GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList *iter = rows; iter != NULL; iter = iter->next) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(rows);

    GtkWidget* all_row = gtk_list_box_row_new();
    GtkWidget* all_label = gtk_label_new("Everyone");
    gtk_container_add(GTK_CONTAINER(all_row), all_label);
    gtk_list_box_insert(GTK_LIST_BOX(list), all_row, -1);

    if (csv_users && strlen(csv_users) > 0) {
        char *copy = g_strdup(csv_users);
        char *saveptr = NULL;
        char *token = strtok_r(copy, ",", &saveptr);
        while (token) {
            if (clientD->clientName && strcmp(token, clientD->clientName) != 0) {
                GtkWidget* row = gtk_list_box_row_new();
                GtkWidget* label = gtk_label_new(token);
                gtk_container_add(GTK_CONTAINER(row), label);
                gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
            }
            token = strtok_r(NULL, ",", &saveptr);
        }
        g_free(copy);
    }

    gtk_widget_show_all(list);

    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
    GtkListBoxRow *selected = NULL;
    for (GList *iter = children; iter != NULL; iter = iter->next) {
        GtkWidget *row = GTK_WIDGET(iter->data);
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
        const char *label_text = gtk_label_get_text(GTK_LABEL(child));
        if (strlen(clientD->active_target) == 0 && strcmp(label_text, "Everyone") == 0) {
            selected = GTK_LIST_BOX_ROW(row);
            break;
        }
        if (strlen(clientD->active_target) > 0 && strcmp(label_text, clientD->active_target) == 0) {
            selected = GTK_LIST_BOX_ROW(row);
            break;
        }
    }
    if (selected) {
        gtk_list_box_select_row(GTK_LIST_BOX(list), selected);
    }
    g_list_free(children);
}

void on_online_user_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    UNUSED(box);
    SMData *ctx = (SMData *)user_data;
    if (!row || !ctx || !ctx->data) return;

    clientDetails *clientD = ctx->data;
    GtkBuilder *builder = ctx->builder;

    GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
    const char *label_text = gtk_label_get_text(GTK_LABEL(child));
    if (strcmp(label_text, "Everyone") == 0) {
        clientD->active_target[0] = '\0';
        clientD->active_target_is_group = FALSE;
    } else {
        strncpy(clientD->active_target, label_text, sizeof(clientD->active_target) - 1);
        clientD->active_target[sizeof(clientD->active_target) - 1] = '\0';
        clientD->active_target_is_group = FALSE;
    }

    GtkWidget *header_label = GTK_WIDGET(gtk_builder_get_object(builder, "chat_header_label"));
    GtkWidget *subtitle_label = GTK_WIDGET(gtk_builder_get_object(builder, "chat_subtitle_label"));

    if (header_label) {
        if (strlen(clientD->active_target) == 0) {
            gtk_label_set_text(GTK_LABEL(header_label), "Group chat");
        } else {
            gtk_label_set_text(GTK_LABEL(header_label), clientD->active_target);
        }
    }
    if (subtitle_label) {
        if (strlen(clientD->active_target) == 0) {
            gtk_label_set_text(GTK_LABEL(subtitle_label), "Broadcasting to everyone");
        } else {
            gtk_label_set_text(GTK_LABEL(subtitle_label), "Direct message");
        }
    }
}

void refresh_groups(GtkBuilder* builder, clientDetails *clientD, const char* csv_groups) {
    GtkWidget* list = GTK_WIDGET(gtk_builder_get_object(builder, "group_list"));
    if (!list || !GTK_IS_LIST_BOX(list)) return;

    GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList *iter = rows; iter != NULL; iter = iter->next) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(rows);

    if (csv_groups && strlen(csv_groups) > 0) {
        char *copy = g_strdup(csv_groups);
        char *saveptr = NULL;
        char *token = strtok_r(copy, ",", &saveptr);
        while (token) {
            GtkWidget* row = gtk_list_box_row_new();
            GtkWidget* label = gtk_label_new(token);
            gtk_container_add(GTK_CONTAINER(row), label);
            gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
            token = strtok_r(NULL, ",", &saveptr);
        }
        g_free(copy);
    }

    gtk_widget_show_all(list);
}

void on_group_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    UNUSED(box);
    SMData *ctx = (SMData *)user_data;
    if (!row || !ctx || !ctx->data) return;

    clientDetails *clientD = ctx->data;
    GtkBuilder *builder = ctx->builder;

    GtkWidget *child = gtk_bin_get_child(GTK_BIN(row));
    const char *label_text = gtk_label_get_text(GTK_LABEL(child));
    strncpy(clientD->active_target, label_text, sizeof(clientD->active_target) - 1);
    clientD->active_target[sizeof(clientD->active_target) - 1] = '\0';
    clientD->active_target_is_group = TRUE;
    clientD->group_joined = FALSE;

    GtkWidget *header_label = GTK_WIDGET(gtk_builder_get_object(builder, "chat_header_label"));
    GtkWidget *subtitle_label = GTK_WIDGET(gtk_builder_get_object(builder, "chat_subtitle_label"));

    if (header_label) {
        gtk_label_set_text(GTK_LABEL(header_label), clientD->active_target);
    }
    if (subtitle_label) {
        gtk_label_set_text(GTK_LABEL(subtitle_label), "Group selected (not joined)");
    }
}

void on_group_create(GtkButton *button, gpointer user_data) {
    UNUSED(button);
    SMData *ctx = (SMData *)user_data;
    if (!ctx || !ctx->data) return;

    GtkBuilder *builder = ctx->builder;
    clientDetails *clientD = ctx->data;

    GtkWidget *entry = GTK_WIDGET(gtk_builder_get_object(builder, "group_entry"));
    const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
    if (!name || strlen(name) == 0) return;

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_SUBSCRIBE;
    hdr.version = 1;
    hdr.flags = 0x1; /* create flag */
    static uint32_t g_msg_id = 10000;
    hdr.messageId = g_msg_id++;
    hdr.timestamp = (uint64_t)time(NULL);
    strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, name, MAX_TOPIC_LEN - 1);

    const unsigned char empty_payload = 0;
    send_protocol_packet(clientD->clientSocketFD, &hdr, &empty_payload, 0, clientD->aes_key);
    gtk_entry_set_text(GTK_ENTRY(entry), "");
}

void on_group_join(GtkButton *button, gpointer user_data) {
    UNUSED(button);
    SMData *ctx = (SMData *)user_data;
    if (!ctx || !ctx->data) return;
    clientDetails *clientD = ctx->data;
    GtkBuilder *builder = ctx->builder;

    if (strlen(clientD->active_target) == 0 || !clientD->active_target_is_group) {
        LOG_ERROR("Select a group before joining.");
        return;
    }

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_SUBSCRIBE;
    hdr.version = 1;
    hdr.flags = 0x0; /* join existing */
    static uint32_t g_sub_msg_id = 20000;
    hdr.messageId = g_sub_msg_id++;
    hdr.timestamp = (uint64_t)time(NULL);
    strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, clientD->active_target, MAX_TOPIC_LEN - 1);

    const unsigned char empty_payload = 0;
    send_protocol_packet(clientD->clientSocketFD, &hdr, &empty_payload, 0, clientD->aes_key);

    clientD->group_joined = TRUE;
    GtkWidget *header_label = GTK_WIDGET(gtk_builder_get_object(builder, "chat_header_label"));
    GtkWidget *subtitle_label = GTK_WIDGET(gtk_builder_get_object(builder, "chat_subtitle_label"));
    if (header_label) {
        gtk_label_set_text(GTK_LABEL(header_label), clientD->active_target);
    }
    if (subtitle_label) {
        gtk_label_set_text(GTK_LABEL(subtitle_label), "Group chat");
    }
}
