-- peshamarsad - A personal local-first job-hunt tracker
-- Copyright (C) 2026  KinuCyber <kinucyber@kinu.uk>
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program.  If not, see <https://www.gnu.org/licenses/>.

-- ============================================================
-- peshamarsad
-- پیشہ مرصد - Profession Observatory
--
-- A personal CLI job-hunt tracker.
-- Database path is passed via --db at runtime. No hardcoded paths.
-- ============================================================

PRAGMA foreign_keys = ON;

-- ------------------------------------------------------------
-- REFERENCE: ALLOWED VALUES
-- Enforced at the C layer, documented here.
-- ------------------------------------------------------------
--
-- Personality archetypes (ordered, comma-separated per contact)
-- First entry = primary, subsequent = secondary, tertiary, etc.
--
--   Mentor      guides, shares knowledge, invested in your growth
--   Gatekeeper  rule-bound, process-first, judges by credentials
--   Operator    task-focused, efficient, little personal interest
--   Politician  relationship-driven, reads room, self-preserving
--   Skeptic     doubts first, needs proof, can become ally if earned
--   Patron      has power, uses it to sponsor people they like
--   Climber     ambitious, transactional, useful if interests align
--   Bureaucrat  follows hierarchy strictly, risk-averse
--   Ally        genuinely on your side, no hidden agenda
--   Neutral     no strong signal yet
--
-- Attitude values (ordered, comma-separated per contact)
-- First entry = primary, subsequent = secondary, tertiary, etc.
--
--   Welcoming   actively positive, opened doors
--   Friendly    warm, approachable, no friction
--   Nurturing   invested in helping you specifically
--   Neutral     no strong signal either way
--   Cautious    reserved, watching before deciding
--   Suspicious  guarded, reading you negatively
--   Hostile     actively negative or obstructive
--
-- Application status values:
--   Applied, No Response, Followed Up,
--   Interview Scheduled, Offer, Rejected, Withdrawn
--
-- Security levels (organization):
--   Low, Medium, High
--
-- Organization size:
--   Micro (1-9), Small (10-49), Medium (50-249),
--   Large (250-999), Enterprise (1000+), Unknown
--
-- Position level:
--   Intern, Entry, Junior, Mid, Senior, Managerial, Executive
--
-- ------------------------------------------------------------


-- ------------------------------------------------------------
-- ORGANIZATIONS
-- ------------------------------------------------------------
CREATE TABLE organizations (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL,
    registered      BOOLEAN,            -- formally registered entity
    website         TEXT,               -- URL or NULL
    has_social      BOOLEAN,            -- any social media presence
    security_level  TEXT,               -- Low, Medium, High
    size            TEXT,               -- see size enum above
    notes           TEXT,
    created_at      TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- REVIEWS
-- One organization can accumulate many reviews over time.
-- ------------------------------------------------------------
CREATE TABLE reviews (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    org_id      INTEGER NOT NULL REFERENCES organizations(id)
                    ON DELETE CASCADE,
    review      TEXT    NOT NULL,
    date        TEXT    NOT NULL,       -- YYYY-MM-DD
    source      TEXT,                   -- Glassdoor, personal, referral, etc.
    created_at  TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- ADDRESSES
-- One organization can have multiple addresses (branches, campuses).
-- Only city is required -- other fields use 'n/a' when not applicable.
-- Stored as structured fields rather than a single string to allow
-- per-field display and future filtering by city.
-- ------------------------------------------------------------
CREATE TABLE addresses (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    org_id        INTEGER NOT NULL REFERENCES organizations(id)
                      ON DELETE CASCADE,
    label         TEXT,               -- "Head Office", "B-17 Branch", etc.
    plot_number   TEXT,               -- plot or house number
    street_number TEXT,               -- street number
    block_number  TEXT,               -- block number
    sector        TEXT,               -- sector (e.g. B-17, F-7)
    city          TEXT    NOT NULL,   -- city is the only required field
    landmark      TEXT,               -- nearby landmark, free description
    created_at    TEXT NOT NULL DEFAULT (date('now'))
);
 
 
-- ------------------------------------------------------------
-- CONTACTS / EMPLOYEE PROFILES
-- Personality and attitude are ordered comma-separated strings.
-- First entry is primary, subsequent entries are secondary etc.
-- Full list replacement is used for reordering or updating.
-- ------------------------------------------------------------
CREATE TABLE contacts (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    name                TEXT    NOT NULL,
    age_range           TEXT,           -- "48", "30-45", "~50"
    org_id              INTEGER NOT NULL REFERENCES organizations(id)
                            ON DELETE CASCADE,
    position_title      TEXT,           -- their actual title, free text
    personality         TEXT,           -- e.g. "Skeptic,Patron,Neutral"
    attitude            TEXT,           -- e.g. "Cautious,Friendly"
    leverage_potential  TEXT,           -- description-based assessment
    notes               TEXT,
    created_at          TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- POSITIONS / ROLES
-- Describes a job opening at an organization.
-- Deliberately has no reference to applications (avoids circular
-- dependency). Applications reference positions, not vice versa.
-- ------------------------------------------------------------
CREATE TABLE positions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    org_id      INTEGER NOT NULL REFERENCES organizations(id)
                    ON DELETE CASCADE,
    department  TEXT,                   -- IT, Creative, Admin, etc.
    role        TEXT,                   -- SOC Analyst, Technician, Modeler
    level       TEXT,                   -- see level enum above
    my_fit      TEXT,                   -- personal assessment, description
    notes       TEXT,
    created_at  TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- APPLICATIONS
--
-- current_status is denormalised from status_history for fast
-- overview queries. The CLI updates both atomically on every
-- status change - they should never drift apart.
--
-- org_id is kept here directly (also reachable via position_id)
-- for convenient filtering of applications by organization.
-- ------------------------------------------------------------
CREATE TABLE applications (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    date                    TEXT    NOT NULL,   -- YYYY-MM-DD, date applied
    position_id             INTEGER NOT NULL REFERENCES positions(id),
    org_id                  INTEGER NOT NULL REFERENCES organizations(id),
    current_status          TEXT    NOT NULL DEFAULT 'Applied',

    -- Documents submitted by applicant
    resume_given            BOOLEAN DEFAULT 0,
    portfolio_given         BOOLEAN DEFAULT 0,
    cover_letter_given      BOOLEAN DEFAULT 0,
    cnic_given              BOOLEAN DEFAULT 0,
    domicile_given          BOOLEAN DEFAULT 0,
    birth_cert_given        BOOLEAN DEFAULT 0,
    matric_cert_given       BOOLEAN DEFAULT 0,
    experience_letter_given BOOLEAN DEFAULT 0,
    misc_documents          TEXT,               -- anything else, free text

    -- Documents the organization requested
    requested_documents     TEXT,               -- their requirements, free text

    follow_up_date          TEXT,               -- YYYY-MM-DD, next chase date
    notes                   TEXT,
    created_at              TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- APPLICATION <-> CONTACTS JUNCTION
-- Multiple contacts can be linked to a single application.
-- ------------------------------------------------------------
CREATE TABLE application_contacts (
    application_id  INTEGER NOT NULL REFERENCES applications(id)
                        ON DELETE CASCADE,
    contact_id      INTEGER NOT NULL REFERENCES contacts(id)
                        ON DELETE CASCADE,
    PRIMARY KEY (application_id, contact_id)
);


-- ------------------------------------------------------------
-- STATUS HISTORY
-- Full timeline of every status change per application.
-- Current status = most recent entry ordered by date DESC.
-- CLI mirrors the latest status into applications.current_status.
-- ------------------------------------------------------------
CREATE TABLE status_history (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    application_id  INTEGER NOT NULL REFERENCES applications(id)
                        ON DELETE CASCADE,
    status          TEXT    NOT NULL,   -- see status enum above
    date            TEXT    NOT NULL,   -- YYYY-MM-DD
    note            TEXT,
    created_at      TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- INDEXES
-- Covers the most common lookup and filter patterns.
-- ------------------------------------------------------------
CREATE INDEX idx_reviews_org         ON reviews(org_id);
CREATE INDEX idx_addresses_org       ON addresses(org_id);
CREATE INDEX idx_contacts_org        ON contacts(org_id);
CREATE INDEX idx_positions_org       ON positions(org_id);
CREATE INDEX idx_applications_org    ON applications(org_id);
CREATE INDEX idx_applications_pos    ON applications(position_id);
CREATE INDEX idx_applications_status ON applications(current_status);
CREATE INDEX idx_status_history_app  ON status_history(application_id);
CREATE INDEX idx_app_contacts_app    ON application_contacts(application_id);
CREATE INDEX idx_app_contacts_con    ON application_contacts(contact_id);
