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
    
    /* TODO: Implement client message loop
     * 
     * Steps:
     * 1. Loop: read data from client->socket using recv()
     * 2. Check if bytes <= 0 (disconnect or error) -> break
     * 3. Null-terminate the buffer: buffer[bytes] = '\0'
     * 4. Declare: message_t msg;
     * 5. Call parse_message(buffer, &msg)
     * 6. If parse fails (returns -1):
     *    - Create error message: create_err_message(ERR_UNREADABLE, "Invalid message format")
     *    - Send error to client
     *    - Free the error message
     *    - Close connection and break (error 0 is fatal!)
     * 7. Check msg.code and route to handler:
     *    - if strcmp(msg.code, MSG_NAM) == 0: handle_nam_message(client, msg.body)
     *    - if strcmp(msg.code, MSG_SET) == 0: handle_set_message(client, msg.body)
     *    - if strcmp(msg.code, MSG_MSG) == 0: handle_msg_message(client, msg.body)
     *    - if strcmp(msg.code, MSG_WHO) == 0: handle_who_message(client, msg.body)
     *    - else: send ERR_UNREADABLE and close (unknown message type)
     * 8. Before sending ANY message, check if client is authenticated!
     *    - Only NAM is allowed before authentication
     *    - For other messages, if !client->authenticated, send error and continue
     * 9. Call free_message(&msg) to free msg.body after handling
     * 10. On loop exit: remove_client(), close socket, free client
     * 
     * IMPORTANT: recv() might not read the entire message at once!
     * For a complete implementation, you may need to buffer partial messages.
     */
    
    while (1) {
        // Read message from client
        ssize_t bytes = recv(client->socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            // Client disconnected or error
            break;
        }
        
        buffer[bytes] = '\0';
        
        // TODO: Uncomment and complete the implementation below:
        // message_t msg;
        // if (parse_message(buffer, &msg) < 0) {
        //     char *err = create_err_message(ERR_UNREADABLE, "Invalid message format");
        //     send_to_client(client, err);
        //     free(err);
        //     break;  // Error 0 is fatal
        // }
        
        // TODO: Route to appropriate handler based on msg.code
        // TODO: Check authentication status
        // TODO: Call free_message(&msg) when done
        
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
    /* TODO: Implement NAM message handling
     * Body format: "screen_name|"
     * 
     * Steps:
     * 1. Extract screen name from body (remove trailing '|'):
     *    - Copy body to temp buffer
     *    - Find the '|' and replace with '\0', or use strtok
     * 2. Validate screen name:
     *    - Call validate_screen_name(name)
     *    - If invalid, determine why:
     *      * If contains illegal chars -> send ERR_ILLEGAL_CHAR
     *      * If too long (>32) or empty -> send ERR_TOO_LONG
     *    - Create error with create_err_message(code, "explanation")
     *    - Send error with send_to_client(client, error_msg)
     *    - Free error message and return
     * 3. Check if name already in use:
     *    - Call find_client_by_name(name)
     *    - If found (not NULL) -> send ERR_NAME_IN_USE
     * 4. If valid and unique:
     *    - strcpy(client->screen_name, name)
     *    - client->authenticated = 1
     *    - Call add_client(client) to add to global list
     *    - Create welcome message:
     *      char *welcome = create_msg_message("#all", name, "Welcome to the chat!");
     *    - Send with send_to_client(client, welcome)
     *    - Free welcome message
     * 
     * Error message examples:
     * - create_err_message(ERR_NAME_IN_USE, "Screen name already in use")
     * - create_err_message(ERR_ILLEGAL_CHAR, "Name contains invalid characters")
     * - create_err_message(ERR_TOO_LONG, "Name too long (max 32 chars)")
     */
}

void handle_set_message(client_t *client, const char *body) {
    /* TODO: Implement SET message handling
     * Body format: "status|"
     * 
     * Steps:
     * 1. Extract status from body (remove trailing '|')
     * 2. Validate status:
     *    - Call validate_status(status)
     *    - If invalid:
     *      * If contains chars outside ASCII 32-126 -> send ERR_ILLEGAL_CHAR
     *      * If too long (>64 chars) -> send ERR_TOO_LONG
     *    - Send error and return if invalid
     * 3. Update client's status:
     *    - strcpy(client->status, status)
     * 4. If status is non-empty (strlen(status) > 0):
     *    - Create broadcast message:
     *      Format: "Bob is now \"Smiling politely\""
     *      char broadcast[256];
     *      snprintf(broadcast, sizeof(broadcast), "%s is now \"%s\"",
     *               client->screen_name, status);
     *    - Create message: create_msg_message("#all", "#all", broadcast)
     *    - Call broadcast_message(msg, NULL) to send to all clients
     *    - Free the message
     * 
     * Note: If status is empty, just update it silently (no broadcast)
     */
}

void handle_msg_message(client_t *client, const char *body) {
    /* TODO: Implement MSG message handling
     * Body format: "sender|recipient|message|"
     * 
     * Steps:
     * 1. Parse the body to extract three fields:
     *    - Make a copy of body (strdup) since strtok modifies it
     *    - char *sender = strtok(copy, "|");    // IGNORE THIS - client could be spoofing!
     *    - char *recipient = strtok(NULL, "|");
     *    - char *text = strtok(NULL, "|");
     *    - Check if recipient and text are not NULL
     * 2. Validate the message text:
     *    - Call validate_message_text(text)
     *    - If invalid:
     *      * Check if contains chars outside ASCII 32-126 -> ERR_ILLEGAL_CHAR
     *      * Check if too long (>80 chars) or empty -> ERR_TOO_LONG
     *    - Send error and return if invalid
     * 3. Determine recipient and send:
     *    - If recipient is "#all":
     *      * Create msg: create_msg_message(client->screen_name, "#all", text)
     *      * Call broadcast_message(msg, NULL) to send to everyone
     *      * Free the message
     *    - Else (private message to specific user):
     *      * Call find_client_by_name(recipient) to find target
     *      * If not found -> send ERR_UNKNOWN_RECIPIENT and return
     *      * If found:
     *        - Create msg: create_msg_message(client->screen_name, recipient, text)
     *        - Call send_to_client(target, msg)
     *        - Free the message
     * 4. Free the copied body string
     * 
     * CRITICAL: Always use client->screen_name as sender, NOT the sender field
     *           from the message body! This prevents users from impersonating others.
     */
}

void handle_who_message(client_t *client, const char *body) {
    /* TODO: Implement WHO message handling
     * Body format: "target|"
     * 
     * Steps:
     * 1. Extract target from body (remove trailing '|')
     * 2. If target is "#all":
     *    - Lock the clients_mutex
     *    - Allocate a large buffer for response (e.g., 10KB)
     *    - Iterate through all clients in the global clients list:
     *      * For each authenticated client:
     *        - If client has status (strlen(status) > 0):
     *          Append "name: status\n" to response
     *        - Else:
     *          Append "name\n" to response
     *    - Remove the last '\n' from response
     *    - Unlock the mutex
     *    - Create message: create_msg_message("#all", client->screen_name, response)
     *    - Send to requesting client: send_to_client(client, msg)
     *    - Free message and response buffer
     * 3. Else (query specific user):
     *    - Call find_client_by_name(target)
     *    - If not found:
     *      * Send ERR_UNKNOWN_RECIPIENT
     *      * Return
     *    - If found:
     *      * If target has status:
     *        Format: "target: status"
     *      * Else:
     *        Response: "No status"
     *      * Create message: create_msg_message("#all", client->screen_name, response)
     *      * Send to client
     *      * Free message
     * 
     * Example responses:
     * - Single user with status: "Alice: I was here first"
     * - Single user no status: "No status"
     * - All users: "Alice: I was here first\nBob: Smiling politely\nCarol"
     */
}
