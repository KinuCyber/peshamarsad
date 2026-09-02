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
 * db_migrate.h -- declaration header for schema migration functions
 *
 * NOT meant to be included directly by other files.
 * Included by db.h only
 *
 * Contents:
 *   PM_SCHEMA_VERSION    integer version this binary expects
 *   PM_VERSION_BUF       buffer size for version PRAGMA string
 *   db_make_backup       hot-copy via SQLite online backup API
 *   db_migrate_fail      rollback + restore backup + exit on failure
 *   db_fk_check          PRAGMA foreign_key_check inside transaction
 *   db_migrate           bring an existing database up to current version
 */

#ifndef DB_MIGRATE_H
#define DB_MIGRATE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sqlite3.h"
#include "db_types.h"
#include "str.h"


/* ------------------------------------------------------------------
 * Schema version constants
 *
 * PM_SCHEMA_VERSION  the version this binary expects. Must match
 *                    PRAGMA user_version at the end of schema.sql.
 *                    Bump this whenever a new migration block is added.
 * PM_VERSION_BUF     buffer size for "PRAGMA user_version = N;"
 * ------------------------------------------------------------------ */
#define PM_SCHEMA_VERSION 2
#define PM_VERSION_BUF    64


/* ------------------------------------------------------------------
 * db_make_backup
 *
 * Create a consistent copy of db at bak_path using SQLite's online
 * backup API. Safe to call while the connection is open -- the API
 * is SQLite's own recommended hot-copy mechanism and handles WAL
 * internally.
 *
 * Returns 1 on success, 0 on failure.
 * ------------------------------------------------------------------ */
int db_make_backup(DB *db, const char *bak_path);

/* ------------------------------------------------------------------
 * db_migrate_fail
 *
 * Called when any migration step fails. Rolls back the open
 * transaction, closes the database, renames the backup over the
 * original, removes any stale WAL/SHM files left by the failed run,
 * then exits. Never returns.
 *
 * The WAL and SHM files belong to the migrated (rolled-back) state
 * and would confuse SQLite when the restored database is opened next.
 * unlink failures are silently ignored -- the files may simply not
 * exist if the migration failed before WAL writes occurred.
 * ------------------------------------------------------------------ */
void db_migrate_fail(DB        *db,
                                   const char *path,
                                   const char *bak_path,
                                   const char *reason);

/* ------------------------------------------------------------------
 * db_fk_check
 *
 * Run PRAGMA foreign_key_check and return 1 if there are no
 * violations, 0 if any violation is found.
 *
 * The pragma returns one row per violation. Zero rows (SQLITE_DONE
 * on the first step) means the database is fully consistent.
 * Called inside the migration transaction after all table rebuilds,
 * before COMMIT.
 * ------------------------------------------------------------------ */
int db_fk_check(DB *db);

/* ------------------------------------------------------------------
 * db_migrate
 *
 * Bring an existing database up to PM_SCHEMA_VERSION. Called by
 * db_open when the database already exists and its user_version is
 * below the current version.
 *
 * Strategy -- table rebuild, no exceptions:
 *   CREATE <table>_new --> INSERT ... SELECT --> DROP <table>
 *   --> ALTER TABLE <table>_new RENAME TO <table>
 *   This is the procedure SQLite's own ALTER TABLE documentation
 *   recommends for structural changes and works uniformly across
 *   all SQLite versions, including older Termux builds.
 *
 *   https://sqlite.org/c3ref/backup_finish.html
 *
 * Safety:
 *   1. db_make_backup creates a consistent .bak before any changes.
 *   2. All DDL runs inside a single transaction.
 *   3. PRAGMA foreign_keys must be OFF before BEGIN (SQLite
 *      requirement when dropping tables that others reference).
 *   4. db_fk_check verifies consistency inside the transaction
 *      before COMMIT.
 *   5. On any failure: ROLLBACK, close, rename .bak → .db,
 *      remove stale WAL/SHM, exit.
 *   6. On success: COMMIT, unlink .bak.
 *
 * Note on my_fit: free text cannot be decomposed into integer fit
 * dimensions. The column is dropped without migration. Use fit_notes
 * for qualitative notes going forward.
 * ------------------------------------------------------------------ */
void db_migrate(DB *db, const char *path);

#endif /* DB_MIGRATE_H */
