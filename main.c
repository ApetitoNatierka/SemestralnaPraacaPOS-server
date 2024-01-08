#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include "pos_sockets/active_socket.h"
#include "pos_sockets/passive_socket.h"
#include "pos_sockets/char_buffer.h"

volatile sig_atomic_t stop_server = 0;

void handle_signal(int signo) {
    if (signo == SIGINT) {
        stop_server = 1;
    }
}

typedef struct game_state {
    struct char_buffer state_buffer;
} GAME_STATE_DATA;

void game_state_init(GAME_STATE_DATA *game_state) {
    char_buffer_init(&game_state->state_buffer);
}

void game_state_destroy(GAME_STATE_DATA *game_state) {
    char_buffer_destroy(&game_state->state_buffer);
}

void game_state_append(GAME_STATE_DATA *game_state, const char *new_data) {
    char_buffer_append(&game_state->state_buffer, new_data, strlen(new_data));
}

typedef struct thread_data {
    ACTIVE_SOCKET* my_socket;
} THREAD_DATA;

void thread_data_init(struct thread_data* data, long long replications_count,
                      ACTIVE_SOCKET* my_socket) {
    data->my_socket = my_socket;
}

void thread_data_destroy(struct thread_data* data) {
    data->my_socket = NULL;
}

void* process_client_data(void* thread_data) {
    struct thread_data *data = thread_data;

    PASSIVE_SOCKET p_socket;
    passive_socket_init(&p_socket);
    passive_socket_start_listening(&p_socket, 13569);

    while (!stop_server) {
        passive_socket_wait_for_client(&p_socket, data->my_socket);

        printf("Klient bol pripojeny!\n");
        active_socket_start_reading(data->my_socket);
        sleep(1);
    }
    passive_socket_stop_listening(&p_socket);
    passive_socket_destroy(&p_socket);

    return NULL;
}

_Bool game_state_try_deserialize(GAME_STATE_DATA *game_state, struct char_buffer *buf) {
    char *pos = strchr(buf->data, ';');
    if (pos != NULL && *(pos + 1) == '\0') {
        char_buffer_append(&game_state->state_buffer, buf->data, pos - buf->data);

        char_buffer_append(&game_state->state_buffer, ";", 1);

        return true;
    }
    return false;
}

_Bool try_get_client_game_state(struct active_socket *my_sock, GAME_STATE_DATA *client_game_state) {
    _Bool result = false;
    CHAR_BUFFER r_buff;
    char_buffer_init(&r_buff);

    if (active_socket_try_get_read_data(my_sock, &r_buff)) {
        if (r_buff.size > 0) {
            if (active_socket_is_end_message(my_sock, &r_buff)) {
                active_socket_stop_reading(my_sock);
            } else if (game_state_try_deserialize(client_game_state, &r_buff)) {
                result = true;
            } else {
                active_socket_stop_reading(my_sock);
            }
        }
    }

    char_buffer_destroy(&r_buff);

    return result;
}

void send_game_state_to_client(ACTIVE_SOCKET* my_socket, GAME_STATE_DATA* game_state) {
    const char* state_data = game_state->state_buffer.data;

    if (state_data != NULL && strlen(state_data) > 0) {
        struct char_buffer message_buffer;
        char_buffer_init_copy(&message_buffer, &game_state->state_buffer);
        active_socket_write_data(my_socket, &message_buffer);
        char_buffer_destroy(&message_buffer);
    }
}

void *consume(void *thread_data) {
    struct thread_data *data = (struct thread_data *)thread_data;

    GAME_STATE_DATA game_state;
    GAME_STATE_DATA client_game_state;
    game_state_init(&game_state);
    game_state_init(&client_game_state);

    while (!stop_server) {
        if (data->my_socket != NULL) {
            if (try_get_client_game_state(data->my_socket, &client_game_state)) {
                printf("Received game state from client:\n%s\n", client_game_state.state_buffer.data);
            }
            send_game_state_to_client(data->my_socket, &client_game_state);
        }
        sleep(1);
    }
    game_state_destroy(&game_state);
    game_state_destroy(&client_game_state);

    return NULL;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);

    pthread_t th_receive;
    struct thread_data data;
    struct active_socket my_socket;

    active_socket_init(&my_socket);
    thread_data_init(&data, 100000, &my_socket);

    pthread_create(&th_receive, NULL, process_client_data, &data);

    while (!stop_server) {
        sleep(1);
    }

    pthread_cancel(th_receive);

    pthread_join(th_receive, NULL);

    thread_data_destroy(&data);
    active_socket_destroy(&my_socket);

    return 0;
}