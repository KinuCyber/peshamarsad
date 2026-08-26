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
 * commands.h - command declarations
 *
 * One function per CLI subcommand. All implementations live in commands.c.
 * peshamarsad.c parses argv and calls into these.
 *
 * Filter arguments use zero/NULL to mean "no filter":
 *   org_id == 0   means all organizations
 *   status == NULL means all statuses
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include "db.h"

/* ------------------------------------------------------------------
 * Organizations
 * ------------------------------------------------------------------ */
void cmd_org_add(DB *db);
void cmd_org_list(DB *db);
void cmd_org_show(DB *db, int id);
void cmd_org_update(DB *db, int id);
void cmd_org_delete(DB *db, int id);

/* ------------------------------------------------------------------
 * Org Links
 * ------------------------------------------------------------------ */
void cmd_org_link_add(DB *db, int org_id);
void cmd_org_link_list(DB *db, int org_id);

/* ------------------------------------------------------------------
 * Reviews
 * ------------------------------------------------------------------ */
void cmd_review_add(DB *db, int org_id);
void cmd_review_list(DB *db, int org_id);

/* ------------------------------------------------------------------
 * Contacts
 * ------------------------------------------------------------------ */
void cmd_contact_add(DB *db);
void cmd_contact_list(DB *db, int org_id);          /* 0 = all orgs        */
void cmd_contact_show(DB *db, int id);
void cmd_contact_update(DB *db, int id);
void cmd_contact_update_personality(DB *db, int id);
void cmd_contact_update_attitude(DB *db, int id);
void cmd_contact_delete(DB *db, int id);

/* ------------------------------------------------------------------
 * Contact Methods
 * ------------------------------------------------------------------ */
void cmd_contact_method_add(DB *db, int contact_id);
void cmd_contact_method_list(DB *db, int contact_id);

/* ------------------------------------------------------------------
 * Positions
 * ------------------------------------------------------------------ */
void cmd_position_add(DB *db);
void cmd_position_list(DB *db, int org_id);         /* 0 = all orgs        */
void cmd_position_show(DB *db, int id);
void cmd_position_update(DB *db, int id);
void cmd_position_delete(DB *db, int id);

/* ------------------------------------------------------------------
 * Applications
 * ------------------------------------------------------------------ */
void cmd_app_add(DB *db);
void cmd_app_list(DB *db, int org_id, const char *status); /* 0/NULL = no filter */
void cmd_app_show(DB *db, int id);
void cmd_app_update(DB *db, int id);
void cmd_app_status(DB *db, int id);
void cmd_app_history(DB *db, int id);
void cmd_app_link_contact(DB *db, int app_id, int contact_id);
void cmd_app_unlink_contact(DB *db, int app_id, int contact_id);
void cmd_app_delete(DB *db, int id);

#endif /* COMMANDS_H */
