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
 * db.h - SQLite wrapper
 *
 * Wraps the SQLite C API into three simpler patterns:
 *
 *   1. db_open / db_close    -- open and close the database
 *   2. db_exec               -- run a statement with no results (INSERT,
 *                               UPDATE, DELETE, CREATE)
 *   3. db_query / db_row /   -- run a SELECT and walk results row by row
 *      db_done
 *
 * The SQLite C API requires careful resource management -- every prepared
 * statement must be finalized, or SQLite holds locks on the database.
 * This wrapper enforces that by making finalization part of the pattern.
 *
 * Header-only for the same reason as str.h: single compile unit,
 * no separate .c file needed.
 *
 * Migration internals live in db_migrate.h, which is included below
 * after DB and db_die are defined (db_migrate.h depends on both).
 * See db_migrate.h for .
 */

#ifndef DB_H
#define DB_H

#include <stdio.h>      /* fprintf, stderr, rename  */
#include <stdlib.h>     /* exit                     */
#include <string.h>     /* strcmp                   */
#include <stdarg.h>     /* va_list, va_arg, ...     */
#include "sqlite3.h"
#include "str.h"
#include "schema.h"     /* embedded schema SQL as schema_sql[] byte array */


/* ------------------------------------------------------------------
 * DB
 *
 * A thin struct wrapping sqlite3 *. Having our own type means we can
 * add fields later (e.g. last insert rowid cache) without changing
 * every call site.
 *
 * A struct in C groups related data under one name. Members are
 * accessed with the dot operator: db.handle
 * When accessed through a pointer: db->handle  (equivalent to (*db).handle)
 * ------------------------------------------------------------------ */
typedef struct {
    sqlite3 *handle;   /* the raw SQLite connection -- never touch directly */
} DB;


/* ------------------------------------------------------------------
 * db_die
 *
 * Internal helper. Print the SQLite error message and exit.
 * Called whenever a SQLite function returns an error code.
 * Also used by db_migrate.h (included below).
 * ------------------------------------------------------------------ */
static inline void db_die(DB *db, const char *context)
{
    fprintf(stderr,
            "peshamarsad: %s: %s\n",
            context,
            sqlite3_errmsg(db->handle)
           );
    exit(1);
}


/* ------------------------------------------------------------------
 * db_migrate.h
 *
 * Included here -- after DB and db_die are defined -- because
 * db_migrate_fail and db_migrate call db_die and accept DB*.
 * Do not move this include above the DB typedef or db_die definition.
 * ------------------------------------------------------------------ */
#include "db_migrate.h"


/* ------------------------------------------------------------------
 * db_open
 *
 * Open (or create) the SQLite database at `path`, then:
 *   - Fresh database:    run the embedded schema SQL (already v2).
 *   - Existing database: call db_migrate, which checks user_version
 *                        and runs any needed migrations.
 * Enables foreign key enforcement.
 * Initializes the schema on first run.
 * sqlite3_open returns SQLITE_OK (value 0) on success.
 * Any other return value is an error.
 * ------------------------------------------------------------------ */
static inline void db_open(DB *db, const char *path)
{
    /* open database from path and assign to pointer of db->handle */
    int rc = sqlite3_open(path, &db->handle);
    if (rc != SQLITE_OK)
        db_die(db, "could not open database");

    /* Foreign key enforcement is OFF by default in SQLite. */
    rc = sqlite3_exec(db->handle,
                      "PRAGMA foreign_keys = ON;",
                      NULL,
		      NULL,
		      NULL);
    if (rc != SQLITE_OK)
        db_die(db, "could not enable foreign keys");

    /* WAL mode: better concurrent read performance; safe for a
     * single-writer local tool. */
    rc = sqlite3_exec(db->handle,
                      "PRAGMA journal_mode=WAL;",
                      NULL,
		      NULL,
		      NULL);
    if (rc != SQLITE_OK)
        db_die(db, "could not enter WAL Mode");

    /* check if schema is already initialized by testing for the
     * organizations table. If not found, run the embedded schema. */

    /* stmt declaration */
    sqlite3_stmt *stmt;

    /* stmt preparation */
    rc = sqlite3_prepare_v2(db->handle,
                            "SELECT name FROM sqlite_master "
                            "WHERE type='table' AND name='organizations';",
                            -1,
			    &stmt,
			    NULL);
    if (rc != SQLITE_OK)
        db_die(db, "schema check failed");

    /* stmt execution (called "step") */
    int has_schema = (sqlite3_step(stmt) == SQLITE_ROW);

    /* stmt finalization - always finalized, even mid-query */
    sqlite3_finalize(stmt);

    if (!has_schema) {
        /* schema_sql is the byte array generated by xxd from schema.sql.
         * It is a valid null-terminated C string of SQL statements.
         * sqlite3_exec runs multiple semicolon-separated statements at once. */
        char *errmsg = NULL;
        if (sqlite3_exec(db->handle,
                         (const char *)schema_sql,
                         NULL,
			 NULL,
			 &errmsg
			) != SQLITE_OK) {
            fprintf(stderr, "peshamarsad: schema init failed: %s\n", errmsg);
            sqlite3_free(errmsg);
            exit(1);
        }
        return;   /* fresh database is already current */
    }

    /* Existing database: migrate if user_version < PM_SCHEMA_VERSION.
     * db_migrate is a no-op when the version is already current. */
    db_migrate(db, path);
}


/* ------------------------------------------------------------------
 * db_close
 *
 * Close the database connection. Call this before the program exits.
 * Passing a zeroed DB is safe -- sqlite3_close(NULL) is a no-op.
 * ------------------------------------------------------------------ */
static inline void db_close(DB *db)
{
    int rc = sqlite3_close(db->handle);
    if (rc != SQLITE_OK)
	fprintf(stderr,
		"peshamarsad: warning: db_close failed: %s\n",
		sqlite3_errmsg(db->handle)
	       );
    db->handle = NULL;   /* prevent accidental use after close */
}


/* ------------------------------------------------------------------
 * db_exec
 *
 * Run a single SQL statement that produces no rows:
 * INSERT, UPDATE, DELETE, or DDL (CREATE, DROP).
 *
 * `sql`  -- the SQL string, with ? placeholders for values
 * `...`  -- values to bind, passed as (type, value) pairs terminated
 *           by a sentinel (see DB_INT, DB_TEXT, DB_END below)
 *
 * Binding replaces ? placeholders in order. This prevents SQL injection
 * by keeping data separate from the SQL structure -- SQLite handles
 * escaping internally.
 *
 * Example:
 *   db_exec(&db, "INSERT INTO organizations (name, registered) VALUES (?, ?)",
 *           DB_TEXT, "Roots School",
 *           DB_INT,  1,
 *           DB_END);
 * ------------------------------------------------------------------ */

/* Bind type sentinels -- passed before each value in db_exec / db_query */
#define DB_TEXT  1   /* value is const char *  */
#define DB_INT   2   /* value is int           */
#define DB_NULL  3   /* bind SQL NULL          */
#define DB_END   0   /* marks end of arguments */

static inline void db_exec(DB *db, const char *sql, ...)
{
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
        db_die(db, "prepare failed");

    /* bind arguments -- walk (type, value) pairs until DB_END */
    va_list args;
    va_start(args, sql);

    int col = 1;   /* SQLite bind indices start at 1, not 0 */
    int type;
    while ((type = va_arg(args, int)) != DB_END) {
        if (type == DB_TEXT) {
            const char *val = va_arg(args, const char *);
	     /* SQLITE_TRANSIENT tells SQLite to copy the string internally.
	      * This is safe even if our buffer is freed before SQLite uses it. */
            sqlite3_bind_text(stmt, col++, val, -1, SQLITE_TRANSIENT);
        } else if (type == DB_INT) {
            int val = va_arg(args, int);
            sqlite3_bind_int(stmt, col++, val);
        } else if (type == DB_NULL) {
            va_arg(args, int);          /* consume dummy value, discard it */
            sqlite3_bind_null(stmt, col++);
        }
    }
    va_end(args);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);   /* always finalize */

    /* SQLITE_DONE means the statement ran and produced no rows -- correct
     * for INSERT/UPDATE/DELETE. SQLITE_ROW would mean unexpected results. */
    if (rc != SQLITE_DONE)
        db_die(db, sql);
}


/* ------------------------------------------------------------------
 * QUERY PATTERN
 *
 * For SELECT statements that return rows, we use three functions:
 *
 *   db_query  -- prepare and bind, return stmt handle
 *   db_row    -- advance one row, return 1 if row available, 0 if done
 *   db_done   -- finalize the statement (MUST always be called)
 *
 * Usage pattern:
 *
 *   sqlite3_stmt *stmt = db_query(&db,
 *       "SELECT id, name FROM organizations WHERE id=?",
 *       DB_INT, 5, DB_END);
 *
 *   while (db_row(stmt)) {
 *       int  id   = db_col_int (stmt, 0);
 *       const char *name = db_col_text(stmt, 1);
 *       printf("%d %s\n", id, name);
 *   }
 *   db_done(stmt);   <- mandatory, never skip this
 *
 * Column indices in db_col_* start at 0, unlike bind indices which
 * start at 1. This inconsistency is SQLite's own API design.
 * ------------------------------------------------------------------ */

static inline sqlite3_stmt *db_query(DB *db, const char *sql, ...)
{
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK)
        db_die(db, "prepare failed");

    va_list args;
    va_start(args, sql);

    int col = 1;
    int type;
    while ((type = va_arg(args, int)) != DB_END) {
        if (type == DB_TEXT) {
            const char *val = va_arg(args, const char *);
            sqlite3_bind_text(stmt, col++, val, -1, SQLITE_TRANSIENT);
        } else if (type == DB_INT) {
            int val = va_arg(args, int);
            sqlite3_bind_int(stmt, col++, val);
        } else if (type == DB_NULL) {
            va_arg(args, int);
            sqlite3_bind_null(stmt, col++);
        }
    }
    va_end(args);

    return stmt;   /* caller receives the handle and drives it with db_row */
}


/* ------------------------------------------------------------------
 * db_row
 *
 * Advance stmt to the next row.
 * Returns 1 if a row is available, 0 if no more rows (SQLITE_DONE).
 * ------------------------------------------------------------------ */
static inline int db_row(sqlite3_stmt *stmt)
{
    return sqlite3_step(stmt) == SQLITE_ROW;
}


/* ------------------------------------------------------------------
 * db_done
 *
 * Finalize a statement. MUST be called after every db_query, whether
 * the loop ran zero times, one time, or a hundred times.
 *
 * Forgetting this is the most common SQLite resource leak in C.
 * ------------------------------------------------------------------ */
static inline void db_done(sqlite3_stmt *stmt)
{
    sqlite3_finalize(stmt);
}


/* ------------------------------------------------------------------
 * Column accessors
 *
 * Read a column value from the current row. Call only inside a
 * db_row loop. Column indices start at 0.
 *
 * db_col_text returns a pointer into SQLite's internal buffer.
 * It is valid only until the next db_row or db_done call.
 * Copy it into your own buffer with str_copy if you need it longer.
 * ------------------------------------------------------------------ */
static inline int db_col_int(sqlite3_stmt *stmt, int col)
{
    return sqlite3_column_int(stmt, col);
}

static inline const char *db_col_text(sqlite3_stmt *stmt, int col)
{
    /* sqlite3_column_text returns unsigned char * -- cast to const char *
     * for compatibility with our str_* functions which expect const char * */
    return (const char *)sqlite3_column_text(stmt, col);
}


/* ------------------------------------------------------------------
 * db_last_id
 *
 * Return the rowid of the most recently inserted row.
 * Call immediately after db_exec for an INSERT statement.
 * ------------------------------------------------------------------ */
static inline int db_last_id(DB *db)
{
    return (int)sqlite3_last_insert_rowid(db->handle);
}


#endif /* DB_H */
