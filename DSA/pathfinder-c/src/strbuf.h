/* strbuf.h - growable string buffer used to build JSON and HTTP responses. */
#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_putc(StrBuf *sb, char c);
void sb_puts(StrBuf *sb, const char *s);
void sb_printf(StrBuf *sb, const char *fmt, ...);
/* Appends s as a quoted, escaped JSON string. */
void sb_json_str(StrBuf *sb, const char *s);

#endif
