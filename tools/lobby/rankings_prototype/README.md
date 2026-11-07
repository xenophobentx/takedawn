# Rankings prototype (discussion only)

This folder is a **self-contained reference model**, not a Takedawn feature or
an implementation of the real online protocol. It is intended to accompany a
early draft PR and be deleted, rewritten in C, or adapted at the maintainer's
discretion. No game files or network connection are required.

Run from the repository root using Python 3 stdlib only (Windows via
`run-tests-windows.cmd` at the bundle root; Linux/macOS via the command below):

```sh
python3 -m unittest discover -s tools/lobby/rankings_prototype -p 'test_*.py' -v
```

`reference.py` tests optimistic semantics for the *future* verified-profile
protocol, but **does not implement identity authentication**, transport crypto,
real-result parsing, scoring, crash-safe background queues or the production
lobby integration. Its `authenticated=True` parameter simulates prior secure
authentication; it is NOT a security mechanism. A hashed token is also NOT
sufficient to prove that a reporter is a particular participant.

Guest play is still **registration-free**: a client-generated secret can
identify a pseudonymous profile automatically, as the maintainer plans. NULL
profile IDs in the prototype only model unresolved legacy/unidentified cases.

Test rules (all proposals): each race has 2-8 original starters; strict
majority is `floor(starters/2)+1`; every counted reporter must be a starter;
reports contain an identical complete result vector (place/takedowns by slot);
retrying the same report is idempotent; changing a report is rejected; failure
to reach quorum by an illustrative 24-hour deadline leaves no credited result.

**Needs author decisions:** report data semantics, whether consensus is per
place/per player or entire vector, recovery of accounts, signed report
identity, legacy/unidentified votes and automatic guest identities, late-report policy, and points formula.

All SQL is parameterized. `PRAGMA foreign_keys=ON` must be set on each SQLite
connection (SQLite does not guarantee it is enabled by default). In a live
server, writes/checkpoints/backups must never block the network event loop.

Cross-device profile linking is **NOT implemented**. The stable `profile_id`
and unique roster constraint anticipate the possibility without imposing a
new authentication scheme. See `docs/cross-device-profiles-note.md`.
