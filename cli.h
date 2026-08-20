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
 *   cli_read                  required field; loops until non-empty
 *   cli_read_opt              optional field; empty is accepted
 *   cli_read_update           show def value; Enter keeps it
 *   cli_read_bool             y/n prompt; returns 1 or 0
 *   cli_read_bool_update      y/n showing default; Enter keeps default
 *   cli_read_int              integer prompt; loops until valid
 *   cli_confirm               "? [y/N]" confirmation; defaults to no
 *   cli_pick                  numbered menu, required choice
 *   cli_pick_opt              numbered menu, 0 to skip
 *   cli_pick_update           numbered menu showing default, 0 to keep
 *   cli_read_date             date field with default fallback
 *   cli_read_ordered_list     validated comma-separated list; blank to skip
 *   cli_update_ordered_list   same, showing current; Enter keeps current
 *   cli_read_fit              fit score 1-5; 0 = skip (stored as NULL)
 *   cli_read_fit_update       same, showing current; 0 clears; Enter keeps
 *   cli_read_salary           optional salary amount; 0 = skip (NULL)
 *   cli_read_salary_update    same, showing current; 0 clears; Enter keeps
 *
 * Display helpers:
 *   cli_safe             "-" for NULL/empty, otherwise the string
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
 * cli_safe
 *
 * Return "-" for NULL or empty strings, otherwise return s unchanged.
 * Used in display code to avoid printing blank or missing values.
 * Defined here rather than in the display section so that input
 * helpers defined below can also use it.
 * ------------------------------------------------------------------ */
static inline const char *cli_safe(const char *s)
{
    return str_empty(s) ? "-" : s;
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
 * cli_read_date
 *
 * Prompt for a date field, showing def as the bracketed default.
 * If the user presses Enter without typing, buf is filled with def.
 * Otherwise buf gets trimmed input.
 *
 * def must not be NULL. Callers supply today_str output or any
 * existing date value. Format validation is the caller's concern
 * if ever needed; this function is intentionally format-agnostic.
 * ------------------------------------------------------------------ */
static inline void cli_read_date(const char *label,
                                 const char *def,
                                 char *buf,
                                 size_t n)
{
    printf("  %s [%s]: ", label, def ? def : "");
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
 * cli_read_ordered_list
 *
 * Prompt for a comma-separated ordered list (e.g. personality,
 * attitude). Prints available values, validates each entry against
 * valid[], loops until all entries are valid or input is blank.
 * Writes comma-joined result into buf.
 * Returns entry count (0 if blank -- buf is set to "" in that case)
 *
 * First entry = primary, subsequent = secondary, tertiary, etc.
 * ------------------------------------------------------------------ */
static inline int cli_read_ordered_list(const char   *label,
                                        const char   *valid[],
                                        int           valid_n,
                                        char         *buf,
                                        size_t        n)
{
    char raw[PM_BUF];
    char entries[PM_LIST_MAX][PM_ENTRY];

    printf("  %s\n    Valid: ", label);
    for (int i = 0; i < valid_n; i++)
        printf("%s%s", valid[i], i < valid_n - 1 ? ", " : "\n");
    printf("    Ordered, comma-separated (first = primary). "
           "Leave blank to skip.\n");

    for (;;) {
        cli_read_opt("Values", raw, PM_BUF);
        if (str_empty(raw)) { buf[0] = '\0'; return 0; }

        int count = str_split(raw, ',', entries, PM_LIST_MAX);
        int ok = 1;
        for (int i = 0; i < count; i++) {
            if (!str_in_list(entries[i], valid, valid_n)) {
                printf("    Invalid: '%s'. Try again.\n", entries[i]);
                ok = 0;
                break;
            }
        }
        if (ok) { str_join(buf, n, entries, count, ","); return count; }
    }
}


/* ------------------------------------------------------------------
 * cli_update_ordered_list
 *
 * Like cli_read_ordered_list but shows the current value.
 * Pressing Enter without typing leaves keeps the current value.
 * ------------------------------------------------------------------ */
static inline int cli_update_ordered_list(const char   *label,
                                          const char   *current,
                                          const char   *valid[],
                                          int           valid_n,
                                          char         *buf,
                                          size_t        n)
{
    char raw[PM_BUF];
    char entries[PM_LIST_MAX][PM_ENTRY];

    printf("  %s [current: %s]\n    Valid: ", label, cli_safe(current));
    for (int i = 0; i < valid_n; i++)
        printf("%s%s", valid[i], i < valid_n - 1 ? ", " : "\n");
    printf("    Ordered, comma-separated. Enter to keep current.\n");

    for (;;) {
        printf("  Values: ");
        fflush(stdout);
        if (!read_line(raw, PM_BUF)) {
            snprintf(buf, n, "%s", current ? current : "");
            return 0;
        }
        str_trim(raw);

        if (str_empty(raw)) {
            snprintf(buf, n, "%s", current ? current : "");
            return 0;
        }

        int count = str_split(raw, ',', entries, PM_LIST_MAX);
        int ok = 1;
        for (int i = 0; i < count; i++) {
            if (!str_in_list(entries[i], valid, valid_n)) {
                printf("    Invalid: '%s'. Try again.\n", entries[i]);
                ok = 0;
                break;
            }
        }
        if (ok) { str_join(buf, n, entries, count, ","); return count; }
    }
}


/* ------------------------------------------------------------------
 * cli_read_fit
 *
 * Prompt for a personal fit score (1 = very poor … 5 = excellent).
 * Returns 0 when the user skips, which maps to SQL NULL via DB_NULL.
 * ------------------------------------------------------------------ */
static inline int cli_read_fit(const char *label)
{
    char buf[16];
    for (;;) {
        printf("  %s [1-5, 0=skip]: ", label);
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) return 0;
        str_trim(buf);
        if (str_empty(buf) || (buf[0] == '0' && buf[1] == '\0')) return 0;
        if (is_valid_int(buf)) {
            int v = atoi(buf);
            if (v >= 1 && v <= 5) return v;
        }
        printf("  Enter 1-5, or 0 to skip.\n");
    }
}


/* ------------------------------------------------------------------
 * cli_read_fit_update
 *
 * Like cli_read_fit but shows the current score and Enter keeps it.
 * 0 explicitly clears the score to NULL. Returns current on Enter.
 * ------------------------------------------------------------------ */
static inline int cli_read_fit_update(const char *label, int current)
{
    char cur_str[12];
    if (current > 0) snprintf(cur_str, sizeof(cur_str), "%d", current);
    else             snprintf(cur_str, sizeof(cur_str), "-");

    char buf[16];
    for (;;) {
        printf("  %s [1-5, 0=clear, Enter=keep (%s)]: ", label, cur_str);
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) return current;
        str_trim(buf);
        if (str_empty(buf)) return current;
        if (buf[0] == '0' && buf[1] == '\0') return 0;
        if (is_valid_int(buf)) {
            int v = atoi(buf);
            if (v >= 1 && v <= 5) return v;
        }
        printf("  Enter 1-5, 0 to clear, or Enter to keep.\n");
    }
}


/* ------------------------------------------------------------------
 * cli_read_salary
 *
 * Prompt for an optional salary amount (positive integer).
 * Returns -1 when the user skips (0 input = skip), which maps to
 * SQL NULL via DB_NULL. Returns the amount otherwise.
 * ------------------------------------------------------------------ */
static inline int cli_read_salary(const char *label)
{
    char buf[PM_BUF];
    for (;;) {
        printf("  %s (0 to skip): ", label);
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) return -1;
        str_trim(buf);
        if (str_empty(buf) || (buf[0] == '0' && buf[1] == '\0')) return -1;
        if (is_valid_int(buf)) {
            int v = atoi(buf);
            if (v > 0) return v;
        }
        printf("  Enter a positive number, or 0 to skip.\n");
    }
}


/* ------------------------------------------------------------------
 * cli_read_salary_update
 *
 * Like cli_read_salary but shows current and Enter keeps it.
 * current is -1 when the field is currently NULL in the database.
 * 0 input clears to NULL. Returns current on Enter.
 * ------------------------------------------------------------------ */
static inline int cli_read_salary_update(const char *label, int current)
{
    char cur_str[32];
    if (current >= 0) snprintf(cur_str, sizeof(cur_str), "%d", current);
    else              snprintf(cur_str, sizeof(cur_str), "-");

    char buf[PM_BUF];
    for (;;) {
        printf("  %s [Enter=keep (%s), 0=clear]: ", label, cur_str);
        fflush(stdout);
        if (!read_line(buf, sizeof(buf))) return current;
        str_trim(buf);
        if (str_empty(buf)) return current;
        if (buf[0] == '0' && buf[1] == '\0') return -1;
        if (is_valid_int(buf)) {
            int v = atoi(buf);
            if (v > 0) return v;
        }
        printf("  Enter a positive number, 0 to clear, or Enter to keep.\n");
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
