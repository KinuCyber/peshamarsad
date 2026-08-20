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
 * db_migrate.h - schema migration internals
 *
 * NOT meant to be included directly by other files.
 * Included by db.h at a specific point -- after DB typedef and db_die
 * are defined -- because db_migrate and db_migrate_fail call db_die
 * and accept DB*.
 *
 * "db_migrate.c compilation unit" is the principled long-term home for
 * this code, but deferred due to time constraints
 * When implementing this, the DB* dependency will need resolving via a
 * db_types.h or similar; db_die will need to move or be redeclared.
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
static inline int db_make_backup(DB *db, const char *bak_path)
{
    sqlite3 *bak_handle;
    if (sqlite3_open(bak_path, &bak_handle) != SQLITE_OK) {
        sqlite3_close(bak_handle);
        return 0;
    }

    sqlite3_backup *b = sqlite3_backup_init(bak_handle, "main",
                                            db->handle,  "main");
    if (!b) {
        sqlite3_close(bak_handle);
        return 0;
    }

    sqlite3_backup_step(b, -1);           /* -1 = copy all pages at once */
    int rc = sqlite3_backup_finish(b);
    sqlite3_close(bak_handle);
    return rc == SQLITE_OK;
}


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
static inline void db_migrate_fail(DB        *db,
                                   const char *path,
                                   const char *bak_path,
                                   const char *reason)
{
    fprintf(stderr, "peshamarsad: migration failed: %s\n", reason);

    sqlite3_exec(db->handle, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(db->handle);
    db->handle = NULL;

    if (rename(bak_path, path) != 0) {
        fprintf(stderr,
                "peshamarsad: WARNING: could not restore backup automatically.\n"
                "peshamarsad: your original database is at: %s\n"
                "peshamarsad: copy it manually to: %s\n",
                bak_path, path);
    } else {
        char wal[PM_BUF], shm[PM_BUF];
        snprintf(wal, sizeof(wal), "%s-wal", path);
        snprintf(shm, sizeof(shm), "%s-shm", path);
        unlink(wal);
        unlink(shm);
        fprintf(stderr,
                "peshamarsad: original database restored from backup.\n");
    }

    exit(1);
}


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
static inline int db_fk_check(DB *db)
{
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->handle,
                           "PRAGMA foreign_key_check;",
                           -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    int clean = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return clean;
}


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
 * Version history:
 *   0 (v1)  original schema; user_version not set (defaults to 0)
 *   2 (v2)  org_links, contact_methods added;
 *           organizations: has_social removed, industry + org_type added;
 *           contacts: relation, how_met, last_contact_date added;
 *           positions: my_fit removed; employment_type, remote_type,
 *             job_location, closing_date, salary_min/max/currency/period,
 *             source, source_url, skill_fit, compensation_fit,
 *             location_fit, level_fit, fit_notes added;
 *           applications: discovery_source, application_channel added;
 *           idx_adresses_org typo corrected to idx_addresses_org.
 *
 * Note on my_fit: free text cannot be decomposed into integer fit
 * dimensions. The column is dropped without migration. Use fit_notes
 * for qualitative notes going forward.
 * ------------------------------------------------------------------ */
static inline void db_migrate(DB *db, const char *path)
{
    /* --- read current schema version ----------------------------- */
    int from = 0;
    {
        sqlite3_stmt *vstmt;
        if (sqlite3_prepare_v2(db->handle, "PRAGMA user_version;",
                               -1, &vstmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(vstmt) == SQLITE_ROW)
                from = sqlite3_column_int(vstmt, 0);
            sqlite3_finalize(vstmt);
        }
    }

    if (from >= PM_SCHEMA_VERSION)
        return;   /* already current, nothing to do */

    /* --- backup before touching anything ------------------------- */
    char bak_path[PM_BUF];
    snprintf(bak_path, sizeof(bak_path), "%s.bak", path);

    if (!db_make_backup(db, bak_path)) {
        fprintf(stderr,
                "peshamarsad: migration aborted: "
                "could not create backup at %s\n", bak_path);
        exit(1);
    }

    fprintf(stderr,
            "peshamarsad: migrating schema v%d -> v%d  (backup: %s)\n",
            from, PM_SCHEMA_VERSION, bak_path);

    /* --- EXEC macro ---------------------------------------------- *
     * Run one SQL statement. On failure, copies the error string,    *
     * frees the SQLite allocation, then calls db_migrate_fail which  *
     * rolls back, restores the backup, and exits.                    *
     *                                                                *
     * m_ prefix on locals avoids shadowing any outer variable.       *
     * ------------------------------------------------------------- */
#define EXEC(msql)                                                      \
    do {                                                                \
        char *merr = NULL;                                              \
        if (sqlite3_exec(db->handle, (msql),                           \
                         NULL, NULL, &merr) != SQLITE_OK) {            \
            char mbuf[PM_BUF];                                          \
            snprintf(mbuf, sizeof(mbuf), "%s",                         \
                     merr ? merr : "(no error message)");              \
            sqlite3_free(merr);                                         \
            db_migrate_fail(db, path, bak_path, mbuf);                 \
        }                                                               \
    } while (0)

    /* PRAGMA foreign_keys must be OFF before BEGIN.
     * SQLite silently ignores it inside a transaction. */
    EXEC("PRAGMA foreign_keys = OFF;");
    EXEC("BEGIN;");

    /* ============================================================ *
     * v1 -> v2                                                      *
     * ============================================================ */
    if (from < 2) {

        /* -------------------------------------------------------- *
         * organizations                                             *
         * Remove: has_social                                        *
         * Add:    industry, org_type                               *
         * -------------------------------------------------------- */
        EXEC(
            "CREATE TABLE organizations_new ("
            "    id              INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    name            TEXT    NOT NULL,"
            "    registered      BOOLEAN,"
            "    website         TEXT,"
            "    industry        TEXT,"
            "    org_type        TEXT,"
            "    security_level  TEXT,"
            "    size            TEXT,"
            "    notes           TEXT,"
            "    created_at      TEXT NOT NULL DEFAULT (date('now'))"
            ");"
        );
        EXEC(
            "INSERT INTO organizations_new"
            "    (id, name, registered, website,"
            "     security_level, size, notes, created_at)"
            " SELECT"
            "    id, name, registered, website,"
            "    security_level, size, notes, created_at"
            " FROM organizations;"
        );
        EXEC("DROP TABLE organizations;");
        EXEC("ALTER TABLE organizations_new RENAME TO organizations;");

        /* -------------------------------------------------------- *
         * org_links  (new table)                                    *
         * -------------------------------------------------------- */
        EXEC(
            "CREATE TABLE org_links ("
            "    id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    org_id     INTEGER NOT NULL"
            "                   REFERENCES organizations(id) ON DELETE CASCADE,"
            "    type       TEXT NOT NULL,"
            "    label      TEXT,"
            "    url        TEXT NOT NULL,"
            "    created_at TEXT NOT NULL DEFAULT (date('now'))"
            ");"
        );

        /* -------------------------------------------------------- *
         * contacts                                                  *
         * Add: relation, how_met, last_contact_date                 *
         * -------------------------------------------------------- */
        EXEC(
            "CREATE TABLE contacts_new ("
            "    id                  INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    name                TEXT    NOT NULL,"
            "    age_range           TEXT,"
            "    org_id              INTEGER NOT NULL"
            "                            REFERENCES organizations(id) ON DELETE CASCADE,"
            "    position_title      TEXT,"
            "    relation            TEXT,"
            "    personality         TEXT,"
            "    attitude            TEXT,"
            "    leverage_potential  TEXT,"
            "    how_met             TEXT,"
            "    last_contact_date   TEXT,"
            "    notes               TEXT,"
            "    created_at          TEXT NOT NULL DEFAULT (date('now'))"
            ");"
        );
        EXEC(
            "INSERT INTO contacts_new"
            "    (id, name, age_range, org_id, position_title,"
            "     personality, attitude, leverage_potential, notes, created_at)"
            " SELECT"
            "    id, name, age_range, org_id, position_title,"
            "    personality, attitude, leverage_potential, notes, created_at"
            " FROM contacts;"
        );
        EXEC("DROP TABLE contacts;");
        EXEC("ALTER TABLE contacts_new RENAME TO contacts;");

        /* -------------------------------------------------------- *
         * contact_methods  (new table)                              *
         * -------------------------------------------------------- */
        EXEC(
            "CREATE TABLE contact_methods ("
            "    id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    contact_id INTEGER NOT NULL"
            "                   REFERENCES contacts(id) ON DELETE CASCADE,"
            "    medium     TEXT NOT NULL,"
            "    address    TEXT NOT NULL,"
            "    created_at TEXT NOT NULL DEFAULT (date('now'))"
            ");"
        );

        /* -------------------------------------------------------- *
         * positions                                                 *
         * Remove: my_fit  (not migrated -- see function comment)    *
         * Add:    employment_type, remote_type, job_location,       *
         *         closing_date, salary_min, salary_max,             *
         *         salary_currency, salary_period, source,           *
         *         source_url, skill_fit, compensation_fit,          *
         *         location_fit, level_fit, fit_notes                *
         * -------------------------------------------------------- */
        EXEC(
            "CREATE TABLE positions_new ("
            "    id               INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    org_id           INTEGER NOT NULL"
            "                         REFERENCES organizations(id) ON DELETE CASCADE,"
            "    department       TEXT,"
            "    role             TEXT,"
            "    level            TEXT,"
            "    employment_type  TEXT,"
            "    remote_type      TEXT,"
            "    job_location     TEXT,"
            "    closing_date     TEXT,"
            "    salary_min       INTEGER,"
            "    salary_max       INTEGER,"
            "    salary_currency  TEXT,"
            "    salary_period    TEXT,"
            "    source           TEXT,"
            "    source_url       TEXT,"
            "    skill_fit        INTEGER"
            "                         CHECK (skill_fit IN (1,2,3,4,5)"
            "                                OR skill_fit IS NULL),"
            "    compensation_fit INTEGER"
            "                         CHECK (compensation_fit IN (1,2,3,4,5)"
            "                                OR compensation_fit IS NULL),"
            "    location_fit     INTEGER"
            "                         CHECK (location_fit IN (1,2,3,4,5)"
            "                                OR location_fit IS NULL),"
            "    level_fit        INTEGER"
            "                         CHECK (level_fit IN (1,2,3,4,5)"
            "                                OR level_fit IS NULL),"
            "    fit_notes        TEXT,"
            "    notes            TEXT,"
            "    created_at       TEXT NOT NULL DEFAULT (date('now'))"
            ");"
        );
        EXEC(
            "INSERT INTO positions_new"
            "    (id, org_id, department, role, level, notes, created_at)"
            " SELECT"
            "    id, org_id, department, role, level, notes, created_at"
            " FROM positions;"
        );
        EXEC("DROP TABLE positions;");
        EXEC("ALTER TABLE positions_new RENAME TO positions;");

        /* -------------------------------------------------------- *
         * applications                                              *
         * Add: discovery_source, application_channel               *
         * -------------------------------------------------------- */
        EXEC(
            "CREATE TABLE applications_new ("
            "    id                      INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    date                    TEXT    NOT NULL,"
            "    position_id             INTEGER NOT NULL REFERENCES positions(id),"
            "    org_id                  INTEGER NOT NULL REFERENCES organizations(id),"
            "    current_status          TEXT    NOT NULL DEFAULT 'Applied',"
            "    discovery_source        TEXT,"
            "    application_channel     TEXT,"
            "    resume_given            BOOLEAN DEFAULT 0,"
            "    portfolio_given         BOOLEAN DEFAULT 0,"
            "    cover_letter_given      BOOLEAN DEFAULT 0,"
            "    cnic_given              BOOLEAN DEFAULT 0,"
            "    domicile_given          BOOLEAN DEFAULT 0,"
            "    birth_cert_given        BOOLEAN DEFAULT 0,"
            "    matric_cert_given       BOOLEAN DEFAULT 0,"
            "    experience_letter_given BOOLEAN DEFAULT 0,"
            "    misc_documents          TEXT,"
            "    requested_documents     TEXT,"
            "    follow_up_date          TEXT,"
            "    notes                   TEXT,"
            "    created_at              TEXT NOT NULL DEFAULT (date('now'))"
            ");"
        );
        EXEC(
            "INSERT INTO applications_new"
            "    (id, date, position_id, org_id, current_status,"
            "     resume_given, portfolio_given, cover_letter_given,"
            "     cnic_given, domicile_given, birth_cert_given,"
            "     matric_cert_given, experience_letter_given,"
            "     misc_documents, requested_documents,"
            "     follow_up_date, notes, created_at)"
            " SELECT"
            "    id, date, position_id, org_id, current_status,"
            "    resume_given, portfolio_given, cover_letter_given,"
            "    cnic_given, domicile_given, birth_cert_given,"
            "    matric_cert_given, experience_letter_given,"
            "    misc_documents, requested_documents,"
            "    follow_up_date, notes, created_at"
            " FROM applications;"
        );
        EXEC("DROP TABLE applications;");
        EXEC("ALTER TABLE applications_new RENAME TO applications;");

        /* -------------------------------------------------------- *
         * Indexes                                                   *
         *                                                           *
         * Dropping a table silently drops all its indexes. Recreate *
         * the ones that were lost (contacts, positions, applications *
         * were all rebuilt above).                                   *
         *                                                           *
         * Indexes on untouched tables (reviews, addresses,          *
         * status_history, application_contacts) survived intact,    *
         * except idx_adresses_org which has a typo from v1 and is   *
         * corrected here without needing to rebuild addresses.       *
         * -------------------------------------------------------- */

        /* new tables */
        EXEC("CREATE INDEX idx_org_links_org"
             "    ON org_links(org_id);");
        EXEC("CREATE INDEX idx_contact_methods_con"
             "    ON contact_methods(contact_id);");

        /* rebuilt tables */
        EXEC("CREATE INDEX idx_contacts_org"
             "    ON contacts(org_id);");
        EXEC("CREATE INDEX idx_positions_org"
             "    ON positions(org_id);");
        EXEC("CREATE INDEX idx_applications_org"
             "    ON applications(org_id);");
        EXEC("CREATE INDEX idx_applications_pos"
             "    ON applications(position_id);");
        EXEC("CREATE INDEX idx_applications_status"
             "    ON applications(current_status);");

        /* fix the v1 typo on the untouched addresses table */
        EXEC("DROP INDEX IF EXISTS idx_adresses_org;");
        EXEC("CREATE INDEX idx_addresses_org"
             "    ON addresses(org_id);");

    }
    /* ============================================================ *
     * end v1 -> v2                                                  *
     * ============================================================ */

    /* --- verify FK integrity before committing ------------------- */
    if (!db_fk_check(db))
        db_migrate_fail(db, path, bak_path,
                        "foreign key check failed after migration");

    /* --- stamp new version and commit ---------------------------- */
    char vsql[PM_VERSION_BUF];
    snprintf(vsql, sizeof(vsql),
             "PRAGMA user_version = %d;", PM_SCHEMA_VERSION);
    EXEC(vsql);
    EXEC("COMMIT;");

#undef EXEC

    /* Re-enable FK enforcement. Non-fatal if this fails -- db_open
     * sets it again immediately on return. */
    sqlite3_exec(db->handle, "PRAGMA foreign_keys = ON;",
                 NULL, NULL, NULL);

    /* --- success: remove backup ---------------------------------- */
    unlink(bak_path);
    fprintf(stderr, "peshamarsad: migration complete.\n");
}


#endif /* DB_MIGRATE_H */
