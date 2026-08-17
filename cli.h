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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * cli.h - prompt and display helpers
 *
 * Header-only, static inline, same pattern as str.h and db.h.
 *
 * Input helpers:
 *   cli_read             required field; loops until non-empty
 *   cli_read_opt         optional field; empty is accepted
 *   cli_read_update      show def value; Enter keeps it
 *   cli_read_bool        y/n prompt; returns 1 or 0
 *   cli_read_bool_update y/n showing default; Enter keeps default
 *   cli_read_int         integer prompt; loops until valid
 *   cli_confirm          "? [y/N]" confirmation; defaults to no
 *   cli_pick             numbered menu, required choice
 *   cli_pick_opt         numbered menu, 0 to skip
 *   cli_pick_update      numbered menu showing default, 0 to keep
 *
 * Display helpers:
 *   cli_header           print a section header with rule
 *   cli_sep              print a horizontal rule
 *   cli_field            "  Label:               value"
 *   cli_bool_field       "  Label:               Yes / No"
 */

#ifndef CLI_H
#define CLI_H

#include <stdio.h>    /* printf, fgets, fflush  */
#include <stdlib.h>   /* atoi                   */
#include "str.h"

/* ------------------------------------------------------------------
 * flush_stdin
 *
 * Flushes the stdin. Prevents overflow when stdin overflows the
 * buffer it was being written to.
 * ----------------------------------------------------------------- */
static inline void flush_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
}


/* ------------------------------------------------------------------
 * read_line
 *
 * Wraps fgets with flush_stdin. Returns user input in terminal and
 * then automatically flushes the stdin if the *buf overflowed
 * ----------------------------------------------------------------- */
static inline int read_line(char *buf, size_t n)
{
    if (!fgets(buf, (int)n, stdin)) { buf[0] = '\0'; return 0; }
    if (strchr(buf, '\n') == NULL) { flush_stdin(); }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') { buf[len - 1] = '\0'; }
    return 1;
}


/* ------------------------------------------------------------------
 * is_valid_int
 *
 * Returns 1 if buf contains a valid integer, otherwise 0.
 * Allows an optional leading minus sign
 * ------------------------------------------------------------------ */
static inline int is_valid_int(const char *buf)
{
    const char *p = buf;
    if (*p == '-') { p++; }
    if (*p == '\0') { return 0; }
    for (; *p; p++) {
	    if (*p < '0' || *p > '9') { return 0; }
    }
    return 1;
}


/* ------------------------------------------------------------------
 * cli_read
 *
 * Prompt for a required field. Loops until the user enters something
 * non-empty. Trims leading/trailing whitespace.
 * ------------------------------------------------------------------ */
static inline void cli_read(const char *label, char *buf, size_t n)
{
    do {
        printf("  %s: ", label);
        fflush(stdout);
        if (!read_line(buf, (int)n)) { return; }
        str_trim(buf);
    } while (str_empty(buf));
}


/* ------------------------------------------------------------------
 * cli_read_opt
 *
 * Prompt for an optional field. Empty input is accepted.
 * ------------------------------------------------------------------ */
static inline void cli_read_opt(const char *label, char *buf, size_t n)
{
    printf("  %s (optional): ", label);
    fflush(stdout);
    if (!read_line(buf, (int)n)) { return; }
    str_trim(buf);
}


/* ------------------------------------------------------------------
 * cli_read_update
 *
 * Show the default value in brackets. If the user presses Enter
 * without typing, buf is filled with default (unchanged). Otherwise
 * buf gets the new input.
 *
 * default may be NULL or "" -- both display as empty brackets.
 * ------------------------------------------------------------------ */
static inline void cli_read_update(const char *label,
                                   const char *def,
                                   char *buf,
				   size_t n)
{
    printf("  %s [%s]: ", label, (def && *def) ? def : "");
    fflush(stdout);
    char tmp[PM_BUF];
    if (!read_line(tmp, sizeof(tmp))) {
        snprintf(buf, n, "%s", def ? def : "");
        return;
    }
    str_trim(tmp);
    if (str_empty(tmp)) {
        snprintf(buf, n, "%s", def ? def : "");
    } else {
        snprintf(buf, n, "%s", tmp);
    }
}


/* ------------------------------------------------------------------
 * cli_read_bool
 *
 * Prompt for y/n. Loops until a valid answer is given.
 * Returns 1 for yes, 0 for no.
 * ------------------------------------------------------------------ */
static inline int cli_read_bool(const char *label)
{
    char buf[8];
    for (;;) {
        printf("  %s [y/n]: ", label);
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) { return 0; }
        if (buf[0] == 'y' || buf[0] == 'Y') { return 1; }
        if (buf[0] == 'n' || buf[0] == 'N') { return 0; }
        printf("  Enter y or n.\n");
    }
}


/* ------------------------------------------------------------------
 * cli_read_bool_update
 *
 * Like cli_read_bool but shows the default value. Pressing Enter
 * keeps the default value.
 * ------------------------------------------------------------------ */
static inline int cli_read_bool_update(const char *label, int def)
{
    char buf[8];
    for (;;) {
        printf("  %s [%s]: ", label, def ? "y" : "n");
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) { return def; }
        if (str_empty(buf)) { return def; }
        if (buf[0] == 'y' || buf[0] == 'Y') { return 1; }
        if (buf[0] == 'n' || buf[0] == 'N') { return 0; }
        printf("  Enter y or n (or Enter to keep).\n");
    }
}


/* ------------------------------------------------------------------
 * cli_read_int
 *
 * Prompt for an integer. Loops until the user enters a valid number.
 * ------------------------------------------------------------------ */
static inline int cli_read_int(const char *label)
{
    char buf[PM_BUF];
    for (;;) {
        printf("  %s: ", label);
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) { continue; }
        str_trim(buf);
        if (str_empty(buf)) { continue; }
	int rc = is_valid_int(buf);
        if (rc) { return atoi(buf); }
        printf("  Enter a number.\n");
    }
}


/* ------------------------------------------------------------------
 * cli_confirm
 *
 * Print "msg [y/N]: " and return 1 only if the user types y/Y.
 * Defaults to no -- pressing Enter without typing means no.
 * ------------------------------------------------------------------ */
static inline int cli_confirm(const char *msg)
{
    char buf[8];
    for (;;) {
        printf("  %s [y/N]: ", msg);
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) { return 0; }
	if (str_empty(buf)) { return 0; }
	if (buf[0] == 'y' || buf[0] == 'Y') { return 1; }
	if (buf[0] == 'n' || buf[0] == 'N') { return 0; }
        printf("  Enter y or n.\n");
    }
}


/* ------------------------------------------------------------------
 * cli_pick
 *
 * Display a numbered list of options. The user must pick one.
 * Loops until a valid choice [1..count] is entered.
 * Writes the chosen option string into buf.
 * ------------------------------------------------------------------ */
static inline void cli_pick(const char *label,
                            const char *options[],
			    int count,
                            char *buf,
			    size_t n)
{
    for (;;) {
        printf("  %s:\n", label);
        for (int i = 0; i < count; i++) {
            printf("    %d. %s\n", i + 1, options[i]);
	}
        printf("  Choice [1-%d]: ", count);
        fflush(stdout);
        char tmp[16];
        if (!read_line(tmp, sizeof(tmp))) { continue; }
	str_trim(tmp);
	if (str_empty(tmp)) { continue; }
	int rc = is_valid_int(tmp);
	if (rc) {
            int choice = atoi(tmp);
            if (choice >= 1 && choice <= count) {
                snprintf(buf, n, "%s", options[choice - 1]);
                return;
            }
	}
        printf("  Invalid. Enter 1-%d.\n", count);
    }
}


/* ------------------------------------------------------------------
 * cli_pick_opt
 *
 * Like cli_pick but 0 (or Enter) skips, leaving buf empty.
 * ------------------------------------------------------------------ */
static inline void cli_pick_opt(const char *label,
                                const char *options[],
				int count,
                                char *buf,
				size_t n)
{
    buf[0] = '\0';
    for (;;) {
        printf("  %s (optional):\n", label);
        printf("    0. Skip\n");
        for (int i = 0; i < count; i++) {
            printf("    %d. %s\n", i + 1, options[i]);
	}
        printf("  Choice [0-%d]: ", count);
        fflush(stdout);
        char tmp[16];
        if (!read_line(tmp, sizeof(tmp))) { return; }
        str_trim(tmp);
        if (str_empty(tmp) || tmp[0] == '0') { return; }
	int rc = is_valid_int(tmp);
	if (rc) {
            int choice = atoi(tmp);
            if (choice >= 1 && choice <= count) {
                snprintf(buf, n, "%s", options[choice - 1]);
                return;
            }
	}
        printf("  Invalid. Enter 0-%d.\n", count);
    }
}


/* ------------------------------------------------------------------
 * cli_pick_update
 *
 * Like cli_pick but shows the default value and 0 keeps it.
 * ------------------------------------------------------------------ */
static inline void cli_pick_update(const char *label,
                                   const char *options[],
				   int count,
                                   const char *def,
                                   char *buf,
				   size_t n)
{
    /* Start with def as default */
    snprintf(buf, n, "%s", def ? def : "");

    for (;;) {
        printf("  %s [def: %s]:\n",
	       label,
               (def && *def) ? def : "-"
	      );
        printf("    0. Keep def\n");
        for (int i = 0; i < count; i++) {
            printf("    %d. %s\n", i + 1, options[i]);
	}
        printf("  Choice [0-%d]: ", count);
        fflush(stdout);
        char tmp[16];
        if (!read_line(tmp, sizeof(tmp))) { return; }
        str_trim(tmp);
        if (str_empty(tmp) || tmp[0] == '0') { return; }
	int rc = is_valid_int(tmp);
	if (rc) {
            int choice = atoi(tmp);
            if (choice >= 1 && choice <= count) {
                snprintf(buf, n, "%s", options[choice - 1]);
                return;
            }
	}
        printf("  Invalid. Enter 0-%d.\n", count);
    }
}


/* ------------------------------------------------------------------
 * Display helpers
 * ------------------------------------------------------------------ */

static inline void cli_header(const char *title)
{
    printf("\n%s\n", title);
    printf("----------------------------------------\n");
}

static inline void cli_sep(void)
{
    printf("----------------------------------------\n");
}

/* Print a labelled field, left-padding the label to 24 chars.
 * value may be NULL -- prints "-" in that case. */
static inline void cli_field(const char *label, const char *value)
{
    printf("  %-24s%s\n", label, (value && *value) ? value : "-");
}

static inline void cli_bool_field(const char *label, int value)
{
    printf("  %-24s%s\n", label, value ? "Yes" : "No");
}


#endif /* CLI_H */
