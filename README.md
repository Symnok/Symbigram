# Symbigram

A Telegram client for Symbian S^3 / Anna / Belle, ported from
[Lumigram](../../Lumigram) / [LumigramPlus](../../LumigramPlus) for Windows Phone 8.1.
Qt 4.7.4 / Qt Quick 1.1, the same shell as JasmineKICQ, SimpleVKM-Symbian and SimpleOKM-Symbian.

It speaks MTProto 2.0 straight to Telegram's datacenters - no bridge, no proxy, no server of
its own. The authorisation key is generated on the phone by Diffie-Hellman and never leaves it.

What it does: sign-in by QR code (scan from a signed-in Telegram) or by phone number and the
code Telegram sends, plus the two-step verification password when the account has one; the
token / phone migration to the account's datacenter is handled. The chat list with unread counts, mute marks and pinned chats,
message history with older pages, sending text with delivery and read marks, replies and
forwards shown as such, groups and channels (with sender names), typing notifications both
ways, presence ("last seen"), finding people by @username, phone number or name, mute /
clear history / delete messages, and Symbian notifications (discreet popup, vibration, the
"new messages" query) for messages that arrive while another app is in front - muted chats
stay quiet. Attachments are shown as notes ("[photo]", "[voice message 0:12]", "[file: ...]"),
not downloaded. English, Russian and Ukrainian.

## Layout

- `src/core/` - the protocol, pure QtCore + QtNetwork, shared with the desktop harness:
  - `tl*` - TL serialisation; `tlobject` walks the generated schema table
    (`tlschema_data.cpp`, layer 228, from `tools/generate-schema.py`) so no parser is written by hand.
  - `bigint`, `sha2`, `aes` (AES-256 IGE), `random`, `inflate` - what Qt 4.7 does not provide.
  - `telegramservers` (RSA keys, datacenters), `dh` (pq factoring, safe-prime checks),
    `authkeyhandshake` (the DH exchange, heavy arithmetic in a thread), `mtprotosession`
    (MTProto 2.0 encryption), `mtprototransport` (intermediate framing over QTcpSocket),
    `mtprotoclient` (requests, containers, salts, clock, pings, updates).
  - `tgapi` (request builders, response readers, the users/chats cache), `srp` (two-step
    verification), `qrcode` (the QR encoder), `telegramsession` (login flow, dialogs,
    history, sending, the update stream with pts tracking, the stored session).
- `src/app/` - `AppController` (network session, reconnection, settings, notifications),
  `ChatsModel`, `MessagesModel`, `Notifier`, `QrImageProvider`.
- `qml/` - Login (QR / password), Chats, Chat, Settings, About pages.
- `tools/symbigram-cli/` - console harness: crypto self test, QR dump, and a live session
  driven by commands from `sgm-cmd.txt`; the way the protocol was verified against Telegram.

## Credentials

Copy `credentials.cfg.template` to `credentials.cfg` and fill in your `api_id` / `api_hash`
from https://my.telegram.org. qmake turns it into `src/core/tgcredentials.h` (both gitignored).

## Building

Phone: `build-symbian.cmd` (bumps the patch version, builds ARMv5 release, packages
`Symbigram_<ver>.sis` for Belle and `Symbigram_installer_<ver>.sis` for Anna/S^3).

Desktop (Qt 4.7.4 MinGW from the Qt SDK, which ships the Symbian components):

    mkdir build-desktop && cd build-desktop
    qmake ../Symbigram.pro -spec win32-g++ CONFIG+=release && mingw32-make

Harness: the same with `tools/symbigram-cli/symbigram-cli.pro`; `symbigram-cli selftest`,
`symbigram-cli run` (writes the QR code to `qr.png`; then `dialogs`, `history <n>`,
`send <n|self> <text>`, `find <query>`, `password <pw>`, `logout`, `quit` in `sgm-cmd.txt`).

Environment for desktop testing: `SGM_DATA_DIR` (where `session.dat` lives), `SGM_LOG_FILE`
(qDebug and QML errors to a file), `SGM_SHOT_DIR` (main.qml walks the pages and saves
screenshots). After changing QML in the shadow build delete `release/qrc_qml.cpp` before
`mingw32-make`.

Translations: `lupdate -extensions qml,cpp,h -no-obsolete src qml -ts translations/*.ts`
(the Simulator kit's lupdate), `python tools/translate.py`, then `lrelease translations/*.ts`.

## Notes

- The session (the auth key, the datacenter, the update position) is `session.dat` in the
  app's data folder. It is a credential: whoever holds it is signed in until the session is
  ended from Telegram's Devices list or by signing out here.
- A 2048-bit modular exponentiation takes ~50 ms on a PC and a good deal longer on the phone;
  the handshake (once per datacenter) and the SRP proof run in worker threads.
- Telegram's servers are reached over plain TCP on port 443 (MTProto is its own encryption);
  where they are blocked, so is this client.

## Development

This code was written with the assistance of [Claude Code](https://claude.com/claude-code),
Anthropic's agentic coding tool.

Telegram channel and discussion group: https://t.me/symbigram_news_channel
