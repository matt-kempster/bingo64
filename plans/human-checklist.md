# Human-only checklist (Matt's list)

Stuff that needs a human — decisions, accounts, or real hands on real
hardware. Everything Claude can do lives in
[netplay-release-checklist.md](netplay-release-checklist.md); this file
is only the parts that block on YOU. Ordered roughly by "do this next".

## Decisions — answer in chat, ~1 minute each

- [ ] **Main-menu instructions text.** A mock is sitting uncommitted
      ("EVERY SEED DEALS A CARD OF 25 GOALS / FIVE IN A ROW IS BINGO.
      GO FAST."). Bless it, reword it, or kill it.
- [ ] **QUIT button on the main menu** — yes or no.
- [ ] **Lobby simplification**: remove the TYPE row and collapse SERVER
      into an ADVANCED sub-view (host-only settings). Go / no-go.
- [ ] **Cheats "walk on environment" hook**: the vanilla-collision
      adoption dropped it from the live engine (racing integrity).
      Bless the drop or ask for it back behind cheats.
- [ ] **Late joiner UX**: someone joins a started race — do they race
      (timer already behind) or spectate?
- [ ] **Whereabouts privacy default**: suggest broadcast ON (it's a
      race), toggleable off.

## Accounts & infra — only your logins can do these

- [ ] **URGENT — reopen the GCP billing account.** Discovered 2026-08-18:
      billing account 01120E-C741C1-31B871 is CLOSED, which disabled
      billing on the project and suspended the VM — the relay is DOWN.
      Reopen at console.cloud.google.com/billing (likely needs a valid
      card; free tier still requires an open billing account). Then
      Claude re-links the project, starts mario-server, and verifies
      the relay + tunnel return.
- [ ] **RELEASE GATE — create the DNS TXT record** at your kempster.com
      registrar: name `_bingo64`, type TXT, TTL 300, value
      `udp:mauritania-defines.tun.ply.gg:16118` (one string). Fresh
      installs default to `auto` and need this to find the server.
- [ ] **GCP billing alert at $1** (console → Billing → Budgets).
      Cheap insurance that free-tier stays free.
- [ ] **playit.gg account check**: confirm the tunnel is on your
      account, claimed, and won't expire/rotate its address — that
      address is about to be baked into DNS.

## Real hands on real machines

- [ ] **Track down "server refused v5".** The live server verifiably
      accepts v5, so whatever showed that message was an old exe or a
      stale config. Which exe/shortcut were you launching? (Suspect:
      an old copy outside bingo64-test, or `%APPDATA%\sm64ex` config
      pointing somewhere odd.)
- [ ] **Window-drag test on Windows** (~5 min): hold the title bar for
      20s mid-connection; see if it disconnects or stutters ghosts.
- [ ] **Casual bug-hunt sessions**: play normally, try your favorite
      glitches/clips. To report physics weirdness, just say the level
      and the move — the bisect/diff-test machinery finds the culprit
      from there.

## The big one — playtest evening (needs friends)

- [ ] **Recruit 3–4 people + pick a date.** Every test so far is
      effectively 2-player. The rude-things matrix to run that night
      is §7 of the netplay checklist (alt-F4s, late joins, wifi drops,
      host quits, race twice in a row, all five modes).
- [ ] Afterward: dump impressions in chat, even vague ones — "the
      lobby felt confusing" is actionable.

## Release moment

- [ ] **Review + merge the branches** (or say "walk me through them"):
      `game-end-flow` (UI/lifecycle/segfault fix), the vanilla-campaign
      branch (collision/math/step adoptions + fast-math fix), the
      harness branch.
- [ ] **Pick a distribution channel**: GitHub release zip? A page on
      kempster.com? Where do friends download from?
- [ ] **Say "this is 1.0"** — that's the moment the clobber-freely
      deploy rule ends and version-namespaced rooms begin (design
      already written up in the netplay checklist §6).
