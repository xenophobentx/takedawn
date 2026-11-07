"""Offline tests of the PROPOSED protocol semantics, no game assets. MIT."""
import hashlib
import os
import secrets
import sqlite3
import tempfile
import unittest

from reference import InvalidReport, RankingPrototype, canonical_report, SCHEMA

T0 = 1_800_000_000_000
WINDOW = 86_400_000  # EXAMPLE 24h, pending maintainer decision


def result(n, winner=0, td=0):
    # Deterministic full result vector, unique places.
    order = [winner] + [slot for slot in range(n) if slot != winner]
    return {slot: {"place": order.index(slot) + 1, "takedowns": td if slot == winner else 0}
            for slot in range(n)}


class Base(unittest.TestCase):
    def setUp(self):
        self.p = RankingPrototype.memory()
        self.addCleanup(self.p.db.close)
        self.token = secrets.token_bytes(32)

    def start(self, n):
        return self.p.create_race([None] * n, self.token, "a1b2c3d4e5f6", T0, WINDOW)

    def send(self, race_id, player, entries, at=T0 + 1000):
        return self.p.submit(race_id, player, self.token, entries, at, authenticated=True)


class RankingTests(Base):

    def test_display_name_change_preserves_profile_id(self):
        # Profile identity does not depend on the local display name.
        profile = self.p.create_profile("Dark", secrets.token_bytes(32), T0)
        self.p.db.execute("UPDATE profiles SET display_name=? WHERE profile_id=?", ("RacerMX", profile))
        row = self.p.db.execute("SELECT profile_id, display_name FROM profiles WHERE profile_id=?", (profile,)).fetchone()
        self.assertEqual((row["profile_id"], row["display_name"]), (profile, "RacerMX"))

    def test_same_profile_can_participate_in_distinct_races(self):
        # Two races can refer to the same persistent profile; this does NOT
        # implement cross-device authentication or account recovery.
        shared = self.p.create_profile("Dark", secrets.token_bytes(32), T0)
        other = self.p.create_profile("Other", secrets.token_bytes(32), T0)
        r1, _ = self.p.create_race([shared, other], self.token, "a1b2c3d4e5f6", T0, WINDOW)
        r2, _ = self.p.create_race([shared, other], secrets.token_bytes(32), "a1b2c3d4e5f6", T0+100, WINDOW)
        self.assertNotEqual(r1, r2)
        rows = self.p.db.execute("SELECT race_id FROM race_participants WHERE profile_id=?", (shared,)).fetchall()
        self.assertEqual({row[0] for row in rows}, {r1, r2})

    def test_same_profile_cannot_get_two_votes_in_one_race(self):
        # Two device connections linked to one profile must not become two
        # voting identities in one ranked race. This tests DB constraints only.
        shared = self.p.create_profile("Dark", secrets.token_bytes(32), T0)
        with self.assertRaises(sqlite3.IntegrityError):
            self.p.create_race([shared, shared], self.token, "a1b2c3d4e5f6", T0, WINDOW)
        self.assertEqual(self.p.db.execute("SELECT COUNT(*) FROM races").fetchone()[0], 0)
        self.assertEqual(self.p.db.execute("SELECT COUNT(*) FROM race_participants").fetchone()[0], 0)

    def test_same_name_separate_identities(self):
        a = self.p.create_profile("Dark", secrets.token_bytes(32), T0)
        b = self.p.create_profile("Dark", secrets.token_bytes(32), T0)
        self.assertNotEqual(a, b)
        self.assertEqual(self.p.db.execute("SELECT count(*) FROM profiles WHERE display_name='Dark'").fetchone()[0], 2)

    def test_only_secret_hash_is_stored(self):
        raw = secrets.token_bytes(32)
        profile = self.p.create_profile("Dark", raw, T0)
        stored = self.p.db.execute("SELECT secret_verifier FROM profiles WHERE profile_id=?", (profile,)).fetchone()[0]
        self.assertNotEqual(raw, stored)
        self.assertEqual(stored, hashlib.sha256(raw).digest())

    def test_two_players_need_both(self):
        r, ps = self.start(2)
        self.assertEqual(self.send(r, ps[0], result(2)), "pending")
        self.assertEqual(self.send(r, ps[1], result(2)), "confirmed")

    def test_two_players_disagree(self):
        r, ps = self.start(2)
        self.send(r, ps[0], result(2))
        self.assertEqual(self.send(r, ps[1], result(2, winner=1)), "disputed")

    def test_three_players_majority(self):
        r, ps = self.start(3)
        self.send(r, ps[0], result(3))
        self.assertEqual(self.send(r, ps[1], result(3)), "confirmed")
        self.assertEqual(self.p.db.execute("SELECT count(*) FROM race_reports WHERE race_id=?",(r,)).fetchone()[0], 2)

    def test_four_players_2_votes_are_not_enough(self):
        r, ps = self.start(4)
        self.send(r, ps[0], result(4))
        self.send(r, ps[1], result(4))
        self.assertEqual(self.p.state(r), "pending")
        self.send(r, ps[2], result(4, winner=1))
        self.assertEqual(self.p.state(r), "pending")
        self.assertEqual(self.send(r, ps[3], result(4)), "confirmed")

    def test_four_split_is_disputed(self):
        r, ps = self.start(4)
        self.send(r, ps[0], result(4))
        self.send(r, ps[1], result(4))
        self.send(r, ps[2], result(4, winner=1))
        self.assertEqual(self.send(r, ps[3], result(4, winner=1)), "disputed")

    def test_impossible_consensus_detected_early(self):
        r, ps = self.start(4)
        self.send(r, ps[0], result(4, winner=0))
        self.send(r, ps[1], result(4, winner=1))
        self.assertEqual(self.send(r, ps[2], result(4, winner=2)), "disputed")

    def test_idempotent_retry(self):
        r, ps = self.start(2)
        self.send(r, ps[0], result(2))
        self.assertEqual(self.send(r, ps[0], result(2)), "pending")
        self.assertEqual(self.p.db.execute("SELECT count(*) FROM race_reports WHERE race_id=?",(r,)).fetchone()[0], 1)
        self.assertEqual(self.send(r, ps[1], result(2)), "confirmed")
        self.assertEqual(self.send(r, ps[0], result(2), T0 + WINDOW + 900), "confirmed")

    def test_modified_retry_rejected(self):
        r, ps = self.start(2)
        self.send(r, ps[0], result(2))
        with self.assertRaises(InvalidReport):
            self.send(r, ps[0], result(2, winner=1))

    def test_unlisted_player_rejected(self):
        r, _ = self.start(2)
        with self.assertRaises(PermissionError):
            self.send(r, secrets.token_hex(16), result(2))

    def test_wrong_token_rejected(self):
        r, ps = self.start(2)
        with self.assertRaises(PermissionError):
            self.p.submit(r, ps[0], secrets.token_bytes(32), result(2), T0+1, authenticated=True)

    def test_missing_auth_rejected(self):
        r, ps = self.start(2)
        with self.assertRaises(PermissionError):
            self.p.submit(r, ps[0], self.token, result(2), T0+1)

    def test_invalid_vector_rejected(self):
        for wrong in [{0:{"place":1,"takedowns":0}},
                      {0:{"place":1,"takedowns":0},1:{"place":1,"takedowns":0}},
                      {0:{"place":1,"takedowns":False},1:{"place":2,"takedowns":0}}]:
            with self.assertRaises(InvalidReport):
                canonical_report(wrong, 2)

    def test_late_new_report_rejected(self):
        r, ps = self.start(2)
        self.send(r, ps[0], result(2))
        with self.assertRaises(InvalidReport):
            self.send(r, ps[1], result(2), T0 + WINDOW + 1)
        self.assertEqual(self.p.state(r), "expired")

    def test_six_players_need_four_reports(self):
        r, ps = self.start(6)
        for i in range(3):
            self.assertEqual(self.send(r, ps[i], result(6)), "pending")
        self.assertEqual(self.send(r, ps[3], result(6)), "confirmed")

    def test_sql_injection_in_name_is_data_only(self):
        special = "A';DROP;--"
        p = self.p.create_profile(special, secrets.token_bytes(32), T0)
        self.assertEqual(self.p.db.execute("SELECT display_name FROM profiles WHERE profile_id=?", (p,)).fetchone()[0], special)
        self.assertIsNotNone(self.p.db.execute("SELECT name FROM sqlite_master WHERE name='profiles'").fetchone())

    def test_token_cannot_be_reused_for_different_race(self):
        r1, ps1 = self.start(2)
        other = secrets.token_bytes(32)
        r2, ps2 = self.p.create_race([None,None], other, "a1b2c3d4e5f6", T0+2, WINDOW)
        with self.assertRaises(PermissionError):
            self.p.submit(r2, ps2[0], self.token, result(2), T0+100, authenticated=True)
        self.assertEqual(self.p.state(r1), "pending")

    def test_reject_boolean_slot(self):
        with self.assertRaises(InvalidReport):
            canonical_report({0:{"place":1,"takedowns":0},True:{"place":2,"takedowns":0}}, 2)

    def test_unidentified_slots_count_toward_quorum_if_allowed(self):
        reg = self.p.create_profile("Dark", secrets.token_bytes(32), T0)
        r, ps = self.p.create_race([reg, None], self.token, "a1b2c3d4e5f6", T0, WINDOW)
        self.assertEqual(self.send(r, ps[0], result(2)), "pending")
        self.assertEqual(self.send(r, ps[1], result(2)), "confirmed")

    def test_fks_enabled_and_enforced(self):
        r, _ = self.start(2)
        with self.assertRaises(sqlite3.IntegrityError):
            self.p.db.execute("INSERT INTO race_participants VALUES (?,?,?,?)", (r, secrets.token_hex(16), 0, "missing_profile"))

    def test_sqlite_online_backup_snapshot(self):
        with tempfile.TemporaryDirectory() as d:
            original = os.path.join(d, "active.db")
            replica = os.path.join(d, "snapshot.db")
            src = sqlite3.connect(original, isolation_level=None)
            try:
                src.executescript(SCHEMA.read_text(encoding="utf-8"))
                self.assertEqual(src.execute("PRAGMA journal_mode=WAL").fetchone()[0], "wal")
                store = RankingPrototype(src)
                store.create_profile("Dark", secrets.token_bytes(32), T0)
                dest = sqlite3.connect(replica)
                try:
                    src.backup(dest)
                    self.assertEqual(dest.execute("PRAGMA integrity_check").fetchone()[0], "ok")
                    self.assertEqual(dest.execute("SELECT count(*) FROM profiles").fetchone()[0], 1)
                finally:
                    dest.close()
            finally:
                src.close()

    def test_reopen_database_keeps_pending_report(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "ranks.db")
            a = RankingPrototype(sqlite3.connect(path, isolation_level=None))
            a.db.executescript(SCHEMA.read_text(encoding="utf-8"))
            race, ps = a.create_race([None,None], self.token, "a1b2c3d4e5f6", T0, WINDOW)
            self.assertEqual(a.submit(race, ps[0], self.token, result(2), T0+100, authenticated=True), "pending")
            a.db.close()
            b = RankingPrototype(sqlite3.connect(path, isolation_level=None))
            try:
                self.assertEqual(b.submit(race, ps[1], self.token, result(2), T0+500, authenticated=True), "confirmed")
            finally:
                b.db.close()


if __name__ == "__main__":
    unittest.main()
