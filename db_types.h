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
 * db_types.h - SQLite Database Types
 *
 * Declares DB typedef and db_die
 *
 *   1. db_die  --  called whenever function returns an error code
 *
 * Database helper utilities live in db.h. This is only for DB
 * and db_die definition.
 */

#ifndef DB_TYPES_H
#define DB_TYPES_H

#include <stdio.h>      /* fprintf, stderr, rename  */
#include <stdlib.h>     /* exit                     */
#include "sqlite3.h"


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
 * Also used by db_migrate.c.
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


#endif /* DB_TYPES_H */
