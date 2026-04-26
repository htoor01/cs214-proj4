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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocol.h"

/* Client structure */
typedef struct client {
    int socket;
    char screen_name[MAX_SCREEN_NAME + 1];
    char status[MAX_STATUS + 1];
    int authenticated;
    struct client *next;
} client_t;

/* Global variables */
static client_t *clients = NULL;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Function prototypes */
void *handle_client(void *arg);
int setup_server_socket(int port);
void add_client(client_t *client);
void remove_client(client_t *client);
client_t *find_client_by_name(const char *name);
void broadcast_message(const char *message, client_t *exclude);
void send_to_client(client_t *client, const char *message);
void handle_nam_message(client_t *client, const char *body);
void handle_set_message(client_t *client, const char *body);
void handle_msg_message(client_t *client, const char *body);
void handle_who_message(client_t *client, const char *body);
void cleanup_and_exit(int status);

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

    int server_socket = setup_server_socket(port);
    if (server_socket < 0) {
        fprintf(stderr, "Error: Failed to set up server socket\n");
        return 1;
    }

    printf("Chat server listening on port %d...\n", port);

    // Main accept loop
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

        // Create client structure
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

        // Create thread to handle client
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
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 10) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[MAX_MSG_LENGTH + 100];
    
    while (1) {
        // Read message from client
        ssize_t bytes = recv(client->socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            // Client disconnected or error
            break;
        }
        
        buffer[bytes] = '\0';
        
        // Parse the message
        message_t msg;
        if (parse_message(buffer, &msg) < 0) {
            // Send error 0 (unreadable) - this is fatal
            char *err = create_err_message(ERR_UNREADABLE, "Invalid message format");
            if (err) {
                send_to_client(client, err);
                free(err);
            }
            break;  // Error 0 is fatal - close connection
        }
        
        // Check authentication - only NAM is allowed before authentication
        if (!client->authenticated && strcmp(msg.code, MSG_NAM) != 0) {
            char *err = create_err_message(ERR_UNREADABLE, "Must authenticate first");
            if (err) {
                send_to_client(client, err);
                free(err);
            }
            free_message(&msg);
            break;  // Error 0 is fatal
        }
        
        // Route to appropriate handler based on message code
        if (strcmp(msg.code, MSG_NAM) == 0) {
            handle_nam_message(client, msg.body);
        } else if (strcmp(msg.code, MSG_SET) == 0) {
            handle_set_message(client, msg.body);
        } else if (strcmp(msg.code, MSG_MSG) == 0) {
            handle_msg_message(client, msg.body);
        } else if (strcmp(msg.code, MSG_WHO) == 0) {
            handle_who_message(client, msg.body);
        } else {
            // Unknown message type - send error 0 (fatal)
            char *err = create_err_message(ERR_UNREADABLE, "Unknown message type");
            if (err) {
                send_to_client(client, err);
                free(err);
            }
            free_message(&msg);
            break;
        }
        
        // Free the message body
        free_message(&msg);
    }
    
    // Clean up
    remove_client(client);
    close(client->socket);
    free(client);
    
    return NULL;
}

void add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    client->next = clients;
    clients = client;
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    
    client_t **curr = &clients;
    while (*curr) {
        if (*curr == client) {
            *curr = client->next;
            break;
        }
        curr = &(*curr)->next;
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

client_t *find_client_by_name(const char *name) {
    pthread_mutex_lock(&clients_mutex);
    
    client_t *curr = clients;
    while (curr) {
        if (curr->authenticated && strcmp(curr->screen_name, name) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return curr;
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

void broadcast_message(const char *message, client_t *exclude) {
    pthread_mutex_lock(&clients_mutex);
    
    client_t *curr = clients;
    while (curr) {
        if (curr != exclude && curr->authenticated) {
            send_to_client(curr, message);
        }
        curr = curr->next;
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

void send_to_client(client_t *client, const char *message) {
    if (!client || !message) return;
    
    size_t len = strlen(message);
    ssize_t sent = send(client->socket, message, len, 0);
    
    if (sent < 0) {
        perror("send");
    }
}

void handle_nam_message(client_t *client, const char *body) {
    // Extract screen name from body (remove trailing '|')
    char name[MAX_SCREEN_NAME + 1];
    strncpy(name, body, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    
    // Remove trailing '|'
    char *pipe = strchr(name, '|');
    if (pipe) *pipe = '\0';
    
    // Validate screen name
    if (!validate_screen_name(name)) {
        // Determine specific error
        size_t len = strlen(name);
        char *err;
        
        if (len == 0 || len > MAX_SCREEN_NAME) {
            err = create_err_message(ERR_TOO_LONG, "Name must be 1-32 characters");
        } else {
            err = create_err_message(ERR_ILLEGAL_CHAR, "Name contains invalid characters");
        }
        
        if (err) {
            send_to_client(client, err);
            free(err);
        }
        return;
    }
    
    // Check if name already in use
    if (find_client_by_name(name) != NULL) {
        char *err = create_err_message(ERR_NAME_IN_USE, "Screen name already in use");
        if (err) {
            send_to_client(client, err);
            free(err);
        }
        return;
    }
    
    // Valid and unique - set up the client
    strcpy(client->screen_name, name);
    client->authenticated = 1;
    add_client(client);
    
    // Send welcome message
    char *welcome = create_msg_message(ROOM_ALL, name, "Welcome to the chat!");
    if (welcome) {
        send_to_client(client, welcome);
        free(welcome);
    }
}

void handle_set_message(client_t *client, const char *body) {
    // Extract status from body (remove trailing '|')
    char status[MAX_STATUS + 1];
    strncpy(status, body, sizeof(status) - 1);
    status[sizeof(status) - 1] = '\0';
    
    // Remove trailing '|'
    char *pipe = strchr(status, '|');
    if (pipe) *pipe = '\0';
    
    // Validate status
    if (!validate_status(status)) {
        char *err;
        size_t len = strlen(status);
        
        if (len > MAX_STATUS) {
            err = create_err_message(ERR_TOO_LONG, "Status must be 0-64 characters");
        } else {
            err = create_err_message(ERR_ILLEGAL_CHAR, "Status contains invalid characters");
        }
        
        if (err) {
            send_to_client(client, err);
            free(err);
        }
        return;
    }
    
    // Update client's status
    strcpy(client->status, status);
    
    // Broadcast status change if non-empty
    if (strlen(status) > 0) {
        char broadcast[256];
        snprintf(broadcast, sizeof(broadcast), "%s is now \"%s\"",
                 client->screen_name, status);
        
        char *msg = create_msg_message(ROOM_ALL, ROOM_ALL, broadcast);
        if (msg) {
            broadcast_message(msg, NULL);
            free(msg);
        }
    }
}

void handle_msg_message(client_t *client, const char *body) {
    // Parse body to extract sender, recipient, and message text
    char *copy = strdup(body);
    if (!copy) return;
    
    strtok(copy, "|");                     // Skip sender - client could be spoofing
    char *recipient = strtok(NULL, "|");
    char *text = strtok(NULL, "|");
    
    if (!recipient || !text) {
        free(copy);
        return;
    }
    
    // Validate message text
    if (!validate_message_text(text)) {
        char *err;
        size_t len = strlen(text);
        
        if (len == 0 || len > MAX_MESSAGE) {
            err = create_err_message(ERR_TOO_LONG, "Message must be 1-80 characters");
        } else {
            err = create_err_message(ERR_ILLEGAL_CHAR, "Message contains invalid characters");
        }
        
        if (err) {
            send_to_client(client, err);
            free(err);
        }
        free(copy);
        return;
    }
    
    // Send message based on recipient
    if (strcmp(recipient, ROOM_ALL) == 0) {
        // Broadcast to all users
        char *msg = create_msg_message(client->screen_name, ROOM_ALL, text);
        if (msg) {
            broadcast_message(msg, NULL);
            free(msg);
        }
    } else {
        // Private message to specific user
        client_t *target = find_client_by_name(recipient);
        if (!target) {
            char *err = create_err_message(ERR_UNKNOWN_RECIPIENT, "User not found");
            if (err) {
                send_to_client(client, err);
                free(err);
            }
        } else {
            char *msg = create_msg_message(client->screen_name, recipient, text);
            if (msg) {
                send_to_client(target, msg);
                free(msg);
            }
        }
    }
    
    free(copy);
}

void handle_who_message(client_t *client, const char *body) {
    // Extract target from body (remove trailing '|')
    char target[MAX_SCREEN_NAME + 10];
    strncpy(target, body, sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';
    
    // Remove trailing '|'
    char *pipe = strchr(target, '|');
    if (pipe) *pipe = '\0';
    
    if (strcmp(target, ROOM_ALL) == 0) {
        // Query all users in the room
        char *response = malloc(10240);  // 10KB buffer
        if (!response) return;
        
        response[0] = '\0';
        int first = 1;
        
        pthread_mutex_lock(&clients_mutex);
        
        client_t *curr = clients;
        while (curr) {
            if (curr->authenticated) {
                if (!first) {
                    strcat(response, "\n");
                }
                first = 0;
                
                if (strlen(curr->status) > 0) {
                    // Format: "name: status"
                    char line[MAX_SCREEN_NAME + MAX_STATUS + 10];
                    snprintf(line, sizeof(line), "%s: %s", curr->screen_name, curr->status);
                    strcat(response, line);
                } else {
                    // Just the name
                    strcat(response, curr->screen_name);
                }
            }
            curr = curr->next;
        }
        
        pthread_mutex_unlock(&clients_mutex);
        
        // Send response to requesting client
        char *msg = create_msg_message(ROOM_ALL, client->screen_name, response);
        if (msg) {
            send_to_client(client, msg);
            free(msg);
        }
        
        free(response);
    } else {
        // Query specific user
        client_t *target_client = find_client_by_name(target);
        
        if (!target_client) {
            char *err = create_err_message(ERR_UNKNOWN_RECIPIENT, "User not found");
            if (err) {
                send_to_client(client, err);
                free(err);
            }
            return;
        }
        
        char response[MAX_SCREEN_NAME + MAX_STATUS + 10];
        
        if (strlen(target_client->status) > 0) {
            // Format: "name: status"
            snprintf(response, sizeof(response), "%s: %s", 
                     target_client->screen_name, target_client->status);
        } else {
            // No status
            strcpy(response, "No status");
        }
        
        char *msg = create_msg_message(ROOM_ALL, client->screen_name, response);
        if (msg) {
            send_to_client(client, msg);
            free(msg);
        }
    }
}
