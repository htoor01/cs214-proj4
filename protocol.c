/*
 * protocol.c - Protocol implementation for chat server
 * CS 214 Spring 2026 - Project IV
 * Authors: Haaris Toor (hbt20), Hassan Ibrahim (hi125)
 */

#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int parse_message(const char *raw, message_t *msg) {
    if (!raw || !msg) return -1;
    
    // Make a copy for strtok (it modifies the string)
    char *copy = strdup(raw);
    if (!copy) return -1;
    
    // Parse version field
    char *version = strtok(copy, "|");
    if (!version) {
        free(copy);
        return -1;
    }
    
    // Verify version is "1"
    if (strcmp(version, "1") != 0) {
        free(copy);
        return -1;
    }
    strncpy(msg->version, version, sizeof(msg->version) - 1);
    msg->version[sizeof(msg->version) - 1] = '\0';
    
    // Parse code field (3 characters)
    char *code = strtok(NULL, "|");
    if (!code || strlen(code) != 3) {
        free(copy);
        return -1;
    }
    strncpy(msg->code, code, sizeof(msg->code) - 1);
    msg->code[sizeof(msg->code) - 1] = '\0';
    
    // Parse length field
    char *length_str = strtok(NULL, "|");
    if (!length_str) {
        free(copy);
        return -1;
    }
    msg->body_length = atoi(length_str);
    
    if (msg->body_length < 0 || msg->body_length > MAX_MSG_LENGTH) {
        free(copy);
        return -1;
    }
    
    // Calculate where the body starts in the original string
    // Find the position after the third '|'
    const char *body_start = raw;
    int pipe_count = 0;
    while (*body_start && pipe_count < 3) {
        if (*body_start == '|') pipe_count++;
        body_start++;
    }
    
    if (pipe_count != 3) {
        free(copy);
        return -1;
    }
    
    // Allocate and copy the body
    msg->body = malloc(msg->body_length + 1);
    if (!msg->body) {
        free(copy);
        return -1;
    }
    
    memcpy(msg->body, body_start, msg->body_length);
    msg->body[msg->body_length] = '\0';
    
    // Verify the body ends with '|'
    if (msg->body_length == 0 || msg->body[msg->body_length - 1] != '|') {
        free(msg->body);
        msg->body = NULL;
        free(copy);
        return -1;
    }
    
    free(copy);
    return 0;
}

char *format_message(const char *code, const char *body) {
    if (!code || !body) return NULL;
    
    int body_len = strlen(body);
    // Calculate exact length: version + | + code + | + length (max 5 digits) + | + body
    int header_len = snprintf(NULL, 0, "%s|%s|%d|", PROTOCOL_VERSION, code, body_len);
    int total_len = header_len + body_len + 1;  // +1 for null terminator
    
    char *msg = malloc(total_len);
    if (!msg) return NULL;
    
    snprintf(msg, total_len, "%s|%s|%d|%s", PROTOCOL_VERSION, code, body_len, body);
    
    return msg;
}

int validate_screen_name(const char *name) {
    if (!name) return 0;
    
    size_t len = strlen(name);
    if (len < 1 || len > MAX_SCREEN_NAME) return 0;
    
    // Check for valid characters: letters, digits, hyphen, underscore
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum(c) && c != '-' && c != '_') {
            return 0;
        }
    }
    
    return 1;
}

int validate_status(const char *status) {
    if (!status) return 1;  // Empty status is valid
    
    size_t len = strlen(status);
    if (len > MAX_STATUS) return 0;
    
    // Check for valid characters: ASCII 32-126
    for (size_t i = 0; i < len; i++) {
        unsigned char c = status[i];
        if (c < 32 || c > 126) {
            return 0;
        }
    }
    
    return 1;
}

int validate_message_text(const char *text) {
    if (!text) return 0;
    
    size_t len = strlen(text);
    if (len < 1 || len > MAX_MESSAGE) return 0;
    
    // Check for valid characters: ASCII 32-126
    for (size_t i = 0; i < len; i++) {
        unsigned char c = text[i];
        if (c < 32 || c > 126) {
            return 0;
        }
    }
    
    return 1;
}

char *create_nam_message(const char *screen_name) {
    if (!screen_name) return NULL;
    
    // Body format: screen_name|
    char body[MAX_SCREEN_NAME + 2];
    snprintf(body, sizeof(body), "%s|", screen_name);
    
    return format_message(MSG_NAM, body);
}

char *create_set_message(const char *status) {
    if (!status) return NULL;
    
    // Body format: status|
    char body[MAX_STATUS + 2];
    snprintf(body, sizeof(body), "%s|", status);
    
    return format_message(MSG_SET, body);
}

char *create_msg_message(const char *sender, const char *recipient, const char *text) {
    if (!sender || !recipient || !text) return NULL;
    
    // Body format: sender|recipient|text|
    char body[MAX_MSG_LENGTH];
    snprintf(body, sizeof(body), "%s|%s|%s|", sender, recipient, text);
    
    return format_message(MSG_MSG, body);
}

char *create_who_message(const char *target) {
    if (!target) return NULL;
    
    // Body format: target|
    char body[MAX_SCREEN_NAME + 2];
    snprintf(body, sizeof(body), "%s|", target);
    
    return format_message(MSG_WHO, body);
}

char *create_err_message(int error_code, const char *explanation) {
    if (!explanation) return NULL;
    
    // Body format: error_code|explanation|
    char body[MAX_MSG_LENGTH];
    snprintf(body, sizeof(body), "%d|%s|", error_code, explanation);
    
    return format_message(MSG_ERR, body);
}

void free_message(message_t *msg) {
    if (msg && msg->body) {
        free(msg->body);
        msg->body = NULL;
    }
}
