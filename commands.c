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
 * commands.c - command implementations
 *
 * Each public cmd_* function maps to one CLI subcommand.
 * Static helpers are internal and not visible outside this file.
 *
 * Conventions used throughout:
 *   - snprintf(buf, n, "%s", val ? val : "") for DB-sourced strings.
 *     (str_copy has an assert that fires on truncation; DB values can
 *     be any length so snprintf is safer here.)
 *   - cli_safe(s) returns "-" for NULL/empty -- used only for display.
 *   - All db_query stmts are finalized with db_done, always.
 *   - Optional text fields stored as "" when blank (not NULL).
 *   - Nullable integers (salary, fit scores) use DB_NULL when not set.
 *     cli_read_salary returns -1 for skip; cli_read_fit returns 0 for skip.
 *     Pattern: (val >= 0 ? DB_INT : DB_NULL), val
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cli.h"
#include "db.h"
#include "commands.h"


/* ================================================================
 * REFERENCE ENUM ARRAYS
 *
 * Passed to cli_pick / cli_pick_opt / cli_pick_update and to
 * str_in_list for validation of ordered list fields.
 * ================================================================ */

static const char *PERSONALITY[] = {
    "Mentor", "Gatekeeper", "Operator", "Politician", "Skeptic",
    "Patron", "Climber", "Bureaucrat", "Ally", "Neutral"
};
#define PERSONALITY_N 10

static const char *ATTITUDE[] = {
    "Welcoming", "Friendly", "Nurturing", "Neutral",
    "Cautious", "Suspicious", "Hostile"
};
#define ATTITUDE_N 7

static const char *APP_STATUS[] = {
    "Applied", "No Response", "Followed Up",
    "Interview Scheduled", "Offer", "Rejected", "Withdrawn"
};
#define APP_STATUS_N 7

static const char *SECURITY[] = { "Low", "Medium", "High" };
#define SECURITY_N 3

static const char *SIZE[] = {
    "Micro (1-9)", "Small (10-49)", "Medium (50-249)",
    "Large (250-999)", "Enterprise (1000+)", "Unknown"
};
#define SIZE_N 6

static const char *ORG_TYPE[] = {
    "University", "School", "NGO", "Startup", "Corporation",
    "Government", "Private Academy", "Military"
};
#define ORG_TYPE_N 8

static const char *LEVEL[] = {
    "Intern", "Entry", "Junior", "Mid", "Senior", "Managerial", "Executive"
};
#define LEVEL_N 7

static const char *CONTACT_RELATION[] = {
    "Recruiter", "Hiring Manager", "Employee", "Manager", "Executive",
    "Alumni", "Friend", "Former Colleague", "Cold Contact", "Reference"
};
#define CONTACT_RELATION_N 10

static const char *EMPLOYMENT_TYPE[] = {
    "Full-time", "Part-time", "Contract", "Internship", "Volunteer"
};
#define EMPLOYMENT_TYPE_N 5

static const char *REMOTE_TYPE[] = { "Remote", "Hybrid", "On-site" };
#define REMOTE_TYPE_N 3

static const char *SALARY_PERIOD[] = {
    "Monthly", "Yearly", "Hourly", "Daily"
};
#define SALARY_PERIOD_N 4

static const char *DISCOVERY_SOURCE[] = {
    "LinkedIn", "Glassdoor", "Indeed", "Rozee.pk", "Company Website",
    "Referral", "Recruiter", "University Portal", "Job Fair",
    "Direct Search", "Other"
};
#define DISCOVERY_SOURCE_N 11

static const char *APP_CHANNEL[] = {
    "Company Website", "LinkedIn", "Email", "Recruiter",
    "Referral", "University Portal", "Hand Delivered", "Other"
};
#define APP_CHANNEL_N 8


/* ================================================================
 * INTERNAL HELPERS
 * ================================================================ */

/* yn: boolean to "Yes" / "No" */
// static const char *yn(int v)
// {
//     return v ? "Yes" : "No";
// }

/* today_str: write today's date as YYYY-MM-DD into buf (min 11 bytes) */
static void today_str(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buf, n, "%Y-%m-%d", tm_info);
}

/* print_doc: document checkbox row for app show/update */
static void print_doc(const char *name, int val)
{
    printf("    [%c] %s\n", val ? 'x' : ' ', name);
}

/* fit_str: display a fit score (0/NULL → "-") */
static const char *fit_str(int v, char *buf, size_t n)
{
    if (v <= 0) return "-";
    snprintf(buf, n, "%d", v);
    return buf;
}

/* ------------------------------------------------------------------
 * pick_org
 *
 * Show all organizations and prompt for an ID.
 * Returns the entered ID, or 0 if no orgs exist.
 * The caller is responsible for verifying the ID is valid if needed
 * (SQLite foreign key enforcement will catch invalid IDs on INSERT).
 * ------------------------------------------------------------------ */
static int pick_org(DB *db)
{
    printf("\n  Organizations:\n");
    sqlite3_stmt *stmt = db_query(db,
        "SELECT id, name FROM organizations ORDER BY id",
        DB_END);

    int found = 0;
    while (db_row(stmt)) {
        printf("    [%d] %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)));
        found = 1;
    }
    db_done(stmt);

    if (!found) {
        printf("  No organizations found. Add one: peshamarsad org add\n");
        return 0;
    }
    return cli_read_int("Organization ID");
}

/* ------------------------------------------------------------------
 * pick_position
 *
 * Show positions for a given org and prompt for an ID.
 * Returns the entered ID, or 0 if no positions exist for that org.
 * ------------------------------------------------------------------ */
static int pick_position(DB *db, int org_id)
{
    printf("\n  Positions:\n");
    sqlite3_stmt *stmt = db_query(db,
        "SELECT id, role, level, department FROM positions "
        "WHERE org_id = ? ORDER BY id",
        DB_INT, org_id, DB_END);

    int found = 0;
    while (db_row(stmt)) {
        printf("    [%d] %s - %s - %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)),
               cli_safe(db_col_text(stmt, 3)));
        found = 1;
    }
    db_done(stmt);

    if (!found) {
        printf("  No positions for this org. "
               "Add one: peshamarsad position add\n");
        return 0;
    }
    return cli_read_int("Position ID");
}

/* ------------------------------------------------------------------
 * add_address
 *
 * Prompt for address fields and insert into addresses table.
 * Called from cmd_org_add (optional) and cmd_org_update (optional).
 * Only city is required; all other fields are optional.
 * ------------------------------------------------------------------ */
static void add_address(DB *db, int org_id)
{
    char label[PM_BUF], plot[PM_BUF], street[PM_BUF];
    char block[PM_BUF], sector[PM_BUF], city[PM_BUF], landmark[PM_BUF];

    printf("\n  --- Add Address ---\n");
    cli_read_opt("Label (e.g. Head Office, B-17 Branch)", label, PM_BUF);
    cli_read_opt("Plot number", plot, PM_BUF);
    cli_read_opt("Street number", street, PM_BUF);
    cli_read_opt("Block number", block, PM_BUF);
    cli_read_opt("Sector", sector, PM_BUF);
    cli_read("City", city, PM_BUF);
    cli_read_opt("Landmark", landmark, PM_BUF);

    db_exec(db,
        "INSERT INTO addresses "
        "(org_id, label, plot_number, street_number, block_number, "
        " sector, city, landmark) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        DB_INT,  org_id,
        DB_TEXT, label,
        DB_TEXT, plot,
        DB_TEXT, street,
        DB_TEXT, block,
        DB_TEXT, sector,
        DB_TEXT, city,
        DB_TEXT, landmark,
        DB_END);

    printf("  Address added.\n");
}

/* ------------------------------------------------------------------
 * backup_before_delete
 *
 * Create a timestamped backup of the database before any destructive
 * delete. Called after the user confirms deletion, before db_exec.
 *
 * Filename: <db_path>.<YYYY-MM-DDTHH-MM>.bak
 * Uses sqlite3_db_filename to retrieve the path from the open
 * connection -- no need to store the path separately in DB.
 * Colons are avoided in the timestamp (use - instead) for
 * filesystem compatibility on all targets including Termux.
 *
 * A backup failure is a warning, not a fatal error -- the user
 * already confirmed the delete and the data is theirs to manage.
 * ------------------------------------------------------------------ */
static void backup_before_delete(DB *db)
{
    const char *path = sqlite3_db_filename(db->handle, "main");
    if (!path || !*path) return;   /* in-memory or unnamed db -- skip */

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H-%M", tm_info);

    char bak[PM_BUF * 2];
    snprintf(bak, sizeof(bak), "%s.%s.bak", path, stamp);

    if (!db_make_backup(db, bak)) {
        fprintf(stderr,
                "peshamarsad: warning: could not create backup before "
                "delete. Proceeding anyway.\n");
        return;
    }
    printf("  Backup saved: %s\n", bak);
}


/* ================================================================
 * ORGANIZATIONS
 * ================================================================ */

void cmd_org_add(DB *db)
{
    char name[PM_BUF], website[PM_BUF], industry[PM_BUF];
    char org_type[PM_BUF], security[PM_BUF], size_val[PM_BUF], notes[PM_BUF];

    cli_header("Add Organization");
    cli_read("Name", name, PM_BUF);
    int registered = cli_read_bool("Registered entity");
    cli_read_opt("Website", website, PM_BUF);
    cli_read_opt("Industry (Education, Technology, Healthcare...)", industry, PM_BUF);
    cli_pick_opt("Organization type", ORG_TYPE, ORG_TYPE_N, org_type, PM_BUF);
    cli_pick_opt("Security level", SECURITY, SECURITY_N, security, PM_BUF);
    cli_pick_opt("Size", SIZE, SIZE_N, size_val, PM_BUF);
    cli_read_opt("Notes", notes, PM_BUF);

    db_exec(db,
        "INSERT INTO organizations "
        "(name, registered, website, industry, org_type, security_level, size, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        DB_TEXT, name,
        DB_INT,  registered,
        DB_TEXT, website,
        DB_TEXT, industry,
        DB_TEXT, org_type,
        DB_TEXT, security,
        DB_TEXT, size_val,
        DB_TEXT, notes,
        DB_END);

    int id = db_last_id(db);
    printf("\nOrganization #%d '%s' added.\n", id, name);

    if (cli_confirm("Add an address now?"))
        add_address(db, id);
}


void cmd_org_list(DB *db)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT id, name, industry, org_type, size, security_level, "
	"created_at FROM organizations ORDER BY id",
        DB_END);

    printf("\n%-4s  %-28s  %-16s  %-16s  %-18s  %-8s  %s\n",
           "ID", "Name", "Industry", "Type", "Size", "Security", "Created");
    printf("%-4s  %-28s  %-16s  %-16s  %-18s  %-8s  %s\n",
           "----", "----------------------------",
           "----------------", "------------------",
           "------------------", "--------", "----------");

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("%-4d  %-28s  %-16s  %-16s  %-18s  %-8s  %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)),
               cli_safe(db_col_text(stmt, 3)),
               cli_safe(db_col_text(stmt, 4)),
               cli_safe(db_col_text(stmt, 5)),
               cli_safe(db_col_text(stmt, 6)));
    }
    db_done(stmt);

    if (!found) printf("  No organizations found.\n");
}


void cmd_org_show(DB *db, int id)
{
    /*
     * Column order (0-based):
     *  0 id  1 name  2 registered  3 website  4 industry
     *  5 org_type  6 security_level  7 size  8 notes  9 created_at
     */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT id, name, registered, website, industry, org_type, "
        "       security_level, size, notes, created_at "
        "FROM organizations WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Organization #%d not found.\n", id);
        return;
    }

    /* Print main fields while stmt is alive */
    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Organization #%d: %s",
             db_col_int(stmt, 0), cli_safe(db_col_text(stmt, 1)));
    cli_header(hdr);
    cli_bool_field("Registered:",    db_col_int(stmt, 2));
    cli_field("Website:",            cli_safe(db_col_text(stmt, 3)));
    cli_field("Industry:",           cli_safe(db_col_text(stmt, 4)));
    cli_field("Type:",               cli_safe(db_col_text(stmt, 5)));
    cli_field("Security Level:",     cli_safe(db_col_text(stmt, 6)));
    cli_field("Size:",               cli_safe(db_col_text(stmt, 7)));
    cli_field("Notes:",              cli_safe(db_col_text(stmt, 8)));
    cli_field("Added:",              cli_safe(db_col_text(stmt, 9)));
    db_done(stmt);

    /* Links */
    printf("\nLinks:\n");
    stmt = db_query(db,
                    "SELECT id, type, label, url FROM org_links "
                    "WHERE org_id = ? ORDER BY id",
                    DB_INT, id,
               	    DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        const char *lbl = db_col_text(stmt, 2);
        printf("  [%d] %s%s%s  %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               (lbl && *lbl) ? " - " : "",
               (lbl && *lbl) ? lbl : "",
               cli_safe(db_col_text(stmt, 3)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");

    /* Addresses */
    printf("\nAddresses:\n");
    stmt = db_query(db,
        "SELECT id, label, plot_number, street_number, block_number, "
        "       sector, city, landmark "
        "FROM addresses WHERE org_id = ? ORDER BY id",
        DB_INT, id, DB_END);

    found = 0;
    while (db_row(stmt)) {
        found = 1;
        const char *lbl    = db_col_text(stmt, 1);
        const char *plot   = db_col_text(stmt, 2);
        const char *street = db_col_text(stmt, 3);
        const char *block  = db_col_text(stmt, 4);
        const char *sector = db_col_text(stmt, 5);
        const char *city   = db_col_text(stmt, 6);
        const char *lmark  = db_col_text(stmt, 7);

        printf("  [%d]%s%s\n",
               db_col_int(stmt, 0),
               (lbl && *lbl) ? " " : "",
               (lbl && *lbl) ? lbl : "");
        printf("      ");
        if (plot   && *plot)   printf("Plot %s  ", plot);
        if (street && *street) printf("Street %s  ", street);
        if (block  && *block)  printf("Block %s  ", block);
        if (sector && *sector) printf("%s  ", sector);
        printf("%s\n", cli_safe(city));
        if (lmark  && *lmark)  printf("      Near: %s\n", lmark);
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");

    /* Reviews */
    printf("\nReviews:\n");
    stmt = db_query(db,
        "SELECT date, source, review FROM reviews "
        "WHERE org_id = ? ORDER BY date DESC",
        DB_INT, id, DB_END);

    found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%s] %s\n",
               cli_safe(db_col_text(stmt, 0)),
               cli_safe(db_col_text(stmt, 1)));
        printf("    %s\n", cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");

    /* Contacts */
    printf("\nContacts:\n");
    stmt = db_query(db,
        "SELECT id, name, position_title, relation, personality, attitude "
        "FROM contacts WHERE org_id = ? ORDER BY id",
        DB_INT, id, DB_END);

    found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%d] %s - %s  [%s] [%s] [%s]\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)),
               cli_safe(db_col_text(stmt, 3)),
               cli_safe(db_col_text(stmt, 4)),
               cli_safe(db_col_text(stmt, 5)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");

    /* Positions */
    printf("\nPositions:\n");
    stmt = db_query(db,
        "SELECT id, role, level, employment_type, department "
        "FROM positions WHERE org_id = ? ORDER BY id",
        DB_INT, id, DB_END);

    found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%d] %s - %s - %s - %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)),
               cli_safe(db_col_text(stmt, 3)),
               cli_safe(db_col_text(stmt, 4)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");

    printf("\n");
}


void cmd_org_update(DB *db, int id)
{
    /* Fetch current values */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name, registered, website, industry, org_type, "
        "       security_level, size, notes "
        "FROM organizations WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Organization #%d not found.\n", id);
        return;
    }

    char name[PM_BUF], website[PM_BUF], industry[PM_BUF];
    char org_type[PM_BUF], security[PM_BUF], size_val[PM_BUF], notes[PM_BUF];
    int  registered = db_col_int(stmt, 1);

    const char *v;
    v = db_col_text(stmt, 0); snprintf(name,     sizeof(name),     "%s", v ? v : "");
    v = db_col_text(stmt, 2); snprintf(website,  sizeof(website),  "%s", v ? v : "");
    v = db_col_text(stmt, 3); snprintf(industry, sizeof(industry), "%s", v ? v : "");
    v = db_col_text(stmt, 4); snprintf(org_type, sizeof(org_type), "%s", v ? v : "");
    v = db_col_text(stmt, 5); snprintf(security, sizeof(security), "%s", v ? v : "");
    v = db_col_text(stmt, 6); snprintf(size_val, sizeof(size_val), "%s", v ? v : "");
    v = db_col_text(stmt, 7); snprintf(notes,    sizeof(notes),    "%s", v ? v : "");
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Update Organization #%d", id);
    cli_header(hdr);
    printf("  Press Enter to keep current value.\n\n");

    char new_name[PM_BUF], new_website[PM_BUF], new_industry[PM_BUF];
    char new_type[PM_BUF], new_security[PM_BUF], new_size[PM_BUF], new_notes[PM_BUF];

    cli_read_update("Name", name, new_name, PM_BUF);
    int new_registered = cli_read_bool_update("Registered", registered);
    cli_read_update("Website", website, new_website, PM_BUF);
    cli_read_update("Industry", industry, new_industry, PM_BUF);
    cli_pick_update("Organization type", ORG_TYPE, ORG_TYPE_N, org_type, new_type, PM_BUF);
    cli_pick_update("Security Level", SECURITY, SECURITY_N, security, new_security, PM_BUF);
    cli_pick_update("Size", SIZE, SIZE_N, size_val, new_size, PM_BUF);
    cli_read_update("Notes", notes, new_notes, PM_BUF);

    db_exec(db,
        "UPDATE organizations SET name=?, registered=?, website=?, "
        "industry=?, org_type=?, security_level=?, size=?, notes=? WHERE id=?",
        DB_TEXT, new_name,
        DB_INT,  new_registered,
        DB_TEXT, new_website,
        DB_TEXT, new_industry,
        DB_TEXT, new_type,
        DB_TEXT, new_security,
        DB_TEXT, new_size,
        DB_TEXT, new_notes,
        DB_INT,  id,
        DB_END);

    printf("\nOrganization #%d updated.\n", id);

    if (cli_confirm("Add a new address?"))
        add_address(db, id);
}


void cmd_org_delete(DB *db, int id)
{
    /* Show what will be deleted */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name FROM organizations WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Organization #%d not found.\n", id);
        return;
    }
    printf("\nOrganization #%d: %s\n", id, cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    printf("WARNING: This permanently deletes the organization and ALL related\n");
    printf("records: links, addresses, contacts, positions, applications, status history.\n");
    if (!cli_confirm("Are you sure?")) {
        printf("Cancelled.\n");
        return;
    }

    backup_before_delete(db);
    db_exec(db, "DELETE FROM organizations WHERE id = ?",
            DB_INT, id, DB_END);
    printf("Organization #%d deleted.\n", id);
}


/* ================================================================
 * ORG LINKS
 * ================================================================ */

void cmd_org_link_add(DB *db, int org_id)
{
    sqlite3_stmt *stmt = db_query(db,
                                  "SELECT name FROM organizations "
				  "WHERE id = ?",
                                  DB_INT, org_id,
	                          DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Organization #%d not found.\n", org_id);
        return;
    }
    char org_name[PM_BUF];
    snprintf(org_name, sizeof(org_name), "%s", cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Add Link - %s (#%d)", org_name, org_id);
    cli_header(hdr);

    char type[PM_BUF], label[PM_BUF], url[PM_BUF];
    cli_read("Type (LinkedIn, Glassdoor, Careers, GitHub...)", type, PM_BUF);
    cli_read_opt("Label (optional description)", label, PM_BUF);
    cli_read("URL", url, PM_BUF);

    db_exec(db,
            "INSERT INTO org_links (org_id, type, label, url) VALUES (?, ?, ?, ?)",
            DB_INT,  org_id,
            DB_TEXT, type,
            DB_TEXT, label,
            DB_TEXT, url,
            DB_END);

    printf("\nLink added to %s.\n", org_name);
}


void cmd_org_link_list(DB *db, int org_id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name FROM organizations WHERE id = ?",
        DB_INT, org_id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Organization #%d not found.\n", org_id);
        return;
    }
    char org_name[PM_BUF];
    snprintf(org_name, sizeof(org_name), "%s", cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Links - %s (#%d)", org_name, org_id);
    cli_header(hdr);

    stmt = db_query(db,
                    "SELECT id, type, label, url FROM org_links "
                    "WHERE org_id = ? ORDER BY id",
                    DB_INT, org_id,
		    DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        const char *lbl = db_col_text(stmt, 2);
        printf("  [%d] %-16s  %s%s%s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 3)),
               (lbl && *lbl) ? "  - " : "",
               (lbl && *lbl) ? lbl : "");
    }
    db_done(stmt);

    if (!found) printf("  No links found.\n");
}


/* ================================================================
 * REVIEWS
 * ================================================================ */

void cmd_review_add(DB *db, int org_id)
{
    /* Verify org exists and show context */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name FROM organizations WHERE id = ?",
        DB_INT, org_id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Organization #%d not found.\n", org_id);
        return;
    }
    char org_name[PM_BUF];
    snprintf(org_name, sizeof(org_name), "%s", cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Add Review - %s (#%d)", org_name, org_id);
    cli_header(hdr);

    char review[PM_BUF], date[PM_BUF], source[PM_BUF];
    char default_date[11];
    today_str(default_date, sizeof(default_date));

    cli_read("Review", review, PM_BUF);
    cli_read_date("Date", default_date, date, sizeof(date));
    cli_read_opt("Source (Glassdoor, personal, referral...)", source, PM_BUF);

    db_exec(db,
        "INSERT INTO reviews (org_id, review, date, source) VALUES (?, ?, ?, ?)",
        DB_INT,  org_id,
        DB_TEXT, review,
        DB_TEXT, date,
        DB_TEXT, source,
        DB_END);

    printf("\nReview added for %s.\n", org_name);
}


void cmd_review_list(DB *db, int org_id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name FROM organizations WHERE id = ?",
        DB_INT, org_id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Organization #%d not found.\n", org_id);
        return;
    }
    char org_name[PM_BUF];
    snprintf(org_name, sizeof(org_name), "%s", cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Reviews - %s (#%d)", org_name, org_id);
    cli_header(hdr);

    stmt = db_query(db,
        "SELECT date, source, review FROM reviews "
        "WHERE org_id = ? ORDER BY date DESC",
        DB_INT, org_id, DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%s] %s\n",
               cli_safe(db_col_text(stmt, 0)),
               cli_safe(db_col_text(stmt, 1)));
        printf("    %s\n\n", cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);

    if (!found) printf("  No reviews found.\n");
}


/* ================================================================
 * CONTACTS
 * ================================================================ */

void cmd_contact_add(DB *db)
{
    cli_header("Add Contact");

    int org_id = pick_org(db);
    if (org_id == 0) return;

    char name[PM_BUF], age_range[PM_BUF], title_buf[PM_BUF];
    char relation[PM_BUF], personality[PM_BUF], attitude[PM_BUF];
    char leverage[PM_BUF], how_met[PM_BUF], last_contact[PM_BUF], notes[PM_BUF];

    printf("\n");
    cli_read("Name", name, PM_BUF);
    cli_read_opt("Age range (e.g. 35, 30-45, ~50)", age_range, PM_BUF);
    cli_read_opt("Position title", title_buf, PM_BUF);
    cli_pick_opt("Relation", CONTACT_RELATION, CONTACT_RELATION_N, relation, PM_BUF);

    cli_read_ordered_list("Personality", PERSONALITY, PERSONALITY_N, personality, PM_BUF);
    cli_read_ordered_list("Attitude", ATTITUDE, ATTITUDE_N, attitude, PM_BUF);

    cli_read_opt("Leverage potential", leverage, PM_BUF);
    cli_read_opt("How met", how_met, PM_BUF);
    cli_read_opt("Last contact date (YYYY-MM-DD)", last_contact, PM_BUF);
    cli_read_opt("Notes", notes, PM_BUF);

    db_exec(db,
        "INSERT INTO contacts "
        "(name, age_range, org_id, position_title, relation, personality, "
        " attitude, leverage_potential, how_met, last_contact_date, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        DB_TEXT, name,
        DB_TEXT, age_range,
        DB_INT,  org_id,
        DB_TEXT, title_buf,
        DB_TEXT, relation,
        DB_TEXT, personality,
        DB_TEXT, attitude,
        DB_TEXT, leverage,
        DB_TEXT, how_met,
        DB_TEXT, last_contact,
        DB_TEXT, notes,
        DB_END);

    printf("\nContact #%d '%s' added.\n", db_last_id(db), name);
}


void cmd_contact_list(DB *db, int org_id)
{
    char sql[1024];
    const char *base =
        "SELECT c.id, c.name, o.name, c.position_title, c.relation, "
        "       c.personality, c.attitude "
        "FROM contacts c JOIN organizations o ON c.org_id = o.id";

    sqlite3_stmt *stmt;
    if (org_id > 0) {
        snprintf(sql, sizeof(sql), "%s WHERE c.org_id = ? ORDER BY c.id", base);
        stmt = db_query(db, sql, DB_INT, org_id, DB_END);
    } else {
        snprintf(sql, sizeof(sql), "%s ORDER BY c.id", base);
        stmt = db_query(db, sql, DB_END);
    }

    printf("\n%-4s  %-20s  %-20s  %-16s  %-14s  %-16s  %s\n",
           "ID", "Name", "Organization", "Title", "Relation",
           "Personality", "Attitude");
    printf("%-4s  %-20s  %-20s  %-16s  %-14s  %-16s  %s\n",
           "----", "--------------------", "--------------------",
           "----------------", "--------------",
           "----------------", "-------");

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("%-4d  %-20s  %-20s  %-16s  %-14s  %-16s  %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)),
               cli_safe(db_col_text(stmt, 3)),
               cli_safe(db_col_text(stmt, 4)),
               cli_safe(db_col_text(stmt, 5)),
               cli_safe(db_col_text(stmt, 6)));
    }
    db_done(stmt);

    if (!found) printf("  No contacts found.\n");
}


void cmd_contact_show(DB *db, int id)
{
    /*
     * Column order (0-based):
     *  0 id  1 name  2 age_range  3 org.name  4 position_title
     *  5 relation  6 personality  7 attitude  8 leverage_potential
     *  9 how_met  10 last_contact_date  11 notes  12 created_at
     */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT c.id, c.name, c.age_range, o.name, c.position_title, "
        "       c.relation, c.personality, c.attitude, c.leverage_potential, "
        "       c.how_met, c.last_contact_date, c.notes, c.created_at "
        "FROM contacts c JOIN organizations o ON c.org_id = o.id "
        "WHERE c.id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Contact #%d not found.\n", id);
        return;
    }

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Contact #%d: %s",
             db_col_int(stmt, 0), cli_safe(db_col_text(stmt, 1)));
    cli_header(hdr);
    cli_field("Age Range:",           cli_safe(db_col_text(stmt, 2)));
    cli_field("Organization:",        cli_safe(db_col_text(stmt, 3)));
    cli_field("Title:",               cli_safe(db_col_text(stmt, 4)));
    cli_field("Relation:",            cli_safe(db_col_text(stmt, 5)));
    cli_field("Personality:",         cli_safe(db_col_text(stmt, 6)));
    cli_field("Attitude:",            cli_safe(db_col_text(stmt, 7)));
    cli_field("Leverage Potential:",  cli_safe(db_col_text(stmt, 8)));
    cli_field("How Met:",             cli_safe(db_col_text(stmt, 9)));
    cli_field("Last Contact:",        cli_safe(db_col_text(stmt, 10)));
    cli_field("Notes:",               cli_safe(db_col_text(stmt, 11)));
    cli_field("Added:",               cli_safe(db_col_text(stmt, 12)));
    db_done(stmt);

    /* Contact methods */
    printf("\nContact Methods:\n");
    stmt = db_query(db,
        "SELECT id, medium, address FROM contact_methods "
        "WHERE contact_id = ? ORDER BY id",
        DB_INT, id, DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%d] %-14s  %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");

    /* Applications this contact is linked to */
    printf("\nLinked Applications:\n");
    stmt = db_query(db,
        "SELECT a.id, a.date, a.current_status "
        "FROM applications a "
        "JOIN application_contacts ac ON ac.application_id = a.id "
        "WHERE ac.contact_id = ? ORDER BY a.date DESC",
        DB_INT, id, DB_END);

    found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%d] %s - %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");
    printf("\n");
}


void cmd_contact_update(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name, age_range, org_id, position_title, relation, "
        "       personality, attitude, leverage_potential, "
        "       how_met, last_contact_date, notes "
        "FROM contacts WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Contact #%d not found.\n", id);
        return;
    }

    char name[PM_BUF], age[PM_BUF], title_buf[PM_BUF], relation[PM_BUF];
    char personality[PM_BUF], attitude[PM_BUF], leverage[PM_BUF];
    char how_met[PM_BUF], last_contact[PM_BUF], notes[PM_BUF];
    int  org_id = db_col_int(stmt, 2);

    const char *v;
    v = db_col_text(stmt, 0 ); snprintf(name,         sizeof(name),         "%s", v ? v : "");
    v = db_col_text(stmt, 1 ); snprintf(age,          sizeof(age),          "%s", v ? v : "");
    v = db_col_text(stmt, 3 ); snprintf(title_buf,    sizeof(title_buf),    "%s", v ? v : "");
    v = db_col_text(stmt, 4 ); snprintf(relation,     sizeof(relation),     "%s", v ? v : "");
    v = db_col_text(stmt, 5 ); snprintf(personality,  sizeof(personality),  "%s", v ? v : "");
    v = db_col_text(stmt, 6 ); snprintf(attitude,     sizeof(attitude),     "%s", v ? v : "");
    v = db_col_text(stmt, 7 ); snprintf(leverage,     sizeof(leverage),     "%s", v ? v : "");
    v = db_col_text(stmt, 8 ); snprintf(how_met,      sizeof(how_met),      "%s", v ? v : "");
    v = db_col_text(stmt, 9 ); snprintf(last_contact, sizeof(last_contact), "%s", v ? v : "");
    v = db_col_text(stmt, 10); snprintf(notes,        sizeof(notes),        "%s", v ? v : "");
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Update Contact #%d", id);
    cli_header(hdr);
    printf("  Press Enter to keep current value.\n\n");

    char new_name[PM_BUF], new_age[PM_BUF], new_title[PM_BUF], new_relation[PM_BUF];
    char new_personality[PM_BUF], new_attitude[PM_BUF], new_leverage[PM_BUF];
    char new_how_met[PM_BUF], new_last_contact[PM_BUF], new_notes[PM_BUF];

    cli_read_update("Name", name, new_name, PM_BUF);
    cli_read_update("Age range", age, new_age, PM_BUF);
    cli_read_update("Position title", title_buf, new_title, PM_BUF);
    cli_pick_update("Relation", CONTACT_RELATION, CONTACT_RELATION_N, relation, new_relation, PM_BUF);
    cli_update_ordered_list("Personality", personality, PERSONALITY, PERSONALITY_N, new_personality, PM_BUF);
    cli_update_ordered_list("Attitude", attitude, ATTITUDE, ATTITUDE_N, new_attitude, PM_BUF);
    cli_read_update("Leverage potential", leverage, new_leverage, PM_BUF);
    cli_read_update("How met", how_met, new_how_met, PM_BUF);
    cli_read_update("Last contact date", last_contact, new_last_contact, PM_BUF);
    cli_read_update("Notes", notes, new_notes, PM_BUF);

    db_exec(db,
        "UPDATE contacts SET name=?, age_range=?, org_id=?, position_title=?, "
        "relation=?, personality=?, attitude=?, leverage_potential=?, "
        "how_met=?, last_contact_date=?, notes=? WHERE id=?",
        DB_TEXT, new_name,
        DB_TEXT, new_age,
        DB_INT,  org_id,
        DB_TEXT, new_title,
        DB_TEXT, new_relation,
        DB_TEXT, new_personality,
        DB_TEXT, new_attitude,
        DB_TEXT, new_leverage,
        DB_TEXT, new_how_met,
        DB_TEXT, new_last_contact,
        DB_TEXT, new_notes,
        DB_INT,  id,
        DB_END);

    printf("\nContact #%d updated.\n", id);
}


void cmd_contact_update_personality(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name, personality FROM contacts WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Contact #%d not found.\n", id);
        return;
    }

    char name[PM_BUF], current[PM_BUF];
    const char *v;
    v = db_col_text(stmt, 0); snprintf(name,    sizeof(name),    "%s", v ? v : "");
    v = db_col_text(stmt, 1); snprintf(current, sizeof(current), "%s", v ? v : "");
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Update Personality - %s (#%d)", name, id);
    cli_header(hdr);

    char new_val[PM_BUF];
    cli_update_ordered_list("Personality", current, PERSONALITY, PERSONALITY_N, new_val, PM_BUF);

    db_exec(db, "UPDATE contacts SET personality=? WHERE id=?",
            DB_TEXT, new_val, DB_INT, id, DB_END);

    printf("\nPersonality updated: %s\n", cli_safe(new_val));
}


void cmd_contact_update_attitude(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name, attitude FROM contacts WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Contact #%d not found.\n", id);
        return;
    }

    char name[PM_BUF], current[PM_BUF];
    const char *v;
    v = db_col_text(stmt, 0); snprintf(name,    sizeof(name),    "%s", v ? v : "");
    v = db_col_text(stmt, 1); snprintf(current, sizeof(current), "%s", v ? v : "");
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Update Attitude - %s (#%d)", name, id);
    cli_header(hdr);

    char new_val[PM_BUF];
    cli_update_ordered_list("Attitude", current, ATTITUDE, ATTITUDE_N, new_val, PM_BUF);

    db_exec(db, "UPDATE contacts SET attitude=? WHERE id=?",
            DB_TEXT, new_val, DB_INT, id, DB_END);

    printf("\nAttitude updated: %s\n", cli_safe(new_val));
}


void cmd_contact_delete(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name FROM contacts WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Contact #%d not found.\n", id);
        return;
    }
    printf("\nContact #%d: %s\n", id, cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    if (!cli_confirm("Delete this contact?")) { printf("Cancelled.\n"); return; }

    backup_before_delete(db);
    db_exec(db, "DELETE FROM contacts WHERE id = ?", DB_INT, id, DB_END);
    printf("Contact #%d deleted.\n", id);
}


/* ================================================================
 * CONTACT METHODS
 * ================================================================ */

void cmd_contact_method_add(DB *db, int contact_id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name FROM contacts WHERE id = ?",
        DB_INT, contact_id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Contact #%d not found.\n", contact_id);
        return;
    }
    char contact_name[PM_BUF];
    snprintf(contact_name, sizeof(contact_name), "%s",
             cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Add Contact Method - %s (#%d)",
             contact_name, contact_id);
    cli_header(hdr);

    char medium[PM_BUF], address[PM_BUF];
    cli_read("Medium (email, phone, telegram, whatsapp...)", medium, PM_BUF);
    cli_read("Address / handle / number", address, PM_BUF);

    db_exec(db,
        "INSERT INTO contact_methods (contact_id, medium, address) VALUES (?, ?, ?)",
        DB_INT,  contact_id,
        DB_TEXT, medium,
        DB_TEXT, address,
        DB_END);

    printf("\nContact method added for %s.\n", contact_name);
}


void cmd_contact_method_list(DB *db, int contact_id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT name FROM contacts WHERE id = ?",
        DB_INT, contact_id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Contact #%d not found.\n", contact_id);
        return;
    }
    char contact_name[PM_BUF];
    snprintf(contact_name, sizeof(contact_name), "%s",
             cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Contact Methods - %s (#%d)",
             contact_name, contact_id);
    cli_header(hdr);

    stmt = db_query(db,
        "SELECT id, medium, address FROM contact_methods "
        "WHERE contact_id = ? ORDER BY id",
        DB_INT, contact_id, DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%d] %-14s  %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);

    if (!found) printf("  No contact methods found.\n");
}


/* ================================================================
 * POSITIONS
 * ================================================================ */

void cmd_position_add(DB *db)
{
    cli_header("Add Position");

    int org_id = pick_org(db);
    if (org_id == 0) return;

    char dept[PM_BUF], role[PM_BUF], level_val[PM_BUF];
    char emp_type[PM_BUF], remote_type[PM_BUF], job_loc[PM_BUF];
    char closing[PM_BUF], currency[PM_BUF], period[PM_BUF];
    char source[PM_BUF], source_url[PM_BUF];
    char fit_notes[PM_BUF], notes[PM_BUF];

    printf("\n");
    cli_read_opt("Department (IT, Creative, Admin...)", dept, PM_BUF);
    cli_read("Role", role, PM_BUF);
    cli_pick_opt("Level", LEVEL, LEVEL_N, level_val, PM_BUF);
    cli_pick_opt("Employment type", EMPLOYMENT_TYPE, EMPLOYMENT_TYPE_N, emp_type, PM_BUF);
    cli_pick_opt("Remote type", REMOTE_TYPE, REMOTE_TYPE_N, remote_type, PM_BUF);
    cli_read_opt("Job location (if different from org address)", job_loc, PM_BUF);
    cli_read_opt("Closing date (YYYY-MM-DD)", closing, PM_BUF);

    printf("\n  Salary:\n");
    cli_read_opt("Currency (PKR, USD, GBP...)", currency, PM_BUF);
    cli_pick_opt("Salary period", SALARY_PERIOD, SALARY_PERIOD_N, period, PM_BUF);
    int sal_min = cli_read_salary("Minimum salary");
    int sal_max = cli_read_salary("Maximum salary");

    printf("\n  Source:\n");
    cli_pick_opt("Discovery source", DISCOVERY_SOURCE, DISCOVERY_SOURCE_N, source, PM_BUF);
    cli_read_opt("Source URL (original listing)", source_url, PM_BUF);

    printf("\n  Fit Assessment (1=very poor, 5=excellent, 0=skip):\n");
    int skill_fit = cli_read_fit("Skill fit");
    int comp_fit  = cli_read_fit("Compensation fit");
    int loc_fit   = cli_read_fit("Location fit");
    int lvl_fit   = cli_read_fit("Level fit");
    cli_read_opt("Fit notes", fit_notes, PM_BUF);

    printf("\n");
    cli_read_opt("Notes", notes, PM_BUF);

    db_exec(db,
        "INSERT INTO positions "
        "(org_id, department, role, level, employment_type, remote_type, "
        " job_location, closing_date, salary_min, salary_max, "
        " salary_currency, salary_period, source, source_url, "
        " skill_fit, compensation_fit, location_fit, level_fit, "
        " fit_notes, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        DB_INT,  org_id,
        DB_TEXT, dept,
        DB_TEXT, role,
        DB_TEXT, level_val,
        DB_TEXT, emp_type,
        DB_TEXT, remote_type,
        DB_TEXT, job_loc,
        DB_TEXT, closing,
        sal_min >= 0 ? DB_INT : DB_NULL, sal_min,
        sal_max >= 0 ? DB_INT : DB_NULL, sal_max,
        DB_TEXT, currency,
        DB_TEXT, period,
        DB_TEXT, source,
        DB_TEXT, source_url,
        skill_fit > 0 ? DB_INT : DB_NULL, skill_fit,
        comp_fit  > 0 ? DB_INT : DB_NULL, comp_fit,
        loc_fit   > 0 ? DB_INT : DB_NULL, loc_fit,
        lvl_fit   > 0 ? DB_INT : DB_NULL, lvl_fit,
        DB_TEXT, fit_notes,
        DB_TEXT, notes,
        DB_END);

    printf("\nPosition #%d '%s' added.\n", db_last_id(db), role);
}


void cmd_position_list(DB *db, int org_id)
{
    char sql[1024];
    const char *base =
        "SELECT p.id, o.name, p.role, p.level, p.employment_type, p.department "
        "FROM positions p JOIN organizations o ON p.org_id = o.id";

    sqlite3_stmt *stmt;
    if (org_id > 0) {
        snprintf(sql, sizeof(sql), "%s WHERE p.org_id = ? ORDER BY p.id", base);
        stmt = db_query(db, sql, DB_INT, org_id, DB_END);
    } else {
        snprintf(sql, sizeof(sql), "%s ORDER BY p.id", base);
        stmt = db_query(db, sql, DB_END);
    }

    printf("\n%-4s  %-24s  %-22s  %-12s  %-12s  %s\n",
           "ID", "Organization", "Role", "Level", "Type", "Department");
    printf("%-4s  %-24s  %-22s  %-12s  %-12s  %s\n",
           "----", "------------------------",
           "----------------------", "------------",
           "------------", "----------");

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("%-4d  %-24s  %-22s  %-12s  %-12s  %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)),
               cli_safe(db_col_text(stmt, 3)),
               cli_safe(db_col_text(stmt, 4)),
               cli_safe(db_col_text(stmt, 5)));
    }
    db_done(stmt);

    if (!found) printf("  No positions found.\n");
}


void cmd_position_show(DB *db, int id)
{
    /*
     * Column order (0-based):
     *  0 id   1 org   2 dept   3 role   4 level
     *  5 emp_type   6 remote_type   7 job_location   8 closing_date
     *  9 sal_min   10 sal_max   11 sal_currency   12 sal_period
     *  13 source   14 source_url
     *  15 skill_fit   16 comp_fit   17 loc_fit   18 lvl_fit
     *  19 fit_notes   20 notes   21 created_at
     */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT p.id, o.name, p.department, p.role, p.level, "
        "       p.employment_type, p.remote_type, p.job_location, "
        "       p.closing_date, p.salary_min, p.salary_max, "
        "       p.salary_currency, p.salary_period, "
        "       p.source, p.source_url, "
        "       p.skill_fit, p.compensation_fit, p.location_fit, p.level_fit, "
        "       p.fit_notes, p.notes, p.created_at "
        "FROM positions p JOIN organizations o ON p.org_id = o.id "
        "WHERE p.id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Position #%d not found.\n", id);
        return;
    }

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Position #%d: %s",
             db_col_int(stmt, 0), cli_safe(db_col_text(stmt, 3)));
    cli_header(hdr);
    cli_field("Organization:",    cli_safe(db_col_text(stmt, 1)));
    cli_field("Department:",      cli_safe(db_col_text(stmt, 2)));
    cli_field("Level:",           cli_safe(db_col_text(stmt, 4)));
    cli_field("Employment Type:", cli_safe(db_col_text(stmt, 5)));
    cli_field("Remote Type:",     cli_safe(db_col_text(stmt, 6)));
    cli_field("Job Location:",    cli_safe(db_col_text(stmt, 7)));
    cli_field("Closing Date:",    cli_safe(db_col_text(stmt, 8)));

    /* Salary: only show if at least one value is set */
    int sal_min = db_col_int(stmt, 9);
    int sal_max = db_col_int(stmt, 10);
    if (sal_min > 0 || sal_max > 0) {
        char sal_str[PM_BUF];
        const char *cur = cli_safe(db_col_text(stmt, 11));
        const char *per = cli_safe(db_col_text(stmt, 12));
        if (sal_min > 0 && sal_max > 0)
            snprintf(sal_str, sizeof(sal_str), "%d – %d %s / %s",
                     sal_min, sal_max, cur, per);
        else if (sal_min > 0)
            snprintf(sal_str, sizeof(sal_str), "%d+ %s / %s",
                     sal_min, cur, per);
        else
            snprintf(sal_str, sizeof(sal_str), "up to %d %s / %s",
                     sal_max, cur, per);
        cli_field("Salary:", sal_str);
    } else {
        cli_field("Salary:", "-");
    }

    cli_field("Source:",      cli_safe(db_col_text(stmt, 13)));
    cli_field("Source URL:",  cli_safe(db_col_text(stmt, 14)));

    char fstr[8];
    printf("\n  Fit Scores:\n");
    cli_field("  Skill:",        fit_str(db_col_int(stmt, 15), fstr, sizeof(fstr)));
    cli_field("  Compensation:", fit_str(db_col_int(stmt, 16), fstr, sizeof(fstr)));
    cli_field("  Location:",     fit_str(db_col_int(stmt, 17), fstr, sizeof(fstr)));
    cli_field("  Level:",        fit_str(db_col_int(stmt, 18), fstr, sizeof(fstr)));
    cli_field("  Notes:",        cli_safe(db_col_text(stmt, 19)));

    cli_field("Notes:",           cli_safe(db_col_text(stmt, 20)));
    cli_field("Added:",           cli_safe(db_col_text(stmt, 21)));
    db_done(stmt);

    /* Applications for this position */
    printf("\nApplications:\n");
    stmt = db_query(db,
        "SELECT id, date, current_status FROM applications "
        "WHERE position_id = ? ORDER BY date DESC",
        DB_INT, id, DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%d] %s - %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");
    printf("\n");
}


void cmd_position_update(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT department, role, level, employment_type, remote_type, "
        "       job_location, closing_date, salary_min, salary_max, "
        "       salary_currency, salary_period, source, source_url, "
        "       skill_fit, compensation_fit, location_fit, level_fit, "
        "       fit_notes, notes "
        "FROM positions WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Position #%d not found.\n", id);
        return;
    }

    char dept[PM_BUF], role[PM_BUF], level_val[PM_BUF];
    char emp_type[PM_BUF], remote_type[PM_BUF], job_loc[PM_BUF], closing[PM_BUF];
    char currency[PM_BUF], period[PM_BUF];
    char source[PM_BUF], source_url[PM_BUF];
    char fit_notes[PM_BUF], notes[PM_BUF];

    /* Nullable integers: 0 from SQLite means NULL was stored */
    int sal_min   = db_col_int(stmt, 7);  if (sal_min == 0)   sal_min   = -1;
    int sal_max   = db_col_int(stmt, 8);  if (sal_max == 0)   sal_max   = -1;
    int skill_fit = db_col_int(stmt, 13);
    int comp_fit  = db_col_int(stmt, 14);
    int loc_fit   = db_col_int(stmt, 15);
    int lvl_fit   = db_col_int(stmt, 16);

    const char *v;
    v = db_col_text(stmt, 0 ); snprintf(dept,       sizeof(dept),       "%s", v ? v : "");
    v = db_col_text(stmt, 1 ); snprintf(role,       sizeof(role),       "%s", v ? v : "");
    v = db_col_text(stmt, 2 ); snprintf(level_val,  sizeof(level_val),  "%s", v ? v : "");
    v = db_col_text(stmt, 3 ); snprintf(emp_type,   sizeof(emp_type),   "%s", v ? v : "");
    v = db_col_text(stmt, 4 ); snprintf(remote_type,sizeof(remote_type),"%s", v ? v : "");
    v = db_col_text(stmt, 5 ); snprintf(job_loc,    sizeof(job_loc),    "%s", v ? v : "");
    v = db_col_text(stmt, 6 ); snprintf(closing,    sizeof(closing),    "%s", v ? v : "");
    v = db_col_text(stmt, 9 ); snprintf(currency,   sizeof(currency),   "%s", v ? v : "");
    v = db_col_text(stmt, 10); snprintf(period,     sizeof(period),     "%s", v ? v : "");
    v = db_col_text(stmt, 11); snprintf(source,     sizeof(source),     "%s", v ? v : "");
    v = db_col_text(stmt, 12); snprintf(source_url, sizeof(source_url), "%s", v ? v : "");
    v = db_col_text(stmt, 17); snprintf(fit_notes,  sizeof(fit_notes),  "%s", v ? v : "");
    v = db_col_text(stmt, 18); snprintf(notes,      sizeof(notes),      "%s", v ? v : "");
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Update Position #%d", id);
    cli_header(hdr);
    printf("  Press Enter to keep current value.\n\n");

    char new_dept[PM_BUF], new_role[PM_BUF], new_level[PM_BUF];
    char new_emp[PM_BUF], new_remote[PM_BUF], new_loc[PM_BUF], new_closing[PM_BUF];
    char new_currency[PM_BUF], new_period[PM_BUF];
    char new_source[PM_BUF], new_source_url[PM_BUF];
    char new_fit_notes[PM_BUF], new_notes[PM_BUF];

    cli_read_update("Department", dept, new_dept, PM_BUF);
    cli_read_update("Role", role, new_role, PM_BUF);
    cli_pick_update("Level", LEVEL, LEVEL_N, level_val, new_level, PM_BUF);
    cli_pick_update("Employment type", EMPLOYMENT_TYPE, EMPLOYMENT_TYPE_N, emp_type, new_emp, PM_BUF);
    cli_pick_update("Remote type", REMOTE_TYPE, REMOTE_TYPE_N, remote_type, new_remote, PM_BUF);
    cli_read_update("Job location", job_loc, new_loc, PM_BUF);
    cli_read_update("Closing date", closing, new_closing, PM_BUF);

    printf("\n  Salary:\n");
    cli_read_update("Currency", currency, new_currency, PM_BUF);
    cli_pick_update("Salary period", SALARY_PERIOD, SALARY_PERIOD_N,
		    period, new_period, PM_BUF);
    int new_sal_min = cli_read_salary_update("Minimum salary", sal_min);
    int new_sal_max = cli_read_salary_update("Maximum salary", sal_max);

    printf("\n  Source:\n");
    cli_read_update("Source", source, new_source, PM_BUF);
    cli_read_update("Source URL", source_url, new_source_url, PM_BUF);

    printf("\n  Fit Scores (1-5, 0=clear, Enter=keep):\n");
    int new_skill_fit = cli_read_fit_update("Skill fit",
		                            skill_fit);
    int new_comp_fit  = cli_read_fit_update("Compensation fit",
		                             comp_fit);
    int new_loc_fit   = cli_read_fit_update("Location fit",
		                            loc_fit);
    int new_lvl_fit   = cli_read_fit_update("Level fit",
		                            lvl_fit);
    cli_read_update("Fit notes", fit_notes, new_fit_notes, PM_BUF);

    printf("\n");
    cli_read_update("Notes", notes, new_notes, PM_BUF);

    db_exec(db,
        "UPDATE positions SET "
        "department=?, role=?, level=?, employment_type=?, remote_type=?, "
        "job_location=?, closing_date=?, salary_min=?, salary_max=?, "
        "salary_currency=?, salary_period=?, source=?, source_url=?, "
        "skill_fit=?, compensation_fit=?, location_fit=?, level_fit=?, "
        "fit_notes=?, notes=? WHERE id=?",
        DB_TEXT, new_dept,
        DB_TEXT, new_role,
        DB_TEXT, new_level,
        DB_TEXT, new_emp,
        DB_TEXT, new_remote,
        DB_TEXT, new_loc,
        DB_TEXT, new_closing,
        new_sal_min >= 0 ? DB_INT : DB_NULL, new_sal_min,
        new_sal_max >= 0 ? DB_INT : DB_NULL, new_sal_max,
        DB_TEXT, new_currency,
        DB_TEXT, new_period,
        DB_TEXT, new_source,
        DB_TEXT, new_source_url,
        new_skill_fit > 0 ? DB_INT : DB_NULL, new_skill_fit,
        new_comp_fit  > 0 ? DB_INT : DB_NULL, new_comp_fit,
        new_loc_fit   > 0 ? DB_INT : DB_NULL, new_loc_fit,
        new_lvl_fit   > 0 ? DB_INT : DB_NULL, new_lvl_fit,
        DB_TEXT, new_fit_notes,
        DB_TEXT, new_notes,
        DB_INT,  id,
        DB_END);

    printf("\nPosition #%d updated.\n", id);
}


void cmd_position_delete(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT role FROM positions WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Position #%d not found.\n", id);
        return;
    }
    printf("\nPosition #%d: %s\n", id, cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    if (!cli_confirm("Delete this position?")) { printf("Cancelled.\n"); return; }

    backup_before_delete(db);
    db_exec(db, "DELETE FROM positions WHERE id = ?", DB_INT, id, DB_END);
    printf("Position #%d deleted.\n", id);
}


/* ================================================================
 * APPLICATIONS
 * ================================================================ */

void cmd_app_add(DB *db)
{
    cli_header("Add Application");

    int org_id = pick_org(db);
    if (org_id == 0) return;

    int pos_id = pick_position(db, org_id);
    if (pos_id == 0) return;

    /* Date: default today */
    char date[PM_BUF], default_date[11];
    char discovery[PM_BUF], channel[PM_BUF];
    today_str(default_date, sizeof(default_date));
    printf("\n");
    cli_read_date("Date applied", default_date, date, sizeof(date));
    cli_pick_opt("Discovery source", DISCOVERY_SOURCE, DISCOVERY_SOURCE_N, discovery, PM_BUF);
    cli_pick_opt("Application channel", APP_CHANNEL, APP_CHANNEL_N, channel, PM_BUF);

    printf("\n  Documents Submitted:\n");
    int resume   = cli_read_bool("  Resume");
    int portfolio= cli_read_bool("  Portfolio");
    int cover    = cli_read_bool("  Cover letter");
    int cnic     = cli_read_bool("  CNIC");
    int domicile = cli_read_bool("  Domicile");
    int birth    = cli_read_bool("  Birth certificate");
    int matric   = cli_read_bool("  Matric certificate");
    int exp_let  = cli_read_bool("  Experience letter");

    char misc_docs[PM_BUF], req_docs[PM_BUF], follow_up[PM_BUF], notes[PM_BUF];
    cli_read_opt("Misc documents (anything else submitted)", misc_docs, PM_BUF);
    cli_read_opt("Documents requested by org", req_docs, PM_BUF);
    cli_read_opt("Follow-up date (YYYY-MM-DD)", follow_up, PM_BUF);
    cli_read_opt("Notes", notes, PM_BUF);

    db_exec(db,
        "INSERT INTO applications "
        "(date, position_id, org_id, current_status, "
        " discovery_source, application_channel, "
        " resume_given, portfolio_given, cover_letter_given, cnic_given, "
        " domicile_given, birth_cert_given, matric_cert_given, "
        " experience_letter_given, misc_documents, requested_documents, "
        " follow_up_date, notes) "
        "VALUES (?, ?, ?, 'Applied', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        DB_TEXT, date,
        DB_INT,  pos_id,
        DB_INT,  org_id,
        DB_TEXT, discovery,
        DB_TEXT, channel,
        DB_INT,  resume,
        DB_INT,  portfolio,
        DB_INT,  cover,
        DB_INT,  cnic,
        DB_INT,  domicile,
        DB_INT,  birth,
        DB_INT,  matric,
        DB_INT,  exp_let,
        DB_TEXT, misc_docs,
        DB_TEXT, req_docs,
        DB_TEXT, follow_up,
        DB_TEXT, notes,
        DB_END);

    int app_id = db_last_id(db);

    /* Record initial status in history */
    db_exec(db,
        "INSERT INTO status_history (application_id, status, date, note) "
        "VALUES (?, 'Applied', ?, '')",
        DB_INT, app_id, DB_TEXT, date, DB_END);

    printf("\nApplication #%d added. Status: Applied\n", app_id);

    /* Offer to link contacts */
    printf("\n  Contacts at this org:\n");
    sqlite3_stmt *stmt = db_query(db,
        "SELECT id, name, position_title FROM contacts WHERE org_id = ? ORDER BY id",
        DB_INT, org_id, DB_END);

    int has_contacts = 0;
    while (db_row(stmt)) {
        has_contacts = 1;
        printf("    [%d] %s - %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);

    if (has_contacts) {
        while (cli_confirm("Link a contact to this application?")) {
            int cid = cli_read_int("Contact ID");
            db_exec(db,
                "INSERT OR IGNORE INTO application_contacts "
                "(application_id, contact_id) VALUES (?, ?)",
                DB_INT, app_id, DB_INT, cid, DB_END);
            printf("  Contact #%d linked.\n", cid);
        }
    }
}


void cmd_app_list(DB *db, int org_id, const char *status)
{
    const char *base =
        "SELECT a.id, a.date, o.name, p.role, p.level, "
        "       a.current_status, a.follow_up_date "
        "FROM applications a "
        "JOIN organizations o ON a.org_id = o.id "
        "JOIN positions p ON a.position_id = p.id";

    char sql[1024];
    sqlite3_stmt *stmt;
    int has_status = (status && *status);

    if (org_id > 0 && has_status) {
        snprintf(sql, sizeof(sql), "%s WHERE a.org_id=? AND a.current_status=? ORDER BY a.date DESC", base);
        stmt = db_query(db, sql, DB_INT, org_id, DB_TEXT, status, DB_END);
    } else if (org_id > 0) {
        snprintf(sql, sizeof(sql), "%s WHERE a.org_id=? ORDER BY a.date DESC", base);
        stmt = db_query(db, sql, DB_INT, org_id, DB_END);
    } else if (has_status) {
        snprintf(sql, sizeof(sql), "%s WHERE a.current_status=? ORDER BY a.date DESC", base);
        stmt = db_query(db, sql, DB_TEXT, status, DB_END);
    } else {
        snprintf(sql, sizeof(sql), "%s ORDER BY a.date DESC", base);
        stmt = db_query(db, sql, DB_END);
    }

    printf("\n%-4s  %-10s  %-22s  %-18s  %-8s  %-20s  %s\n",
           "ID", "Date", "Organization", "Role", "Level",
           "Status", "Follow-up");
    printf("%-4s  %-10s  %-22s  %-18s  %-8s  %-20s  %s\n",
           "----", "----------", "----------------------",
           "------------------", "--------",
           "--------------------", "----------");

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("%-4d  %-10s  %-22s  %-18s  %-8s  %-20s  %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)),
               cli_safe(db_col_text(stmt, 3)),
               cli_safe(db_col_text(stmt, 4)),
               cli_safe(db_col_text(stmt, 5)),
               cli_safe(db_col_text(stmt, 6)));
    }
    db_done(stmt);

    if (!found) printf("  No applications found.\n");
}


void cmd_app_show(DB *db, int id)
{
    /*
     * Column order (0-based):
     *  0  a.id              8  a.portfolio_given      17  a.misc_documents
     *  1  a.date            9  a.cover_letter_given   18  a.requested_documents
     *  2  o.name           10  a.cnic_given           19  a.follow_up_date
     *  3  p.role           11  a.domicile_given       20  a.notes
     *  4  p.level          12  a.birth_cert_given     21  a.created_at
     *  5  p.department     13  a.matric_cert_given
     *  6  a.current_status 14  a.experience_letter_given
     *  7  a.resume_given   15  a.discovery_source
     *                      16  a.application_channel
     */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT a.id, a.date, o.name, p.role, p.level, p.department, "
        "       a.current_status, "
        "       a.resume_given, a.portfolio_given, a.cover_letter_given, "
        "       a.cnic_given, a.domicile_given, a.birth_cert_given, "
        "       a.matric_cert_given, a.experience_letter_given, "
        "       a.discovery_source, a.application_channel, "
        "       a.misc_documents, a.requested_documents, "
        "       a.follow_up_date, a.notes, a.created_at "
        "FROM applications a "
        "JOIN organizations o ON a.org_id = o.id "
        "JOIN positions p ON a.position_id = p.id "
        "WHERE a.id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Application #%d not found.\n", id);
        return;
    }

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Application #%d", db_col_int(stmt, 0));
    cli_header(hdr);
    cli_field("Date Applied:",       cli_safe(db_col_text(stmt, 1)));
    cli_field("Organization:",       cli_safe(db_col_text(stmt, 2)));

    char pos_str[PM_BUF];
    snprintf(pos_str, sizeof(pos_str), "%s (%s) - %s",
             cli_safe(db_col_text(stmt, 3)),
             cli_safe(db_col_text(stmt, 4)),
             cli_safe(db_col_text(stmt, 5)));
    cli_field("Position:",           pos_str);
    cli_field("Status:",             cli_safe(db_col_text(stmt, 6)));
    cli_field("Discovery Source:",   cli_safe(db_col_text(stmt, 15)));
    cli_field("Applied Via:",        cli_safe(db_col_text(stmt, 16)));
    cli_field("Follow-up:",          cli_safe(db_col_text(stmt, 19)));
    cli_field("Notes:",              cli_safe(db_col_text(stmt, 20)));
    cli_field("Added:",              cli_safe(db_col_text(stmt, 21)));

    printf("\n  Documents Submitted:\n");
    print_doc("Resume",             db_col_int(stmt, 7));
    print_doc("Portfolio",          db_col_int(stmt, 8));
    print_doc("Cover Letter",       db_col_int(stmt, 9));
    print_doc("CNIC",               db_col_int(stmt, 10));
    print_doc("Domicile",           db_col_int(stmt, 11));
    print_doc("Birth Certificate",  db_col_int(stmt, 12));
    print_doc("Matric Certificate", db_col_int(stmt, 13));
    print_doc("Experience Letter",  db_col_int(stmt, 14));

    const char *misc = db_col_text(stmt, 17);
    if (misc && *misc) printf("    Misc: %s\n", misc);

    const char *req = db_col_text(stmt, 18);
    if (req && *req) printf("\n  Requested by Org:\n    %s\n", req);
    db_done(stmt);

    /* Linked contacts */
    printf("\nLinked Contacts:\n");
    stmt = db_query(db,
        "SELECT c.id, c.name, c.position_title "
        "FROM contacts c "
        "JOIN application_contacts ac ON ac.contact_id = c.id "
        "WHERE ac.application_id = ? ORDER BY c.id",
        DB_INT, id, DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        printf("  [%d] %s - %s\n",
               db_col_int(stmt, 0),
               cli_safe(db_col_text(stmt, 1)),
               cli_safe(db_col_text(stmt, 2)));
    }
    db_done(stmt);
    if (!found) printf("  (none)\n");
    printf("\n");
}


void cmd_app_update(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT a.date, "
        "       a.resume_given, a.portfolio_given, a.cover_letter_given, "
        "       a.cnic_given, a.domicile_given, a.birth_cert_given, "
        "       a.matric_cert_given, a.experience_letter_given, "
        "       a.misc_documents, a.requested_documents, "
        "       a.follow_up_date, a.notes, "
        "       a.discovery_source, a.application_channel "
        "FROM applications a WHERE a.id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Application #%d not found.\n", id);
        return;
    }

    char date[PM_BUF], misc[PM_BUF], req_docs[PM_BUF];
    char follow_up[PM_BUF], notes[PM_BUF];
    char discovery[PM_BUF], channel[PM_BUF];
    int resume   = db_col_int(stmt, 1);
    int portfolio= db_col_int(stmt, 2);
    int cover    = db_col_int(stmt, 3);
    int cnic     = db_col_int(stmt, 4);
    int domicile = db_col_int(stmt, 5);
    int birth    = db_col_int(stmt, 6);
    int matric   = db_col_int(stmt, 7);
    int exp_let  = db_col_int(stmt, 8);

    const char *v;
    v = db_col_text(stmt, 0 ); snprintf(date,      sizeof(date),      "%s", v ? v : "");
    v = db_col_text(stmt, 9 ); snprintf(misc,      sizeof(misc),      "%s", v ? v : "");
    v = db_col_text(stmt, 10); snprintf(req_docs,  sizeof(req_docs),  "%s", v ? v : "");
    v = db_col_text(stmt, 11); snprintf(follow_up, sizeof(follow_up), "%s", v ? v : "");
    v = db_col_text(stmt, 12); snprintf(notes,     sizeof(notes),     "%s", v ? v : "");
    v = db_col_text(stmt, 13); snprintf(discovery, sizeof(discovery), "%s", v ? v : "");
    v = db_col_text(stmt, 14); snprintf(channel,   sizeof(channel),   "%s", v ? v : "");
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Update Application #%d", id);
    cli_header(hdr);
    printf("  Press Enter to keep current value.\n\n");

    char new_date[PM_BUF], new_misc[PM_BUF], new_req[PM_BUF];
    char new_follow[PM_BUF], new_notes[PM_BUF];
    char new_discovery[PM_BUF], new_channel[PM_BUF];

    cli_read_update("Date applied", date, new_date, PM_BUF);
    cli_pick_update("Discovery source", DISCOVERY_SOURCE, DISCOVERY_SOURCE_N,
                    discovery, new_discovery, PM_BUF);
    cli_pick_update("Application channel", APP_CHANNEL, APP_CHANNEL_N,
                    channel, new_channel, PM_BUF);

    printf("\n  Documents Submitted:\n");
    int nr = cli_read_bool_update("  Resume",               resume);
    int np = cli_read_bool_update("  Portfolio",            portfolio);
    int nc = cli_read_bool_update("  Cover letter",         cover);
    int ni = cli_read_bool_update("  CNIC",                 cnic);
    int nd = cli_read_bool_update("  Domicile",             domicile);
    int nb = cli_read_bool_update("  Birth certificate",    birth);
    int nm = cli_read_bool_update("  Matric certificate",   matric);
    int ne = cli_read_bool_update("  Experience letter",    exp_let);

    printf("\n");
    cli_read_update("Misc documents", misc, new_misc, PM_BUF);
    cli_read_update("Requested documents", req_docs, new_req, PM_BUF);
    cli_read_update("Follow-up date", follow_up, new_follow, PM_BUF);
    cli_read_update("Notes", notes, new_notes, PM_BUF);

    db_exec(db,
        "UPDATE applications SET "
        "date=?, discovery_source=?, application_channel=?, "
        "resume_given=?, portfolio_given=?, cover_letter_given=?, "
        "cnic_given=?, domicile_given=?, birth_cert_given=?, matric_cert_given=?, "
        "experience_letter_given=?, misc_documents=?, requested_documents=?, "
        "follow_up_date=?, notes=? "
        "WHERE id=?",
        DB_TEXT, new_date,
        DB_TEXT, new_discovery,
        DB_TEXT, new_channel,
        DB_INT,  nr, DB_INT, np, DB_INT, nc, DB_INT, ni,
        DB_INT,  nd, DB_INT, nb, DB_INT, nm, DB_INT, ne,
        DB_TEXT, new_misc,
        DB_TEXT, new_req,
        DB_TEXT, new_follow,
        DB_TEXT, new_notes,
        DB_INT,  id,
        DB_END);

    printf("\nApplication #%d updated.\n", id);
}


void cmd_app_status(DB *db, int id)
{
    /* Verify exists and show current status */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT current_status FROM applications WHERE id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Application #%d not found.\n", id);
        return;
    }
    char current[PM_BUF];
    snprintf(current, sizeof(current), "%s", cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Update Status - Application #%d", id);
    cli_header(hdr);
    printf("  Current status: %s\n\n", current);

    char new_status[PM_BUF], note[PM_BUF], date[PM_BUF], default_date[11];
    today_str(default_date, sizeof(default_date));

    cli_pick("New status", APP_STATUS, APP_STATUS_N, new_status, PM_BUF);
    cli_read_opt("Note (optional)", note, PM_BUF);
    cli_read_date("Date", default_date, date, sizeof(date));

    /* Insert history entry */
    db_exec(db,
        "INSERT INTO status_history (application_id, status, date, note) "
        "VALUES (?, ?, ?, ?)",
        DB_INT, id, DB_TEXT, new_status, DB_TEXT, date, DB_TEXT, note, DB_END);

    /* Sync denormalised field */
    db_exec(db,
        "UPDATE applications SET current_status=? WHERE id=?",
        DB_TEXT, new_status, DB_INT, id, DB_END);

    printf("\nStatus updated: %s → %s\n", current, new_status);
}


void cmd_app_history(DB *db, int id)
{
    /* Verify exists */
    sqlite3_stmt *stmt = db_query(db,
        "SELECT a.date, o.name, p.role "
        "FROM applications a "
        "JOIN organizations o ON a.org_id = o.id "
        "JOIN positions p ON a.position_id = p.id "
        "WHERE a.id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Application #%d not found.\n", id);
        return;
    }
    char hdr[PM_BUF * 2];
    snprintf(hdr, sizeof(hdr), "Status History - Application #%d (%s at %s)",
             id, cli_safe(db_col_text(stmt, 2)), cli_safe(db_col_text(stmt, 1)));
    cli_header(hdr);
    db_done(stmt);

    stmt = db_query(db,
        "SELECT date, status, note FROM status_history "
        "WHERE application_id = ? ORDER BY date ASC, id ASC",
        DB_INT, id, DB_END);

    int found = 0;
    while (db_row(stmt)) {
        found = 1;
        const char *note = db_col_text(stmt, 2);
        printf("  %s  %s%s%s\n",
               cli_safe(db_col_text(stmt, 0)),
               cli_safe(db_col_text(stmt, 1)),
               (note && *note) ? "  - " : "",
               (note && *note) ? note   : "");
    }
    db_done(stmt);

    if (!found) printf("  No history found.\n");
    printf("\n");
}


void cmd_app_link_contact(DB *db, int app_id, int contact_id)
{
    db_exec(db,
        "INSERT OR IGNORE INTO application_contacts "
        "(application_id, contact_id) VALUES (?, ?)",
        DB_INT, app_id, DB_INT, contact_id, DB_END);
    printf("Contact #%d linked to application #%d.\n", contact_id, app_id);
}


void cmd_app_unlink_contact(DB *db, int app_id, int contact_id)
{
    db_exec(db,
        "DELETE FROM application_contacts "
        "WHERE application_id=? AND contact_id=?",
        DB_INT, app_id, DB_INT, contact_id, DB_END);
    printf("Contact #%d unlinked from application #%d.\n", contact_id, app_id);
}


void cmd_app_delete(DB *db, int id)
{
    sqlite3_stmt *stmt = db_query(db,
        "SELECT a.date, o.name, p.role "
        "FROM applications a "
        "JOIN organizations o ON a.org_id = o.id "
        "JOIN positions p ON a.position_id = p.id "
        "WHERE a.id = ?",
        DB_INT, id, DB_END);

    if (!db_row(stmt)) {
        db_done(stmt);
        printf("Application #%d not found.\n", id);
        return;
    }
    printf("\nApplication #%d: %s at %s (%s)\n",
           id,
           cli_safe(db_col_text(stmt, 2)),
           cli_safe(db_col_text(stmt, 1)),
           cli_safe(db_col_text(stmt, 0)));
    db_done(stmt);

    printf("WARNING: This deletes the application, its status history, "
           "and all contact links.\n");
    if (!cli_confirm("Are you sure?")) { printf("Cancelled.\n"); return; }

    backup_before_delete(db);
    db_exec(db, "DELETE FROM applications WHERE id=?", DB_INT, id, DB_END);
    printf("Application #%d deleted.\n", id);
}
