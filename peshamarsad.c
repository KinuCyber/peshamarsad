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
 * peshamarsad.c - entry point
 *
 * Parses argv, opens the database, routes to the correct cmd_* function,
 * closes the database, and exits.
 *
 * Argument structure:
 *   peshamarsad [--db <path>] <noun> <verb> [args...] [--flags...]
 *
 *   --db <path>   path to SQLite file; defaults to ./peshamarsad.db
 *   noun          org | review | contact | position | app
 *   verb          add | list | show | update | delete | ...
 *   args          positional integers (IDs), consumed in order
 *   --org <id>    filter by organization (app list, contact list, position list)
 *   --status <s>  filter by status (app list)
 *
 * Routing is a simple chain of strcmp comparisons. No dispatch table --
 * the command set is small enough that clarity beats cleverness here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "db.h"
#include "commands.h"

#define PM_VERSION "0.2.0-alpha"

/* ------------------------------------------------------------------
 * usage
 *
 * Print command reference and exit. Called when the user passes no
 * arguments or an unrecognised noun/verb.
 * ------------------------------------------------------------------ */
static void usage(void)
{
    printf(
    /* --- GPL notice (required for interactive programs under GPL v3) --- */
        "\n"
        "    peshamarsad  Copyright (C) 2026  KinuCyber\n"
        "    This program comes with ABSOLUTELY NO WARRANTY; "
        "    for details type `show w'.\n"
        "    This is free software, and you are welcome to redistribute it\n"
        "    under certain conditions; type `show c' for details.\n"
        "\n"
        "peshamarsad v" PM_VERSION "\n"
        "Usage: peshamarsad [--db <path>] <command>\n"
        "\n"
        "Organizations:\n"
        "  org add\n"
        "  org list\n"
        "  org show <id>\n"
        "  org update <id>\n"
        "  org delete <id>\n"
        "  org link-add <org_id>\n"
        "  org link-list <org_id>\n"
        "\n"
        "Reviews:\n"
        "  review add <org_id>\n"
        "  review list <org_id>\n"
        "\n"
        "Contacts:\n"
        "  contact add\n"
        "  contact list [--org <id>]\n"
        "  contact show <id>\n"
        "  contact update <id>\n"
        "  contact update-personality <id>\n"
        "  contact update-attitude <id>\n"
        "  contact method-add <contact_id>\n"
        "  contact method-list <contact_id>\n"
        "  contact delete <id>\n"
        "\n"
        "Positions:\n"
        "  position add\n"
        "  position list [--org <id>]\n"
        "  position show <id>\n"
        "  position update <id>\n"
        "  position delete <id>\n"
        "\n"
        "Applications:\n"
        "  app add\n"
        "  app list [--org <id>] [--status <status>]\n"
        "  app show <id>\n"
        "  app update <id>\n"
        "  app status <id>\n"
        "  app history <id>\n"
        "  app link-contact <app_id> <contact_id>\n"
        "  app unlink-contact <app_id> <contact_id>\n"
        "  app delete <id>\n"
        "\n"
        "If --db is omitted, defaults to ./peshamarsad.db\n"
    );
}


/* ------------------------------------------------------------------
 * require_id
 *
 * Print an error and return 0 if id is 0 (not provided or invalid).
 * Returns 1 if id is valid so callers can do: if (!require_id(...)) return 1;
 * ------------------------------------------------------------------ */
static int require_id(int id, const char *usage_hint)
{
    if (id == 0) {
        fprintf(stderr, "peshamarsad: %s\n", usage_hint);
        return 0;
    }
    return 1;
}


/* ------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    const char *db_path = "peshamarsad.db";
    int i = 1;

    /* --- parse --db flag --- */
    if (i < argc && strcmp(argv[i], "--db") == 0) {
        if (i + 1 >= argc) {
            fprintf(stderr, "peshamarsad: --db requires a path\n");
            return 1;
        }
        db_path = argv[++i];
        i++;
    }

    if (i >= argc) {
        usage();
        return 1;
    }

    /* --- open database --- */
    DB db = {0};
    db_open(&db, db_path);

    /* --- consume noun and verb --- */
    const char *noun = argv[i++];
    const char *verb = (i < argc) ? argv[i++] : NULL;

    if (!verb) {
        fprintf(stderr, "peshamarsad: expected a command after '%s'\n", noun);
        fprintf(stderr, "Run peshamarsad with no arguments for usage.\n");
        db_close(&db);
        return 1;
    }

    /* --- parse remaining positional args and flags ---
     *
     * Positional integers are consumed into arg1, arg2 in order.
     * Flags (--org, --status) can appear anywhere after the verb.
     * Unrecognised tokens are silently skipped.
     */
    int  arg1 = 0, arg2 = 0;
    int  filter_org = 0;
    char filter_status[PM_BUF] = "";

    while (i < argc) {
        if (strcmp(argv[i], "--org") == 0 && i + 1 < argc) {
            filter_org = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--status") == 0 && i + 1 < argc) {
            snprintf(filter_status, sizeof(filter_status), "%s", argv[++i]);
        } else if (arg1 == 0) {
            arg1 = atoi(argv[i]);   /* first positional integer */
        } else if (arg2 == 0) {
            arg2 = atoi(argv[i]);   /* second positional integer */
        }
        i++;
    }

    int ok = 1;   /* set to 0 on unrecognised command to print usage hint */

    /* ================================================================
     * ROUTING
     * ================================================================ */

    if (strcmp(noun, "org") == 0) {

        if      (strcmp(verb, "add")    == 0) cmd_org_add(&db);
        else if (strcmp(verb, "list")   == 0) cmd_org_list(&db);
        else if (strcmp(verb, "show")   == 0) {
            if (require_id(arg1, "usage: org show <id>"))
                cmd_org_show(&db, arg1);
        }
        else if (strcmp(verb, "update") == 0) {
            if (require_id(arg1, "usage: org update <id>"))
                cmd_org_update(&db, arg1);
        }
        else if (strcmp(verb, "delete") == 0) {
            if (require_id(arg1, "usage: org delete <id>"))
                cmd_org_delete(&db, arg1);
        }
        else if (strcmp(verb, "link-add") == 0) {
            if (require_id(arg1, "usage: org link-add <org_id>"))
                cmd_org_link_add(&db, arg1);
        }
        else if (strcmp(verb, "link-list") == 0) {
            if (require_id(arg1, "usage: org link-list <org_id>"))
                cmd_org_link_list(&db, arg1);
        }
        else ok = 0;

    } else if (strcmp(noun, "review") == 0) {

        if      (strcmp(verb, "add")  == 0) {
            if (require_id(arg1, "usage: review add <org_id>"))
                cmd_review_add(&db, arg1);
        }
        else if (strcmp(verb, "list") == 0) {
            if (require_id(arg1, "usage: review list <org_id>"))
                cmd_review_list(&db, arg1);
        }
        else ok = 0;

    } else if (strcmp(noun, "contact") == 0) {

        if      (strcmp(verb, "add")                == 0) cmd_contact_add(&db);
        else if (strcmp(verb, "list")               == 0) cmd_contact_list(&db, filter_org);
        else if (strcmp(verb, "show")               == 0) {
            if (require_id(arg1, "usage: contact show <id>"))
                cmd_contact_show(&db, arg1);
        }
        else if (strcmp(verb, "update")             == 0) {
            if (require_id(arg1, "usage: contact update <id>"))
                cmd_contact_update(&db, arg1);
        }
        else if (strcmp(verb, "update-personality") == 0) {
            if (require_id(arg1, "usage: contact update-personality <id>"))
                cmd_contact_update_personality(&db, arg1);
        }
        else if (strcmp(verb, "update-attitude")    == 0) {
            if (require_id(arg1, "usage: contact update-attitude <id>"))
                cmd_contact_update_attitude(&db, arg1);
        }
        else if (strcmp(verb, "delete")             == 0) {
            if (require_id(arg1, "usage: contact delete <id>"))
                cmd_contact_delete(&db, arg1);
        }
        else if (strcmp(verb, "method-add")         == 0) {
            if (require_id(arg1, "usage: contact method-add <contact_id>"))
                cmd_contact_method_add(&db, arg1);
        }
        else if (strcmp(verb, "method-list")        == 0) {
            if (require_id(arg1, "usage: contact method-list <contact_id>"))
                cmd_contact_method_list(&db, arg1);
        }
        else ok = 0;

    } else if (strcmp(noun, "position") == 0) {

        if      (strcmp(verb, "add")    == 0) cmd_position_add(&db);
        else if (strcmp(verb, "list")   == 0) cmd_position_list(&db, filter_org);
        else if (strcmp(verb, "show")   == 0) {
            if (require_id(arg1, "usage: position show <id>"))
                cmd_position_show(&db, arg1);
        }
        else if (strcmp(verb, "update") == 0) {
            if (require_id(arg1, "usage: position update <id>"))
                cmd_position_update(&db, arg1);
        }
        else if (strcmp(verb, "delete") == 0) {
            if (require_id(arg1, "usage: position delete <id>"))
                cmd_position_delete(&db, arg1);
        }
        else ok = 0;

    } else if (strcmp(noun, "app") == 0) {

        if      (strcmp(verb, "add")            == 0) cmd_app_add(&db);
        else if (strcmp(verb, "list")           == 0) {
            cmd_app_list(&db, filter_org,
                         str_empty(filter_status) ? NULL : filter_status);
        }
        else if (strcmp(verb, "show")           == 0) {
            if (require_id(arg1, "usage: app show <id>"))
                cmd_app_show(&db, arg1);
        }
        else if (strcmp(verb, "update")         == 0) {
            if (require_id(arg1, "usage: app update <id>"))
                cmd_app_update(&db, arg1);
        }
        else if (strcmp(verb, "status")         == 0) {
            if (require_id(arg1, "usage: app status <id>"))
                cmd_app_status(&db, arg1);
        }
        else if (strcmp(verb, "history")        == 0) {
            if (require_id(arg1, "usage: app history <id>"))
                cmd_app_history(&db, arg1);
        }
        else if (strcmp(verb, "link-contact")   == 0) {
            if (require_id(arg1, "usage: app link-contact <app_id> <contact_id>") &&
                require_id(arg2, "usage: app link-contact <app_id> <contact_id>"))
                cmd_app_link_contact(&db, arg1, arg2);
        }
        else if (strcmp(verb, "unlink-contact") == 0) {
            if (require_id(arg1, "usage: app unlink-contact <app_id> <contact_id>") &&
                require_id(arg2, "usage: app unlink-contact <app_id> <contact_id>"))
                cmd_app_unlink_contact(&db, arg1, arg2);
        }
        else if (strcmp(verb, "delete")         == 0) {
            if (require_id(arg1, "usage: app delete <id>"))
                cmd_app_delete(&db, arg1);
        }
        else ok = 0;

    } else if (strcmp(noun, "show") == 0) {

        if (strcmp(verb, "w") == 0) {
            printf(
                "WARRANTY DISCLAIMER\n"
                "----------------------------------------\n"
                "THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED\n"
                "BY APPLICABLE LAW. EXCEPT WHEN OTHERWISE STATED IN WRITING,\n"
                "THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM\n"
                "\"AS IS\" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR\n"
                "IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES\n"
                "OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE\n"
                "ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM\n"
                "IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME\n"
                "THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.\n"
                "\n"
                "See sections 15-16 of the GNU General Public License v3 for\n"
                "the full warranty disclaimer.\n"
            );
        } else if (strcmp(verb, "c") == 0) {
            printf(
                "CONDITIONS\n"
                "----------------------------------------\n"
                "peshamarsad is free software: you can redistribute it and/or\n"
                "modify it under the terms of the GNU General Public License as\n"
                "published by the Free Software Foundation, either version 3 of\n"
                "the License, or (at your option) any later version.\n"
                "\n"
                "You may copy, distribute and modify the software as long as you\n"
                "track changes/dates in source files. Any modifications to or\n"
                "software including (via compiler) GPL-licensed code must also be\n"
                "made available under the GPL, along with build & install\n"
                "instructions.\n"
                "\n"
                "Full license text: <https://www.gnu.org/licenses/gpl-3.0.html>\n"
                "Source repository: <https://github.com/KinuCyber/peshamarsad>\n"
            );
        } else ok = 0;

    } else {
        ok = 0;
    }

    if (!ok) {
        fprintf(stderr,
                "peshamarsad: unknown command '%s %s'\n"
                "Run peshamarsad with no arguments for usage.\n",
                noun, verb);
        db_close(&db);
        return 1;
    }

    db_close(&db);
    return 0;
}
