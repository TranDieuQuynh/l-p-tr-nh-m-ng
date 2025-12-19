#include "client.h"
#include "utils.h"


int main(int argc, char **argv) {

    clientDetails clientD;
    memset(clientD.active_target, 0, sizeof(clientD.active_target));

    if (set_workdir_to_project_root() != 0) {
        LOG_ERROR("Failed to set working directory to project root.");
        return EXIT_FAILURE;
    }

    // integrating basic UI
    GtkWidget* window;
    GtkBuilder* builder;

    gtk_init(&argc, &argv);

    GtkCssProvider *provider = gtk_css_provider_new();
    if (!gtk_css_provider_load_from_path(provider, "gui/theme.css", NULL)) {
        g_printerr("Failed to load GUI theme.\n");
    } else {
        GdkScreen *screen = gdk_screen_get_default();
        gtk_style_context_add_provider_for_screen(screen,
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);

    builder = gtk_builder_new();
    GError *error = NULL;
    if (gtk_builder_add_from_file(builder, UI_CONNECTION_PATH, &error) == 0) {
        g_printerr("Error loading connection UI: %s\n", error->message);
        return 1;
    }
    if (gtk_builder_add_from_file(builder, UI_MAIN_PATH, &error) == 0) {
        g_printerr("Error loading main UI: %s\n", error->message);
        return 1;
    }

    window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    g_signal_connect(window, "destroy", gtk_main_quit, NULL);
    gtk_builder_connect_signals(builder, NULL);
    gtk_widget_show_all(window);

    caro_init_state(&clientD.caro, builder);
    if (setupClientFromGUI(&clientD, builder) == -1) {
        LOG_ERROR("Client setup failed.");
        return EXIT_FAILURE;
    }


    pthread_t sendThread, receiveThread;
    SMData pack_ptr = {.data = &clientD, .builder = builder};

    if (pthread_create(&sendThread, NULL, sendMessagesWithGUI, &pack_ptr) != 0) {
        LOG_ERROR("Failed to create send thread: %s", strerror(errno));
        close(clientD.clientSocketFD);
        free(clientD.clientName);
        free(clientD.serverAddress);
        return EXIT_FAILURE;
    }
    LOG_SUCCESS("Send thread created successfully.");

    RMWGUI r_pack = {.clientD = &clientD, .builder = builder};
    if (pthread_create(&receiveThread, NULL, receiveMessagesWithGUI, &r_pack) != 0) {
        LOG_ERROR("Failed to create receive thread: %s", strerror(errno));

        pthread_cancel(sendThread);
        pthread_join(sendThread, NULL);

        close(clientD.clientSocketFD);
        free(clientD.clientName);
        free(clientD.serverAddress);
        return EXIT_FAILURE;
    }
    LOG_SUCCESS("Receive thread created successfully.");

    GtkWidget* online_list = GTK_WIDGET(gtk_builder_get_object(builder, "online_user_list"));
    if (online_list) {
        g_signal_connect(online_list, "row-selected", G_CALLBACK(on_online_user_selected), &pack_ptr);
    }
    refresh_online_users(builder, &clientD, "");
    refresh_groups(builder, &clientD, "");
    clientD.group_joined = FALSE;

    gtk_main();

    pthread_cancel(sendThread);
    pthread_cancel(receiveThread);
    pthread_join(sendThread, NULL);
    pthread_join(receiveThread, NULL);

    LOG_INFO("Client shutting down...");
    cleanup(&clientD);
    LOG_SUCCESS("Client shutdown complete.");
    return EXIT_SUCCESS;

    return EXIT_SUCCESS;
}
