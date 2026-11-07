"""Discussion-only, standard-library reference for SQLite ranking semantics.

NOT server integration, authentication, or anti-cheat. MIT.
The caller MUST authenticate a reporter; the `authenticated` test parameter
simulates this precondition. Do not use this module as a network endpoint.
"""
from collections import Counter
import hashlib
import hmac
import json
from pathlib import Path
import secrets
import sqlite3

SCHEMA = Path(__file__).with_name("schema.sql")


class InvalidReport(ValueError):
    pass


def canonical_report(entries, count):
    """Strict, illustrative all-finishers result vector (not final protocol)."""
    if not isinstance(entries, dict) or any(type(k) is not int for k in entries) or set(entries) != set(range(count)):
        raise InvalidReport("Report must contain each starting slot exactly once")
    places = set()
    rows = []
    for slot in range(count):
        d = entries[slot]
        if not isinstance(d, dict) or set(d) != {"place", "takedowns"}:
            raise InvalidReport("Only place and takedowns are modeled")
        place, td = d["place"], d["takedowns"]
        if type(place) is not int or not 1 <= place <= count or place in places:
            raise InvalidReport("Places must be a unique permutation of 1..N")
        if type(td) is not int or not 0 <= td <= 100000:
            raise InvalidReport("Takedown count out of prototype bounds")
        places.add(place)
        rows.append([slot, place, td])
    return json.dumps(rows, separators=(",", ":"))


class RankingPrototype:
    def __init__(self, connection):
        self.db = connection
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA foreign_keys=ON")
        if self.db.execute("PRAGMA foreign_keys").fetchone()[0] != 1:
            raise RuntimeError("SQLite foreign key support is required")

    @classmethod
    def memory(cls):
        db = sqlite3.connect(":memory:", isolation_level=None)
        obj = cls(db)
        obj.db.executescript(SCHEMA.read_text(encoding="utf-8"))
        return obj

    def create_profile(self, name, secret, now_ms):
        # Random 256-bit secret is assumed by the design; secret cannot be
        # chosen by a human. This SHA-256 verifier is NOT an auth protocol.
        if not isinstance(secret, bytes) or len(secret) != 32:
            raise ValueError("Expected a high-entropy 32-byte secret")
        profile_id = secrets.token_hex(16)
        self.db.execute("INSERT INTO profiles VALUES (?,?,?,?,?)",
                        (profile_id, name, hashlib.sha256(secret).digest(), 0, now_ms))
        return profile_id

    def create_race(self, profiles, token, tk_gid, now_ms, window_ms):
        if not 2 <= len(profiles) <= 8:
            raise ValueError("A ranked race needs 2..8 starters")
        if not isinstance(token, bytes) or len(token) != 32:
            raise ValueError("Expected 32-byte unpredictable race token")
        if not isinstance(window_ms, int) or window_ms <= 0:
            raise ValueError("Invalid acceptance window")
        race_id = secrets.token_hex(16)
        ids = [secrets.token_hex(16) for _ in profiles]
        self.db.execute("BEGIN IMMEDIATE")
        try:
            self.db.execute("INSERT INTO races (race_id,tk_gid,token_hash,participants_total,started_at_ms,accept_until_ms) VALUES (?,?,?,?,?,?)",
                            (race_id, tk_gid, hashlib.sha256(token).digest(), len(profiles), now_ms, now_ms+window_ms))
            for slot, (pid, profile) in enumerate(zip(ids, profiles)):
                self.db.execute("INSERT INTO race_participants VALUES (?,?,?,?)", (race_id, pid, slot, profile))
            self.db.execute("COMMIT")
        except Exception:
            if self.db.in_transaction:
                self.db.execute("ROLLBACK")
            raise
        return race_id, ids

    def state(self, race_id):
        row = self.db.execute("SELECT status FROM races WHERE race_id=?", (race_id,)).fetchone()
        if row is None:
            raise KeyError("Unknown race")
        return row["status"]

    def _evaluate(self, race_id, at_ms):
        race = self.db.execute("SELECT * FROM races WHERE race_id=?", (race_id,)).fetchone()
        if race["status"] != "pending":
            return race["status"]
        n = race["participants_total"]
        counts = Counter(row[0] for row in self.db.execute("SELECT report_hash FROM race_reports WHERE race_id=?", (race_id,)))
        q = n // 2 + 1  # strict majority of original starters, never received reports
        best_hash, best_count = max(counts.items(), key=lambda p: p[1]) if counts else (None, 0)
        sent = sum(counts.values())
        if best_count >= q:
            status = "confirmed"
        elif best_count + (n - sent) < q:
            status = "disputed"  # even all outstanding reports cannot produce quorum
        elif at_ms > race["accept_until_ms"]:
            status = "expired"
        else:
            status = "pending"
        if status != "pending":
            self.db.execute("UPDATE races SET status=?,agreed_report_hash=?,finalized_at_ms=? WHERE race_id=? AND status='pending'",
                            (status, best_hash if status == "confirmed" else None, at_ms, race_id))
        return status

    def submit(self, race_id, reporter_id, token, entries, now_ms, *, authenticated=False):
        if not authenticated:  # simulates upstream identity verification ONLY
            raise PermissionError("Reporter identity must be verified upstream")
        if not isinstance(token, bytes):
            raise PermissionError("Invalid token")
        self.db.execute("BEGIN IMMEDIATE")
        try:
            race = self.db.execute("SELECT * FROM races WHERE race_id=?", (race_id,)).fetchone()
            if race is None:
                raise KeyError("Unknown race")
            if not hmac.compare_digest(hashlib.sha256(token).digest(), race["token_hash"]):
                raise PermissionError("Wrong race token")
            allowed = self.db.execute("SELECT 1 FROM race_participants WHERE race_id=? AND participant_id=?",
                                      (race_id, reporter_id)).fetchone()
            if not allowed:
                raise PermissionError("Reporter was not in starting roster")
            payload = canonical_report(entries, race["participants_total"])
            digest = hashlib.sha256(payload.encode("utf-8")).digest()
            old = self.db.execute("SELECT report_hash FROM race_reports WHERE race_id=? AND reporter_id=?",
                                  (race_id, reporter_id)).fetchone()
            if old is not None:
                if not hmac.compare_digest(old["report_hash"], digest):
                    raise InvalidReport("Conflicting resubmission from the same participant")
                status = self._evaluate(race_id, now_ms)  # idempotent retry
                self.db.execute("COMMIT")
                return status
            if race["status"] != "pending":
                raise InvalidReport("Race already finalized")
            if now_ms > race["accept_until_ms"]:
                self._evaluate(race_id, now_ms)
                self.db.execute("COMMIT")
                raise InvalidReport("Acceptance window elapsed")
            self.db.execute("INSERT INTO race_reports VALUES (?,?,?,?,?)", (race_id, reporter_id, digest, payload, now_ms))
            status = self._evaluate(race_id, now_ms)
            self.db.execute("COMMIT")
            return status
        except Exception:
            if self.db.in_transaction:
                self.db.execute("ROLLBACK")
            raise

    def expire(self, race_id, now_ms):
        self.db.execute("BEGIN IMMEDIATE")
        try:
            status = self._evaluate(race_id, now_ms)
            self.db.execute("COMMIT")
            return status
        except Exception:
            if self.db.in_transaction:
                self.db.execute("ROLLBACK")
            raise
