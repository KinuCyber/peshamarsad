# peshamarsad
### پیشہ مرصد - Observatory of Profession

A personal, local-first CLI job-hunt tracker built in C with SQLite.
Tracks organizations, contacts, open positions, applications, documents
submitted, and the full status history of every application.

Part of the Kinu Cyber toolkit. Sits alongside
[Auraq](https://github.com/AuraqLabs) (pages) and
[Ilm Fehrist](https://github.com/KinuCyber/Ilm-Fehrist) (knowledge index).

---

## Design principles

- **Local-first.** Your data lives in a single SQLite file you control.
  No cloud, no account, no sync service.
- **Portable.** The database path is passed at runtime via `--db`.
  Run the same binary against different database files on different machines.
- **No hardcoded paths.** Works on any POSIX system - tested on
  Arch Linux (Kitty terminal) and Termux (Android, Redmi A2+).
- **Documentation-first.** Every non-obvious design decision is
  explained in comments inside `schema.sql`.

---

## Usage

```bash
# Pass any .sqlite file as the database
peshamarsad --db ~/documents/pesha.db <command>

# If --db is omitted, defaults to ./peshamarsad.db in the current directory
peshamarsad <command>
```

---

## Commands (planned)

### Organizations
```bash
peshamarsad org add
peshamarsad org list
peshamarsad org show <id>
peshamarsad org update <id>
peshamarsad org delete <id>
```

### Reviews
```bash
peshamarsad review add <org_id>
peshamarsad review list <org_id>
```

### Contacts
```bash
peshamarsad contact add
peshamarsad contact list [--org <org_id>]
peshamarsad contact show <id>
peshamarsad contact update <id>
peshamarsad contact update-personality <id>   # reorder/replace personality list
peshamarsad contact update-attitude <id>      # reorder/replace attitude list
peshamarsad contact delete <id>
```

### Positions
```bash
peshamarsad position add
peshamarsad position list [--org <org_id>]
peshamarsad position show <id>
peshamarsad position update <id>
peshamarsad position delete <id>
```

### Applications
```bash
peshamarsad app add
peshamarsad app list [--org <org_id>] [--status <status>]
peshamarsad app show <id>
peshamarsad app update <id>
peshamarsad app status <id>           # add a new status entry + update current_status
peshamarsad app history <id>          # full status timeline
peshamarsad app link-contact <app_id> <contact_id>
peshamarsad app unlink-contact <app_id> <contact_id>
peshamarsad app delete <id>
```

---

## Reference: allowed values

These are enforced at the C layer. Stored as plain TEXT in SQLite.

### Personality archetypes
Ordered, comma-separated per contact. First entry is primary.

| Archetype  | Description                                          |
|------------|------------------------------------------------------|
| Mentor     | Guides, shares knowledge, invested in your growth    |
| Gatekeeper | Rule-bound, process-first, judges by credentials     |
| Operator   | Task-focused, efficient, little personal interest    |
| Politician | Relationship-driven, reads room, self-preserving     |
| Skeptic    | Doubts first, needs proof, can become ally if earned |
| Patron     | Has power, uses it to sponsor people they like       |
| Climber    | Ambitious, transactional, useful if interests align  |
| Bureaucrat | Follows hierarchy strictly, risk-averse              |
| Ally       | Genuinely on your side, no hidden agenda             |
| Neutral    | No strong signal yet                                 |

### Attitude values
Ordered, comma-separated per contact. First entry is primary.

| Attitude   | Description                                     |
|------------|-------------------------------------------------|
| Welcoming  | Actively positive, opened doors                 |
| Friendly   | Warm, approachable, no friction                 |
| Nurturing  | Invested in helping you specifically            |
| Neutral    | No strong signal either way                     |
| Cautious   | Reserved, watching before deciding              |
| Suspicious | Guarded, reading you negatively                 |
| Hostile    | Actively negative or obstructive                |

### Application status values
```
Applied
No Response
Followed Up
Interview Scheduled
Offer
Rejected
Withdrawn
```

### Organization security level
```
Low, Medium, High
```

### Organization size
```
Micro (1-9)
Small (10-49)
Medium (50-249)
Large (250-999)
Enterprise (1000+)
Unknown
```

### Position level
```
Intern, Entry, Junior, Mid, Senior, Managerial, Executive
```

---

## Schema

See [`schema.sql`](schema.sql) for the full annotated database schema.

Tables:
- `organizations` - companies and institutions being tracked
- `reviews` - time-stamped reviews per organization
- `contacts` - employee profiles with ordered personality and attitude lists
- `positions` - job openings at organizations
- `applications` - applications submitted, with documents and follow-up date
- `application_contacts` - junction table linking contacts to applications
- `status_history` - full timeline of status changes per application

---

## Status history vs current_status

`applications.current_status` is a denormalised convenience field.
`status_history` is the authoritative record.

The CLI updates both atomically on every status change.
To query the current status independently:

```sql
SELECT status FROM status_history
WHERE application_id = ?
ORDER BY date DESC
LIMIT 1;
```

---

## Notes on personality and attitude fields

Both fields on the `contacts` table are ordered, comma-separated strings.

```
personality: "Skeptic,Patron"
attitude:    "Cautious,Friendly"
```

The CLI's `update-personality` and `update-attitude` commands display
the current list, accept a new comma-separated ordered input, validate
each entry, and replace the field atomically. This single-replacement
approach handles additions, removals, and reorderings in one step.

---

## Stack

- C
- SQLite 3.53.1 (amalgamation, embedded, no ORM)
- No external dependencies beyond the C standard library

---

##  Internal Dependencies

- SQLite (sqlite3.c, sqlite3.h)
  - Public domain. https://sqlite.org
  - Amalgamation version 3.53.1

---

## License

    peshamarsad - A personal local-first job-hunt tracker
    Copyright (C) 2026  KinuCyber <kinucyber@kinu.uk>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.


