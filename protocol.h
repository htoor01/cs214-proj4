/*
 * protocol.h - Protocol definitions and message handling for chat server
 * CS 214 Spring 2026 - Project IV
 * Authors: Haaris Toor (hbt20), Hassan Ibrahim (hi125)
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

/* Protocol constants */
#define PROTOCOL_VERSION "1"
#define MAX_SCREEN_NAME 32
#define MAX_STATUS 64
#define MAX_MESSAGE 80
#define MAX_MSG_LENGTH 99999  /* 5-digit max */
#define FIELD_DELIMITER '|'

/* Message type codes */
#define MSG_NAM "NAM"
#define MSG_SET "SET"
#define MSG_MSG "MSG"
#define MSG_WHO "WHO"
#define MSG_ERR "ERR"

/* Error codes */
#define ERR_UNREADABLE 0
#define ERR_NAME_IN_USE 1
#define ERR_UNKNOWN_RECIPIENT 2
#define ERR_ILLEGAL_CHAR 3
#define ERR_TOO_LONG 4

/* Room name */
#define ROOM_ALL "#all"

/* Message structure */
typedef struct {
    char version[8];
    char code[4];
    int body_length;
    char *body;
} message_t;

/* Function prototypes */

/**
 * Parse a raw message string into a message structure
 * Returns 0 on success, -1 on error
 */
int parse_message(const char *raw, message_t *msg);

/**
 * Format a message structure into a raw string
 * Returns the formatted string (caller must free), or NULL on error
 */
char *format_message(const char *code, const char *body);

/**
 * Validate a screen name (1-32 chars, letters/digits/hyphen/underscore)
 * Returns 1 if valid, 0 if invalid
 */
int validate_screen_name(const char *name);

/**
 * Validate a status (0-64 chars, ASCII 32-126)
 * Returns 1 if valid, 0 if invalid
 */
int validate_status(const char *status);

/**
 * Validate a message (1-80 chars, ASCII 32-126)
 * Returns 1 if valid, 0 if invalid
 */
int validate_message_text(const char *text);

/**
 * Create a NAM message
 */
char *create_nam_message(const char *screen_name);

/**
 * Create a SET message
 */
char *create_set_message(const char *status);

/**
 * Create a MSG message
 */
char *create_msg_message(const char *sender, const char *recipient, const char *text);

/**
 * Create a WHO message
 */
char *create_who_message(const char *target);

/**
 * Create an ERR message
 */
char *create_err_message(int error_code, const char *explanation);

/**
 * Free a message structure
 */
void free_message(message_t *msg);

#endif /* PROTOCOL_H */
