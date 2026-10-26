# Trophy Manager

*A native PS5 app to see, unlock, lock and back up the trophies of your games, from the console itself, with the controller.*

**English:** Trophy Manager lists the PS5 games installed on a jailbroken console with their trophy progress, opens a game to show every trophy (picture, name, description, grade, date) in the console's language, and lets you unlock or lock one trophy, a ticked selection, or all of them (the platinum last, with a few seconds between trophies, like a real run). Every edit saves a copy of the trophy file first (the first copy is kept for good, plus the last eight), writes the new file beside the old one, checks it and only then swaps it, and **Restore original** puts the first copy back. An unlock shows an in-app pop-up. Nothing needs a PC and no payload is injected: the app reads and writes the console's local trophy files through the Elf Arsenal HTTP (6969) and FTP (2121) servers on `127.0.0.1`. It refuses to edit unless the PSN block (NanoDNS) is on and no other title is running, so nothing is ever sent to PSN. English, Portuguese and Spanish, picked from the console language (L1 switches it). PS5 games only; PS4 trophies are encrypted and not supported.

**Português:** o Trophy Manager lista os jogos de PS5 do console com o progresso de troféus, abre um jogo para mostrar todos os troféus (imagem, nome, descrição, tipo e data) no idioma do console e permite desbloquear ou bloquear um troféu, uma seleção marcada ou todos de uma vez (a platina por último, com alguns segundos entre os troféus). Antes de cada edição o app guarda uma cópia do arquivo de troféus (a primeira fica para sempre, mais as oito últimas); **Restaurar o original** devolve a primeira. Tudo roda no console, sem PC e sem injetar payload. Só edita com o bloqueio da PSN ligado e nenhum outro título aberto, então nada é enviado à PSN. Só jogos de PS5; os troféus de PS4 são criptografados e não são suportados.

**Español:** Trophy Manager muestra los juegos de PS5 de la consola con su progreso de trofeos, abre un juego para ver todos sus trofeos en el idioma de la consola y permite desbloquear o bloquear uno, una selección marcada o todos (el platino al final). Antes de cada edición guarda una copia del archivo (la primera se conserva siempre, más las ocho últimas); **Restaurar el original** la devuelve. Todo ocurre en la consola, sin PC ni payload. Solo edita con el bloqueo de la PSN activado y ningún otro título abierto, así que nada se envía a la PSN. Solo juegos de PS5.

## Controls

| Screen | Button | Action |
| --- | --- | --- |
| Games | X | Open the game |
| Games | Square | Refresh the list |
| Trophies | X | Unlock / lock the trophy (or open the menu when some are ticked) |
| Trophies | Square | Tick / untick the trophy |
| Trophies | R1 | Tick all, or clear when all are ticked |
| Trophies | Triangle / Options | Menu: unlock or lock the selection, select all, clear, unlock all, lock all, restore original |
| Both | L2 / R2 | Page up / down |
| Both | L1 | Change language |
| Trophies | O | Back |

## Requirements

- A jailbroken PS5 (only **firmware 11.60** with OnionHEN has been tested) running Elf Arsenal and ShadowMountPlus.
- The **PSN block** on (Elf Arsenal's NanoDNS). Without it the app only reads.
- The console clock set (trophy times are written from it).

## Safety and honest limits

- Edits change the *local* trophy file only. With the PSN block on, trophies never sync to PSN and the global trophy level is not changed. Do not use this to fake progress online.
- The format was reverse-engineered from real files, and the unlock model reproduces a real console unlock byte for byte. It is tested on firmware 11.60 only, on a handful of games. Keep the backups (`/data/trophy-manager/<NPWR>/`) until you have checked the PS5's own trophy list.
- Dates are shown in UTC (the console's C library has no usable time zone).
- Prior art that made this possible and that this project learned from: [KIK-Trophy](https://github.com/kiikeep/KIK-Trophy) (a Windows tool that edits `TRPTITLE.DAT` and documents `TROPHY.UCP`) and [PS5-PHU-Trophy-System](https://github.com/ArkSama/PS5-PHU-Trophy-System) (a payload that unlocks through the system). Trophy Manager differs in running natively on the console, with no PC and no payload injection.

## Build

Linux/WSL with `clang-18`: `make` builds `dist/PPSA99842/` and its zip; `make deploy PS5_HOST=<ip> APP_PARAM=trophy-sce/param.json` sends it over FTP. The host tests (`tests/test_t2pd.cpp`, hashes and the real unlock/lock model) build with `clang++ -std=c++20 tests/test_t2pd.cpp`; they need real trophy files under `tests/fixtures/`, which are not in the repository because they hold account ids.

The app is a media app (`PPSA99842`); it may appear under Games until the console re-registers it.

## Credits and license

GPL-3.0-or-later. Built on the [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) and [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) by BlackBearReloaded, and on the code of [Estante](https://github.com/kauansilvalopes/estante). See `THIRD_PARTY_NOTICES.md`.

Autor: Kauan ([@kndofc](https://instagram.com/kndofc)).
