#include "http_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Extract a field from URL-encoded body: "field=value&..."
// Handles %XX decoding and + as space
void url_decode_field(const char *body, const char *field, char *out, size_t out_size)
{
    out[0] = '\0';
    // Find "field=" in body
    char key[64];
    snprintf(key, sizeof(key), "%s=", field);
    const char *start = strstr(body, key);
    if (!start) return;
    start += strlen(key);
    const char *end = strchr(start, '&');
    if (!end) end = start + strlen(start);

    size_t i = 0;
    while (start < end && i < out_size - 1) {
        if (*start == '+') {
            out[i++] = ' ';
            start++;
        } else if (*start == '%' && start + 2 < end) {
            char hex[3] = { start[1], start[2], '\0' };
            out[i++] = (char)strtoul(hex, NULL, 16);
            start += 3;
        } else {
            out[i++] = *start++;
        }
    }
    out[i] = '\0';
}
