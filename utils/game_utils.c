#include "utils.h"
#include <time.h>

#define CARO_INVITE_TEMPLATE "CARO|%d"
#define CARO_PAYLOAD_BUFSIZE 64
#define CARO_DEFAULT_SIZE 10
#define CARO_MIN_SIZE 3
#define CARO_MAX_SIZE 10

static int caro_send_packet(clientDetails *clientD, MessageType type, const char *target, const char *payload);
static gboolean caro_check_win(CaroState *state, int row, int col, char symbol);
static gboolean caro_board_full(CaroState *state);
static void caro_finish_game(CaroState *state, const char *status_text);
static int caro_required_in_a_row(int size);
static int caro_sanitize_size(int size);
static int caro_parse_board_size(const char *payload);
static void caro_play_again_clicked(GtkButton *button, gpointer user_data);
static void caro_set_cell_symbol(GtkWidget *btn, char symbol);
static void caro_clear_cell_style(GtkWidget *btn);
static void caro_mark_win_line(CaroState *state, int start_r, int start_c, int dr, int dc, char symbol);
static void caro_show_win_banner(CaroState *state, const char *text);

static uint32_t next_game_msg_id(void) {
    static uint32_t counter = 50000;
    return counter++;
}

static void caro_push_system_message(GtkBuilder *builder, const char *text, gboolean is_sender) {
    if (!builder || !text) return;
    add_to_messages_interface(builder, text, is_sender, "Game", NULL);
}

static int caro_required_in_a_row(int size) {
    switch (size) {
        case 3: return 3;
        case 4: return 4;
        case 5: return 5;
        case 10: return 5;
        default: return 5;
    }
}

static int caro_sanitize_size(int size) {
    if (size == 3 || size == 4 || size == 5 || size == 10) return size;
    return CARO_DEFAULT_SIZE;
}

static int caro_parse_board_size(const char *payload) {
    int size = CARO_DEFAULT_SIZE;
    if (payload && strncmp(payload, "CARO|", 5) == 0) {
        int parsed = atoi(payload + 5);
        size = caro_sanitize_size(parsed);
    }
    return size;
}

static void caro_clear_cell_style(GtkWidget *btn) {
    if (!btn) return;
    GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
    gtk_style_context_remove_class(ctx, "caro-x");
    gtk_style_context_remove_class(ctx, "caro-o");
    gtk_style_context_remove_class(ctx, "caro-win");
}

static void caro_set_cell_symbol(GtkWidget *btn, char symbol) {
    if (!btn) return;
    caro_clear_cell_style(btn);
    gtk_button_set_label(GTK_BUTTON(btn), (const gchar[]){ symbol, '\0' });
    GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
    if (symbol == 'X') gtk_style_context_add_class(ctx, "caro-x");
    else if (symbol == 'O') gtk_style_context_add_class(ctx, "caro-o");
}

static void caro_mark_win_line(CaroState *state, int start_r, int start_c, int dr, int dc, char symbol) {
    if (!state) return;
    for (int i = 0; i < state->win_length; i++) {
        int r = start_r + dr * i;
        int c = start_c + dc * i;
        if (r < 0 || c < 0 || r >= state->board_size || c >= state->board_size) break;
        if (state->cells[r][c]) {
            GtkStyleContext *ctx = gtk_widget_get_style_context(state->cells[r][c]);
            gtk_style_context_add_class(ctx, "caro-win");
            gtk_style_context_add_class(ctx, (symbol == 'X') ? "caro-x" : "caro-o");
        }
    }
}

static void caro_show_win_banner(CaroState *state, const char *text) {
    if (!state || !state->win_label || !text) return;
    gtk_label_set_text(GTK_LABEL(state->win_label), text);
    gtk_widget_show(state->win_label);
}

static int caro_send_packet(clientDetails *clientD, MessageType type, const char *target, const char *payload) {
    if (!clientD || !clientD->aes_key || !target) return -1;

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = type;
    hdr.version = 1;
    hdr.messageId = next_game_msg_id();
    hdr.timestamp = (uint64_t)time(NULL);
    if (clientD->clientName) strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, target, MAX_TOPIC_LEN - 1);

    const unsigned char placeholder = 0;
    size_t payload_len = payload ? strlen(payload) : 0;
    const unsigned char *buf = (payload_len > 0) ? (const unsigned char *)payload : &placeholder;

    return send_protocol_packet(clientD->clientSocketFD, &hdr, buf, payload_len, clientD->aes_key);
}

static void caro_reset_board(CaroState *state) {
    if (!state) return;
    int size = state->board_size > 0 ? state->board_size : CARO_DEFAULT_SIZE;
    for (int r = 0; r < size; r++) {
        for (int c = 0; c < size; c++) {
            state->board[r][c] = '\0';
            if (state->cells[r][c]) {
                caro_clear_cell_style(state->cells[r][c]);
                gtk_button_set_label(GTK_BUTTON(state->cells[r][c]), " ");
            }
        }
    }
    if (state->win_label) {
        gtk_widget_hide(state->win_label);
    }
}

static void caro_update_labels(CaroState *state, const char *status, const char *turn) {
    if (!state) return;
    if (state->status_label && status) {
        gtk_label_set_text(GTK_LABEL(state->status_label), status);
    }
    if (state->turn_label && turn) {
        gtk_label_set_text(GTK_LABEL(state->turn_label), turn);
    }
}

static gboolean caro_on_window_close(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    UNUSED(event);
    clientDetails *clientD = (clientDetails *)user_data;
    if (!clientD) return FALSE;
    CaroState *state = &clientD->caro;
    if (state->in_game || state->waiting_accept) {
        if (strlen(state->opponent) > 0) {
            caro_send_packet(clientD, MSG_GAME_END, state->opponent, "RESIGN");
            char note[128];
            snprintf(note, sizeof(note), "You left the game with %s.", state->opponent);
            caro_push_system_message(state->builder, note, TRUE);
        }
    }
    state->in_game = FALSE;
    state->waiting_accept = FALSE;
    gtk_widget_hide(widget);
    return TRUE;
}

static void caro_cell_clicked(GtkButton *button, gpointer user_data) {
    clientDetails *clientD = (clientDetails *)user_data;
    if (!clientD) return;
    CaroState *state = &clientD->caro;

    if (!state->in_game || !state->my_turn) {
        LOG_ERROR("Not your turn or no active game.");
        return;
    }
    int size = state->board_size;
    int row = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "caro-row"));
    int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "caro-col"));
    if (row < 0 || col < 0 || row >= size || col >= size) return;
    if (state->board[row][col] != '\0') {
        LOG_ERROR("Cell already played.");
        return;
    }

    state->board[row][col] = state->my_symbol;
    caro_set_cell_symbol(GTK_WIDGET(button), state->my_symbol);

    gboolean win = caro_check_win(state, row, col, state->my_symbol);
    gboolean draw = (!win) && caro_board_full(state);

    if (win) {
        char msg[64];
        snprintf(msg, sizeof(msg), "You won! %d in a row.", state->win_length);
        caro_finish_game(state, msg);
        caro_show_win_banner(state, (state->my_symbol == 'X') ? "Player X Wins" : "Player O Wins");
    } else if (draw) {
        caro_finish_game(state, "Draw. Board is full.");
        if (state->win_label) gtk_widget_hide(state->win_label);
    } else {
        state->my_turn = FALSE;
        caro_update_labels(state, NULL, "Waiting for opponent...");
    }

    char payload[CARO_PAYLOAD_BUFSIZE];
    snprintf(payload, sizeof(payload), "%d,%d", row, col);

    if (caro_send_packet(clientD, MSG_GAME_MOVE, state->opponent, payload) != 0) {
        LOG_ERROR("Failed to send game move.");
    }
}

static void caro_attach_grid(clientDetails *clientD) {
    CaroState *state = &clientD->caro;
    int size = caro_sanitize_size(state->board_size);
    state->board_size = size;

    if (state->grid) {
        if (state->scroller) {
            gtk_container_remove(GTK_CONTAINER(state->scroller), state->grid);
        }
        gtk_widget_destroy(state->grid);
        state->grid = NULL;
    }

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);

    for (int r = 0; r < size; r++) {
        for (int c = 0; c < size; c++) {
            GtkWidget *btn = gtk_button_new_with_label(" ");
            gtk_widget_set_size_request(btn, 28, 28);
            g_object_set_data(G_OBJECT(btn), "caro-row", GINT_TO_POINTER(r));
            g_object_set_data(G_OBJECT(btn), "caro-col", GINT_TO_POINTER(c));
            g_signal_connect(btn, "clicked", G_CALLBACK(caro_cell_clicked), clientD);
            gtk_grid_attach(GTK_GRID(grid), btn, c, r, 1, 1);
            state->cells[r][c] = btn;
        }
    }

    state->grid = grid;
    if (state->scroller) {
        gtk_container_add(GTK_CONTAINER(state->scroller), state->grid);
    }
}

static void caro_ensure_window(clientDetails *clientD) {
    CaroState *state = &clientD->caro;
    if (state->window) return;

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Caro (2-player)");
    gtk_window_set_default_size(GTK_WINDOW(window), 620, 720);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(window), root);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    state->opponent_label = gtk_label_new("Opponent: -");
    state->status_label = gtk_label_new("Not in game");
    state->turn_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(header), state->opponent_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), state->status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), state->turn_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    caro_attach_grid(clientD);

    GtkWidget *overlay = gtk_overlay_new();
    state->overlay = overlay;

    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_halign(scroller, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(scroller, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_size_request(scroller, 520, 520);
    state->scroller = scroller;
    gtk_container_add(GTK_CONTAINER(scroller), state->grid);
    gtk_container_add(GTK_CONTAINER(overlay), scroller);

    GtkWidget *win_label = gtk_label_new(NULL);
    gtk_widget_set_halign(win_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(win_label, GTK_ALIGN_CENTER);
    gtk_widget_set_no_show_all(win_label, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(win_label), "caro-win-label");
    state->win_label = win_label;
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), win_label);

    gtk_box_pack_start(GTK_BOX(root), overlay, TRUE, TRUE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *leave_btn = gtk_button_new_with_label("Leave game");
    g_signal_connect(leave_btn, "clicked", G_CALLBACK(caro_on_window_close), clientD);
    GtkWidget *play_again_btn = gtk_button_new_with_label("Play again");
    state->play_again_btn = play_again_btn;
    gtk_widget_set_sensitive(play_again_btn, FALSE);
    g_signal_connect(play_again_btn, "clicked", G_CALLBACK(caro_play_again_clicked), clientD);
    gtk_box_pack_end(GTK_BOX(controls), leave_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(controls), play_again_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), controls, FALSE, FALSE, 0);

    g_signal_connect(window, "delete-event", G_CALLBACK(caro_on_window_close), clientD);

    state->window = window;
}

void caro_init_state(CaroState *state, GtkBuilder *builder) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->builder = builder;
    state->my_symbol = 'X';
    state->opp_symbol = 'O';
    state->board_size = CARO_DEFAULT_SIZE;
    state->win_length = caro_required_in_a_row(state->board_size);
}

static void caro_start_match(clientDetails *clientD, const char *opponent, gboolean start_first, char my_symbol) {
    CaroState *state = &clientD->caro;
    state->builder = state->builder ? state->builder : NULL;
    strncpy(state->opponent, opponent, sizeof(state->opponent) - 1);
    state->opponent[sizeof(state->opponent) - 1] = '\0';
    state->in_game = TRUE;
    state->waiting_accept = FALSE;
    state->my_turn = start_first;
    state->my_symbol = my_symbol;
    state->opp_symbol = (my_symbol == 'X') ? 'O' : 'X';
    state->win_length = caro_required_in_a_row(state->board_size);

    caro_ensure_window(clientD);
    caro_attach_grid(clientD);
    caro_reset_board(state);
    if (state->win_label) gtk_widget_hide(state->win_label);

    char opp_label[128];
    snprintf(opp_label, sizeof(opp_label), "Opponent: %s", opponent);
    if (state->opponent_label) gtk_label_set_text(GTK_LABEL(state->opponent_label), opp_label);

    if (state->status_label) {
        gtk_label_set_text(GTK_LABEL(state->status_label), start_first ? "Game started - you go first" : "Game started - opponent to move");
    }
    if (state->turn_label) {
        gtk_label_set_text(GTK_LABEL(state->turn_label), start_first ? "Your turn" : "Waiting for opponent...");
    }
    if (state->play_again_btn) {
        gtk_widget_set_sensitive(state->play_again_btn, FALSE);
    }

    gtk_widget_show_all(state->window);
}

static gboolean caro_check_win(CaroState *state, int row, int col, char symbol) {
    int size = state->board_size;
    int need = state->win_length;
    const int dirs[4][2] = { {1,0}, {0,1}, {1,1}, {1,-1} };
    for (int d = 0; d < 4; d++) {
        int count = 1;
        for (int step = 1; step < need; step++) {
            int r = row + dirs[d][0] * step;
            int c = col + dirs[d][1] * step;
            if (r < 0 || c < 0 || r >= size || c >= size) break;
            if (state->board[r][c] == symbol) count++;
            else break;
        }
        for (int step = 1; step < need; step++) {
            int r = row - dirs[d][0] * step;
            int c = col - dirs[d][1] * step;
            if (r < 0 || c < 0 || r >= size || c >= size) break;
            if (state->board[r][c] == symbol) count++;
            else break;
        }
        if (count >= need) {
            int sr = row;
            int sc = col;
            while (sr - dirs[d][0] >= 0 && sc - dirs[d][1] >= 0 &&
                   sr - dirs[d][0] < size && sc - dirs[d][1] < size &&
                   state->board[sr - dirs[d][0]][sc - dirs[d][1]] == symbol) {
                sr -= dirs[d][0];
                sc -= dirs[d][1];
            }
            caro_mark_win_line(state, sr, sc, dirs[d][0], dirs[d][1], symbol);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean caro_board_full(CaroState *state) {
    int size = state->board_size;
    for (int r = 0; r < size; r++) {
        for (int c = 0; c < size; c++) {
            if (state->board[r][c] == '\0') return FALSE;
        }
    }
    return TRUE;
}

static void caro_finish_game(CaroState *state, const char *status_text) {
    state->in_game = FALSE;
    state->waiting_accept = FALSE;
    state->my_turn = FALSE;
    caro_update_labels(state, status_text, "Game finished");
    if (state->play_again_btn) {
        gtk_widget_set_sensitive(state->play_again_btn, TRUE);
    }
}

static int caro_send_end(clientDetails *clientD, const char *reason) {
    if (!clientD || !clientD->aes_key || !reason) return -1;
    CaroState *state = &clientD->caro;
    if (strlen(state->opponent) == 0) return -1;

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_GAME_END;
    hdr.version = 1;
    hdr.messageId = next_game_msg_id();
    hdr.timestamp = (uint64_t)time(NULL);
    if (clientD->clientName) strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, state->opponent, MAX_TOPIC_LEN - 1);
    const unsigned char *buf = (const unsigned char *)reason;
    return send_protocol_packet(clientD->clientSocketFD, &hdr, buf, strlen(reason), clientD->aes_key);
}

static void caro_play_again_clicked(GtkButton *button, gpointer user_data) {
    UNUSED(button);
    clientDetails *clientD = (clientDetails *)user_data;
    if (!clientD) return;
    CaroState *state = &clientD->caro;

    if (strlen(state->opponent) == 0) {
        LOG_ERROR("Không xác định đối thủ để mời lại.");
        return;
    }
    if (!clientD->aes_key) {
        LOG_ERROR("Kết nối bảo mật chưa sẵn sàng.");
        return;
    }
    char payload[CARO_PAYLOAD_BUFSIZE];
    int size = caro_sanitize_size(state->board_size);
    snprintf(payload, sizeof(payload), CARO_INVITE_TEMPLATE, size);

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_GAME_INVITE;
    hdr.version = 1;
    hdr.messageId = next_game_msg_id();
    hdr.timestamp = (uint64_t)time(NULL);
    if (clientD->clientName) strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, state->opponent, MAX_TOPIC_LEN - 1);

    const unsigned char *buf = (const unsigned char *)payload;
    if (send_protocol_packet(clientD->clientSocketFD, &hdr, buf, strlen(payload), clientD->aes_key) != 0) {
        LOG_ERROR("Gửi lời mời chơi lại thất bại.");
        return;
    }
    state->waiting_accept = TRUE;
    char note[128];
    snprintf(note, sizeof(note), "Đã gửi lời mời chơi lại tới %s (bàn %dx%d).", state->opponent, size, size);
    caro_push_system_message(state->builder, note, TRUE);
}

void caro_invite_button_handler(GtkWidget *button, SMData *pack) {
    UNUSED(button);
    if (!pack || !pack->data || !pack->builder) return;
    clientDetails *clientD = pack->data;
    CaroState *state = &clientD->caro;
    state->builder = pack->builder;

    if (clientD->active_target_is_group || strlen(clientD->active_target) == 0) {
        LOG_ERROR("Select an online user (not a group) to invite.");
        return;
    }
    if (clientD->clientName && strcmp(clientD->active_target, clientD->clientName) == 0) {
        LOG_ERROR("Cannot invite yourself.");
        return;
    }
    if (state->in_game || state->waiting_accept) {
        LOG_ERROR("Finish the current game before inviting someone else.");
        return;
    }
    if (!clientD->aes_key) {
        LOG_ERROR("Secure session not ready yet. Try again in a moment.");
        return;
    }

    GtkWidget *combo = GTK_WIDGET(gtk_builder_get_object(pack->builder, "caro_size_combo"));
    int board_size = CARO_DEFAULT_SIZE;
    if (combo && GTK_IS_COMBO_BOX_TEXT(combo)) {
        gchar *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
        if (text) {
            int parsed = atoi(text);
            board_size = caro_sanitize_size(parsed);
            g_free(text);
        }
    }
    state->board_size = board_size;
    state->win_length = caro_required_in_a_row(board_size);

    char rules[256];
    snprintf(rules, sizeof(rules),
             "Luật chơi:\n- 3x3: thắng khi 3 liên tiếp\n- 4x4: thắng khi 4 liên tiếp\n- 5x5: thắng khi 5 liên tiếp\n- 10x10: thắng khi 5 liên tiếp\n\nBạn chọn bảng %dx%d, cần %d liên tiếp. Gửi lời mời?",
             board_size, board_size, state->win_length);

    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK_CANCEL,
                                               "%s", rules);
    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp != GTK_RESPONSE_OK) {
        return;
    }

    char payload[CARO_PAYLOAD_BUFSIZE];
    snprintf(payload, sizeof(payload), CARO_INVITE_TEMPLATE, board_size);

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_GAME_INVITE;
    hdr.version = 1;
    hdr.messageId = next_game_msg_id();
    hdr.timestamp = (uint64_t)time(NULL);
    if (clientD->clientName) strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, clientD->active_target, MAX_TOPIC_LEN - 1);

    const unsigned char *buf = (const unsigned char *)payload;
    if (send_protocol_packet(clientD->clientSocketFD, &hdr, buf, strlen(payload), clientD->aes_key) != 0) {
        LOG_ERROR("Failed to send game invite.");
        return;
    }

    strncpy(state->opponent, clientD->active_target, sizeof(state->opponent) - 1);
    state->opponent[sizeof(state->opponent) - 1] = '\0';
    state->waiting_accept = TRUE;
    state->in_game = FALSE;
    state->my_symbol = 'X';
    state->opp_symbol = 'O';
    caro_push_system_message(pack->builder, "Game invite sent. Waiting for response...", TRUE);
    if (state->play_again_btn) {
        gtk_widget_set_sensitive(state->play_again_btn, FALSE);
    }
}

void caro_handle_invite(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload) {
    CaroState *state = &clientD->caro;
    state->builder = builder;

    if (state->in_game || state->waiting_accept) {
        PacketHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.msgType = MSG_GAME_ACCEPT;
        hdr.version = 1;
        hdr.messageId = next_game_msg_id();
        hdr.timestamp = (uint64_t)time(NULL);
        if (clientD->clientName) strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
        strncpy(hdr.topic, from, MAX_TOPIC_LEN - 1);
        const char *busy = "BUSY";
        send_protocol_packet(clientD->clientSocketFD, &hdr, (const unsigned char *)busy, strlen(busy), clientD->aes_key);
        return;
    }

    int board_size = caro_parse_board_size(payload);
    state->board_size = board_size;
    state->win_length = caro_required_in_a_row(board_size);

    char prompt[256];
    snprintf(prompt, sizeof(prompt), "%s mời bạn chơi Caro %dx%d (thắng khi %d liên tiếp). Chấp nhận?", from, board_size, board_size, state->win_length);

    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_NONE,
                                               "%s", prompt);
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Decline", GTK_RESPONSE_REJECT, "_Accept", GTK_RESPONSE_ACCEPT, NULL);
    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    PacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.msgType = MSG_GAME_ACCEPT;
    hdr.version = 1;
    hdr.messageId = next_game_msg_id();
    hdr.timestamp = (uint64_t)time(NULL);
    if (clientD->clientName) strncpy(hdr.sender, clientD->clientName, MAX_USERNAME_LEN - 1);
    strncpy(hdr.topic, from, MAX_TOPIC_LEN - 1);

    if (resp == GTK_RESPONSE_ACCEPT) {
        const char *accept = "ACCEPT";
        send_protocol_packet(clientD->clientSocketFD, &hdr, (const unsigned char *)accept, strlen(accept), clientD->aes_key);
        caro_start_match(clientD, from, FALSE, 'O');
        caro_push_system_message(builder, "Accepted game invite. You play second (O).", FALSE);
    } else {
        const char *decline = "DECLINE";
        send_protocol_packet(clientD->clientSocketFD, &hdr, (const unsigned char *)decline, strlen(decline), clientD->aes_key);
        caro_push_system_message(builder, "Declined the game invite.", FALSE);
    }
}

void caro_handle_accept(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload) {
    CaroState *state = &clientD->caro;
    state->builder = builder;
    if (!state->waiting_accept || strcmp(state->opponent, from) != 0) {
        LOG_ERROR("Received unexpected game response from %s", from);
        return;
    }

    if (g_str_has_prefix(payload, "ACCEPT")) {
        state->waiting_accept = FALSE;
        caro_start_match(clientD, from, TRUE, 'X');
        caro_push_system_message(builder, "Your game invite was accepted. You play first (X).", TRUE);
    } else if (g_str_has_prefix(payload, "BUSY")) {
        state->waiting_accept = FALSE;
        caro_push_system_message(builder, "Opponent is busy and cannot play right now.", FALSE);
    } else {
        state->waiting_accept = FALSE;
        caro_push_system_message(builder, "Game invite was declined.", FALSE);
    }
}

void caro_handle_move(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload) {
    CaroState *state = &clientD->caro;
    state->builder = builder;
    if (!state->in_game || strcmp(state->opponent, from) != 0) {
        LOG_ERROR("Received a move for a non-active game.");
        return;
    }

    int row = -1, col = -1;
    if (!payload || sscanf(payload, "%d,%d", &row, &col) != 2) {
        LOG_ERROR("Invalid move payload.");
        return;
    }
    int size = state->board_size;
    if (row < 0 || col < 0 || row >= size || col >= size) {
        LOG_ERROR("Move out of range.");
        return;
    }
    if (state->board[row][col] != '\0') {
        LOG_ERROR("Cell already filled on receive.");
        return;
    }

    state->board[row][col] = state->opp_symbol;
    if (state->cells[row][col]) {
        caro_set_cell_symbol(state->cells[row][col], state->opp_symbol);
    }

    if (caro_check_win(state, row, col, state->opp_symbol)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "You lost. Opponent made %d in a row.", state->win_length);
        caro_finish_game(state, msg);
        caro_show_win_banner(state, (state->opp_symbol == 'X') ? "Player X Wins" : "Player O Wins");
        return;
    }
    if (caro_board_full(state)) {
        caro_finish_game(state, "Draw. Board is full.");
        return;
    }

    state->my_turn = TRUE;
    caro_update_labels(state, NULL, "Your turn");
}

void caro_handle_end(clientDetails *clientD, GtkBuilder *builder, const char *from, const char *payload) {
    CaroState *state = &clientD->caro;
    state->builder = builder;
    if (strcmp(state->opponent, from) != 0) {
        LOG_ERROR("Received game end from non-opponent.");
        return;
    }

    if (g_str_has_prefix(payload, "RESIGN")) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Opponent (%s) resigned. You win.", from);
        caro_finish_game(state, msg);
        caro_push_system_message(builder, msg, FALSE);
        caro_show_win_banner(state, (state->my_symbol == 'X') ? "Player X Wins" : "Player O Wins");
    } else if (g_str_has_prefix(payload, "CANCEL")) {
        caro_finish_game(state, "Opponent cancelled the game.");
        if (state->win_label) gtk_widget_hide(state->win_label);
    } else if (g_str_has_prefix(payload, "DRAW")) {
        caro_finish_game(state, "Game ended in a draw.");
        if (state->win_label) gtk_widget_hide(state->win_label);
    } else {
        caro_finish_game(state, "Game ended.");
        if (state->win_label) gtk_widget_hide(state->win_label);
    }
    if (state->play_again_btn) {
        gtk_widget_set_sensitive(state->play_again_btn, TRUE);
    }
}
