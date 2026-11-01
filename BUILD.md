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

- `ctest --test-dir build/<os>-release -C Release` : tests unitaires sans fenêtre (valeurs de référence du générateur du monde, `tests/worldrandom_test.cpp` ; lecture/écriture little-endian bornée de `src/net/wire.h`, `tests/wire_test.cpp` ; message VOICE, règles de distance et aller-retour opus, `tests/voice_test.cpp` ; cartes sauvegardées : aller-retour, fichiers corrompus, tronqués ou trop gros refusés, sauvegardes d'origine construites à la main, `tests/map_test.cpp`), lancés par la CI sur les trois OS. `map_test --check <fichier>` dit si un fichier est une carte valide.
- Tests réseau contre un serveur local (`HK_AUTOSTART=1 <binaire> &`, puis) :
  - `python3 tests/net_handshake.py` : version dans INFO, refus des versions incompatibles ;
  - `python3 tests/net_identity.py` : une nouvelle demande garde son slot, un autre client a le sien ;
  - `python3 tests/net_join.py` : rejoint la partie comme un vrai client et décode PLAYERNAMES, LANDINFO, SERVERVARS, etc. (tailles et valeurs du format explicite), puis envoie des messages tronqués ;
  - `python3 tests/net_fuzz.py` : paquets malformés, à lancer de préférence contre un build AddressSanitizer (voir l'en-tête du script) ;
  - `python3 tests/net_voice.py` : trois clients ; la voix n'est relayée qu'au joueur proche (y compris de l'autre côté du bord du monde), sous le numéro de l'émetteur, et jamais si le message est malformé ;
  - `python3 tests/net_map.py --write /tmp/test.map`, puis `HK_AUTOSTART=1 HK_MAPFILE=/tmp/test.map <binaire> &` et `python3 tests/net_map.py /tmp/test.map` : un client qui rejoint un serveur lancé avec une carte reçoit son relief, son ciel et tous ses blocs.
  - `tests/run_netplay_tests.sh <binaire>` lance les six, un serveur neuf par test (sous Linux avec Xvfb si aucun écran) ; la CI le fait à chaque push contre un build AddressSanitizer, et une release n'est publiée que s'ils passent.
- Partie à deux sur une machine : `HK_AUTOSTART=1 <binaire> &` puis `HK_JOIN=127.0.0.1 <binaire>` (le second rejoint le premier serveur qui répond). `HK_CONSOLE_LOG=1` recopie la console sur stderr.

## GameNetworkingSockets (expérimental, M7)

Build avec vcpkg : `cmake -S . -B build/gns -DEDIFICE_GNS=ON -DVCPKG_MANIFEST_FEATURES=gns -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` (Windows : `-DVCPKG_TARGET_TRIPLET=x64-windows -A x64`, tout le jeu passe alors au runtime C++ partagé `/MD`). Sans l'option, rien ne change, sur le fil non plus.

- `transport=gns` dans `net.ini` (ou `HK_TRANSPORT=gns`) : les parties par IP directe passent par une connexion GameNetworkingSockets chiffrée (`src/net/gnslink.cpp`, choix des liens dans `src/net/netlink.cpp`). L'hôte écoute en plus sur `gnsport` (défaut : `datasendport`, 27021) ; un client se connecte au `gnsport` du serveur qu'il interroge par IP. La recherche LAN et les pairs en UDP simple restent en UDP : un serveur en mode gns accepte aussi les clients UDP. Défaut : `transport=udp`.
- `tests/run_gns_netplay_test.sh build/gns/Edifice build/gns/gns_netplay_test` : deux clients C++ (`tests/gns_netplay_test.cpp`, `Sock` sur `GnsLink`) rejoignent un serveur en mode gns, reçoivent LANDINFO, jouent et parlent près de l'hôte ; puis une deuxième instance du jeu le rejoint (`HK_JOIN`) ; puis les tests UDP (`run_netplay_tests.sh`) contre des serveurs en mode gns. Lancé par le job CI `gns` sous Linux.

Format réseau (protocole 5) : aucun struct n'est copié tel quel sur le fil ; chaque message a ses fonctions d'écriture et de lecture explicites (`Sync*` dans `src/net/netmsg.h` et `landwire.h`, `WriteSimple`/`ReadSimple` dans `src/net/sock.cpp`, primitives dans `src/net/wire.h`). Changer un de ces formats impose d'incrémenter `EDIFICE_PROTOCOL_VERSION` (`src/net/version.h`).

## Fuzzing

Un harnais libFuzzer par parseur d'entrée réseau ou fichier, dans `tests/fuzz/` : `Sock::Receive` (découpage des datagrammes, transferts fiables, ACK, alimenté sans socket par `Sock::Feed`), chaque message (`fuzz_position`, `fuzz_firegun`, `fuzz_killedposition`, `fuzz_updateserverobject`, `fuzz_snapshot`, `fuzz_servervars`, `fuzz_rgd`, `fuzz_landinfo`, `fuzz_simple`, `fuzz_hello` pour REQUESTENTER/INFO et le trailer de version, `fuzz_playernames`, `fuzz_chat`, `fuzz_blocks`, `fuzz_voice`) et les cartes (`fuzz_map`, `fuzz_map_legacy`). Les parseurs sont dans `src/net/netmsg.h`, `landwire.h`, `voicemsg.h`, `version.cpp`, `sock.cpp` et `src/game/mapfile.cpp`, appelables sans le jeu.

```bash
cmake -S . -B build/fuzz -G Ninja -DEDIFICE_FUZZ=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/fuzz --target fuzz
ctest --test-dir build/fuzz -R '^fuzz_'          # chaque corpus de départ une fois
mkdir -p /tmp/c && cp tests/fuzz/corpus/fuzz_map/* /tmp/c
build/fuzz/fuzz_map -max_total_time=60 /tmp/c    # fuzzing (le corpus grandit dans /tmp/c)
```

- Avec un clang qui a libFuzzer (Linux, ou `brew install llvm` sur macOS : `-DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++`), les harnais sont compilés avec `-fsanitize=fuzzer,address,undefined`. Apple clang n'a pas libFuzzer : les harnais sont alors des programmes qui passent une fois les fichiers ou dossiers donnés (`tests/fuzz/fuzz_main.cpp`), avec AddressSanitizer et UBSan.
- Corpus de départ dans `tests/fuzz/corpus/<harnais>/`, produits par les écrivains du jeu : `fuzz_make_seeds tests/fuzz/corpus` et `map_test --write tests/fuzz/corpus/fuzz_map tests/fuzz/corpus/fuzz_map_legacy`. `NET_JOIN_DUMP=<dossier> python3 tests/net_join.py` enregistre en plus les messages et datagrammes envoyés par un vrai serveur.
- La CI (job `fuzzing`, ubuntu-22.04, clang) passe les corpus puis fuzze chaque harnais 60 s ; un crash fait échouer le job et l'entrée fautive est publiée en artefact (`fuzz-crashes`). Pour la rejouer : `build/fuzz/fuzz_<nom> <fichier>`.

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

- **Server Setup** (bouton Server) : nom du serveur, mode (capture du drapeau / bac à sable), captures pour gagner, limite de temps, ciel, relief, décor, carte ; « Random » pour ciel/relief/décor et bouton « Randomize all ». « Map » : « New random » (un nouveau terrain, par défaut) ou une des cartes sauvegardées (voir plus bas), qui apporte alors son ciel, son relief, son décor et ses bâtiments. Sauvegardé dans `sv.ini` / `net.ini` (`servermap`).
- **Multiplayer Game** (bouton Play) : IP ou nom d'hôte, recherche LAN (F5).
- **Options** : touches, vidéo/audio (plein écran, VSync, distance d'affichage, souris, volume, volume des voix, micro), nom du joueur.
- **Credits** : auteurs originaux.

## Cartes sauvegardées

Le terrain (relief, ciel, décor) et tous les bâtiments, dans `maps/<nom>.map` du dossier utilisateur (voir Données). Commandes de console (`` ` ``) :

- `savemap <nom>` : enregistre la partie en cours (serveur ou client) ; le nom ne contient que lettres, chiffres, `_` et `-` (32 au plus), un fichier existant est remplacé ;
- `loadmap <nom>` : sur le serveur, remplace le monde par la carte pour tout le monde (les clients la reçoivent comme un nouveau terrain : LANDINFO, puis tous les blocs) ; hors partie, démarre un serveur avec cette carte ; refusé quand on est client d'un autre serveur ;
- `maps` : liste les cartes enregistrées.

Un serveur lancé avec une carte (`loadmap` ou Server Setup) la recharge à chaque nouvelle manche, avec ses bâtiments d'origine. Debug : `HK_AUTOSTART=1 HK_MAPFILE=<chemin absolu>` démarre le serveur avec ce fichier (le jeu se place dans son dossier de données : un chemin relatif partirait de là).

Format 2, décrit dans `src/game/mapfile.h` : en-tête `EDIFICEMAP`, version du format, protocole (informatif) et dimensions de la grille, puis le ciel, les `LandDefs` et les blocs écrits champ par champ en little-endian, en plages de cellules identiques (quelques Ko pour un monde peu construit), et un CRC-32. Un fichier tronqué, trop gros, corrompu, d'un autre format ou d'une autre taille de monde est refusé avec un message ; les noms de textures et les compteurs sont bornés comme ceux d'un message LANDINFO. Les sauvegardes du jeu d'origine (Windows, 2000 : `edifice` suivi des structs brutes, environ 55 Mo) se chargent aussi : les copier dans `maps/` avec l'extension `.map`. Leur relief, leur ciel et leurs bâtiments sont repris ; le décor (arbres, taches du sol) diffère, le générateur du monde ayant changé au protocole 3.

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

- `HK_AUTOSTART=1` démarre un serveur local directement (avec `HK_MAPFILE=<fichier .map>` : sur cette carte).
- `HK_ZOOM_TEST=1` simule un appui sur la touche de zoom (zoom ×3).
- `HK_SEED=N` génère le même monde à chaque lancement (pour comparer des captures).
- `HK_KEYLOG=1` affiche dans le terminal chaque touche reçue par le jeu (code VK) et les changements de zoom.
- `HK_MENU=options|options2|player|credits|join|server|serverstart` ouvre cet écran au démarrage.
- `HK_SHOT=fichier.ppm` (et `HK_SHOT_FRAME=N`) enregistre l'image rendue N.
- Chat vocal : `HK_VOICE_TEST_TONE=1` émet une tonalité à la place du micro, `HK_VOICE_AUTOTALK=1` émet en permanence (sans touche), `HK_VOICE_LOOPBACK=1` fait entendre sa propre voix par le chemin de réception, `HK_VOICE_LOG=1` affiche sur stderr chaque début de prise de parole entendue.
