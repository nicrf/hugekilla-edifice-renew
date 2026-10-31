# Build d'Edifice (HugeKilla)

Un seul code pour les trois systèmes : **SDL2** (fenêtre, entrées, audio) + **OpenGL 2.1** (profil legacy, fixed-function).
SDL2 est téléchargée et compilée **en statique** par CMake (`FetchContent`, SDL 2.32.10), de même que libopus 1.5.2 (codec du chat vocal, licence BSD) : les binaires ne dépendent que du système.

| OS | Résultat |
|---|---|
| macOS (Intel / Apple Silicon) | `Edifice.app` autonome (données dans `Contents/Resources/data`), signé ad hoc |
| Linux | `Edifice` + dossier `data/` à côté |
| Windows (MSVC, x64) | `Edifice.exe` + dossier `data/` à côté, CRT statique |

## Prérequis

Seulement la chaîne de compilation (+ Python 3) ; pas besoin d'installer SDL2.

- **macOS** : `xcode-select --install`, puis `brew install cmake ninja`
- **Linux (Debian/Ubuntu)** :
  ```bash
  sudo apt install build-essential cmake ninja-build libglu1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libwayland-dev libxkbcommon-dev libasound2-dev libpulse-dev
  ```
  (les en-têtes X11/Wayland/audio servent à compiler SDL ; à l'exécution SDL charge ce qui est présent)
- **Windows** : Visual Studio 2019+ (charge « Développement Desktop en C++ »), CMake (fourni avec VS), Python 3.

## Compiler

```bash
python3 build.py                     # Release dans build/<os>-release
```

```bash
python3 build.py --config Debug --run
```

```bash
python3 build.py --package           # dist/Edifice-<version>-<os>.zip|.tar.gz
```

Sous Windows : `py build.py ...`. Options : `--clean`, `--generator <G>`, `EDIFICE_ARCH=Win32` pour du 32 bits.
`-DEDIFICE_SYSTEM_SDL2=ON` (CMake) utilise une SDL2 installée au lieu de la compiler.
La version vient de `project(Edifice VERSION …)` dans `CMakeLists.txt`.

Avec les presets CMake : `cmake --preset macos-release && cmake --build --preset macos-release` (ou `linux-release`, `windows-vs` / `windows-release`).

## Tests

- `ctest --test-dir build/<os>-release -C Release` : tests unitaires sans fenêtre (valeurs de référence du générateur du monde, `tests/worldrandom_test.cpp` ; lecture/écriture little-endian bornée de `src/net/wire.h`, `tests/wire_test.cpp` ; message VOICE, règles de distance et aller-retour opus, `tests/voice_test.cpp`), lancés par la CI sur les trois OS.
- Tests réseau contre un serveur local (`HK_AUTOSTART=1 <binaire> &`, puis) :
  - `python3 tests/net_handshake.py` : version dans INFO, refus des versions incompatibles ;
  - `python3 tests/net_identity.py` : une nouvelle demande garde son slot, un autre client a le sien ;
  - `python3 tests/net_join.py` : rejoint la partie comme un vrai client et décode PLAYERNAMES, LANDINFO, SERVERVARS, etc. (tailles et valeurs du format explicite), puis envoie des messages tronqués ;
  - `python3 tests/net_fuzz.py` : paquets malformés, à lancer de préférence contre un build AddressSanitizer (voir l'en-tête du script) ;
  - `python3 tests/net_voice.py` : trois clients ; la voix n'est relayée qu'au joueur proche (y compris de l'autre côté du bord du monde), sous le numéro de l'émetteur, et jamais si le message est malformé.
  - `tests/run_netplay_tests.sh <binaire>` lance les cinq, un serveur neuf par test (sous Linux avec Xvfb si aucun écran) ; la CI le fait à chaque push contre un build AddressSanitizer, et une release n'est publiée que s'ils passent.
- Partie à deux sur une machine : `HK_AUTOSTART=1 <binaire> &` puis `HK_JOIN=127.0.0.1 <binaire>` (le second rejoint le premier serveur qui répond).

Format réseau (protocole 5) : aucun struct n'est copié tel quel sur le fil ; chaque message a ses fonctions d'écriture et de lecture explicites (`Sync*` dans `src/net/multiplay.cpp`, `WriteSimple`/`ReadSimple` dans `src/net/sock.cpp`, primitives dans `src/net/wire.h`). Changer un de ces formats impose d'incrémenter `EDIFICE_PROTOCOL_VERSION` (`src/net/version.h`).

## Releases automatiques

`.github/workflows/release.yml` : à chaque tag `v*` poussé (`git tag v0.2.0 && git push origin v0.2.0`), GitHub Actions compile macOS, Linux et Windows avec `build.py --package` et publie une release GitHub avec les trois archives. Lancement manuel possible (« Run workflow ») : les archives sont alors disponibles comme artefacts, sans release.

## Arborescence

```
src/game/      boucle de jeu, terrain (land*), structures, physique, config, vector, StdAfx.h, hugekilla.cpp
src/render/    rendu OpenGL (redrawing), textures, modèles .cob (ase)
src/net/       sockets UDP (sock), protocole et serveur/client (multiplay*)
src/ui/        menus en jeu, console, Options, Server Setup, crédits
src/audio/     sound_sdl.cpp (SDL) ; sound.cpp = DirectSound d'origine, plus compilé
src/platform/  platform_sdl.cpp : main SDL (fenêtre, contexte GL, entrées)
src/compat/    win32compat.* : émulation Win32 (POSIX) / redirections SDL (Windows) ; include/ = faux windows.h, gl/gl.h… (POSIX)
win32/         hugekilla.rc, resource.h, icônes ; .sln/.vcproj d'origine pour référence
data/          données du jeu (ini, textures, sons, objets, Structures)
```

Le code Win32 natif d'origine (WinMain, DirectSound, boîtes de dialogue) reste lisible dans les sources derrière `HK_WIN32_NATIVE`, jamais défini.

## Données

`EnterDataDir` (`src/platform/platform_sdl.cpp`) cherche `control.ini`, dans l'ordre : premier argument, `HUGEKILLA_DATA`, `<dossier de l'exécutable>/data` (= `Contents/Resources/data` dans le bundle), puis `data/` du dépôt (builds Debug seulement).

Les réglages que le jeu réécrit (`control.ini`, `net.ini`, `sv.ini`) et les captures d'écran (`screenshot-*.bmp`) sont dans le dossier utilisateur, copiés depuis `data/` au premier lancement : `~/Library/Application Support/Edifice` (macOS), `%APPDATA%\Edifice` (Windows), `~/.local/share/Edifice` (Linux). Le dossier de données (et le `.app` signé) n'est jamais modifié ; supprimez un fichier du dossier utilisateur pour revenir aux valeurs par défaut.

Les données viennent de Windows (insensible à la casse) : hors Windows, `HKPath` retrouve un chemin sans tenir compte de la casse. Les `.ini` et `Structures/*.txt` sont en CRLF et le code s'appuie sur le `\r` : `.gitattributes` marque `data/**` en `-text`.

## Écrans ajoutés

- **Server Setup** (bouton Server) : nom du serveur, mode (capture du drapeau / bac à sable), captures pour gagner, limite de temps, ciel, relief, décor ; « Random » pour ciel/relief/décor et bouton « Randomize all ». Sauvegardé dans `sv.ini` / `net.ini`.
- **Multiplayer Game** (bouton Play) : IP ou nom d'hôte, recherche LAN (F5).
- **Options** : touches, vidéo/audio (plein écran, VSync, distance d'affichage, souris, volume, volume des voix, micro), nom du joueur.
- **Credits** : auteurs originaux.

## Chat vocal

Push-to-talk de proximité : maintenir `T` (Options → Controls → « Push to talk », `pushtotalk` dans `control.ini`) pour parler. Seuls les joueurs à moins de 40 unités vous entendent (la distance d'affichage est de 57) : plein volume jusqu'à 8 unités, puis de plus en plus faible, et la voix vient de la position du joueur (gauche/droite). Toutes équipes confondues. Constantes dans `src/audio/voice.h`.

- Capture SDL 48 kHz mono ouverte au premier appui, active seulement pendant l'appui ; trames opus de 20 ms (~24 kbit/s) envoyées en non fiable (message `VOICE`, `src/net/voicemsg.h`).
- Le serveur remplace le numéro de joueur par celui du client, ignore les joueurs hors partie et ne relaie qu'aux joueurs proches ; l'hôte entend directement les clients proches.
- Réception : un décodeur opus et un petit tampon de gigue par joueur (ordre par numéro de séquence, trames en retard ou en double ignorées, trame perdue masquée par opus), mixés dans le callback audio (`src/audio/sound_sdl.cpp`, désormais à 48 kHz).
- HUD : « Mic on » pendant l'émission, « Speaking: nom » pour les joueurs entendus.
- Options → Video/Audio : « Voice volume » (`voicevolume`, 0–100) et « Microphone » (`voicemic`, 0/1).
- macOS demande l'accès au micro au premier appui (`NSMicrophoneUsageDescription` dans `cmake/MacOSXBundleInfo.plist.in`). Sans micro ou sans autorisation, rien n'est émis (message unique sur stderr).

## Raccourcis

`F6` plein écran · `` ` `` console · `Échap` libérer/capturer la souris.
Les lettres suivent la disposition du clavier : sur AZERTY, remappez Avancer/Gauche sur `z`/`q` dans Options → Controls.

## Aides au debug

- `HK_AUTOSTART=1` démarre un serveur local directement.
- `HK_ZOOM_TEST=1` simule un appui sur la touche de zoom (zoom ×3).
- `HK_SEED=N` génère le même monde à chaque lancement (pour comparer des captures).
- `HK_KEYLOG=1` affiche dans le terminal chaque touche reçue par le jeu (code VK) et les changements de zoom.
- `HK_MENU=options|options2|player|credits|join|server|serverstart` ouvre cet écran au démarrage.
- `HK_SHOT=fichier.ppm` (et `HK_SHOT_FRAME=N`) enregistre l'image rendue N.
- Chat vocal : `HK_VOICE_TEST_TONE=1` émet une tonalité à la place du micro, `HK_VOICE_AUTOTALK=1` émet en permanence (sans touche), `HK_VOICE_LOOPBACK=1` fait entendre sa propre voix par le chemin de réception, `HK_VOICE_LOG=1` affiche sur stderr chaque début de prise de parole entendue.
