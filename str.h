/*
 * peshamarsad - A personal local-first job-hunt tracker
 * Copyright (C) 2026  KinuCyber <kinucyber@kinu.uk>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>
 */

/*
 * str.h - string helpers
 *
 * Header-only. No str.c exists. Every .c file that includes this gets
 * its own copy of each function compiled in. That is what `static` does
 * here -- it tells the compiler each function belongs only to the file
 * that included it, preventing "multiple definition" linker errors.
 * `inline` asks the compiler to paste the function body at the call
 * site instead of doing a real function call, which is faster for small
 * functions like these. This is a signal of intent, not a command.
 *
 * Buffer size constants live here so every file agrees on the same limits.
 */

#ifndef STR_H   /* header guard: if STR_H is not yet defined...   */
#define STR_H   /* ...define it, then include everything below.   */
                /* The closing #endif is at the bottom of file.   */
                /* If this file is included twice, the second     */
                /* inclusion sees STR_H already defined and skips */
                /* straight to #endif, preventing duplicate code. */

#include <assert.h>   /* assert()                                  */
#include <string.h>   /* strlen, strncpy, memcpy, memmove, strncat */
#include <ctype.h>    /* isspace                                   */

/* ------------------------------------------------------------------
 * Buffer size constants
 *
 * PM_BUF      - one line of user input (any field the user can type)
 * PM_ENTRY    - one entry in a comma-separated list (e.g. "Skeptic")
 * PM_LIST_MAX - maximum entries in a personality or attitude list
 * ------------------------------------------------------------------ */
#define PM_BUF      512
#define PM_ENTRY     64
#define PM_LIST_MAX   8


/* ------------------------------------------------------------------
 * str_trim
 *
 * Remove leading and trailing whitespace from s, modifying it in place.
 * "In place" means we change the actual bytes inside the buffer -- no
 * new string is created. Returns s so callers can chain it if needed.
 *
 * Why (unsigned char)*start ?
 *   isspace() takes an int. On systems where char is signed, characters
 *   with values above 127 (e.g. some UTF-8 bytes) become negative when
 *   cast to int, which is undefined behaviour in isspace(). Casting to
 *   unsigned char first makes it always a value 0-255, which is safe.
 * ------------------------------------------------------------------ */
static inline char *str_trim(char *s)
{
    assert(s != NULL);  /* Only compiled when debugging is enabled */
    if (!s) return s;   /* guard: do nothing if pointer is NULL */

    /* --- trim leading whitespace --- */
    char *start = s;
    while (*start && isspace((unsigned char)*start))
        start++;
    /* start now points at the first non-space character (or '\0') */

    if (start != s)
        memmove(s, start, strlen(start) + 1);
        /* memmove shifts the trimmed content back to the buffer start.
         * We use memmove (not memcpy) because the source and destination
         * regions inside the same buffer can overlap.                   */

    /* --- trim trailing whitespace --- */
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end))
        *end-- = '\0';
    /* Write '\0' over each trailing space, working backwards.
     * '\0' is the null terminator -- the byte that tells C "string ends
     * here". Every C string ends with one.                              */

    return s;
}


/* ------------------------------------------------------------------
 * str_empty
 *
 * Returns 1 (true) if s is NULL or an empty string, 0 (false) otherwise.
 * Call this after str_trim() to check whether the user typed anything.
 *
 * *s dereferences the pointer: it reads the byte s is pointing at.
 * The first byte of a C string is '\0' if and only if the string is empty.
 * ------------------------------------------------------------------ */
static inline int str_empty(const char *s)
{
    return !s || *s == '\0';
}


/* ------------------------------------------------------------------
 * str_copy
 *
 * Safe string copy. Copies at most n-1 characters from src into dst,
 * then writes '\0' at dst[n-1] to guarantee null termination.
 *
 * The standard strncpy() does NOT guarantee a null terminator if src
 * is longer than n. This wrapper always does, which prevents a whole
 * class of bugs where a string "runs off the end" of its buffer.
 *
 * The whole purpose is to prefer buffer overflow.
 * This is achieved by ensuring that the string always ends with null
 * terminator and of n size, even at the cost of string truncation.i
 *
 * The asset helps with identifying truncation in debug builds.
 *  ------------------------------------------------------------------ */
static inline void str_copy(char *dst, const char *src, size_t n)
{
    assert(dst != NULL);
    assert(src != NULL);
    assert(n > 0);

    /* warn in debug builds if src would be truncated */
    assert(strlen(src) < n && "str_copy: input truncated");

    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}


/* ------------------------------------------------------------------
 * str_split
 *
 * Split the string src on delimiter delim into the 2D array out[][PM_ENTRY].
 * Each entry is trimmed. Writes at most `max` entries. Returns entry count.
 *
 * A 2D array out[][PM_ENTRY] means: an array of strings, where each
 * string is a fixed-size char array of length PM_ENTRY. In memory this
 * is just a flat block: [entry0 chars][entry1 chars][entry2 chars]...
 *
 * Example usage:
 *   char entries[PM_LIST_MAX][PM_ENTRY];
 *   int n = str_split("Skeptic,Patron,Neutral", ',', entries, PM_LIST_MAX);
 *   // n == 3
 *   // entries[0] == "Skeptic"
 *   // entries[1] == "Patron"
 *   // entries[2] == "Neutral"
 * ------------------------------------------------------------------ */
static inline int str_split(const char *src,
		            char delim,
                            char out[][PM_ENTRY],
			    int max)
{
    if (str_empty(src)) return 0;

    int count = 0;
    const char *p = src;   /* p walks through src character by character */

    while (*p && count < max) {
        /* q scans forward from p until it hits the delimiter or end */
        const char *q = p;
        while (*q && *q != delim)
            q++;

        /* copy the slice [p, q) into out[count] */
        size_t len = (size_t)(q - p);
        if (len >= PM_ENTRY) len = PM_ENTRY - 1;   /* clamp to buffer */
        memcpy(out[count], p, len);
        out[count][len] = '\0';
        str_trim(out[count]);

        /* only count it if something remains after trimming */
        if (!str_empty(out[count]))
            count++;

        /* advance p past the delimiter, or stop if we hit end of string */
        p = *q ? q + 1 : q;
    }

    return count;
}


/* ------------------------------------------------------------------
 * str_join
 *
 * Join `count` entries from entries[][PM_ENTRY] into dst, separated by
 * sep. Writes at most dstn-1 characters. Always null-terminates.
 *
 * Example:
 *   char buf[PM_BUF];
 *   str_join(buf, PM_BUF, entries, 3, ",");
 *   // buf == "Skeptic,Patron,Neutral"
 * ------------------------------------------------------------------ */
static inline void str_join(char *dst,
		            size_t dstn,
                            char entries[][PM_ENTRY],
			    int count,
                            const char *sep)
{
    dst[0] = '\0';   /* start with empty string */
    for (int i = 0; i < count; i++) {
        if (i > 0)
            strncat(dst, sep, dstn - strlen(dst) - 1);
        strncat(dst, entries[i], dstn - strlen(dst) - 1);
        /* dstn - strlen(dst) - 1 is the remaining space in the buffer.
         * We subtract 1 to always leave room for the null terminator.  */
    }
}


/* ------------------------------------------------------------------
 * str_in_list
 *
 * Returns 1 if needle exactly matches any string in list[0..count-1].
 * Case-sensitive. Used to validate enum values before inserting to DB.
 *
 * const char *list[] is an array of pointers, each pointing to a string.
 * strcmp returns 0 when two strings are identical.
 * ------------------------------------------------------------------ */
static inline int str_in_list(const char *needle,
                              const char *list[],
			      int count)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(needle, list[i]) == 0)
            return 1;
    }
    return 0;
}


#endif /* STR_H */
