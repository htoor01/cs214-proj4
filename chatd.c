/*
 * chatd.c - Main chat server implementation
 * CS 214 Spring 2026 - Project IV
 * Authors: Haaris Toor (hbt20), Hassan Ibrahim (hi125)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocol.h"

/* Per-client receive buffer: header overhead + max body */
#define CLIENT_BUF_SIZE (MAX_MSG_LENGTH + 200)
/* WHO #all response buffer cap */
#define WHO_BUF_SIZE 10240

typedef struct client {
    int socket;
    char screen_name[MAX_SCREEN_NAME + 1];
    char status[MAX_STATUS + 1];
    int authenticated;
    struct client *next;
} client_t;

static client_t *clients = NULL;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Function prototypes */
void *handle_client(void *arg);
int setup_server_socket(int port);
static int try_add_client(client_t *client);
void remove_client(client_t *client);
void broadcast_message(const char *message, client_t *exclude);
void send_to_client(client_t *client, const char *message);
void handle_nam_message(client_t *client, const char *body);
void handle_set_message(client_t *client, const char *body);
void handle_msg_message(client_t *client, const char *body);
void handle_who_message(client_t *client, const char *body);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Invalid port number\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN); /* don't crash if a client drops mid-send */

    int server_socket = setup_server_socket(port);
    if (server_socket < 0) {
        fprintf(stderr, "Error: Failed to set up server socket\n");
        return 1;
    }

    printf("Chat server listening on port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }

        printf("New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        client_t *client = malloc(sizeof(client_t));
        if (!client) {
            perror("malloc");
            close(client_socket);
            continue;
        }

        client->socket = client_socket;
        client->screen_name[0] = '\0';
        client->status[0] = '\0';
        client->authenticated = 0;
        client->next = NULL;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, client) != 0) {
            perror("pthread_create");
            close(client_socket);
            free(client);
            continue;
        }

        pthread_detach(thread);
    }

    close(server_socket);
    return 0;
}

int setup_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return -1; }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt"); close(sockfd); return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); close(sockfd); return -1;
    }

    if (listen(sockfd, 10) < 0) {
        perror("listen"); close(sockfd); return -1;
    }

    return sockfd;
}

void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;

    char *buf = malloc(CLIENT_BUF_SIZE);
    if (!buf) {
        close(client->socket);
        free(client);
        return NULL;
    }
    int fill = 0;
    int running = 1;

    while (running) {
        ssize_t bytes = recv(client->socket, buf + fill, CLIENT_BUF_SIZE - fill - 1, 0);
        if (bytes <= 0)
            break;
        fill += (int)bytes;
        buf[fill] = '\0';

        int processed;
        do {
            processed = 0;

            /* Find the first three pipe positions to determine body_length */
            int pipes[3], pc = 0;
            for (int i = 0; i < fill && pc < 3; i++) {
                if (buf[i] == '|') pipes[pc++] = i;
            }
            if (pc < 3)
                break; /* incomplete header — wait for more data */

            int lf_len = pipes[2] - pipes[1] - 1;
            if (lf_len <= 0 || lf_len > 5) {
                char *err = create_err_message(ERR_UNREADABLE, "Bad length field");
                if (err) { send_to_client(client, err); free(err); }
                running = 0; break;
            }

            char lstr[8];
            memcpy(lstr, buf + pipes[1] + 1, lf_len);
            lstr[lf_len] = '\0';
            int body_len = atoi(lstr);

            if (body_len < 0 || body_len > MAX_MSG_LENGTH) {
                char *err = create_err_message(ERR_UNREADABLE, "Bad body length");
                if (err) { send_to_client(client, err); free(err); }
                running = 0; break;
            }

            int total = pipes[2] + 1 + body_len;
            if (fill < total)
                break; /* body not fully received yet */

            char *raw = malloc(total + 1);
            if (!raw) { running = 0; break; }
            memcpy(raw, buf, total);
            raw[total] = '\0';

            message_t msg;
            if (parse_message(raw, &msg) < 0) {
                free(raw);
                char *err = create_err_message(ERR_UNREADABLE, "Invalid message format");
                if (err) { send_to_client(client, err); free(err); }
                running = 0; break;
            }
            free(raw);

            /* NAM must be the first message */
            if (!client->authenticated && strcmp(msg.code, MSG_NAM) != 0) {
                free_message(&msg);
                char *err = create_err_message(ERR_UNREADABLE, "Must authenticate first");
                if (err) { send_to_client(client, err); free(err); }
                running = 0; break;
            }

            if (strcmp(msg.code, MSG_NAM) == 0) {
                handle_nam_message(client, msg.body);
            } else if (strcmp(msg.code, MSG_SET) == 0) {
                handle_set_message(client, msg.body);
            } else if (strcmp(msg.code, MSG_MSG) == 0) {
                handle_msg_message(client, msg.body);
            } else if (strcmp(msg.code, MSG_WHO) == 0) {
                handle_who_message(client, msg.body);
            } else {
                free_message(&msg);
                char *err = create_err_message(ERR_UNREADABLE, "Unknown message type");
                if (err) { send_to_client(client, err); free(err); }
                running = 0; break;
            }

            free_message(&msg);

            memmove(buf, buf + total, fill - total);
            fill -= total;
            buf[fill] = '\0';
            processed = 1;

        } while (processed);
    }

    free(buf);
    remove_client(client);
    close(client->socket);
    free(client);
    return NULL;
}

/* check + insert under one lock so no duplicate can slip through */
static int try_add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    client_t *curr = clients;
    while (curr) {
        if (curr->authenticated &&
            strcmp(curr->screen_name, client->screen_name) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return 0;
        }
        curr = curr->next;
    }
    client->next = clients;
    clients = client;
    pthread_mutex_unlock(&clients_mutex);
    return 1;
}

void remove_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    client_t **curr = &clients;
    while (*curr) {
        if (*curr == client) { *curr = client->next; break; }
        curr = &(*curr)->next;
    }
    pthread_mutex_unlock(&clients_mutex);
}

/* collect fds first, send after releasing lock */
void broadcast_message(const char *message, client_t *exclude) {
    int sockets[1024];
    int count = 0;

    pthread_mutex_lock(&clients_mutex);
    client_t *curr = clients;
    while (curr && count < 1024) {
        if (curr != exclude && curr->authenticated)
            sockets[count++] = curr->socket;
        curr = curr->next;
    }
    pthread_mutex_unlock(&clients_mutex);

    size_t len = strlen(message);
    for (int i = 0; i < count; i++)
        send(sockets[i], message, len, 0);
}

void send_to_client(client_t *client, const char *message) {
    if (!client || !message) return;
    send(client->socket, message, strlen(message), 0);
}

void handle_nam_message(client_t *client, const char *body) {
    /* Ignore re-NAM from an already-authenticated client */
    if (client->authenticated) return;

    char name[MAX_SCREEN_NAME + 1];
    strncpy(name, body, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    char *pipe = strchr(name, '|');
    if (pipe) *pipe = '\0';

    if (!validate_screen_name(name)) {
        size_t len = strlen(name);
        char *err = (len == 0 || len > MAX_SCREEN_NAME)
            ? create_err_message(ERR_TOO_LONG,    "Name must be 1-32 characters")
            : create_err_message(ERR_ILLEGAL_CHAR, "Name contains invalid characters");
        if (err) { send_to_client(client, err); free(err); }
        return;
    }

    strcpy(client->screen_name, name);
    client->authenticated = 1;

    if (!try_add_client(client)) {
        client->screen_name[0] = '\0';
        client->authenticated = 0;
        char *err = create_err_message(ERR_NAME_IN_USE, "Screen name already in use");
        if (err) { send_to_client(client, err); free(err); }
        return;
    }

    char *welcome = create_msg_message(ROOM_ALL, name, "Welcome to the chat!");
    if (welcome) { send_to_client(client, welcome); free(welcome); }
}

void handle_set_message(client_t *client, const char *body) {
    char status[MAX_STATUS + 1];
    strncpy(status, body, sizeof(status) - 1);
    status[sizeof(status) - 1] = '\0';

    char *pipe = strchr(status, '|');
    if (pipe) *pipe = '\0';

    if (!validate_status(status)) {
        size_t len = strlen(status);
        char *err = (len > MAX_STATUS)
            ? create_err_message(ERR_TOO_LONG,    "Status must be 0-64 characters")
            : create_err_message(ERR_ILLEGAL_CHAR, "Status contains invalid characters");
        if (err) { send_to_client(client, err); free(err); }
        return;
    }

    strcpy(client->status, status);

    if (strlen(status) > 0) {
        char bcast[MAX_SCREEN_NAME + MAX_STATUS + 20];
        snprintf(bcast, sizeof(bcast), "%s is now \"%s\"",
                 client->screen_name, status);
        char *msg = create_msg_message(ROOM_ALL, ROOM_ALL, bcast);
        if (msg) { broadcast_message(msg, NULL); free(msg); }
    }
}

void handle_msg_message(client_t *client, const char *body) {
    char *copy = strdup(body);
    if (!copy) return;

    /* Skip the sender field (everything up to and including the first |) */
    char *first_pipe = strchr(copy, '|');
    if (!first_pipe) { free(copy); return; }

    char *recipient = first_pipe + 1;
    char *second_pipe = strchr(recipient, '|');
    if (!second_pipe) { free(copy); return; }
    *second_pipe = '\0';

    char *text = second_pipe + 1;
    char *last_pipe = strrchr(text, '|'); /* last |, text itself can contain | */
    if (!last_pipe) { free(copy); return; }
    *last_pipe = '\0';

    if (!validate_message_text(text)) {
        size_t len = strlen(text);
        char *err = (len == 0 || len > MAX_MESSAGE)
            ? create_err_message(ERR_TOO_LONG,    "Message must be 1-80 characters")
            : create_err_message(ERR_ILLEGAL_CHAR, "Message contains invalid characters");
        if (err) { send_to_client(client, err); free(err); }
        free(copy);
        return;
    }

    if (strcmp(recipient, ROOM_ALL) == 0) {
        char *msg = create_msg_message(client->screen_name, ROOM_ALL, text);
        if (msg) { broadcast_message(msg, NULL); free(msg); }
    } else {
        int target_fd = -1;
        pthread_mutex_lock(&clients_mutex);
        client_t *c = clients;
        while (c) {
            if (c->authenticated && strcmp(c->screen_name, recipient) == 0) {
                target_fd = c->socket;
                break;
            }
            c = c->next;
        }
        pthread_mutex_unlock(&clients_mutex);

        if (target_fd < 0) {
            char *err = create_err_message(ERR_UNKNOWN_RECIPIENT, "User not found");
            if (err) { send_to_client(client, err); free(err); }
        } else {
            char *msg = create_msg_message(client->screen_name, recipient, text);
            if (msg) { send(target_fd, msg, strlen(msg), 0); free(msg); }
        }
    }

    free(copy);
}

void handle_who_message(client_t *client, const char *body) {
    char target[MAX_SCREEN_NAME + 10];
    strncpy(target, body, sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';

    char *pipe = strchr(target, '|');
    if (pipe) *pipe = '\0';

    if (strcmp(target, ROOM_ALL) == 0) {
        char *response = malloc(WHO_BUF_SIZE);
        if (!response) return;
        int off = 0;

        pthread_mutex_lock(&clients_mutex);
        client_t *curr = clients;
        while (curr) {
            if (curr->authenticated) {
                if (off > 0 && off < WHO_BUF_SIZE - 1)
                    response[off++] = '\n';

                int n;
                if (strlen(curr->status) > 0)
                    n = snprintf(response + off, WHO_BUF_SIZE - off,
                                 "%s: %s", curr->screen_name, curr->status);
                else
                    n = snprintf(response + off, WHO_BUF_SIZE - off,
                                 "%s", curr->screen_name);

                if (n > 0) off += n;
                if (off >= WHO_BUF_SIZE - 1) break; /* buffer full */
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(&clients_mutex);
        response[off] = '\0';

        char *msg = create_msg_message(ROOM_ALL, client->screen_name, response);
        if (msg) { send_to_client(client, msg); free(msg); }
        free(response);
    } else {
        char status_copy[MAX_STATUS + 1];
        int found = 0;

        pthread_mutex_lock(&clients_mutex);
        client_t *curr = clients;
        while (curr) {
            if (curr->authenticated && strcmp(curr->screen_name, target) == 0) {
                strncpy(status_copy, curr->status, MAX_STATUS);
                status_copy[MAX_STATUS] = '\0';
                found = 1;
                break;
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(&clients_mutex);

        if (!found) {
            char *err = create_err_message(ERR_UNKNOWN_RECIPIENT, "User not found");
            if (err) { send_to_client(client, err); free(err); }
            return;
        }

        char response[MAX_SCREEN_NAME + MAX_STATUS + 10];
        if (strlen(status_copy) > 0)
            snprintf(response, sizeof(response), "%s: %s", target, status_copy);
        else
            strcpy(response, "No status");

        char *msg = create_msg_message(ROOM_ALL, client->screen_name, response);
        if (msg) { send_to_client(client, msg); free(msg); }
    }
}
