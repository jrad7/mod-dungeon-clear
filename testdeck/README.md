# DC Test Deck

A web frontend for **mod-dungeon-clear** dungeon test runs, built to be handed
to testers: log in with a game GM account, pick a dungeon, and a full 5-bot
party runs it while you watch live. No service control, no shell access — just
testing.

- **Launch** — dungeon grid; start a single run, an N-run campaign ("plan"),
  or a hand-picked roster of real characters. Difficulty, bot level, comp seed
  and the gear ceiling are all on the one form.
- **Live** — every active run: party HP/mana chips, boss progress, a full
  status timeline, stop buttons. Plan progress bars.
- **Roster** — browse real characters (class colors, talent spec, instance
  budget), fill the five positional slots (tank / heal / dps ×3), save named
  rosters.
- **History** — finished runs with the post-mortem (party, boss kills,
  deaths, status timeline) and campaign summaries. *Rerun* relaunches a run
  with its own dungeon, difficulty, level, gear ceiling and comp seed — the
  same party, a new run id.
- **Logs** — live tail of the server's `*.log` files.

Login uses **real game accounts**: username/password are verified against the
auth database's SRP6 salt+verifier, and the account needs
`account_access.gmlevel >= 1`. Every start/stop is audit-logged with the
account name.

## Quick start

Test Deck runs on the machine your worldserver runs on — Linux, macOS or
Windows.

**1. Check you have the two things it needs.** **Python 3.9+** (on Windows,
tick *"Add python.exe to PATH"* in the installer) and a **MySQL client**
(`mysql` / `mysql.exe`), which is how it reads the game databases. No Node.js —
the UI ships pre-built in `dist/`.

**2. Turn on SOAP** in `worldserver.conf`, and restart the worldserver. This is
how the deck talks to your server; setup will check it and tell you if it is
still off.

```
SOAP.Enabled = 1
SOAP.IP      = "127.0.0.1"
SOAP.Port    = 7878
```

**3. Create the bridge account** on the worldserver console. SOAP requires
administrator level:

```
account create tdbridge <a long random password>
account set gmlevel tdbridge 3 -1
```

**4. Start it**, from `modules/mod-dungeon-clear/testdeck`:

| | |
|---|---|
| **Windows** | double-click **`testdeck.bat`** |
| **Linux / macOS** | `./testdeck.sh` |
| either, by hand | `python3 launch.py` |

The first run creates a private Python environment, installs what it needs,
asks a handful of questions (mostly confirming what it already worked out),
writes `testdeck.toml`, then serves the deck and opens your browser at it.
Later runs skip straight to serving. It prints both the local URL and the LAN
one — **testers elsewhere on the network need nothing installed, just that
second address in a browser.**

**5. Log in** with any game account that has `gmlevel >= 1` — see *Create tester
accounts* below. There is no separate Test Deck password.

That is the whole setup. There is a third account involved — the *test driver*
the module logs in to anchor each run — but it creates that one itself on the
first launch; see *The test driver character* if you want to know what it is.

### Your first run

Pick a dungeon from the grid on **Launch**, leave every option alone, and press
*Start run*. A five-bot party spawns, gears itself, teleports in and clears the
place. Watch it on **Live**; when it finishes, the post-mortem — party, boss
kills, deaths, what wiped it — is on **History**.

Once that works, the three launch modes are what the rest of the deck is for:

- **Quick run** — one run, now. The loop for "did my change help?"
- **Plan** — the same run N times, for a success *rate* rather than an anecdote.
- **Roster** — a hand-picked party of real characters instead of pool bots.

### If something is wrong

```sh
python3 -m testdeck check       # every problem this host has, in one list
```

`check` names the config key behind each finding. Common ones:

- *the mysql client is not on PATH* — normal on Windows, where no installer
  puts it there. Setup looks in the usual MySQL/MariaDB directories; if yours
  is somewhere else, set `[paths] mysql_bin` to the full path of `mysql.exe`.
- *test driver character 'Dcdriver' does not exist yet* — a note, not a fault:
  the module creates it on the first launch. It becomes an error only if you
  emptied `DungeonClear.TestRun.DriverAccount`, which turns that off. `check`
  also catches the conf name and the character differing only in
  capitalisation, which the server itself can only report as "not found".
- *soap_user is unset* / *cannot reach the SOAP endpoint* — see
  *The worldserver bridge* below.
- *base … does not exist* — setup guessed the wrong install. Re-run
  `python3 -m testdeck setup --force` and give it the path when it asks.
- *server_root … does not exist* — Test Deck could not tell which directory
  the worldserver runs in. That directory is the one holding
  `worldserver`/`worldserver.exe`, and it decides more than its name suggests:
  the module writes its `dc_*` files there by relative name, so the launch,
  Live and history panels all read out of it, and the relative paths inside
  `worldserver.conf` (`LogsDir`, `DataDir`) are relative to it too. Set
  `[paths] server_root` to it if the server is started from somewhere the
  binary is not — a service with its own working directory, or a launcher
  script that `cd`s first.
- *log_dir … does not exist* — only the log viewer depends on this. It follows
  `LogsDir` in `worldserver.conf`; set `[paths] log_dir` if the logs really
  live elsewhere.
- *playerbots_conf … not found* / *dbc_dir … not found* — both are found from
  the install itself (module confs beside `worldserver.conf`, and on Windows
  under `<server_root>/configs/modules/` where the core hardcodes them; DBCs
  via `DataDir`). If a pack puts them somewhere of its own, name them with
  `[paths] playerbots_conf` and `[paths] dbc_dir`.

`check` prints the two directories these come from, so you can see at a glance
which one is wrong:

```
  server    C:\WoW\SingleCraft  (worldserver working directory)
  log_dir   C:\WoW\SingleCraft\logs
  sidecars  C:\WoW\SingleCraft  (found)
```

Symptoms as they appear in the browser:

| What you see | What it means |
|---|---|
| **the MySQL client was not found** on login | step 1 of the quick start; set `[paths] mysql_bin` if it is installed but not on PATH |
| **auth database unavailable** on login | the `mysql` client is there but cannot reach the database — check `LoginDatabaseInfo` in `worldserver.conf` |
| **this account has GM level 0** | the message names the exact `account set gmlevel` line to run |
| **forbidden: unexpected Host header** | you reached the deck by a *name*; add it to `[server] allowed_hosts`, or use the address the startup banner printed |
| **No dungeon catalogue yet** | the worldserver writes its dungeon list shortly after startup — if the realm is up and it never appears, either mod-dungeon-clear is not loaded, or the deck is looking in the wrong directory: `check` prints the `sidecars` line, and `dc_test_dungeons.json` has to be in it |
| **Live** stays empty while a run is going | same directory question as above — `dc_testrun_live.json` is written next to the worldserver, not into the log directory |
| **test driver character 'Dcdriver' not found** on *Start run* | the module could not create it — usually `DungeonClear.TestRun.DriverCharacter` is not a capitalised name, or its account is a random-bot one. `check` names which |
| a run starts but nothing appears on **Live** | the test driver was still logging in; the deck queues it as a one-run plan, which waits it out. Expected on the very first launch, which creates the driver |

## The worldserver bridge

Test Deck issues `.dc test …` commands to the worldserver. **SOAP is the
recommended transport and the only one that exists on Windows** — it works no
matter how the worldserver is launched and needs no privileges. Quick start
steps 2 and 3 above are the whole of its setup; the deck reads your
`worldserver.conf` itself and fills in the URL.

Keep the bridge password out of the config file with `TESTDECK_SOAP_PASS` if
you prefer.

Two POSIX-only alternatives exist for hosts that would rather not enable SOAP:
`screen` (worldserver inside a GNU screen session; set `screen_session`) and
`tmux` (set `tmux_target`). If the session belongs to another user, set
`use_sudo = true` and install the snippet `python3 -m testdeck sudoers` prints.

## The test driver character

The harness is built around an **issuing GM**: the party bots log in under
that player's account and keep it as their playerbots master, which is what
holds them on the stock real-player fast path. When a GM types `.dc test start`
in the game, they are it.

A launch from Test Deck has no such player. All three bridges type into the
worldserver **console** — SOAP included, since the core runs a SOAP command
through the same CLI queue — so there is no session and no player behind the
command. The module covers that by logging in one dedicated character headlessly
and using it as the stand-in GM. That is `Dcdriver`, and nothing the deck can
start works without it.

**You do not have to create it.** The first time a console or Test Deck
`.dc test` needs a driver, the module creates the account
(`DungeonClear.TestRun.DriverAccount`, default `dcdriver`, with a random
password that is not recorded) and the character on it, logs it in, elevates
its own session to GM and reuses it forever. So on a host that has never
launched anything the character is simply absent, `check` says so as a note,
and the first launch may answer *"is logging in — retry in a few seconds"*
while it happens. The deck handles that by queueing the run as a one-run plan,
which waits the driver out server-side.

Two different accounts, easy to conflate:

| | what it is | needs |
|---|---|---|
| `tdbridge` | the account **SOAP authenticates as** — no character, never enters the world | `gmlevel 3` (the core refuses SOAP below administrator); **you create this one** (quick start step 3) |
| `dcdriver` | the account that **owns the `Dcdriver` character** | nothing — the module creates it as a plain player account and elevates the session, not the account |

The two conf keys, if you want to change either:

- `DungeonClear.TestRun.DriverCharacter` — the character name. It is resolved
  by an exact, case-sensitive lookup, so `dcdriver` in the conf will never
  find a character called `Dcdriver`, and the only thing the server can say is
  "not found". The module refuses to *create* a name that could not be looked
  up again, and `python3 -m testdeck check` catches an existing mismatch by
  name.
- `DungeonClear.TestRun.DriverAccount` — the account to put it on. It must not
  be one of `AiPlayerbot.RandomBotAccounts` and must not be an addclass pool
  account, or the bot rotation would log the driver out from under a live run;
  the module refuses such an account rather than using it. Set it to `""` to
  turn provisioning off and supply the character yourself — then a missing
  driver is an error `check` reports, because nothing else will create it.

## Create tester accounts

From the worldserver console (or via SOAP):

```
account create alice <password>
account set gmlevel alice 1 -1
```

gmlevel 1 can launch and watch; clearing shared history and deleting saved
rosters needs `[auth] admin_gmlevel` (default 3). Verify an account end to end
with:

```sh
python3 -m testdeck check-auth alice
```

## Running it as a service

Nothing about the launcher is required — any supervisor that runs
`python3 -m testdeck serve` works. `ac-testdeck.service.in` is a systemd unit
to fill in; on Windows, Task Scheduler with *"Run whether user is logged on or
not"* pointed at `testdeck.bat` does the same job.

## Configuration

`testdeck.example.toml` documents every setting. The file is looked for in
this order: `--config PATH`, `$TESTDECK_CONFIG`, `./testdeck.toml`, the
checkout's own `testdeck.toml`, `~/.config/testdeck.toml`, `/etc/testdeck.toml`.

Missing config is never fatal: the server boots on derived defaults and says
what is wrong in its own banner, because a server that refuses to start cannot
tell you why it refuses to start.

## Security model

DC Test Deck is a LAN tool. Defence in depth, outermost first: a `Host` header
check (`[server] allowed_hosts`), a source-address allowlist (`[server]
allowed_nets`), then GM-account login (SRP6, per-IP and per-username throttled,
generic failure messages), a signed HttpOnly session cookie, and a CSRF header
on every mutation. Command strings are built server-side from validated fields
only — nothing typed in the browser is ever spliced into a console command; the
SQL layer escapes every value through one helper and validates it against a
strict allowlist on top. Static files are served only from inside `dist/`,
checked by resolving the path and confirming containment. Every response
carries a strict CSP, `X-Frame-Options: DENY` and `nosniff`. Do not expose it
to the internet without a TLS reverse proxy in front.

**Revoking access** is `account set gmlevel <user> 0 -1`, and it takes effect
within about a minute: authorization re-reads the account's GM level and ban
state from the auth database on every request rather than trusting the level
baked into the cookie. An active `account_banned` row does the same. Sessions
last `[auth] session_hours` (default 12) — the cookie rides plain HTTP on a
LAN, so that number is how long a sniffed one stays useful.

**Who can do what.** `[auth] min_gmlevel` (default 1) logs in, launches pool
runs and plans, and drafts characters **on their own account**.
`[auth] admin_gmlevel` (default 3) is required for anything that affects
someone else: clearing shared run/plan history, deleting or overwriting
another tester's saved roster, and running characters that belong to another
account — a roster run logs a character in, teleports it and re-gears it.

Reach the deck by **address** (what the startup banner prints) and you need no
extra config. If testers use a *hostname* instead — a reverse proxy, a DNS or
hosts entry — list it in `[server] allowed_hosts`, or the deck will refuse the
request. That check is what stops a hostile page a tester visits from pointing
a name it controls at your server and driving it from their browser.

`[paths] data_dir` holds the session secret and is kept at mode 0700; a
readable copy of that file is enough to forge an admin session, so do not
relax it or place it somewhere shared.

## Development

```sh
cd web
npm install
npm run dev        # hot-reload UI, /api proxied to 127.0.0.1:8790
npm run build      # writes ../dist — COMMIT the result
```

Tests: `bash t/run_tests.sh` (backend pytest; plus the dist smoke and a
build-as-type-check when `web/node_modules` exists). No root, no live
worldserver, no database needed.

## API

See `docs/api.md`. Everything is JSON under `/api/`; the SPA in `dist/` is
served for every non-API path.
