# Build d'Edifice (HugeKilla)

Un seul code pour les trois systèmes : **SDL2** (fenêtre, entrées, audio) + **OpenGL 2.1** (profil legacy, fixed-function).
SDL2 est téléchargée et compilée **en statique** par CMake (`FetchContent`, SDL 2.32.10), de même que libopus 1.5.2 (codec du chat vocal, licence BSD) : les binaires ne dépendent que du système.

| OS | Résultat |
|---|---|
| macOS (Intel / Apple Silicon) | `Edifice.app` autonome (données dans `Contents/Resources/data`), signé ad hoc |
| Linux | `Edifice` + dossier `data/` à côté |
| Windows (MSVC, x64) | `Edifice.exe` + dossier `data/` à côté, CRT statique (avec `--gns` : DLL de GameNetworkingSockets et du runtime C++ à côté) |

`build.py` compile avec GameNetworkingSockets par défaut, comme les releases, pour le P2P des parties Internet : sans `VCPKG_ROOT`, il récupère vcpkg dans `build/vcpkg` (au commit `VCPKG_COMMIT` de la CI) ; le premier build compile ses bibliothèques (OpenSSL, protobuf, abseil), les suivants les réutilisent. `--no-gns` compile sans (parties Internet en UDP direct).

## Prérequis

Seulement la chaîne de compilation (+ Python 3) ; pas besoin d'installer SDL2.

- **macOS** : `xcode-select --install`, puis `brew install cmake ninja`
- **Linux (Debian/Ubuntu)** :
  ```bash
  sudo apt install build-essential cmake ninja-build libglu1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libwayland-dev libxkbcommon-dev libasound2-dev libpulse-dev libcurl4-openssl-dev
  ```
  (les en-têtes X11/Wayland/audio servent à compiler SDL ; à l'exécution SDL charge ce qui est présent ; libcurl sert à la liste des parties Internet, le jeu se compile sans, l'onglet Internet dit alors qu'il n'est pas disponible)
- **Windows** : Visual Studio 2019+ (charge « Développement Desktop en C++ »), CMake (fourni avec VS), Python 3. libcurl (liste des parties Internet) est téléchargé et compilé avec le TLS de Windows (Schannel), comme SDL2 ; `-DEDIFICE_LOBBY=OFF` s'en passe.

## Compiler

```bash
python3 build.py                     # Release avec P2P (GameNetworkingSockets) dans build/<os>-release-gns
```

```bash
python3 build.py --config Debug --run
```

```bash
python3 build.py --package           # dist/Edifice-<version>-<os>.zip|.tar.gz
```

Sous Windows : `py build.py ...`. Options : `--clean`, `--test` (tests unitaires après le build), `--generator <G>`, `EDIFICE_ARCH=Win32` pour du 32 bits, `--universal` (macOS : un binaire arm64 + x86_64, macOS 10.13 minimum, comme la release), `--no-gns` (sans GameNetworkingSockets, donc sans P2P : plus rapide à compiler, mais les parties Internet demandent des ports UDP ouverts ; voir ci-dessous). `EDIFICE_CMAKE_ARGS="-D…"` ajoute des arguments CMake.
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
  - `python3 tests/net_voice.py` : trois clients ; la voix n'est relayée qu'au joueur proche, sous le numéro de l'émetteur, et jamais si le message est malformé ;
  - `python3 tests/net_map.py --write /tmp/test.map`, puis `HK_AUTOSTART=1 HK_MAPFILE=/tmp/test.map <binaire> &` et `python3 tests/net_map.py /tmp/test.map` : un client qui rejoint un serveur lancé avec une carte reçoit son relief, son ciel et tous ses blocs.
  - `tests/run_netplay_tests.sh <binaire>` lance les six, un serveur neuf par test (sous Linux avec Xvfb si aucun écran) ; à lancer en local (pas dans la CI), de préférence contre un build AddressSanitizer (`EDIFICE_CMAKE_ARGS="-DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_C_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address" python3 build.py --config Debug --no-gns`).
- Partie à deux sur une machine : `HK_AUTOSTART=1 <binaire> &` puis `HK_JOIN=127.0.0.1 <binaire>` (le second rejoint le premier serveur qui répond). `HK_CONSOLE_LOG=1` recopie la console sur stderr. `HK_ANTICHEAT_LOG=1` écrit sur stderr les événements anti-triche du serveur (`docs/M11.md`).
- Parties Internet (lobby, M7c) : `python3 tests/net_lobby.py <binaire> <edifice-lobby> [<lobby_test>]` (Linux, avec un écran ou `xvfb-run`) lance le lobby de `server/`, un hôte qui s'y annonce et un client qui le rejoint par son code de partie ; `HK_LOBBY_URL=http://127.0.0.1:8080 lobby_test` teste seul le client du lobby.

Parties Internet : dans `net.ini`, `lobby=https://…` donne l'adresse du serveur `edifice-lobby` (par défaut l'instance publique `https://edifice.rousseaufournier.com` ; `lobby=` vide coupe les parties Internet, et l'onglet Internet de l'écran Join dit alors qu'aucun lobby n'est configuré), et `servervisibility=lan|public|friends|private` (ligne « Internet » de Server Setup) dit si l'hôte s'annonce : `public` est listée, `friends`/`private` se rejoignent par le code de partie (6 caractères, affiché dans la console et le tableau des scores de l'hôte). Avec GameNetworkingSockets (`EDIFICE_GNS`, ci-dessous), une partie Internet se rejoint en P2P (M7d) : rien à ouvrir chez l'hôte. Sans lui (ou avec `p2p=0`), c'est l'UDP direct vers l'IP vue par le lobby, et l'hôte doit ouvrir ses ports UDP (27020 et suivants).

## GameNetworkingSockets (M7, dans les releases)

GameNetworkingSockets (Valve, BSD-3), avec protobuf et OpenSSL, vient de vcpkg (fonction `gns` de `vcpkg.json`). Une fois vcpkg installé :

```bash
git clone https://github.com/microsoft/vcpkg && vcpkg/bootstrap-vcpkg.sh   # Windows : vcpkg\bootstrap-vcpkg.bat
export VCPKG_ROOT=$PWD/vcpkg
python3 build.py --package                       # build/<os>-release-gns, comme la release
python3 build.py --universal --package           # macOS : binaire universel, comme la release
```

Le premier build compile OpenSSL, protobuf et abseil (de quelques minutes à une demi-heure selon la machine), ensuite le cache binaire de vcpkg sert. Triplets : `x64-linux-release` (statique), `x64-windows-release` (`cmake/triplets/`, DLL : tout le jeu passe au runtime C++ partagé `/MD`, et les DLL du runtime sont copiées à côté de `Edifice.exe`), `universal-osx` (`cmake/triplets/`, statique, arm64 + x86_64) ; en Debug, ceux par défaut de vcpkg. `-DVCPKG_TARGET_TRIPLET=…` dans `EDIFICE_CMAKE_ARGS` en impose un autre. À la main : `cmake -S . -B build/gns -DEDIFICE_GNS=ON -DVCPKG_MANIFEST_FEATURES=gns -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` (Windows : `-DVCPKG_TARGET_TRIPLET=x64-windows -A x64`). Sans l'option, rien ne change, sur le fil non plus.

`python3 tests/check_package.py dist/Edifice-<version>-<os>.<ext> --gns` vérifie une archive comme la CI : contenu et licences, dépendances (`otool`, `readelf`, `dumpbin`), puis le jeu décompressé lancé en serveur GameNetworkingSockets, avec son propre dossier utilisateur et `servervisibility=lan`, et `tests/net_handshake.py` contre lui ; `--lobby <edifice-lobby>` y ajoute un lobby local en HTTPS (certificat auto-signé, `openssl`) où le jeu doit s'annoncer et ouvrir sa session P2P (la CI le fait sous Linux) ; `--no-run` saute le lancement (écran verrouillé sous macOS : le rendu bloque).

- `transport=gns` dans `net.ini` (ou `HK_TRANSPORT=gns`) : les parties par IP directe passent par une connexion GameNetworkingSockets chiffrée (`src/net/gnslink.cpp`, choix des liens dans `src/net/netlink.cpp`). L'hôte écoute en plus sur `gnsport` (défaut : `datasendport`, 27021) ; un client se connecte au `gnsport` du serveur qu'il interroge par IP. La recherche LAN et les pairs en UDP simple restent en UDP : un serveur en mode gns accepte aussi les clients UDP. Défaut : `transport=udp`.
- Parties Internet en P2P (M7d, `src/net/gnssignal.cpp`, détails dans `docs/M7.md`) : un hôte annoncé sur le lobby (`servervisibility` autre que `lan`) écoute aussi les connexions P2P sous l'identité de sa session de lobby ; le client qui rejoint une partie de l'onglet Internet (ou par son code) ouvre sa propre session et s'y connecte par ICE (STUN/TURN du lobby, signalisation par le lobby), avec repli sur l'UDP direct. Aucun port à ouvrir, même avec `transport=udp`. `net.ini` : `p2p=1` (défaut), `p2p=0` (jamais : UDP direct comme en M7c), `p2p=relay` (seulement le relais TURN du lobby) ; `HK_P2P` a priorité. `HK_GNS_DEBUG=1` affiche les messages de GameNetworkingSockets (ICE compris).
- `python3 tests/net_p2p.py build/gns/Edifice <edifice-lobby> build/gns/gns_p2p_test` : lance le lobby avec son TURN sur l'adresse LAN de la machine (GameNetworkingSockets n'utilise jamais les adresses de bouclage pour ICE), puis deux fois, en direct puis en relais TURN seul : deux processus `tests/gns_p2p_test.cpp` (`GnsLink` seul), puis un hôte du jeu et un client qui le rejoint par son code de partie (Linux, avec `xvfb-run` ; macOS avec `NET_P2P_GAMES=1`). Tout tourne sur une machine, donc sans NAT entre les joueurs : sont testés la signalisation, ICE, le relais TURN et le jeu par-dessus, pas la traversée de NAT elle-même.
- `tests/run_gns_netplay_test.sh build/gns/Edifice build/gns/gns_netplay_test` : deux clients C++ (`tests/gns_netplay_test.cpp`, `Sock` sur `GnsLink`) rejoignent un serveur en mode gns, reçoivent LANDINFO, jouent et parlent près de l'hôte ; puis une deuxième instance du jeu le rejoint (`HK_JOIN`) ; puis les tests UDP (`run_netplay_tests.sh`) contre des serveurs en mode gns.

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

`.github/workflows/release.yml` : à chaque tag `v*` poussé (`git tag v0.2.0 && git push origin v0.2.0`), GitHub Actions compile macOS, Linux et Windows avec `build.py --config Release --gns --package --test` (`--universal` sur macOS ; vcpkg épinglé par `VCPKG_COMMIT`, cache binaire gardé par `actions/cache`), vérifie chaque archive avec `tests/check_package.py`, puis publie une release GitHub avec les trois archives. À chaque push et PR, les archives sont aussi disponibles comme artefacts, sans release. Minimums : macOS 10.13 (Intel) / 11 (Apple Silicon), glibc 2.35 (Ubuntu 22.04 et plus récentes), Windows 10.

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

- **Server Setup** (bouton Server) : nom du serveur, mode (capture du drapeau / bac à sable), captures pour gagner, limite de temps, ciel, relief, décor, carte ; « Random » pour ciel/relief/décor et bouton « Randomize all ». « Map » : « New random » (un nouveau terrain, par défaut) ou une des cartes sauvegardées (voir plus bas), qui apporte alors son ciel, son relief, son décor et ses bâtiments. Sauvegardé dans `sv.ini` / `net.ini` (`servermap`). Clavier : Haut/Bas (ou Tab) choisit une ligne, Gauche/Droite la change, Entrée démarre, Échap revient.
- **Join a game** (bouton Play) : onglets LAN / IP (IP ou nom d'hôte, recherche LAN) et Internet (parties publiques du lobby, code de partie). Clavier : Tab passe d'un champ à l'autre, Entrée ajoute l'adresse / cherche le code ou rejoint le serveur choisi, Haut/Bas choisit, F5 rafraîchit, Échap revient.
- Tous les écrans 2D (menu principal, Join, Server Setup, Options, Credits, menu de construction, scores, chat, textes du HUD) écrivent avec la police du jeu en espacement proportionnel (`UIText` dans `src/ui/ui.cpp`, chasse mesurée sur la texture de la police) et partagent les mêmes panneaux et boutons (`UIPanel`, `UIButton`, `UIField`) ; seule la console garde une police à chasse fixe. La fenêtre est « high-DPI » (`SDL_WINDOW_ALLOW_HIGHDPI`, `NSHighResolutionCapable` ; sous Windows `permonitorv2` + `SDL_HINT_WINDOWS_DPI_SCALING`) : le rendu se fait à la taille réelle en pixels (2x sur Retina, captures `HK_SHOT` comprises), la mise en page reste en points.
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
Console : `help` liste les commandes, `Tab` complète une commande, une variable ou le nom de carte de `loadmap` (les candidats s'affichent au-dessus de l'invite), `Haut`/`Bas` historique, `Gauche`/`Droite`/`Début`/`Fin`/`Suppr` édition, `PgUp`/`PgDn` ou la molette font défiler, `clear` vide la console.
Les lettres suivent la disposition du clavier : sur AZERTY, remappez Avancer/Gauche sur `z`/`q` dans Options → Controls.

## Aides au debug

- `HK_AUTOSTART=1` démarre un serveur local directement (avec `HK_MAPFILE=<fichier .map>` : sur cette carte).
- `HK_ZOOM_TEST=1` simule un appui sur la touche de zoom (zoom ×3).
- `HK_SEED=N` génère le même monde à chaque lancement (pour comparer des captures).
- `HK_KEYLOG=1` affiche dans le terminal chaque touche reçue par le jeu (code VK) et les changements de zoom.
- `HK_MENU=options|options2|player|credits|join|internet|server|serverstart` ouvre cet écran au démarrage (`internet` : l'onglet Internet de Join) ; `build`, `scores` et `chat` démarrent un serveur local et ouvrent le menu de construction, les scores ou le chat.
- `HK_LOBBYJOIN=<code>` cherche ce code de partie sur le lobby (`net.ini` `lobby=`) et rejoint la partie, par l'onglet Internet (en P2P si elle a une identité, voir GameNetworkingSockets).
- `HK_P2P=0|1|relay` remplace `p2p=` de `net.ini` ; `HK_GNS_DEBUG=1` affiche les messages de GameNetworkingSockets. `HK_SIGNAL_TCP=1` : signalisation par le relais TCP même si le lobby propose son relais WebSocket (`wss://`). `EDIFICE_TEST_CA_FILE=cert.pem` (tests seulement) : certificat PEM d'un lobby auto-signé, accepté en plus de ceux du système pour le lobby et la signalisation (la vérification reste faite).
- `HK_CONSOLE=1` démarre un serveur local et ouvre la console après quelques commandes (`help`, `maps`, …) ; `HK_CONSOLE=scroll` la fait défiler vers le haut, `HK_CONSOLE=notify` la laisse fermée (dernières lignes sur le jeu), toute autre valeur reste tapée à l'invite (`HK_CONSOLE=gr` : complétion).
- `HK_SHOT=fichier.ppm` (et `HK_SHOT_FRAME=N`) enregistre l'image rendue N (sans VSync : sous macOS, l'échange d'images attend sinon indéfiniment quand l'écran est en veille).
- `HK_USERDIR=<dossier/>` remplace le dossier utilisateur (`control.ini`, `net.ini`, `sv.ini`, cartes) : des essais (captures, taille de fenêtre `screenwidth`/`screenheight`) sans toucher aux vrais réglages.
- Chat vocal : `HK_VOICE_TEST_TONE=1` émet une tonalité à la place du micro, `HK_VOICE_AUTOTALK=1` émet en permanence (sans touche), `HK_VOICE_LOOPBACK=1` fait entendre sa propre voix par le chemin de réception, `HK_VOICE_LOG=1` affiche sur stderr chaque début de prise de parole entendue.
