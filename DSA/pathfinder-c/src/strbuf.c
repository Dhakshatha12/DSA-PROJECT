/* strbuf.c - growable string buffer. */
#include "strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sb_reserve(StrBuf *sb, size_t extra)
{
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap)
        return;
    size_t cap = sb->cap ? sb->cap : 256;
    while (cap < need)
        cap *= 2;
    char *nb = realloc(sb->buf, cap);
    if (!nb) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    sb->buf = nb;
    sb->cap = cap;
}

void sb_init(StrBuf *sb)
{
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb_reserve(sb, 0);
    sb->buf[0] = '\0';
}

void sb_free(StrBuf *sb)
{
    free(sb->buf);
    sb->buf = NULL;
    sb->len = sb->cap = 0;
}

void sb_putc(StrBuf *sb, char c)
{
    sb_reserve(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

void sb_puts(StrBuf *sb, const char *s)
{
    size_t n = strlen(s);
    sb_reserve(sb, n);
    memcpy(sb->buf + sb->len, s, n + 1);
    sb->len += n;
}

void sb_printf(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->buf + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
}

void sb_json_str(StrBuf *sb, const char *s)
{
    sb_putc(sb, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  sb_puts(sb, "\\\""); break;
        case '\\': sb_puts(sb, "\\\\"); break;
        case '\n': sb_puts(sb, "\\n");  break;
        case '\r': sb_puts(sb, "\\r");  break;
        case '\t': sb_puts(sb, "\\t");  break;
        default:
            if (*p < 0x20)
                sb_printf(sb, "\\u%04x", *p);
            else
                sb_putc(sb, (char)*p);
        }
    }
    sb_putc(sb, '"');
}
