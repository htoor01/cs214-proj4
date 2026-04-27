/*
 * test_client.c - Simple test client for chatd
 * CS 214 Spring 2026 - Project IV
 * Authors: Haaris Toor (hbt20), Hassan Ibrahim (hi125)
 *
 * This is a COMPLETE WORKING EXAMPLE test client for the chat server.
 * You can use this to test your server implementation and as a reference
 * for how the protocol works.
 * 
 * Usage: ./test_client <host> <port> <screen_name>
 * 
 * Commands once connected:
 *   /msg <user> <message>  - Send private message to user
 *   /who [user|#all]       - Query user info (defaults to #all)
 *   /status <status>       - Set your status
 *   /quit                  - Disconnect
 *   <any other text>       - Send message to #all
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

void *receive_messages(void *arg);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> <screen_name>\n", argv[0]);
        return 1;
    }

    char *host = argv[1];
    int port = atoi(argv[2]);
    char *screen_name = argv[3];

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Resolve hostname
    struct hostent *server = gethostbyname(host);
    if (!server) {
        fprintf(stderr, "Error: No such host %s\n", host);
        return 1;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    printf("Connected to %s:%d\n", host, port);

    // Send NAM message to set screen name
    char nam_msg[BUFFER_SIZE];
    int name_len = strlen(screen_name);
    snprintf(nam_msg, sizeof(nam_msg), "1|NAM|%d|%s|", name_len + 1, screen_name);
    
    if (send(sockfd, nam_msg, strlen(nam_msg), 0) < 0) {
        perror("send");
        close(sockfd);
        return 1;
    }

    printf("Sent NAM message for '%s'\n", screen_name);

    // Start thread to receive messages
    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive_messages, &sockfd) != 0) {
        perror("pthread_create");
        close(sockfd);
        return 1;
    }

    // Main loop: read from stdin and send messages
    char input[BUFFER_SIZE];
    printf("\nEnter messages (or commands):\n");
    printf("  /msg <user> <message> - Send private message\n");
    printf("  /who [user|#all]      - Query user info\n");
    printf("  /status <status>      - Set status\n");
    printf("  /quit                 - Disconnect\n\n");

    while (fgets(input, sizeof(input), stdin)) {
        // Remove newline
        input[strcspn(input, "\n")] = 0;

        if (strlen(input) == 0) continue;

        if (strcmp(input, "/quit") == 0) {
            break;
        }

        char message[BUFFER_SIZE];
        
        if (strncmp(input, "/msg ", 5) == 0) {
            // Private message: /msg user message
            char *recipient = strtok(input + 5, " ");
            char *text = strtok(NULL, "");
            
            if (recipient && text) {
                char body[BUFFER_SIZE];
                snprintf(body, sizeof(body), "||%s|%s|", recipient, text);
                snprintf(message, sizeof(message), "1|MSG|%zu|%s", strlen(body), body);
                send(sockfd, message, strlen(message), 0);
            }
        } else if (strncmp(input, "/who", 4) == 0) {
            // WHO query
            char *target = strtok(input + 4, " ");
            if (!target) target = "#all";
            
            char body[BUFFER_SIZE];
            snprintf(body, sizeof(body), "%s|", target);
            snprintf(message, sizeof(message), "1|WHO|%zu|%s", strlen(body), body);
            send(sockfd, message, strlen(message), 0);
        } else if (strncmp(input, "/status ", 8) == 0) {
            // Set status
            char *status = input + 8;
            char body[BUFFER_SIZE];
            snprintf(body, sizeof(body), "%s|", status);
            snprintf(message, sizeof(message), "1|SET|%zu|%s", strlen(body), body);
            send(sockfd, message, strlen(message), 0);
        } else {
            // Regular message to #all
            char body[BUFFER_SIZE];
            snprintf(body, sizeof(body), "||#all|%s|", input);
            snprintf(message, sizeof(message), "1|MSG|%zu|%s", strlen(body), body);
            send(sockfd, message, strlen(message), 0);
        }
    }

    close(sockfd);
    printf("Disconnected.\n");
    return 0;
}

void *receive_messages(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[BUFFER_SIZE];
    
    while (1) {
        ssize_t bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            printf("\nConnection closed by server.\n");
            break;
        }
        
        buffer[bytes] = '\0';
        printf("\n<< %s\n", buffer);
    }
    
    return NULL;
}
