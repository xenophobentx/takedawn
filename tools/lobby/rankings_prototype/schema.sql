-- Proposed SQLite schema for a discussion-only ranking prototype. MIT.
-- Not integrated with takedawn-lobby; subject to maintainer approval.
-- PRAGMA foreign_keys = ON is REQUIRED on every connection.

CREATE TABLE profiles (
    profile_id          TEXT PRIMARY KEY CHECK(length(profile_id) = 32),
    display_name        TEXT NOT NULL CHECK(length(display_name) BETWEEN 1 AND 15),
    secret_verifier     BLOB NOT NULL UNIQUE CHECK(length(secret_verifier) = 32),
    points_total        INTEGER NOT NULL DEFAULT 0 CHECK(points_total >= 0),
    created_at_ms       INTEGER NOT NULL CHECK(created_at_ms > 0)
);
-- display_name is deliberately not UNIQUE.

CREATE TABLE races (
    race_id             TEXT PRIMARY KEY CHECK(length(race_id) = 32),
    tk_gid              TEXT NOT NULL CHECK(length(tk_gid) = 12),
    token_hash          BLOB NOT NULL UNIQUE CHECK(length(token_hash) = 32),
    participants_total  INTEGER NOT NULL CHECK(participants_total BETWEEN 2 AND 8),
    started_at_ms       INTEGER NOT NULL CHECK(started_at_ms > 0),
    accept_until_ms     INTEGER NOT NULL CHECK(accept_until_ms > started_at_ms),
    status              TEXT NOT NULL DEFAULT 'pending'
                        CHECK(status IN ('pending','confirmed','disputed','expired')),
    agreed_report_hash  BLOB CHECK(agreed_report_hash IS NULL OR length(agreed_report_hash)=32),
    finalized_at_ms     INTEGER,
    CHECK((status = 'confirmed') = (agreed_report_hash IS NOT NULL))
);
-- Race ID is a separate server-generated 128-bit random ID; TKGID is NOT
-- the durable database primary key (it is only 48-bit and resets across runs).

CREATE TABLE race_participants (
    race_id             TEXT NOT NULL REFERENCES races(race_id) ON DELETE RESTRICT,
    participant_id      TEXT NOT NULL CHECK(length(participant_id) = 32),
    slot                INTEGER NOT NULL CHECK(slot BETWEEN 0 AND 7),
    profile_id          TEXT REFERENCES profiles(profile_id) ON DELETE RESTRICT,
    PRIMARY KEY (race_id, participant_id),
    UNIQUE (race_id, slot),
    UNIQUE (race_id, profile_id)
);
-- NULL profile_id is reserved for legacy/unidentified or explicitly unranked
-- participants, IF supported. Normal guest play may create a pseudonymous
-- profile automatically under the maintainer's planned secret scheme.

CREATE TABLE race_reports (
    race_id             TEXT NOT NULL,
    reporter_id         TEXT NOT NULL,
    report_hash         BLOB NOT NULL CHECK(length(report_hash) = 32),
    canonical_json      TEXT NOT NULL CHECK(length(canonical_json) BETWEEN 2 AND 4096),
    received_at_ms      INTEGER NOT NULL CHECK(received_at_ms > 0),
    PRIMARY KEY (race_id, reporter_id),
    FOREIGN KEY (race_id, reporter_id)
      REFERENCES race_participants(race_id, participant_id) ON DELETE RESTRICT
);

CREATE INDEX idx_races_status_deadline ON races(status, accept_until_ms);
CREATE INDEX idx_reports_race_hash ON race_reports(race_id, report_hash);
CREATE INDEX idx_participants_profile ON race_participants(profile_id);
