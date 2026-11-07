# Draft: Persistent identities and ranked results — SQLite data model and agreement rules

> **Discussion draft, not an implemented feature.** Proposed in response to the
> maintainer's invitation to help design future rankings. This document and the
> accompanying Python/SQLite reference are intentionally separate from the
> production lobby. No gameplay or network code is changed.

## Intended outcome and non-goals

The agreed direction from the maintainer is: unchanged guest play; a random
secret generated on first online use and stored with the save; only a verifier
hash on the server; SQLite local to `takedawn-lobby` with daily off-machine
backups; a one-time race token; place/takedown reports from all copies; count
points only when most copies agree and at least two people started; rankings
are optional and must never block a race; the existing public statistics page
can show the leaderboard. Account recovery and cross-device use remain open.
This PR does not propose an account-linking mechanism.

An important distinction: **"guest" means no manual registration**, not
necessarily no server-side identity. Under the author's described plan, the
client silently generates a local random secret for every first-time online
player. This draft therefore models automatically created *pseudonymous*
profiles; a signup flow is NOT required. Whether ranking publication itself
should be optional is a separate UX/privacy question.

This draft addresses **persistence, report agreement, idempotency, and failure
behavior only**. It does **not** choose a recovery method, implement network
authentication or encryption, assert that all result fields are available,
implement the scoring formula, or require changes to the lobby in the next
release.

## Existing code reviewed

- `src/game/online.c` / `online.name`: a guest name is locally selected and can
  change; the generated Xbox-compatible XUID mixes name, port offset and IP.
- `tools/lobby/aries_server.c`, `game_t.gid`, `race_started()`, `race_t`:
  `TKGID` is a random **12-hex-character** game correlation ID. `race_t`
  currently tracks the start roster **in memory**, and its records may be
  retired/replaced. It is not durable ranking history.
- `tools/lobby/aries_server.c`, `rank`: currently accepts and counts the
  report (`st_result()`), without storing an individual result.
- `docs/online.md`: `TKSID` belongs to a connection; the lobby connection
  closes at race start and returns after the race. Neither `TKSID` nor the
  name is a suitable durable player key.
- `docs/online.md`, `tools/lobby/stats_page.html`: current public statistics
  are aggregates, not player rankings.
- `tools/lobby/test_lobby.c`, `tools/lobby/crowd/`: existing tests and
  simulated clients may be extended after the protocol is agreed.

The initial source review used Takedawn `main` at commit `9f7c371`.
**Check and rebase against current `main` before integration**, as the lobby
is changing rapidly.

## Cross-device identity (deferred design, not a promise)

The same server-side `profile_id` can eventually be used from Windows and
Android even if each device has a different credential. A single current
`secret_verifier` is sufficient for the maintainer's initial single-secret
model but does **not** implement device linking or safe account recovery.
If approved later, a `profile_credentials` relation can support multiple
independently revocable device credentials without changing race history.
The existing `UNIQUE(race_id, profile_id)` prevents one registered profile
from receiving multiple votes in one ranked race, including when it uses
multiple devices. Multi-device linking and recovery remain out of scope.

## Proposed v0 schema

The complete, executable exploratory schema is
`tools/lobby/rankings_prototype/schema.sql`. It has four entities:

| Table | Purpose | Critical constraint |
|---|---|---|
| `profiles` | Pseudonymous identities automatically backed by locally generated secrets, visible name and verifier | Random ID unique; display name *not* unique; only a verifier, not raw secret |
| `races` | Durable unique race ID, `TKGID` correlation, hashed one-time token, participant count and finalization | The 12-hex `TKGID` is **not** the DB primary key; use independent random 128-bit `race_id` |
| `race_participants` | Exact roster and slot assigned at start; profile may be NULL for legacy/unidentified participant | Unique per-race slot and profile if present |
| `race_reports` | One immutable canonical report from each original participant | Unique `(race_id, reporter_id)`; FK to roster; payload/hash and arrival timestamp |

Times are Unix **milliseconds**, not client-reported timestamps. The examples
use 128-bit random record IDs and 256-bit random tokens; their byte sizes are
design choices, not current Takedawn protocol guarantees. Tokens should be
bound to a race, expire, be single-purpose and be checked without leaking in
logs. The server stores a token verifier, not a plaintext token. A token
**alone does not authenticate a participant**.

A player can remain a **guest in the UI (no registration)** yet have an
automatically generated pseudonymous profile, as described by the maintainer.
When a player changes their display name, `profile_id` remains stable; the
name is presentation metadata. The nullable `profile_id` in the prototype
represents *legacy/unidentified* participants or an explicitly unranked
fallback, **not the default guest experience**. Whether such participants
should vote requires maintainer review. Whether public leaderboard visibility
is automatic or opt-in is a separate privacy/product decision.

## Proposed conservative agreement policy (for discussion)

1. **At race start**, persist the exact original roster of `N` starters, with
   `2 <= N <= 8`, and a server-generated race identifier/token. The title's
   game result flow must be inspected before determining the final wire
   representation. `N` is frozen even if players disconnect later.
2. **Authenticate the reporting participant separately** from the one-time
   race token. Reject non-roster reporters; do not trust a user-provided
   name, slot, XUID or race ID alone.
3. **Normalize reports** against the frozen slots and agreed wire data types,
   then hash the exact canonical bytes. The reference prototype models a
   complete vector `[[slot, place, takedowns], ...]`. This is deliberately
   strict and may need replacement with per-field/per-player consensus if
   clients disagree on incidental fields, modes, DNF, or late races.
4. **Majority threshold**: `quorum = floor(N / 2) + 1`, where `N` counts
   **original starters**, not only received reports. For two players the rule
   requires 2/2 agreement; for three, 2/3; four, 3/4; six, 4/6.
5. **One vote per starting participant**. A byte-identical resend is an
   idempotent retry; a changed second report is rejected and retained only in
   rate-limited security diagnostics if needed. Missing reports never reduce
   the denominator.
6. `pending` before quorum; `confirmed` when one canonical report wins strict
   majority; `disputed` once even all remaining reports cannot produce a
   quorum; `expired` when the configured acceptance period lapses without
   quorum. No points are awarded for disputed/expired races.
7. **Freeze the decision** after `confirmed`. Further duplicates are harmless;
   decide explicitly whether late conflicting reports are logged for audit or
   ignored. Human/admin correction policy is out of scope for v0.
8. **Point awards are a separate idempotent step** after confirmation. The
   ranking formula is not currently specified; a later award ledger should
   have a unique `(race_id, profile_id, rule_version)` constraint so a restart
   cannot double-count. The draft prototype does **not** award points.

### Truth table for exact-vector majority

| Starters | Reports so far | Decision |
|---|---|---|
| 2 | one A | pending |
| 2 | A, A | confirmed |
| 2 | A, B | disputed |
| 3 | A, A | confirmed |
| 4 | A, A | pending |
| 4 | A, A, B | pending |
| 4 | A, A, B, A | confirmed |
| 4 | A, A, B, B | disputed |
| 4 | A, B, C | disputed (the one remaining cannot make 3) |

**Important limitation:** agreeing clients may be wrong or collude; strict
majority is **consistency**, not a complete anti-cheat system. Two colluding
players can agree 2/2. Competitive trust limits and optional admin moderation
need explicit product decisions. This cannot be solved by SQLite alone.

## Late and out-of-order reporting

- Accept reports in any order; anchor all timestamps/deadlines to the server.
- A retry with the same authenticated identity and same canonical data is
  idempotent even after confirmation. Do not accept a *new* report after the
  deadline. `24 h` in the prototype is an **illustrative test window only**:
  the maintainer must decide how many hours/days to keep server records and
  queued client submissions, especially after server outages.
- An initial report accepted before the deadline must survive restart.
- Preserve `race_id`, frozen roster, and token verifier across reconnects and
  server restarts (subject to expiration); do not infer a race from current
  online names or short `TKGID` alone.
- If the ranking DB is unavailable, **lobby and relay traffic keep running**.
  Clients may retry after recovery. Clearly specify the rare case where a
  race cannot be durably registered before starting: best-effort history may
  be absent, but gameplay still proceeds. Never award scores for unverified
  reports invented after a restart.

## Authentication and data exposure — review before implementation

- The planned 256-bit local random secret may be hashed for server storage,
  but **decide the actual authentication protocol** before passing any secret
  over the currently plain TCP lobby connection. Merely comparing a hash of a
  secret does not protect the secret when transmitted unencrypted, and using
  the stored hash as a challenge-response key has different DB-compromise
  properties. A vetted authenticated proof/challenge or a protected channel
  is required before real ranked accounts are enabled.
- Never log raw secrets, recovery codes, race tokens or reusable credentials.
  Do not derive a persistent player ID from IP/XUID/name.
- Derive identifiers/tokens using a CSPRNG; compare secret verifiers in constant
  time; enforce length limits and one-use semantics; cap requests by session/IP
  and apply SQL parameters.
- Decide whether leaderboard publication is automatic or opt-in before
  exposing names/points; do not silently repurpose private metrics. Keep any non-ranked unidentified participant data short-lived and exclude private
  rooms/codes from public datasets by default unless explicitly chosen.
- Public leaderboard JSON should contain only approved display information;
  no IPs, tokens, verifiers, session IDs or private game metadata.
- The reference Python implementation's `authenticated=True` flag **is a
  test fixture, not authentication**. Never expose it as a real API parameter.

## SQLite behavior and operational safety

- Keep SQLite **on the same host** as the lobby; do not put a WAL database on
  a shared/network filesystem. Use `PRAGMA foreign_keys = ON` for **every**
  connection, since enabling FK constraints is not guaranteed by default.
- WAL can permit readers alongside a writer but still has **one writer**;
  keep transactions short, use a bounded write queue or separate worker so
  slow I/O, checkpointing and backups cannot block the lobby's main event
  loop. Ensure queue overflow cannot cause unbounded RAM growth.
- Choose durability pragmas after benchmarking on the deployment system;
  prefer consistent transactions and documented crash recovery to an
  unmeasured performance tweak.
- Back up daily **off the machine** via SQLite's online backup API (or an
  equivalent consistent snapshot), not a naive copy of the main `.db` file
  while WAL is active. Periodically **test restoring** a backup. Document
  retention and deletion for players who opt out.
- Degrade safely: if ranking writes, migrations, checkpoints or backups fail,
  report the failure separately and leave race transport unaffected.

Official SQLite docs:
- https://www.sqlite.org/wal.html
- https://www.sqlite.org/foreignkeys.html
- https://www.sqlite.org/backup.html

## Maintainer decisions requested before live integration

1. Is the voter denominator **all original participants**, including automatic guest identities,
   or only rank-eligible authenticated profiles? How do automatically pseudonymous guests authenticate? Should unidentified legacy participants count?
2. Must full result vectors match **exactly**, or should agreement be computed
   per participant/place/takedown? How are ties, DNF, Road Rage, Team Crush,
   Crash and incomplete/abandoned races represented?
3. Which fields are actually sent in *real* `rank` messages by each supported
   game build? Are they per-race or cumulative?
4. Which lifetime should result tokens and pending races have? What is the
   policy for a server outage lasting beyond that lifetime?
5. How will a reporter prove identity without leaking reusable secrets over
   the existing plain TCP protocol?
6. Should public rankings be per lobby server or aggregated across independent
   lobbies? How are private games handled?
7. Is a provisional/contested result shown to the player, and can a finalized
   score ever be reversed? Which point-scoring formula/version applies?

## Testing status

The accompanying 26 reference tests passed on Windows (Python 3.12.10)
and in the preparation environment using Python standard-library SQLite
with in-memory/on-disk databases. They cover sample two-, three-
and four-player consensus, conflicts, retries, tampered resubmissions,
unknown reporters, token mismatch, a simulated authentication gate, unidentified/legacy
roster counting, restart persistence, and FK checks, SQL-injection-shaped names, cross-race token reuse, stable profiles across separate races, name changes and prevention of same-profile double voting, and a
consistent SQLite online-backup snapshot. **They do not run any
Takedawn game or exercise the actual TCP/UDP protocol.**

This draft changes only documentation and a standalone Python/SQLite
reference prototype. It does not modify the production lobby or server.
