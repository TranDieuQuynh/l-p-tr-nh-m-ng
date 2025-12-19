#include "utils.h"

static void broadcast_groups(serverDetails *serverD);

static unsigned char *get_key_for_fd(serverDetails *serverD, int fd) {
    if (!serverD) return NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (serverD->clientFDStore[i] == fd) {
            return serverD->client_aes_keyStore[i];
        }
    }
    return NULL;
}

static int lookup_fd_by_username(serverDetails *serverD, const char *username) {
    if (!serverD || !username) return -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (serverD->clientFDStore[i] != -1 && serverD->clientNames[i] && strcmp(serverD->clientNames[i], username) == 0) {
            return serverD->clientFDStore[i];
        }
    }
    return -1;
}

static void remove_client(serverDetails *serverD, int clientFd, const char *username) {
    if (!serverD) return;
    for (int x = 0; x < MAX_CLIENTS; x++) {
        if (serverD->clientFDStore[x] == clientFd) {
            serverD->clientFDStore[x] = -1;
            if (serverD->client_aes_keyStore[x]) {
                free(serverD->client_aes_keyStore[x]);
                serverD->client_aes_keyStore[x] = NULL;
            }
            if (serverD->clientNames[x]) {
                free(serverD->clientNames[x]);
                serverD->clientNames[x] = NULL;
            }
            break;
        }
    }
    if (username) {
        db_set_user_offline(&serverD->db, username);
        db_group_leave_all(&serverD->db, username);
    }
    for (int g = 0; g < MAX_GROUPS; g++) {
        for (int i = 0; i < serverD->groupCounts[g]; i++) {
            if (serverD->groupMembers[g][i] == clientFd) {
                serverD->groupMembers[g][i] = -1;
            }
        }
        while (serverD->groupCounts[g] > 0 && serverD->groupMembers[g][serverD->groupCounts[g]-1] == -1) {
            serverD->groupCounts[g]--;
        }
    }
    broadcast_presence(serverD);
}

void *handleOtherOperationsOnSeperateThread(void *serverD) {
    struct sockaddr clientAddress;
    socklen_t addr_len = sizeof(clientAddress);
    int server_fd = ((serverDetails *)serverD)->serverSocketFD;

    while (1) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            LOG_ERROR("Failed to allocate memory for client fd");
            continue;
        }

        *client_fd = accept(server_fd, &clientAddress, &addr_len);
        if (*client_fd < 0) {
            free(client_fd);
            LOG_ERROR("Accept failed");
            continue;
        }
        LOG_SUCCESS("Client connected.\n");

        pthread_t threadId;
        HNAC *param = (HNAC *)malloc(sizeof(HNAC));
        if (!param) {
            LOG_ERROR("Failed to allocate memory for HNAC structure");
            close(*client_fd);
            continue;
        }
        param->clientSocketFD = client_fd;
        param->serverD = (serverDetails *)serverD;
        if (pthread_create(&threadId, NULL, handleNewlyAcceptedClient, param) != 0) {
            LOG_ERROR("Failed to create the thread to handle new client operation");
            close(*client_fd);
            free(client_fd);
        }
    }
}

void *handleNewlyAcceptedClient(void *param) {
    const char *basic_message = "secured Connection to Server is established Successfully\n";
    int clientFd = *(((HNAC *)param)->clientSocketFD);
    serverDetails *serverD = ((HNAC *)param)->serverD;

    if (send(clientFd, serverD->keys->public_key, strlen(serverD->keys->public_key), 0) == -1) {
        LOG_ERROR("sending == [ public-security-key ] == failed");
        close(clientFd);
        return NULL;
    } else {
        LOG_SUCCESS("sent == [ public-security-key ] == of size [ %zu ]", strlen(serverD->keys->public_key));
    }

    char encrypted_aes_key_str[NETWORK_MESSAGE_BUFFER_SIZE];
    if (recv(clientFd, encrypted_aes_key_str, sizeof(encrypted_aes_key_str), 0) == -1) {
        LOG_ERROR("Failed to recieve AES Key\n");
        close(clientFd);
        return NULL;
    }

    unsigned char* decrypted_aes_key = decrypt_aes_key(serverD->keys->private_key, encrypted_aes_key_str);

    if (send(clientFd, basic_message, strlen(basic_message), 0) == -1) {
        LOG_ERROR("sending welcome message failed");
        close(clientFd);
        return NULL;
    } else {
        g_print("Welcome message sent\n");
    }

    char clientUsername[CLIENT_NAME_INPUT_MAX];
    if (recv(clientFd, clientUsername, sizeof(clientUsername), 0) == -1) {
        LOG_ERROR("Failed to recieve client details\n");
        close(clientFd);
        return NULL;
    }

    int x;
    int *clientFDStore = serverD->clientFDStore;

    for (x = 0; x < MAX_CLIENTS; x++) {
        if (clientFDStore[x] == -1) {
            clientFDStore[x] = clientFd;
            unsigned char* key_ptr = (unsigned char *)malloc(AES_KEY_SIZE * sizeof(unsigned char *));
            if (!key_ptr) {
                LOG_ERROR("Memory allocation failed for client AES key");
                break;
            }
            serverD->client_aes_keyStore[x] = key_ptr;
            memcpy(key_ptr, decrypted_aes_key, AES_KEY_SIZE);

            serverD->clientNames[x] = strdup(clientUsername);

            break;
        }
    }

    if (x == MAX_CLIENTS) {
        LOG_INFO("Client FD Store is full; consider increasing MAX_CLIENTS");
    }
    db_set_user_online(&serverD->db, clientUsername);
    broadcast_presence(serverD);

    while (1) {
        PacketHeader hdr_in;
        unsigned char *payload = NULL;
        size_t payload_len = 0;
        if (recv_protocol_packet(clientFd, &hdr_in, &payload, &payload_len, decrypted_aes_key) != 0) {
            LOG_ERROR("recv failed");
            free(payload);
            break;
        }

        hdr_in.sender[MAX_USERNAME_LEN - 1] = '\0';
        const char *sender = clientUsername;
        int delivered = 0;

        if (hdr_in.msgType == MSG_PUBLISH_TEXT || hdr_in.msgType == MSG_PUBLISH_FILE) {
            const char *topic = hdr_in.topic;
            gboolean is_group = (hdr_in.flags & 0x1) != 0;
            db_save_message(&serverD->db, sender, topic, (const char *)payload);

            PacketHeader hdr_out = hdr_in;
            hdr_out.payloadLength = (uint32_t)payload_len;
            strncpy(hdr_out.sender, sender, MAX_USERNAME_LEN - 1);

            if (is_group && topic && strlen(topic) > 0) {
                int groupIndex = -1;
                for (int g = 0; g < MAX_GROUPS; g++) {
                    if (serverD->groupNames[g] && strcmp(serverD->groupNames[g], topic) == 0) {
                        groupIndex = g;
                        break;
                    }
                }
                if (groupIndex != -1) {
                    for (int i = 0; i < serverD->groupCounts[groupIndex]; i++) {
                        int target_fd = serverD->groupMembers[groupIndex][i];
                        if (target_fd != -1) {
                            unsigned char *key = get_key_for_fd(serverD, target_fd);
                            send_protocol_packet(target_fd, &hdr_out, payload, payload_len, key);
                            delivered++;
                        }
                    }
                    LOG_INFO("[GROUP %s] %s -> delivered=%d", topic, sender, delivered);
                }
            } else if (topic && strcmp(topic, MESSAGE_TYPE_BROADCAST) != 0) {
                int target_fd = lookup_fd_by_username(serverD, topic);
                if (target_fd != -1) {
                    unsigned char *key = get_key_for_fd(serverD, target_fd);
                    send_protocol_packet(target_fd, &hdr_out, payload, payload_len, key);
                    delivered = 1;
                }
            } else {
                strncpy(hdr_out.topic, MESSAGE_TYPE_BROADCAST, MAX_TOPIC_LEN - 1);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clientFDStore[i] != clientFd && clientFDStore[i] != -1) {
                        unsigned char *key = serverD->client_aes_keyStore[i];
                        send_protocol_packet(clientFDStore[i], &hdr_out, payload, payload_len, key);
                        delivered++;
                    }
                }
                LOG_INFO("[BROADCAST] %s delivered=%d", sender, delivered);
            }
        } else if (hdr_in.msgType == MSG_SUBSCRIBE) {
            const char *groupName = hdr_in.topic;
            if (!groupName || strlen(groupName) == 0) { free(payload); continue; }
            int groupIndex = -1;
            for (int g = 0; g < MAX_GROUPS; g++) {
                if (serverD->groupNames[g] && strcmp(serverD->groupNames[g], groupName) == 0) {
                    groupIndex = g;
                    break;
                }
            }
            if ((hdr_in.flags & 0x1) && groupIndex == -1) {
                for (int g = 0; g < MAX_GROUPS; g++) {
                    if (!serverD->groupNames[g]) {
                        serverD->groupNames[g] = strdup(groupName);
                        serverD->groupCounts[g] = 0;
                        groupIndex = g;
                        LOG_INFO("Group created: %s by %s", groupName, sender);
                        break;
                    }
                }
            }
            if (groupIndex != -1 && serverD->groupCounts[groupIndex] < MAX_CLIENTS) {
                int already = 0;
                int empty_slot = -1;
                for (int i = 0; i < serverD->groupCounts[groupIndex]; i++) {
                    if (serverD->groupMembers[groupIndex][i] == clientFd) {
                        already = 1;
                        break;
                    }
                    if (serverD->groupMembers[groupIndex][i] == -1 && empty_slot == -1) {
                        empty_slot = i;
                    }
                }
                if (!already) {
                    if (empty_slot != -1) {
                        serverD->groupMembers[groupIndex][empty_slot] = clientFd;
                        if (empty_slot + 1 > serverD->groupCounts[groupIndex]) {
                            serverD->groupCounts[groupIndex] = empty_slot + 1;
                        }
                    } else {
                        serverD->groupMembers[groupIndex][serverD->groupCounts[groupIndex]++] = clientFd;
                    }
                    db_group_join(&serverD->db, sender, groupName);
                    LOG_INFO("Group join: %s joined %s (size=%d)", sender, groupName, serverD->groupCounts[groupIndex]);
                }
            }
            broadcast_groups(serverD);
        } else if (hdr_in.msgType == MSG_GAME_INVITE ||
                   hdr_in.msgType == MSG_GAME_ACCEPT ||
                   hdr_in.msgType == MSG_GAME_MOVE ||
                   hdr_in.msgType == MSG_GAME_END) {
            const char *target = hdr_in.topic;
            if (target && strlen(target) > 0) {
                int target_fd = lookup_fd_by_username(serverD, target);
                if (target_fd != -1) {
                    PacketHeader hdr_out = hdr_in;
                    hdr_out.payloadLength = (uint32_t)payload_len;
                    strncpy(hdr_out.sender, sender, MAX_USERNAME_LEN - 1);

                    unsigned char *key = get_key_for_fd(serverD, target_fd);
                    const unsigned char *buf = payload ? payload : (const unsigned char *)"";
                    send_protocol_packet(target_fd, &hdr_out, buf, payload_len, key);
                    delivered = 1;
                } else {
                    LOG_ERROR("[GAME] Target user %s not online", target);
                }
            }
        }

        free(payload);
    }

    free(decrypted_aes_key);
    remove_client(serverD, clientFd, clientUsername);
    close(clientFd);
    free(((HNAC *)param)->clientSocketFD);
    return NULL;
}

void broadcastMessage(char *clientUsername, char *receivedMessage, int currentClientFD, int *clientFDStore, unsigned char **client_aes_keyStore) {
    UNUSED(clientUsername);
    UNUSED(receivedMessage);
    UNUSED(currentClientFD);
    UNUSED(clientFDStore);
    UNUSED(client_aes_keyStore);
}

void broadcast_presence(serverDetails *serverD) {
    if (!serverD) return;

    char user_list[NETWORK_MESSAGE_BUFFER_SIZE] = {0};
    size_t offset = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (serverD->clientFDStore[i] != -1 && serverD->clientNames[i]) {
            size_t name_len = strlen(serverD->clientNames[i]);
            if (offset + name_len + 2 >= sizeof(user_list)) break;
            memcpy(user_list + offset, serverD->clientNames[i], name_len);
            offset += name_len;
            user_list[offset++] = ',';
        }
    }
    if (offset > 0 && user_list[offset - 1] == ',') {
        user_list[offset - 1] = '\0';
    }

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_ACK;
    hdr.version = 1;
    strncpy(hdr.topic, "PRESENCE", MAX_TOPIC_LEN - 1);
    hdr.payloadLength = (uint32_t)strlen(user_list);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (serverD->clientFDStore[i] != -1) {
            unsigned char *key = serverD->client_aes_keyStore[i];
            send_protocol_packet(serverD->clientFDStore[i], &hdr, (unsigned char *)user_list, strlen(user_list), key);
        }
    }
    broadcast_groups(serverD);
}

static void broadcast_groups(serverDetails *serverD) {
    char group_list[NETWORK_MESSAGE_BUFFER_SIZE] = {0};
    size_t offset = 0;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (serverD->groupNames[i]) {
            size_t name_len = strlen(serverD->groupNames[i]);
            if (offset + name_len + 2 >= sizeof(group_list)) break;
            memcpy(group_list + offset, serverD->groupNames[i], name_len);
            offset += name_len;
            group_list[offset++] = ',';
        }
    }
    if (offset > 0 && group_list[offset - 1] == ',') {
        group_list[offset - 1] = '\0';
    }

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_ACK;
    hdr.version = 1;
    strncpy(hdr.topic, "GROUPS", MAX_TOPIC_LEN - 1);
    hdr.payloadLength = (uint32_t)strlen(group_list);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (serverD->clientFDStore[i] != -1) {
            unsigned char *key = serverD->client_aes_keyStore[i];
            send_protocol_packet(serverD->clientFDStore[i], &hdr, (unsigned char *)group_list, strlen(group_list), key);
        }
    }
}
