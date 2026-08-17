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
-- Schema version: 2 (tracked via PRAGMA user_version at end of file)
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
-- Contact relation values:
--   Recruiter, Hiring Manager, Employee, Manager, Executive,
--   Alumni, Friend, Former Colleague, Cold Contact, Reference
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
-- Organization type:
--   University, School, NGO, Startup, Corporation,
--   Government, Private Academy, Military
--
-- Position level:
--   Intern, Entry, Junior, Mid, Senior, Managerial, Executive
--
-- Employment type:
--   Full-time, Part-time, Contract, Internship, Volunteer
--
-- Remote type:
--   Remote, Hybrid, On-site
--
-- Salary period:
--   Monthly, Yearly, Hourly, Daily
--
-- Discovery source (where you first encountered the opportunity):
--   LinkedIn, Glassdoor, Indeed, Rozee.pk, Company Website,
--   Referral, Recruiter, University Portal, Job Fair,
--   Direct Search, Other
--
-- Application channel (how you actually submitted):
--   Company Website, LinkedIn, Email, Recruiter,
--   Referral, University Portal, Hand Delivered, Other
--
-- Fit scores (positions): integer 1-5, or NULL if not yet assessed
--   1 = very poor fit   2 = poor fit   3 = moderate fit
--   4 = good fit        5 = excellent fit
--   Dimensions: skill_fit, compensation_fit, location_fit, level_fit
--   CHECK constraints provide a database-level backstop; C layer
--   validates before insert/update.
--
-- ------------------------------------------------------------


-- ------------------------------------------------------------
-- ORGANIZATIONS
-- ------------------------------------------------------------
CREATE TABLE organizations (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL,
    registered      BOOLEAN,            -- formally registered entity
    website         TEXT,               -- primary URL; kept as a direct column
                                        -- for convenience. Additional links
                                        -- (LinkedIn, Glassdoor, etc.) go in
                                        -- org_links
    industry        TEXT,               -- Education, Technology, Healthcare, etc.
    org_type        TEXT,               -- University, School, NGO, Startup, etc.
    security_level  TEXT,               -- Low, Medium, High
    size            TEXT,               -- see size enum above
    notes           TEXT,
    created_at      TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- ORG LINKS
-- Social media, job boards, registries, and any other URLs
-- associated with an organization. Replaces the would-be
-- social_media TEXT column -- a separate table handles unlimited
-- link types cleanly without schema changes per new platform.
--
-- type is free text with examples: LinkedIn, GitHub, Glassdoor,
-- Careers, Crunchbase, Registry, Instagram, X, Facebook, etc.
-- ------------------------------------------------------------
CREATE TABLE org_links (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    org_id      INTEGER NOT NULL REFERENCES organizations(id)
                    ON DELETE CASCADE,
    type        TEXT NOT NULL,   -- LinkedIn, Glassdoor, Careers, etc.
    label       TEXT,            -- optional human-readable label
    url         TEXT NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (date('now'))
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
    relation            TEXT,           -- Recruiter, Hiring Manager, etc.
    personality         TEXT,           -- e.g. "Skeptic,Patron,Neutral"
    attitude            TEXT,           -- e.g. "Cautious,Friendly"
    leverage_potential  TEXT,           -- description-based assessment
    how_met             TEXT,           -- networking event, LinkedIn, etc.
    last_contact_date   TEXT,           -- YYYY-MM-DD
    notes               TEXT,
    created_at          TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- CONTACT METHODS
-- Email, phone, messaging handles per contact.
-- Replaces the would-be contact_info TEXT column -- same
-- reasoning as org_links: unlimited media types, clean
-- per-medium lookup, no column proliferation.
--
-- medium is free text with examples: email, phone, telegram,
-- whatsapp, linkedin, signal, etc.
-- address is the actual value: email address, number, handle.
-- ------------------------------------------------------------
CREATE TABLE contact_methods (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    contact_id  INTEGER NOT NULL REFERENCES contacts(id)
                    ON DELETE CASCADE,
    medium      TEXT NOT NULL,   -- email, phone, telegram, etc.
    address     TEXT NOT NULL,   -- the actual value
    created_at  TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- POSITIONS / ROLES
-- Describes a job opening at an organization.
-- Deliberately has no reference to applications (avoids circular
-- dependency). Applications reference positions, not vice versa.
--
-- job_location is the explicit position location and may differ
-- from the org's address: a Karachi company can advertise an
-- Islamabad role.
--
-- source_url captures the original listing URL, which tends to
-- disappear from job boards quickly after the role closes.
--
-- Fit scores are personal assessments (Layer C), not objective
-- properties of the role. NULL means not yet assessed.
-- CHECK constraints are a database-level backstop; the C layer
-- validates before every insert and update.
-- ------------------------------------------------------------
CREATE TABLE positions (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    org_id              INTEGER NOT NULL REFERENCES organizations(id)
                            ON DELETE CASCADE,
    department          TEXT,                   -- IT, Creative, Admin, etc.
    role                TEXT,                   -- SOC Analyst, Technician, etc.
    level               TEXT,                   -- see level enum above
    employment_type     TEXT,                   -- Full-time, Part-time, etc.
    remote_type         TEXT,                   -- Remote, Hybrid, On-site
    job_location        TEXT,                   -- explicit position location
    closing_date        TEXT,                   -- YYYY-MM-DD, application deadline
    salary_min          INTEGER,                -- minimum salary
    salary_max          INTEGER,                -- maximum salary
    salary_currency     TEXT,                   -- ISO 4217 (PKR, USD, GBP, etc.)
    salary_period       TEXT,                   -- Monthly, Yearly, Hourly, Daily
    source              TEXT,                   -- where posting was found
    source_url          TEXT,                   -- original listing URL
    skill_fit           INTEGER                 -- skill match, 1-5 or NULL
                            CHECK (skill_fit IN (1,2,3,4,5)
                                   OR skill_fit IS NULL),
    compensation_fit    INTEGER                 -- salary/benefits alignment, 1-5 or NULL
                            CHECK (compensation_fit IN (1,2,3,4,5)
                                   OR compensation_fit IS NULL),
    location_fit        INTEGER                 -- location/remote fit, 1-5 or NULL
                            CHECK (location_fit IN (1,2,3,4,5)
                                   OR location_fit IS NULL),
    level_fit           INTEGER                 -- seniority alignment, 1-5 or NULL
                            CHECK (level_fit IN (1,2,3,4,5)
                                   OR level_fit IS NULL),
    fit_notes           TEXT,                   -- qualitative notes across all fit
                                                -- dimensions; one shared field avoids
                                                -- bloating column count
    notes               TEXT,
    created_at          TEXT NOT NULL DEFAULT (date('now'))
);


-- ------------------------------------------------------------
-- APPLICATIONS
--
-- current_status is denormalised from status_history for fast
-- overview queries. The CLI updates both atomically on every
-- status change -- they should never drift apart.
--
-- org_id is kept here directly (also reachable via position_id)
-- for convenient filtering of applications by organization.
--
-- discovery_source and application_channel are distinct per the
-- SHRM source-of-hire framework: you may discover a role on
-- LinkedIn but submit via the company's own website. Tracking
-- both enables accurate source-of-hire funnel analysis.
-- ------------------------------------------------------------
CREATE TABLE applications (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    date                    TEXT    NOT NULL,   -- YYYY-MM-DD, date applied
    position_id             INTEGER NOT NULL REFERENCES positions(id),
    org_id                  INTEGER NOT NULL REFERENCES organizations(id),
    current_status          TEXT    NOT NULL DEFAULT 'Applied',
    discovery_source        TEXT,               -- where you first found the role
    application_channel     TEXT,               -- how you actually submitted

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
-- New tables (org_links, contact_methods) indexed on their
-- foreign keys.
-- idx_addresses_org corrects the typo in v1 (idx_adresses_org).
-- ------------------------------------------------------------
CREATE INDEX idx_org_links_org          ON org_links(org_id);
CREATE INDEX idx_contact_methods_con    ON contact_methods(contact_id);
CREATE INDEX idx_reviews_org            ON reviews(org_id);
CREATE INDEX idx_addresses_org          ON addresses(org_id);
CREATE INDEX idx_contacts_org           ON contacts(org_id);
CREATE INDEX idx_positions_org          ON positions(org_id);
CREATE INDEX idx_applications_org       ON applications(org_id);
CREATE INDEX idx_applications_pos       ON applications(position_id);
CREATE INDEX idx_applications_status    ON applications(current_status);
CREATE INDEX idx_status_history_app     ON status_history(application_id);
CREATE INDEX idx_app_contacts_app       ON application_contacts(application_id);
CREATE INDEX idx_app_contacts_con       ON application_contacts(contact_id);


-- ------------------------------------------------------------
-- SCHEMA VERSION
-- Tracks the schema version for db_migrate in db.h.
-- Fresh databases are stamped here at creation time.
-- Existing v1 databases (user_version = 0) are migrated
-- by db_migrate before normal operation begins.
-- ------------------------------------------------------------
PRAGMA user_version = 2;
