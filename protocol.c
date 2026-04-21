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
    
    /* TODO: Implement message parsing
     * Format: VERSION|CODE|LENGTH|BODY|
     * Example: "1|NAM|4|Bob|"
     * 
     * Steps:
     * 1. Make a copy of raw string (use strdup) since strtok modifies it
     * 2. Use strtok() with delimiter "|" to extract:
     *    - version field -> copy to msg->version
     *    - code field -> copy to msg->code (exactly 3 chars)
     *    - length field -> convert to int with atoi(), store in msg->body_length
     * 3. After parsing header, calculate where body starts in original string
     * 4. Allocate msg->body_length bytes for msg->body
     * 5. Copy exactly msg->body_length bytes from raw into msg->body
     * 6. Verify the last character in body is '|'
     * 7. Verify version is "1" (if not, return -1)
     * 8. Free the duplicated string
     * 9. Return 0 on success, -1 on any error
     * 
     * Hint: The body includes the trailing '|' in its length!
     */
    
    return 0;
}

char *format_message(const char *code, const char *body) {
    if (!code || !body) return NULL;
    
    /* NOTE: This function is mostly complete but may need adjustment.
     * Currently it formats messages as: VERSION|CODE|LENGTH|BODY|
     * The body parameter should already include the trailing '|' for all fields.
     * 
     * TODO: Verify the total_len calculation is correct and adjust if needed.
     * The current calculation might allocate more space than necessary.
     * Consider using snprintf with NULL to calculate exact length needed.
     */
    
    int body_len = strlen(body);
    int total_len = 1 + 1 + strlen(code) + 1 + 5 + 1 + body_len + 1;
    
    char *msg = malloc(total_len);
    if (!msg) return NULL;
    
    snprintf(msg, total_len, "%s|%s|%d|%s|", PROTOCOL_VERSION, code, body_len, body);
    
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
    return format_message(MSG_NAM, (char *)screen_name);
}

char *create_set_message(const char *status) {
    if (!status) return NULL;
    return format_message(MSG_SET, (char *)status);
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
    return format_message(MSG_WHO, (char *)target);
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
