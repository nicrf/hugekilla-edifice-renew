# Feuille de route Edifice / HugeKilla : port, netplay sûr, versionnage, Steam

> Document de planification rédigé en lecture seule. Les références `fichier:ligne` correspondent à l'état actuel du dossier. `multiplay.cpp` est en cours d'édition par l'agent du port macOS, donc ses numéros de ligne peuvent avoir glissé de 1 à 4.

## 1. Synthèse

- **Le netplay n'a aucune version.** `RequestEnter{type,namelen,name[32]}` et `Info{type,namelen,name[100]}` (multiplaystruct.h:46-58) ne transportent ni version de jeu ni version de protocole. Le seul « version » du code est `MAKEWORD(2,2)` de Winsock (multiplay.cpp:125). Un build incompatible se connecte quand même. Il se désynchronise ensuite, ou corrompt la mémoire, sans afficher le moindre message.
- **La couche réseau n'est pas sûre.** Il y a plus de dix écritures mémoire hors limites déclenchables à distance, dans sock.cpp et multiplay.cpp. Exposer des serveurs sur Internet ou via Steam avant de les corriger reviendrait à ouvrir une exécution de code à distance.
- **Deux sources d'incompatibilité silencieuse existent déjà :**
  - Le build Debug|Win32 est compilé en `/Zp1` (hugekilla.vcproj:152, `StructMemberAlignment="1"`). Release et clang macOS utilisent l'alignement naturel.
  - Le décor et les « blotches » sont générés avec `rand()`, dont l'implémentation diffère entre la CRT MSVC et la libc macOS (land.cpp:18, 635-667, 488-495).
- **Ordre retenu :** stabiliser le port, puis la version netplay, puis les correctifs mémoire, puis l'anti-usurpation, puis le terrain déterministe, puis l'outillage (CMake, CI, tests), puis la sérialisation explicite, puis l'abstraction du transport, puis Steam, et enfin la restructuration lourde.

## 2. Dépendances entre les chantiers

| Chantier | Dépend de | Raison |
|---|---|---|
| Version netplay (M1) | Feu vert du port (M0) | Touche `multiplay.cpp` : JoinScreen*, ClientLoop, dialogue Win32 |
| Durcissement (M2, M3) | M1 (taille reçue lue) | La validation des longueurs a besoin de la taille réelle du paquet |
| Terrain déterministe (M4) | M1 | Change le terrain à seed égal : incrément obligatoire de la version de protocole |
| Sérialisation explicite (M6) | M2, M5 (tests) | Migration message par message, avec des tests aller-retour et du fuzzing |
| Abstraction transport (M7) | M6 recommandé, M0 obligatoire | Évite de toucher deux fois aux mêmes lignes |
| **Steam (M8-M10)** | **M2, M3, M7** | SDR authentifie l'identité Steam, mais un pair malveillant reste un pair : indices, chaînes et tailles doivent déjà être bornés |
| Serveur autoritaire (M11) | M7 | Modèle de messages entièrement nouveau |
| Restructuration lourde (M12) | Fin du port, M5, M6 | Déplacer des fichiers pendant le port produirait des conflits en série |
| Rendu sokol_gfx (M13) | M12.5 (`IRenderer`) | Le backend ne se remplace qu'une fois tous les appels `gl*` regroupés dans `src/render/` |
| Portage web (M14) | M13, M7d (signalisation, TURN) | WebGL2/WebGPU seulement côté rendu, WebRTC seulement côté réseau |

**Couche réseau ou protocole d'abord ?** On restructure la couche réseau (abstraction du transport, M7) **sans changer le protocole UDP** : les callbacks de `SendReliable` (sock.cpp:118-150) font partie du contrat de l'interface, et chaque backend les émule. On ne refond le protocole (M6, puis éventuellement M11) qu'avec un incrément de `EDIFICE_PROTOCOL_VERSION` à chaque lot.

## 3. Jalons

Effort : S ≈ moins d'un jour, M ≈ quelques jours, L ≈ 1 à 2 semaines, XL ≈ plus.

### M0 - Référence jouable et coordination avec le port (S)
- **Objectif :** partir d'une base stable et éviter les conflits avec l'agent du port.
- **Tâches :**
  - Attendre que l'agent du port déclare `multiplay.cpp`, `platform_sdl.cpp` et `compat/` stables, puis archiver cet état comme baseline. Le dossier n'est pas un dépôt git, donc il faut soit le copier, soit faire un `git init` (à décider).
  - Écrire une recette manuelle : solo ; host et join LAN Mac↔Mac et Win Release↔Mac ; join par IP ; save et load.
  - Relever le `sizeof` des structs réseau sur chaque build, y compris Win Debug `/Zp1`.
- **Fait quand :** la recette passe sur la baseline et les sizeof sont documentés.

### M1 - Gestion de version du netplay (M) - demande explicite de l'utilisateur
Détail complet en section 5.
- **Tâches :**
  - Créer `version.h`, versionné dans le dépôt (pas de `configure_file`, puisque le `.vcproj` ne passe pas par CMake).
  - Récupérer la taille reçue au retour de `Receive`.
  - Ajouter un trailer de version après le NUL du nom dans `REQUESTENTER` et `INFO`.
  - Factoriser `SendRequestEnter()` et `SendInfo()`.
  - Faire en sorte que le serveur envoie réellement son refus (`VERSIONMISMATCH`, `SERVERFULL`).
  - Contrôler côté client sur `INFO`.
  - Afficher la version dans l'écran Join SDL, la listbox Win32, la console et le menu.
- **Fait quand :** la matrice de tests de la section 5.8 passe, ancien contre nouveau dans les deux sens.

### M2 - Correctifs des corruptions mémoire distantes (M, en trois patchs S)
- **Tâches :**
  - **M2a, `Sock::Receive` borné** (sock.cpp:62-68, 154-205). Ajouter un paramètre `maxsize`. Pour les paquets groupés : exiger `recvbufpos+2+wsize<=recvbufsize`. Pour le reliable : exiger `recvbufsize>=8`, `num*450+(recvbufsize-8)<=BIGBUFSIZE` et `dwsize<=maxsize`. Pour l'ACK : exiger `recvbufsize>=4`. Signaler les callbacks d'ACK locaux par un canal distinct, pour que les `RECEIVED*` ne puissent plus arriver depuis le réseau.
  - **M2b, validation des indices.** Ajouter `ValidPlayer(n)` et `ValidSObj(n)` et les appliquer à la frontière réseau : multiplay.cpp:619, 645, 653, 662, 668, 680, 684, 694 (`credit<10`), 799, 884, 1356, 1650, 1751, 1893. Borner `weapon` (1371) avant `ammoingun[weapon]` (1708), et `sound.cpp:176` comme le fait déjà sound_sdl.cpp:225. Ne pas modifier `GetPlayer()` (playing.cpp:106-109).
  - **M2c, chaînes et longueurs.** Utiliser `snprintf` ou un `strlcpy` maison aux lignes multiplay.cpp:405-410, 418, 457, 815, 1281, 1563-1575, 1760. Chat : `len=min(len, reçu-5, 255)`. PLAYERNAMES : traiter `namelen` en non signé avec un maximum de 31, imposer `numnames<=MAXPLAYERS` et comparer `bufpos` à la taille reçue. LANDINFO : forcer le NUL terminal, imposer `numdefs<=NUMDEF` et `numgroundtex` entre 0 et 9, plafonner `numblotch`, refuser `..`, `/`, `\` et `:` dans les noms de texture. Corriger aussi land.cpp:495 (`x=min(x,LANDTEXGRIDSIZE-1)`). ReceiveBlockBuffer : imposer `numblocks<=(reçu-5)/9`, borner x/y/z, et ne rediffuser que les octets effectivement reçus.
- Aucun changement de format, donc pas d'incrément de protocole.
- **Fait quand :** chacune des vulnérabilités critiques de la section 4 a un correctif, et la recette M0 passe toujours.

### M3 - Identité de l'émetteur et bugs logiques réseau (M)
- **Tâches :**
  - `ReceivePosition` côté serveur : forcer `pnum=i` (multiplay.cpp:516) et limiter `state`.
  - Sur `infosock` (496-500), n'accepter que `REQUESTINFO` et `REQUESTENTER`, et ne jamais passer `pnum=0`.
  - Filtrer par **IP** source et non par ip:port (le NAT change le port). Garder la réécriture de `dest` (sock.cpp:156), mais la déplacer après le checksum et la restreindre à la même IP et à la phase de handshake.
  - Côté client, ne pas rétablir tel quel le filtre de 717, qui casserait le broadcast LAN (1026).
  - Accepter `ENTER` uniquement de l'IP à laquelle on a envoyé la requête.
  - Dédupliquer `REQUESTENTER` par ip:port source.
  - Corrections ponctuelles :
    - multiplay.cpp:551 : `SendReliable(kp,sizeof(*kp),0)`. Aujourd'hui, c'est l'adresse du pointeur qui part.
    - sock.cpp:169 : `(WORD)(lastreliableid+1)`.
    - sock.cpp:27 : `sizeof(*this)`.
    - multiplay.cpp:420-431 : comparer les ports dans le même ordre d'octets et vérifier le retour de `bind` (445).
  - File reliable bornée (environ 256 messages ou 1 Mo par client). Renvoi sur une horloge réelle (`GetTickCount`/`SDL_GetTicks`, et non `curtime`, voir playing.cpp:501), avec backoff et abandon.
  - Le serveur ignore les `CMSG_SCORE` et `CMSG_OBJECTIVE` envoyés par un client (1557).
- **Fait quand :**
  - Une usurpation depuis une IP tierce n'a plus aucun effet.
  - Un client derrière un NAT continue de se connecter.
  - La découverte LAN fonctionne toujours.

### M4 - Terrain identique partout (M)
- **Tâches :**
  - Dans `rng.h`, remplacer `rand()`/`RND` par un PCG32, en tirage indexé de préférence (`hash(seed,i,k)`). Cela concerne le décor (land.cpp:635-664) et Blotchify (474-495, appelé en 667). `RandomLand` côté serveur et les `rand()` purement cosmétiques ne changent pas.
  - Faire transporter par `LANDINFO` les champs de `LandSettings` qu'utilise `AddRidge` (land.cpp:255-270, 504-505), au lieu de relire le `Land.ini` local.
  - Option : ajouter un hash FNV-1a des données de gameplay non transmises (Structures/, Object.ini), sur des chemins relatifs et avec les fins de ligne normalisées.
  - **Incrémenter `EDIFICE_PROTOCOL_VERSION`.**
- **Fait quand :** le hash du heightmap et du décor est identique sous Windows et sous macOS pour une même seed (golden test). Si ce n'est pas le cas à cause de la libm (les `goto reposition`, land.cpp:655-658), le serveur envoie lui-même les positions.

### M5 - CMake unique, CI et socle de tests (L)
- **Tâches :**
  - Étendre CMakeLists.txt à MSVC et Linux. Aujourd'hui, la ligne 4-5 dit « Windows keeps using hugekilla.sln ».
  - Créer une cible `edifice_core` statique.
  - Passer `project(VERSION)` et le vérifier contre `version.h`.
  - Mettre en place GitHub Actions sur 3 OS, ASan/UBSan en Debug, et doctest dans `edifice_tests`.
  - Premiers tests : `CheckPeerVersion`, golden PRNG et décor, `static_assert` des sizeof contre `EDIFICE_LAYOUT_HASH`.
  - Retirer le `.sln`/`.vcproj` (et donc `/Zp1`) seulement après plusieurs semaines de CI MSVC verte.
- **Fait quand :** la CI est verte sur 3 OS et les tests tournent.

### M6 - Sérialisation explicite (L)
- **Tâches :**
  - Créer `net/wire.h` avec `ByteWriter`/`ByteReader` : little-endian, bornes vérifiées, paquet marqué invalide en cas de dépassement.
  - Migrer un message à la fois : PlayerPosition, SimpleMessage, ServerVars (multiplay.cpp:788, 1804), LandInfo, UpdateServerObject, KilledPosition, PLAYERNAMES.
  - Retirer les casts `*(int*)&buf[...]`, y compris ceux de sock.cpp:158, 162-163, 260 et 364.
  - Arrêter d'envoyer `ServerObject::timeout`.
  - Incrémenter la version de protocole à chaque lot.
  - Sauvegardes : signature `edifice` réellement vérifiée (hugekilla.cpp:731), champ `formatVersion`, `CREATE_ALWAYS`, lecture legacy des anciens fichiers.
  - Fuzzing libFuzzer sur `Sock::Receive` et sur chaque parseur.
  - *Fait (protocole 4, en un seul lot) :* `net/wire.h` ; tous les messages migrés (`Sync*` dans multiplay.cpp, `WriteSimple`/`ReadSimple` dans sock.cpp, SimpleMessage garde ses 12 octets) ; plus de casts dans multiplay.cpp et sock.cpp ; `timeout` non envoyé ; champ layout du trailer figé (`EDIFICE_NET_LAYOUT`) et ignoré ; signature `edifice` vérifiée au Load. Tests : `tests/wire_test.cpp`, `tests/net_join.py` (aller-retour réel), messages tronqués.
  - *Fait (sans changement de protocole) :* **cartes sauvegardées**, format 2 (`src/game/mapfile.h`) : `EDIFICEMAP`, `formatVersion`=2, protocole à la sauvegarde (informatif), dimensions de la grille, puis ciel, `LandDefs` et les deux grilles de blocs écrits champ par champ (`landwire.h`, mêmes fonctions que LANDINFO), en plages de cellules identiques, CRC-32 final. Lecture bornée : magie, version, taille exacte, nombre de plages, champs des blocs ; noms de fichiers et compteurs bornés par la même fonction que LANDINFO (`SanitizeLandInfo`). Lecture des sauvegardes d'origine (`edifice` + structs brutes MSVC 32 bits, alignement naturel ou `/Zp1`, taille vérifiée avant lecture). Écriture en troncature (`"wb"`, l'équivalent de `CREATE_ALWAYS`). Accessible dans le build SDL : console `savemap`/`loadmap`/`maps` (`maps/<nom>.map` du dossier utilisateur), ligne « Map » de Server Setup ; les clients reçoivent la carte par LANDINFO puis BLOCKS, et chaque nouvelle manche la recharge.
  - *Fait :* **libFuzzer**, un harnais par parseur (`tests/fuzz`, option CMake `EDIFICE_FUZZ`) : `Sock::Receive` alimenté sans socket (`Sock::Feed`), chaque message de `netmsg.h` (Sync* et parseurs sortis de multiplay.cpp : PLAYERNAMES, CHATMESSAGE, BLOCKS), REQUESTENTER/INFO et le trailer de version (`version.cpp`), VOICE, cartes format 2 et d'origine. Corpus de départ produits par les écrivains du jeu (`tests/fuzz/make_seeds.cpp`, `map_test --write`) ; job CI `fuzzing` de 60 s par harnais, artefacts des crashs.
  - *Corrigé en chemin :* BLOCKS acceptait des textures hors de `blockbuf[32]` du rendu (écriture hors bornes chez tous les clients) et des types inconnus : ces blocs sont ignorés. LANDINFO bornait les compteurs mais pas `sw`/`sx`/`sy` des déformations (boucle de AddRidge de milliards de tours) ni les flottants NaN : bornés aussi (`SanitizeLandInfo`, partagée avec les cartes).
  - Tests : `tests/map_test.cpp` (aller-retour, fichiers corrompus, tronqués, trop gros, sauvegardes d'origine construites à la main d'après `git show 998563e:land.h`), `tests/net_map.py` (un client reçoit la carte et tous ses blocs).
- **Fait quand :** plus aucun memcpy de struct sur le réseau. `EDIFICE_LAYOUT_HASH` n'a alors plus de raison d'être.

### M7 - Abstraction du transport (L)
- **Tâches :**
  - Créer `INetTransport {Host, Connect, Send(peer,data,len,reliable,callbackId), Poll, Close, Flush, CheckResend}` et un type `NetPeer` opaque, qui remplace `SOCKADDR_IN` dans `infosock`, `datasock`, `clientsock[]` et `locallistaddr[]` (multiplay.cpp:42-51).
  - `UdpTransport` enveloppe `Sock` à l'identique : un port par client (419-448), même format sur le fil.
  - Le backend Internet sera ENet, GameNetworkingSockets ou Steam (voir les décisions en section 7).
  - *Fait (M7a à M7e, détails dans [M7.md](M7.md)) :* `DatagramLink` sous `Sock` (`UdpLink`, `GnsLink` sur GameNetworkingSockets), client du lobby `edifice-lobby` (onglet Internet, code de partie), P2P par ICE avec signalisation authentifiée et relais TURN, et depuis M7e les trois archives de release compilées avec GameNetworkingSockets (vérifiées par la CI) : une partie Internet se rejoint sans ouvrir de port. Reste à vérifier à deux machines derrière de vrais NAT (CGNAT, NAT symétrique) avec l'instance publique.
- **Fait quand :** une partie LAN Windows contre macOS se comporte exactement comme avant.

### M8 - Steam : SDK, initialisation, SteamTransport (L)
- **Tâches :**
  - CMake : `option(HK_STEAM OFF)` et un stub `SteamAvailable()=0`. Le SDK reste hors du dépôt. Prévoir une configuration « Release Steam » dans le `.vcproj` si le build Windows est conservé, en notant que le build actuel est probablement x86, donc `steam_api.dll`.
  - `SteamAPI_Init` après `EnterDataDir` (platform_sdl.cpp:307) et dans `WinMain` (hugekilla.cpp:74-76). En cas d'échec, continuer sans Steam. `steam_appid.txt` (480) n'est utilisé qu'en développement, et jamais livré.
  - Un seul `SteamAPI_RunCallbacks()`, au début de `SockLoop`.
  - Nom du joueur par défaut tiré de `GetPersonaName()`, tronqué par `SendRequestEnter`.
  - `ISteamNetworkingSockets` en P2P avec SDR. Les callbacks sont émulés avec un en-tête de 2 octets `{flags, callbackId}` et un ACK applicatif automatique. Sous Steam, ENTER part sans port. HKDS (hugekilla.cpp:146) reste en UDP.
- **Fait quand :** deux comptes sur deux machines jouent ensemble via SDR, y compris avec FakePacketLoss et FakePacketLag.

### M9 - Steam : lobbies, rich presence, « Rejoindre la partie » (L)
- **Tâches :**
  - `CreateLobby` dans `StartServer` (multiplay.cpp:132), avec `maxplayers` réellement lu depuis net.ini. Corriger au passage `LoadNetSettings` (102-118) et la vérification de serveur plein (411).
  - Lobby data et filtres décrits en section 5.6.
  - `SetRichPresence("connect","+connect_lobby <id>")`.
  - Gérer `GameLobbyJoinRequested_t` et `GameRichPresenceJoinRequested_t`.
  - Lire `+connect_lobby` dans argv (EnterDataDir doit ignorer les arguments qui commencent par `+`, platform_sdl.cpp:279) et dans `lpCmdLine` (hugekilla.cpp:76).
  - Onglet Steam dans l'écran Join.
  - `LeaveLobby` dans `Disconnect()` (multiplay.cpp:176).
- **Fait quand :** les scénarios de la section 5.8 passent : liste d'amis, overlay, lancement à froid.

### M10 - Packaging et mise en production Steam (M+M)
- **Tâches :**
  - macOS : dylib dans `Contents/Frameworks` avec un rpath correct, signature en hardened runtime, entitlements `disable-library-validation` et `allow-dyld-environment-variables` pour l'overlay, notarisation.
  - Windows : DLL livrée à côté de l'exe.
  - Depots Steamworks, App ID payant, localisation de la rich presence.
- **Fait quand :** un build notarisé tourne depuis Steam, et la non-régression non-Steam en LAN et par IP passe.

### M11 - Anti-triche et serveur autoritaire (M puis XL)
- **Tâches :**
  - Phase 1 (M) : limite de débit sur KILLED/KILLEDPOSITION, munitions et cadence tenues par le serveur pour tous (multiplay.cpp:1704-1708), contrôle des sauts de position (en log seulement au début, à cause du respawn, du jetpack et du deltaplane), distance maximale de construction, limite de débit par IP.
    *Fait :* conception et réglages dans `docs/M11.md` (logique dans `src/net/anticheat.h`). Le serveur compte les dégâts qu'il voit sur les joueurs distants et tue lui-même celui qui ne meurt pas ; sauts de position en journal seulement.
  - Phase 2 (M, seulement sans Steam ni ENet connectID) : handshake avec cookie HMAC, sans allouer de slot avant la validation.
    *Fait :* `docs/M11.md` (phase 2). Cookie HMAC-SHA256 (IP, port, seau de temps) renvoyé par le client avant tout slot, toujours au protocole 5 (octet de capacités après le trailer de version) ; les anciens clients rejoignent tant que `ac_requirecookie` vaut 0 (défaut) ; pas de cookie par GameNetworkingSockets.
  - Phase 3 (XL) : entrées numérotées, tick fixe sur une horloge réelle, tirs et morts décidés par le serveur (pour remplacer `Killed()` côté victime, playing.cpp:385 et 460), snapshots delta, prédiction et réconciliation côté client, slots stables au lieu du décalage de `DeletePlayer` (multiplay.cpp:1296).
    *Partiel (3a) :* `docs/M11.md` (phase 3a). Le serveur décide des coups (sa propre balle, testée en segment contre la position que le tireur voyait, rembobinée de son ping, `ac_lagcomp` ; ligne de vue par le relief et les blocs ; origine du tir imposée), des dégâts de grenade (sa propre grenade) et des morts ; les KILLED des victimes n'accusent plus personne. Toujours le protocole 5 : capacité `HELLOCAP_SERVERHITS`, messages HITMODE/SERVERHIT pour les nouveaux clients seulement ; un nouveau client sur un ancien serveur compte ses dégâts comme avant. Restent pour la 3b : slots stables, entrées numérotées, tick fixe.
    *Partiel (3b) :* `docs/M11.md` (phase 3b). Slots stables : un partant libère son slot, les autres gardent leur numéro, un nouveau prend le premier slot libre, tant que tous les clients l'annoncent (`HELLOCAP_STABLESLOTS`, drapeau `PLAYERLEFT_KEEPSLOTS` de DELETEPLAYER) ; avec un ancien client, décalage comme avant. Positions numérotées (`HELLOCAP_POSSEQ`) : doublons, positions dépassées et sans numéro ignorés, cadence bornée (`ac_posrate`) ; mouvement retenu à `ac_maxspeed` derrière `ac_moveclamp` (0 par défaut, journal). Toujours le protocole 5. Restent : simulation des mouvements par le serveur d'après les entrées (protocole 6), tick fixe, décalage d'après le numéro de la position vue par le tireur.
- **Fait quand :** on ne peut plus être invulnérable en n'envoyant pas KILLED.

### M12 - Restructuration lourde (XL, à mener par petites PR)
Chaque PR rejoue la recette et garde la CI MSVC verte.
- **Tâches :**
  1. Arborescence `src/{platform,render,net,game,ui,assets}`, `tests/` et `data/`, en supprimant `StdAfx.h`.
     *Partiel :* l'arborescence `src/{game,render,net,ui,audio,platform,compat}`, `win32/` et `data/` (ex-`HugeKilla/`) est faite (déplacements seuls). Les chemins de ce document (`compat/…`, `HugeKilla/…`, sources à la racine) renvoient à l'ancienne arborescence.
     *Fait :* `StdAfx.h` supprimé. Chaque `.cpp` inclut ce qu'il utilise : les noms Win32 par `win32compat.h` (qui définit aussi `WIN32_LEAN_AND_MEAN`, comme le faisait `StdAfx.h`), OpenGL et GLU par `src/platform/gl.h` (seuls les fichiers qui dessinent), la fenêtre, les entrées et `UserFile` par `src/platform/platform.h`. Les faux headers de `src/compat/include/` (`windows.h`, `winsock2.h`, `mmsystem.h`, `gl/gl.h`…) sont retirés : plus rien ne les inclut. Les headers du jeu se compilent seuls (sauf `land.inl`, un fragment, et `std.h`, mort). Le chemin `HK_WIN32_NATIVE` (jamais compilé) garde ses includes propres (`gl.h` et `<commdlg.h>` dans `hugekilla.cpp`, `resource.h` dans `ui.cpp`), mais il ne se lierait toujours pas (`UserFile`, `HKPixelScale`… n'existent que dans `platform_sdl.cpp`). Vérifié : les objets compilés (macOS, avec et sans GNS) sont identiques à ceux de `main` (code, données, symboles). Restent `src/assets` et `tests/`.
     *Reporté :* une cible CMake (bibliothèque statique) par module. Les modules s'appellent en cycle : `redrawing.cpp` lit `numplayers` et `rgd` de `multiplay.cpp`, qui tire les modèles de `redrawing.cpp` (`projectile`) ; `playing.cpp` ouvre l'UI, qui lit l'état du jeu et du réseau. Des bibliothèques en cycle demandent des groupes d'édition de liens (`--start-group` sous Linux) sans rien isoler : à refaire quand `World`/`NetState` et `IRenderer` auront cassé ces cycles.
  2. Un header par module. Cela supprime les 95 `extern` locaux et l'ODR `projectile[20]` contre `[10]` (multiplay.cpp:30 contre redrawing.cpp:40). Introduire ensuite `World`, `NetState` et `RenderState`.
     *Fait :* les 132 `extern` locaux des `.cpp` (et 7 prototypes locaux de fonctions d'un autre module) sont remplacés par une déclaration unique dans le header du module qui définit la globale : `land.h` (terrain, blocs, ciel), `playing.h` (`sv`, `curtime`, `object`, `sobject`, `menu`…), `multiplay.h` (`server`, `numplayers`, `netsettings`, `rgd`…), `sock.h` (débits), `redrawing.h` (`graphicsprefs`, particules, nénuphars), `models.h` (nouveau : modèles 3D de `redrawing.cpp` et `textures.cpp`), `ase.h`, `gameplay.h`, `hugekilla.h` (`ldfs`), `ui.h` (console, qui gagne une garde d'inclusion) et `platform.h` (`hWnd`, `hdc`, `mousepos`…). ODR corrigés : `projectile` est `[10]` partout (seuls 0 à 2 servent : roquette, balle, grenade ; `textures.cpp` le déclarait `[10]`, `multiplay.cpp` et `GamePlay.cpp` `[20]`, `playing.cpp` `[4]`), et `plobj` est `[4]` (défini `[4]`, déclaré `[MAXSCENERY]` dans `textures.cpp`). La struct `sobject` de `ase.h` devient `asemesh` : elle portait le nom du tableau `sobject` des objets serveur, que les fichiers de rendu ne pouvaient plus voir en même temps. Les réglages `ac_*` sont définis avant `SendInfo` au lieu d'y être redéclarés.
     *Reporté :* `World`, `NetState` et `RenderState`. Regrouper les globales change chaque accès (`land[x][y]` → `world.land[x][y]`, plus d'un millier de lignes) : à faire module par module, avec la même vérification des objets, et pas d'un bloc.
  3. Remplacer les shims Win32 par des abstractions plateforme. SDL devient le main partout, `DirectSound` est retiré.
     *Fait :* le build Win32 natif, jamais compilé, est retiré : les blocs `HK_WIN32_NATIVE` (WinMain, WndProc, WGL, menus, boîte de dialogue Multiplay, ancien tableau des scores ; plus la saisie du chat touche par touche, devenue inaccessible) et `HKDS` (variante serveur dédié du même build, console GDI `DrawConsoleNonGL`), `src/audio/sound.cpp` (DirectSound), `Save`/`Load` (entrées de menu), et `win32/` (projet VS 2003, `resource.h`, `hugekilla.rc` avec ses menus et dialogues). Restent sous Windows `src/platform/windows/edifice.rc` et ses deux icônes (mêmes IDs 107 et 108). Vérifié avant : rien de compilé n'en dépendait (`main()` SDL partout, `SDL2main` pour `WinMain`). `src/compat/win32compat.*` est remplacé par de petites interfaces de `src/platform/` : `hktime.h` (`HKTicks`, `HKPerfCounter`/`HKPerfFrequency`, `HKSleep`, sur SDL dans `system_sdl.cpp`), `hkkeys.h` (`KEY_*` aux valeurs des codes VK, que `control.ini` désigne par leur nom ; `HKKeyDown` remplace `GetKeyState`), `platform.h` (`HKMessageBox` par `SDL_ShowSimpleMessageBox`, `HKSwapWindow`, `HKQuit`, `HKWindowSize` à la place de `SwapBuffers`/`DestroyWindow`/`GetClientRect` et des faux `hWnd`/`hdc`), `hksocket.h` (Winsock 2 sous Windows, les noms `SOCKET`/`SOCKADDR_IN`/`INVALID_SOCKET`/`closesocket` ailleurs, `HKSocketsInit`/`HKSocketsQuit` pour `WSAStartup`), et `HKChangeDir` dans `fileio` (au lieu de `access`/`chdir`). `ZeroMemory`/`CopyMemory`/`MoveMemory` deviennent `memset`/`memcpy`/`memmove`, `stricmp` `strcasecmp`, `itoa` `sprintf`, `LPCTSTR` `const char *`, `MAKEWPARAM`/`LOWORD`/`HIWORD` des décalages. *Gardé :* `hktypes.h` avec `DWORD`/`WORD`/`BYTE` (mêmes typedefs que `<windows.h>` sous Windows) : `DWORD` sert ~120 fois, dans les structs réseau et les sauvegardes, un renommage en masse ne serait que du bruit ; il inclut aussi la bibliothèque C que les sources attendaient de leur header commun. La couche sockets (`udplink.cpp`, `gnssignal.cpp`, `lobbygame.cpp`) était déjà portable et reste telle quelle. Seul changement visible : sous Windows `HKTicks` compte depuis le lancement (`SDL_GetTicks`, comme déjà sous macOS et Linux) et non depuis le démarrage de la machine. Validé : tests unitaires et fuzzers (macOS, avec et sans GNS), suite netplay locale, écrans 2D identiques au pixel près à `main` (`tests/pixel_diff.py`), touches vérifiées avec `HK_KEYLOG=1`.
  4. Chemins d'assets : supprimer les 122 lignes avec `\\`, passer à `std::filesystem` et à stb_image.
     *Fait :* chemins en `/` partout (textures, objects, sound, structures, maps). Accès aux fichiers par `src/platform/fileio.{h,cpp}` (`HKReadFile`, `HKFileOpen`, `HKListDir`, `HKMakeDir`, `HKRenameFile`, `HKFileExists` ; `std::filesystem` + stdio, chemins UTF-8 aussi sous Windows), avec la recherche insensible à la casse de l'ancien `HKPath` hors Windows (`Structures/` sur le disque, `structures` dans le code). Les shims `CreateFile`/`ReadFile`/`WriteFile`/`GetFileSize`/`CloseHandle`/`FindFirstFile`/`FindNextFile`/`FindClose`/`MoveFileEx`/`LoadImage`/`GetObject`/`DeleteObject` et `HKPath` sont retirés de `src/compat/` ; seul `src/audio/sound.cpp` (DirectSound, jamais compilé) appelle encore le vrai `CreateFile` de Win32. BMP décodés par stb_image (`src/third_party/stb/`, `src/render/image.cpp`), identiques à l'octet près à l'ancien lecteur sur les 87 BMP de `data/` (dont `landcloud.bmp` en 8 bits) ; les `.pxr` gardent leur lecteur (`LoadRaw`). C++17 et macOS 10.15 minimum (`std::filesystem`). Test : `fileio_test`.
  5. `IRenderer` : `glGenTextures` à la place des IDs codés en dur (1, 2, 4, 6, 300, 400+i, 600, 800, 1300+), avec une validation par pixel-diff.
     *Partiel :* registre de textures (`src/render/textures.h`, backend OpenGL seul dans `texture_gl.cpp`) : les emplacements nommés (`TEX_FONT`, `TEX_CLOUDS`, `TEX_GROUND0+i`, `TEX_MENU0+i`…) gardent les anciens numéros, mais leurs noms GL viennent de `glGenTextures` à la première utilisation et sont libérés à la fermeture (`TexShutdown`). Plus aucun `glBindTexture`, `gluBuild2DMipmaps` ni ID littéral hors du registre ; les chargeurs (`LoadTexture`, `LoadRaw`) décodent le fichier puis appellent `TexUpload`. Validé par pixel-diff (`tests/pixel_diff.py`) : écrans 2D identiques au pixel près, écrans en jeu comparés hors bruit d'une exécution à l'autre. Restent pour `IRenderer` (M13.1) : la mesure de la police relit la texture par `glGetTexImage` (`src/ui/ui.cpp`, fait en M13.1 : `TexFontAlpha`) et les états GL autour des textures (`glEnable(GL_TEXTURE_2D)`, `glTexEnv`, fait en M13.1 : `RCAP_TEXTURE`, `GL_MODULATE` fixé par le backend).
  6. Passe UB finale : retirer `-Wno-return-type`, `-fwrapv` et `-fno-strict-aliasing` (CMakeLists.txt:59-62), et passer en `-Werror`.
     *Fait :* les trois options sont retirées (jeu, tests GNS, fuzzers) et `-w` aussi sous GCC. Corrigé ce qu'elles cachaient : `GetAmmoHold`/`GetReloadTime` sans valeur de retour pour une arme inconnue (0 désormais), `l33tify` (chat) qui rendait l'adresse d'un tableau local, lu après sa disparition (`static` désormais). Pas d'accès par type incompatible dans le jeu : les messages réseau passent par `wire.h` depuis M5, les `(float *)&vector` donnés à OpenGL lisent des `float`. Aucun dépassement d'entier signé vu par UBSan. UBSan (`-fsanitize=address,undefined`, tests unitaires, suite netplay, courtes parties) a trouvé des conversions float -> entier hors limites, comportement indéfini : `GetAng` (angle négatif vers `unsigned char`), les positions envoyées (`x == LANDGRIDSIZE` donne 65536 dans un `WORD`) et les vitesses (`char`). Elles passent maintenant par `int`, ce qui donne le repliement modulo que produisait déjà le code compilé : rien ne change sur le fil. Même chose pour l'horloge du jeu (`HKPerfMillis`) : le compteur haute résolution part du démarrage de la machine, et ses millisecondes ne tiennent plus dans un `DWORD` après 49,7 jours (indéfini avant, repliement explicite maintenant). Avertissements corrigés : `case INFOMENU` (0x80 ne rentre pas dans `char menu` : jamais atteint, retiré, la suite du `case BUILDING` reste), `if(flag&&F_HOLE)` (toujours `flag`, gardé tel quel : `flag&F_HOLE` changerait les modèles), `pi` redéfini dans `multiplay.cpp` (`WIREPI`, même valeur : les angles sur le fil ne changent pas), adresses de temporaires dans `glVertex3fv` (`Vertex()`), constantes 255 vers `char` (paramètres `unsigned char`), une borne que GCC -O3 ne voyait pas (`ReadVersionTrailer`). `EDIFICE_WERROR=ON` (`-Werror`, `/WX`) sur le jeu et ses tests, activé en CI sur les trois systèmes ; le code tiers est hors de ces cibles. Validé comme M12.3 : tests unitaires, suite netplay locale (aussi en build ASan + UBSan), écrans 2D identiques à `main`.

### M13 - Rendu moderne avec sokol_gfx (L, à mener par petites PR)
Le rendu (`src/render/`, environ 3 300 lignes) est du pipeline fixe OpenGL 2 : 47 `glBegin`/`glEnd`, plus de 300 `glVertex`/`glTexCoord`/`glColor`, pile de matrices, `glFog`, `glPushAttrib`. OpenGL est figé en 4.1 et déprécié sur macOS. **sokol_gfx** (un header C, zlib, compatible LGPL) cible Metal, D3D11, GL 3.3, WebGL2 et WebGPU, et ouvre donc aussi le portage web (M14). SDL3 GPU a été écarté faute de backend web, et wgpu ou Dawn parce qu'ils sont plus lourds à intégrer et réservés à WebGPU.
- **Tâches :**
  1. Terminer `IRenderer` (M12.5) avec un batcher : `pushQuad`, `pushTri`, `drawMesh(ase)`, états de blend, de cull et de brouillard. Les matrices passent à une petite lib (glm ou HandmadeMath) au lieu de la pile GL. Plus aucun appel `gl*` hors de `src/render/`.
     *Fait :* `src/render/renderer.h`, une API C du rendu sur le modèle de sokol_gl (mode immédiat en lots) plutôt qu'une interface virtuelle : `RBegin`/`REnd` avec `RVertex*`, `RColor*`, `RTexCoord2f` (couleur et coordonnée de texture courantes, comme `glBegin`), `RPushQuad`/`RPushTri`/`RPushLine`/`RPushPoint` (sommets complets), `RDrawMesh` (modèles `.cob` d'`AseObj::Draw` : triangles par plages d'une texture, avec une matrice de modèle) ; états `REnable`/`RDisable` (`RCAP_TEXTURE`, `BLEND`, `DEPTHTEST`, `CULL`, `FOG`, `SCISSOR`, `LINESMOOTH`), `RBlendFunc` (5 modes nommés), `RCullFace`, `RDepthMask`, `RLineWidth`, `RPointSize`, `RFogRange`/`RFogColor` (brouillard linéaire), `TexBind` (emplacement du registre), `RPushAttrib`/`RPopAttrib` (groupes de `glPushAttrib` utilisés par le jeu), viewport, scissor, `RClear`, relecture ; passes 2D (`RSet2D` : coordonnées normalisées, matrices identité) et 3D (`RSetProjection`/`RSetModelView`). `renderer.cpp`, commun à tous les backends, garde l'état et le lot : un changement d'état dessine d'abord le lot en cours, les listes consécutives d'un même type (quads, triangles, lignes, points) s'y regroupent, et le backend reçoit chaque lot avec son état complet (`rbackend.h`) ; un backend sokol_gfx en fera un pipeline et des uniformes (brouillard, alpha). Les matrices viennent de `src/render/rmath.h`, une petite lib `Mat4` écrite pour le jeu plutôt que vendorisée (produit, échelle, translation, rotation, `gluPerspective`, `gluLookAt`, `gluProject`, avec les formules de GLU). Plus aucun appel `gl*` hors de `renderer_gl.cpp` et `texture_gl.cpp` : ni dans `src/ui/`, ni dans `src/platform/` (`HK_SHOT`, viewport), ni dans `redrawing.cpp`/`ase.cpp` ; l'UI mesure la police sur l'alpha gardé au chargement (`TexFontAlpha`) au lieu de `glGetTexImage`. Appels GL sans effet retirés : `glDisable(GL_LIGHTING/GL_ALPHA_TEST)` (jamais activés), `glTexEnv(GL_MODULATE)` (le défaut), `glFogf(GL_FOG_DENSITY)` (brouillard linéaire), `glBlendFunc(GL_COLOR,GL_ZERO)` (facteur invalide, ignoré par GL), la pile de matrices qui débordait. Nouvelle aide `HK_FIXEDCLOCK` (horloge du jeu fixe par image) pour des captures en jeu répétables.
  2. Premier backend : le batcher en OpenGL fixe, pour valider l'abstraction par pixel-diff (`HK_SHOT`) sans changer de rendu.
     *Fait :* `renderer_gl.cpp` dessine chaque lot par tableaux de sommets côté client (`glDrawArrays` par plage de primitives, quads, bandes et éventails compris), après n'avoir changé que les états GL qui diffèrent ; `texture_gl.cpp` garde le seul `glBindTexture`. Validé par pixel-diff (macOS, Apple Silicon, 2048x1536, `HK_SEED`, `HK_FIXEDCLOCK=16`) : écrans 2D (menus, Options, Join, Server Setup, Credits, console) identiques à `main` au pixel près ; en jeu, identiques au pixel près à la seule bascule des matrices (première partie de la tâche 1), et à `main` à une dizaine de pixels d'une unité près dans les nuages : le pilote GL d'Apple multiplie ses matrices par FMA et fait `gluPerspective` par un `glFrustum` aux formules internes, d'où un dernier bit parfois différent (mesuré sur un contexte hors écran ; `rmath.h` garde les formules de GLU, les mêmes sur tous les OS). Temps par image inchangé (0,93 ms des deux côtés, sans VSync, scène de départ en jeu).
  3. Backend sokol_gfx derrière `option(HK_RENDER_SOKOL)`, avec des shaders GLSL convertis par `sokol-shdc` (MSL, HLSL, GLSL ES, WGSL) au build. Il reproduit d'abord le rendu actuel, brouillard compris, et doit passer le même pixel-diff à une tolérance près.
     *Fait :* `renderer_sokol.cpp`, `texture_sokol.cpp` et `sokol_native.cpp` derrière `HK_RENDER_SOKOL` (`build.py --sokol`), `sokol_gfx.h` vendorisé (`src/third_party/sokol`, zlib, commit `170136d`, crédits et `licenses/`). Metal sous macOS (`SDL_WINDOW_METAL`, `CAMetalLayer`), Direct3D 11 sous Windows, OpenGL 4.1 core sous Linux (et sous macOS avec `HK_SOKOL_BACKEND=GLCORE`) : `sokol-shdc` ne produit plus de GLSL 330, son minimum de bureau est le GLSL 410. Shaders en GLSL annoté (`src/render/shaders/edifice.glsl`) convertis au build par `sokol-shdc`, téléchargé par CMake au commit `b975d56` de sokol-tools-bin avec son SHA-256 (le dépôt n'a pas de releases ; `HK_SOKOL_SHDC` pour un build hors ligne), en Metal, HLSL 5, GLSL 410, et déjà GLSL ES 3.00 et WGSL pour M14 ; Metal et HLSL en source, compilés au lancement. Le shader reproduit le pipeline fixe : couleur de sommet bornée à 0..1 fois la texture (une texture blanche sans texture), brouillard linéaire par la profondeur |z| de l'œil (la coordonnée de brouillard de GL par défaut) ; blending (5 modes), test et écriture de profondeur, culling sont des pipelines (créés à la demande, 288 au plus) ; pas d'alpha test (jamais activé). La matrice de projection reste celle de GL, ramenée en profondeur 0..1 pour Metal et D3D. Les quads, bandes, éventails et boucles deviennent des listes ; lignes larges et points deviennent des quads face à l'écran (Metal et D3D n'ont que des lignes et points d'un pixel), construits dans l'espace de l'œil pour garder le brouillard ; l'antialiasing des lignes n'est pas reproduit. Une image est enregistrée puis dessinée dans une image hors écran à la présentation ou à une relecture (sokol_gfx écrit un vertex buffer par image), puis copiée dans la fenêtre ; `glClear` devient un quad au plan lointain. Relectures par l'API (sokol_gfx n'en a pas) : blit Metal vers un buffer partagé, `glReadPixels` sur un framebuffer à nous ; la profondeur sous le soleil se relit donc comme avant, au prix d'une attente du GPU par image, comme `glReadPixels` sous GL. Textures : mêmes mipmaps que `gluBuild2DMipmaps` (taille en puissance de deux par `nearestPower`, `gluScaleImage` par aire, moitiés 2x2 arrondies), RGBA8, mêmes filtres. Nouvelles aides `HK_PLAY` (apparaît en jeu sans le panneau) et `HK_FRAMETIME` (temps moyen par image) ; `tests/render_smoke.py` dessine menus et partie, et échoue sur une erreur de validation ; job CI `sokol` (Debug, validation de sokol_gfx et des erreurs GL) sous macOS, Windows et Linux.
     Validé (macOS, Apple Silicon, 2048x1536, `HK_SEED=42`, `HK_FIXEDCLOCK=16`, contre le backend OpenGL fixe ; la capture de référence la plus proche parmi plusieurs exécutions, le ciel tiré au hasard changeant d'une exécution GL à l'autre) : écrans 2D (menu, Options et Player, Join, Server Setup, Credits) à 1 près au plus sur quelques pixels ; en jeu, écart maximal 20 et 0,001 % des pixels au-delà de 8 (écran d'apparition, menu de construction, scores, chat), 0,25 % (console, partie à l'image 600 : une bande du sol où le niveau de mipmap change, `GL_LINEAR_MIPMAP_NEAREST`) ; en jeu sans panneau (`HK_PLAY`, image 900) écart maximal 95 sur 0,033 % des pixels, des pixels isolés au bord des silhouettes du relief (règles de rastérisation). GL 4.1 core contre Metal : à 12 près. Profondeur relue identique sur le relief ; aucune erreur de validation (builds Debug Metal et GL core). Temps par image en jeu (sans VSync, fenêtre masquée : 600 images après l'image 300) : 0,55 ms en Metal contre 1,24 à 1,31 ms en GL fixe. **Direct3D 11** sous Windows : device et swapchain DXGI sur le `HWND` de SDL (matériel, sinon WARP ; modèle flip, sinon l'ancien), relectures par des textures `STAGING` (le tampon de profondeur entier, que D3D11 ne copie qu'entier) ; validé par la CI seulement (runner sans GPU, pilote de rendu de base, niveau de fonctionnalités 11.0 : menus et partie dessinés, sans erreur de validation), pas sur une vraie machine. En chemin, le plantage du jeu sous Windows juste après « Creating land » (dépassement de pile, que `check_package.py` attribuait à l'OpenGL des runners) : `SmoothLight` et `SmoothLand` gardaient 1 Mo sur la pile, toute la pile du thread principal sous Windows.
  4. Bascule par défaut sur sokol_gfx dans les releases macOS, Linux et Windows, puis retrait du backend OpenGL fixe.
     *Partiel :* sokol_gfx est le rendu par défaut (`HK_RENDER_SOKOL=ON`, `build.py` sans option), donc celui des trois releases (Metal, Direct3D 11, OpenGL 4.1 core) ; leur licence est dans `licenses/sokol.txt` (vérifié par `tests/check_package.py`). Le backend OpenGL fixe reste sélectionnable le temps d'une release (`build.py --gl`, `-DHK_RENDER_SOKOL=OFF`) pour une machine sans OpenGL 4.1 ni Direct3D 11 ; le job CI `sokol` le compile encore et le dessine sous macOS et Linux. Reste : retirer `renderer_gl.cpp`, `texture_gl.cpp`, `rcontext_gl.cpp`, `gl.h` et GLU à la release suivante.
  5. Modernisation visuelle, une PR par effet, chacun désactivable dans les options : éclairage par pixel, ombres (shadow map), MSAA, post-process (bloom, tonemapping), brouillard en shader.
     *En cours :* effets demandés par l'utilisateur, côté client seulement (aucun changement de protocole), une PR par effet, chacun désactivable dans Options → Video/Audio (enregistré dans `control.ini`), sur les deux backends (tout passe par `renderer.h`).
     - **Arme en première personne** (« Gun over walls », `fxviewmodel`) : l'arme du joueur local et son éclair de tir sont dessinés en dernier (`DrawViewModel`, `redrawing.cpp`), après un effacement de la profondeur seule, avec leur propre projection : champ de 70° fixe (le zoom grossit le monde, pas l'arme tenue à l'œil) et plan proche à 0,02 au lieu de 0,15 (l'arme est à un demi-mètre, le plan proche de la scène en coupait déjà des morceaux). La modelview et le brouillard restent ceux de la scène, et le modèle garde son éclairage par le soleil en coordonnées du monde : l'arme n'a pas l'air collée par-dessus, sous l'eau non plus. Elle ne rentre plus dans les murs ni le relief (capture d'un mur de blocs à 0,4 devant le joueur, Metal et OpenGL fixe identiques). Effet de bord : l'arme ne cache plus le soleil à la relecture de profondeur, l'éclat du soleil passe donc derrière elle au lieu de baisser.
     - **Explosions des grenades** (« Explosions », `fxexplosions`) : nouveau module `src/render/effects.cpp` (`effects.h`), une réserve de particules à part de `particle[]` (pluie, neige, réacteur), animées au temps de l'horloge du jeu (donc répétables avec `HK_FIXEDCLOCK`) en unités du monde par seconde, avec son propre générateur aléatoire (`rand()` reste celui du jeu), et dessinées en billboards par `renderer.h` après l'eau. L'explosion (`FxExplosion`, appelée là où la grenade explose dans `TickObjects`) : un éclair (0,14 s), 14 bouffées de feu (texture procédurale aux bords bruités, qui grandissent, freinent, montent et passent du blanc à l'orange puis au rouge), 40 étincelles (traînées le long de la vitesse, tombent et rebondissent sur le relief et les blocs, `GetHeight`), 16 débris (pierres sombres découpées au bruit, mélange alpha, triés de loin en près) et une lueur orange de 0,4 s posée sur le relief (une grille de 12×12 qui suit `GetLandSmooth`, en addition). Les lueurs s'ajoutent (`RBLEND_ADD`) avec un brouillard noir, donc s'éteignent au loin au lieu de prendre la couleur du brouillard ; rien n'écrit la profondeur. Textures générées au chargement (`FxLoadTextures`, emplacements `TEX_FX0`…, sans fichier). « Classic » garde la gerbe de 200 particules d'origine. Mêmes images en Metal et en OpenGL fixe.
     - **Fumée** (« Smoke », `fxsmoke`) : après une explosion (en mode « New »), 10 bouffées de suie qui apparaissent pendant que le feu retombe (âge négatif = attente), grandissent de 1 à 3 unités, freinent jusqu'à la vitesse d'un air chaud qui monte et d'un léger vent, et s'éclaircissent en 3,5 à 6 s ; et une traînée de petites bouffées claires derrière chaque grenade en vol, une toutes les 30 ms, émise côté rendu d'après `object[]` (indépendante du nombre de ticks par image). Le jeu n'a pas de roquettes (`projectile[0]`, le modèle « rocket », n'est lancé par aucune arme) : la traînée ne concerne que les grenades. Texture procédurale de volutes (128×128 : bord bruité sur un cœur dense, ombrée comme éclairée d'en haut), mélange alpha dans le brouillard de la scène, triée de loin en près avec les débris (une pierre passe devant ou derrière une bouffée comme il faut) ; rien n'écrit la profondeur, ce qui suffit : les bouffées d'une même explosion sont de la même couleur et presque transparentes.
     - **Impacts et eau** (« Impacts and splashes », `fximpacts`) : une balle qui franchit la surface (`TickObjects`, point d'entrée interpolé sur le pas de la balle) fait des ronds posés sur l'eau à la hauteur des vagues du moment (`WaterSurfaceHeight`, la formule de `DrawWater`), des gouttes en traînées qui retombent et disparaissent dans l'eau, un peu d'embruns ; sous l'eau, un sillage de bulles (qui montent en ondulant et disparaissent à la surface). La balle garde sa vitesse : ce qu'elle touche reste l'affaire du jeu et de l'anti-triche (M11), l'effet n'est que visuel. Sur le relief, un nuage de poussière plus claire que le sol (une bouffée rapide et une plus lente) et 5 grains, reculés le long de la trajectoire pour ne pas naître dans le sol ou le mur ; sur un bloc, poussière grise et 3 étincelles ; au fond d'un lac, de la vase qui reste en suspension et des bulles. Une grenade qui tombe dans l'eau éclabousse ; elle explose sous l'eau en éclair terne, 70 bulles et une gerbe d'eau (plus forte si elle est peu profonde), sans feu ni fumée. Les grenades qui coulent laissent des bulles au lieu de fumée. Dessin en deux passes : ce qui est sous l'eau avant l'eau quand la caméra est au-dessus (la surface, dessinée par-dessus, le teinte et le cache comme le fond du lac), le reste après ; depuis le dessous, tout après (les particules n'écrivent pas la profondeur, la surface dessinée ensuite couvrirait les bulles proches). « Off » garde les éclats bruns d'origine.
     - **Éclair de tir** (« Muzzle flash », `fxmuzzleflash`) : en plus du sprite de tir de chaque arme (`DrawGunExplosions`), une lueur additive pendant les 80 premières millisecondes (gerbe orange à la texture du feu, tournée à chaque tir pour scintiller, et cœur blanc), sans brouillard, testée en profondeur : dans la passe de l'arme pour le joueur local (le canon la masque en partie), dans la scène pour les autres. Pas pour la grenade (pas de sprite de tir).
     - **Antialiasing des lignes** : le seul usage de `RCAP_LINESMOOTH` est la boucle des étincelles de `Draw` (`spark[]`), qui ne sont jamais émises (code mort hérité, `sparkbright` reste à 0) : sous sokol_gfx, rien de visible n'est perdu. Les étincelles des effets sont des traînées texturées, antialiasées par leur texture sur les deux backends. Les lignes larges (éclairs d'orage, aperçu de construction) restent pleines sous sokol_gfx.
     - **Coût** (macOS, Apple Silicon, 2048×1536, Release, sans VSync, `HK_FRAMETIME=600`) : scène chargée (gatling en continu sur le sol, une grenade qui explose toutes les 250 ms, fumée qui s'accumule) à 0,58 ms par image avec tous les effets contre 0,28 ms sans en Metal, 0,93 contre 0,79 ms en OpenGL fixe. Les particules sont dessinées en quelques lots par passe ; le tri alterne débris et fumée, donc des changements de texture (un atlas les réduirait si besoin).
- **Fait quand :** les trois releases tournent sur sokol_gfx (Metal sur macOS), sans régression visible à la recette, et `src/render/` ne contient plus aucun appel OpenGL direct.

### M13.6 - Graphismes « Ultra » (expérimental, pour le plaisir)
Demande de l'utilisateur : un rendu « fou » (eau réaliste à vagues, herbe au vent, halos et rayons de soleil, ombres en temps réel, PBR). raylib/r3d écartés (ils remplaceraient la fenêtre SDL2, les entrées, sokol_gfx et la version web) : un chemin « Ultra » dans le backend sokol_gfx, derrière l'option Graphics (Classic par défaut, rendu Classic identique au pixel près). Détails dans `BUILD.md` (« Graphismes Ultra »).
- *Fait :* pipeline HDR et post-traitement (bloom, ACES, FXAA, exposition, sRGB) ; PBR (terrain mélangé par hauteur et pente : herbe en deux variantes, roche en triplanaire, sable, neige, terre, avec cartes normales ; blocs avec textures HD ; modèles, armes et personnages avec rugosité, métal et relief tirés de leurs textures) ; ombres en cascades ; eau (Gerstner, réflexion plane, réfraction et absorption, écume, reflets, sous l'eau) ; herbe instanciée et vent, arbres de la carte qui se balancent ; ciel atmosphérique, disque et halo du soleil, god rays, brouillard de hauteur ; textures HD (`data/hd`, 27 générations FLUX.1 schnell sur Replicate, Apache-2.0). Coût mesuré (macOS, Apple Silicon, 1920×1080, `HK_FRAMETIME`) : 8,3 ms par image en Ultra (tout au maximum) comme en Classic, la limite de l'écran à 120 Hz que la mesure n'arrive pas à lever sous Metal : au moins 120 images/s.
- *Fait (2) :* arbres, buissons et rochers instanciés, placés de façon déterministe d'après le relief, LOD, vent, ombres, reflets ; visuels seulement (voir la note d'équité ci-dessous).
- *Suites demandées :* nuages volumétriques, fumée volumétrique, grenade fumigène (gameplay, réseau) : PR suivantes. Note d'équité : la végétation et les rochers d'Ultra peuvent cacher un joueur qu'un joueur Classic voit.

### M14 - Portage web (L puis M)
Le rendu ne pose pas de difficulté après M13 : SDL2 et sokol_gfx sont supportés par Emscripten. Les vrais obstacles sont le réseau et l'environnement du navigateur.
- **Tâches :**
  1. Build Emscripten **en solo et hors ligne** (preset CMake `web`), **sans threads** : GitHub Pages ne permet pas de définir les en-têtes COOP/COEP, donc pas de `SharedArrayBuffer` ni de pthreads Emscripten.
     - Boucle principale via `emscripten_set_main_loop`, ou Asyncify en solution de transition.
     - `data/` préchargé (`--preload-file`), `.sav` et `net.ini` dans IndexedDB (IDBFS).
     - Audio par SDL2. Le micro du chat vocal (`voice.cpp`, opus) demande la permission du navigateur.
     - Rendu **WebGL2 par défaut** (sokol_gfx, GLSL ES 3.00), qui tourne sur pratiquement tous les navigateurs.

     *Fait :* preset CMake `web` et `python3 build.py --web [--run] [--test] [--package]` (emsdk épinglé, `EMSDK_VERSION` 6.0.12 dans `release.yml`, installé dans `build/emsdk` s'il n'y a pas d'`EMSDK`) : `build/web/index.html`, `.js`, `.wasm` (1,3 Mo) et `.data` (`data/` préchargé, 19 Mo). La boucle principale devient `Frame()` (`platform_sdl.cpp`), un passage de boucle appelé en boucle par `main()` en natif (comportement inchangé) et par le navigateur à chaque image sur le web (`emscripten_set_main_loop`). SDL2 est le port d'Emscripten (`-sUSE_SDL=2` : WebGL, Web Audio, entrées), sokol_gfx passe en `SOKOL_GLES3` sur un contexte WebGL2 créé par SDL (`sokol_native.cpp`, sans alpha ; pas de relecture de la profondeur en WebGL2, le soleil n'est donc jamais caché), libopus compilé sans intrinsics. Ni pthreads, ni libcurl, ni GameNetworkingSockets (refusé en CMake) : les sockets UDP deviennent un `NullLink` (`netlink.cpp`), une partie solo garde son serveur local muet. Dossier utilisateur `/user` en IndexedDB (IDBFS), chargé avant `main()` par `src/platform/web/pre.js`, réenregistré au plus une fois par seconde quand le jeu y écrit (`HKFilesWritten`, `fileio.cpp`) et quand la page est masquée. Page `src/platform/web/shell.html` : canvas plein écran que SDL suit, progression, son repris au premier clic ou à la première touche, pointer lock au clic (Échap ramène au menu). Les aides `HK_*` passent par l'URL (`?HK_MENU=serverstart`). Tests unitaires compilés aussi en WebAssembly et lancés par Node.js ; `tests/web_smoke.py` joue la build dans Chromium sans écran (Playwright, WebGL2 de SwiftShader) : menu, partie solo par Server Setup, apparition au clic et marche, dossier utilisateur gardé après rechargement ; job CI `web` à chaque push et PR. Validé localement (macOS, Chromium sans écran) : menu, écran d'apparition et partie dessinés, 60 images par seconde dans le navigateur. Reste : la permission du micro (chat vocal) n'a pas été essayée ; Firefox et Safari pas encore essayés.
  2. Lobby : remplacer curl (`lobby.cpp`) par `emscripten_fetch` derrière la même interface. `edifice-lobby` répond avec `Access-Control-Allow-Origin` pour l'origine de la page (`https://nicrf.github.io`, plus le domaine personnalisé s'il y en a un).

     *Fait :* `lobby.cpp` a une troisième implémentation de `LobbySend`/`LobbyPoll`/`LobbyDrop`/`LobbyShutdown` sous `__EMSCRIPTEN__` : `emscripten_fetch` (`-sFETCH=1`), asynchrone sur le thread du jeu (les rappels arrivent entre deux images), mêmes limites (16 requêtes en cours, réponse de 512 Ko au plus, 10 s). Le lobby (`server/cors.go`) répond avec `Access-Control-Allow-Origin` (et `Vary: Origin`) aux origines de `EDIFICE_CORS_ORIGINS` / `-cors-origins` (par défaut `https://nicrf.github.io` ; `*` toutes, `none` aucune), et répond lui-même aux pré-vérifications `OPTIONS` (`GET, POST, PUT, DELETE`, `Authorization, Content-Type`) avant la limite de débit ; tests Go (`cors_test.go`) ; `api` passe à 3. Dans la build web, l'onglet Internet liste les parties ; rejoindre une partie Internet dit que ce n'est pas encore possible depuis le navigateur, et une partie hébergée dans le navigateur reste `lan` (jamais annoncée), en attendant la tâche 3. `tests/web_smoke.py --lobby` le vérifie contre un lobby local sur une autre origine (job CI `web`). **À faire sur le serveur** : redéployer `edifice-lobby` (image `latest` après la fusion) ; `EDIFICE_CORS_ORIGINS` n'est à définir dans `.env` que pour ajouter un domaine personnalisé.
  3. Réseau : le navigateur n'ouvre pas de socket UDP, et `UdpLink` comme `GnsLink` y sont donc indisponibles. Ajouter un `DatagramLink` WebRTC DataChannel (non fiable, non ordonné) qui réutilise la signalisation et le TURN de M7d. Il faut aussi un pont côté natif pour que joueurs web et natifs se rejoignent. Les threads de `gnssignal.cpp` et `lobby.cpp` sont remplacés par de l'asynchrone dans la build web.

     *Fait (rejoindre depuis le navigateur ; détails dans `docs/M7.md`, « Joueurs du navigateur ») :*
     - **3a. Signalisation** : lignes `rtc1:K:CID:A[:B]` sur le relais existant (`signaling.cpp`, `SignalRtcFormat`/`SignalRtcParse`, testées et fuzzées), aucun changement de serveur. **Écart avec le plan** : pas de JSON, des champs séparés par `:` en base64url (le SDP ; le candidat et son `mid`), et un identifiant de tentative `CID` pour que l'hôte oublie un essai précédent du même navigateur.
     - **3b. Navigateur** : `src/net/webrtclink.cpp` (`WebRtcLink`) et `src/platform/web/webrtc.js` (`--js-library`) : WebSocket vers `signaling_url`, `RTCPeerConnection` avec les STUN/TURN de la session, DataChannel `edifice` `{ordered:false, maxRetransmits:0}`. Le JavaScript ne fait que mettre en file, le jeu lit et écrit les signaux en C++ (le même code que l'hôte) et relève tout à chaque image. `NetP2PMode`, `NetP2PStart`, `NetQueryHostP2P`, `NetOpenDataLink` passent par lui sous `__EMSCRIPTEN__` ; le chemin P2P de l'écran Join sert tel quel, et le blocage « Can't join … from the browser yet » a disparu. Les parties dont l'hôte ne prend pas les navigateurs sont grisées.
     - **3c. Hôte** : `src/net/rtchost.cpp`, option `EDIFICE_WEBRTC` (avec `EDIFICE_GNS`), fonction `webrtc` de `vcpkg.json` (libdatachannel 0.24.6 sans WebSocket, avec libjuice, usrsctp, plog), mise par `build.py` avec `--gns` (donc dans les releases), `--no-webrtc` pour s'en passer. Navigateurs sous une pseudo-adresse `0.1.x.y`, sans cookie (DTLS), slots stables et coups décidés par le serveur comme pour tout client (ces capacités passent par REQUESTENTER, pas par le transport). Binaire macOS arm64 : 11,4 → 14,1 Mo (+2,7 Mo).
     - **3d. Lobby** : `"webrtc": true` dans l'annonce, gardé seulement avec une identité vérifiée ; `api` passe à 4 (`server/webrtc_test.go`). TURN sur TCP/TLS pas fait : le TURN UDP suffit tant que l'UDP sort (box, mobile) ; à ajouter (`turns:` sur 443 derrière Caddy) si des joueurs derrière des réseaux fermés le demandent.
     - **3e. Tests** : `tests/net_webrtc.py` (lobby local `-allow-private`, `-cors-origins` de la page, TURN sur l'adresse LAN ; hôte `HK_AUTOSTART` avec `HK_USERDIR`, `servervisibility=public` vers ce lobby seulement, ports 37220/37221 ; Chromium sans écran qui rejoint par le code, apparaît, tire et pose un bloc, que l'hôte reçoit, `HK_NET_EVENTS`), en direct et en `--relay` : passe en local sous macOS. **Pas dans la CI** : il faudrait la build native GameNetworkingSockets/libdatachannel et la build web dans le même job, et les tests réseau restent locaux (choix de l'utilisateur).
     - **À faire sur le serveur** : redéployer `edifice-lobby` (API 4) ; sans cela le lobby public jette le champ `webrtc` et la version web grise toutes les parties. Rien d'autre (même relais, même TURN, CORS déjà en place).
     - **Non vérifié** : un vrai NAT entre navigateur et hôte, Firefox et Safari, un hôte Windows ou Linux (compilé en CI seulement), le passage du WebSocket du navigateur par le Caddy public.
     - **Héberger depuis le navigateur** (fait ensuite, à la demande ; détails dans `docs/M7.md`) : l'onglet répond aux offres WebRTC (un DataChannel par joueur, pseudo-adresses `0.1.x.y`, pas de cookie), garde le relais ouvert et s'annonce `"host": "web"` (lobby **API 5**, `server/webrtc_test.go`) ; les navigateurs rejoignent comme avant, la version de bureau fait l'offre par libdatachannel (`RtcHostConnect`). 8 joueurs au plus pour un hôte web, bandeau « keep this tab open and visible », boucle sur minuteries quand l'onglet est masqué. `tests/net_webhost.py` (local, direct et `--relay`). **À faire sur le serveur** : redéployer `edifice-lobby` (API 5) ; sans cela le lobby public jette `host` et la version de bureau traiterait l'hôte web comme un hôte natif (échec de connexion GameNetworkingSockets). **Non vérifié** : un onglet réellement masqué (Chromium sans écran le garde visible), un vrai NAT, Firefox et Safari en hôte. Le chat vocal depuis le navigateur demande la permission du micro (`getUserMedia` via SDL) et reste à essayer.
  4. Publication sur **GitHub Pages** (`https://nicrf.github.io/hugekilla-edifice-renew/`, domaine personnalisé possible, par exemple `play.rousseaufournier.com`) :
     - Un job `web` dans `.github/workflows/release.yml` installe emsdk (`mymindstorm/setup-emsdk`) et compile le preset `web` à chaque tag `v*`. Il publie le site avec `actions/upload-pages-artifact` et `actions/deploy-pages` (`permissions: pages: write, id-token: write`), et joint aussi une archive `.zip` à la release.
     - Une PR ne compile que la build web, sans la déployer, pour garder la CI comme garde-fou.
     - Activer Pages dans les réglages du dépôt, avec GitHub Actions comme source.
     - Limites à respecter : 1 Go par site, 100 Mo par fichier (découper `data/` si besoin), environ 100 Go de bande passante par mois.

     *Fait (sauf l'activation de Pages, qui revient au propriétaire du dépôt) :*
     - **Job `web`** (`release.yml`) : à chaque push et PR, emsdk épinglé (`mymindstorm/setup-emsdk`, `EMSDK_VERSION`), puis `build.py --web --test --package` et `tests/web_smoke.py`. Le zip va dans l'artefact `edifice-web`, les captures dans `web-shots`.
     - **À un tag `v*`** : le même job publie le site (les quatre fichiers `index.*`) par `actions/upload-pages-artifact`. Le job `pages` le déploie par `actions/deploy-pages` (`permissions: pages: write, id-token: write`, environnement `github-pages`). Le job `release` joint `Edifice-<version>-web.zip` aux trois archives.
     - **Taille** : environ 21 Mo au total, dont `index.data` 19 Mo, loin des limites.
     - **Pour activer** :
       1. Settings → Pages → Build and deployment → Source : **GitHub Actions**.
       2. Settings → Environments → `github-pages` → Deployment branches and tags : ajouter la règle de tag `v*`. Sinon la règle par défaut, la branche par défaut seulement, refuse le déploiement depuis un tag.
       3. Puis pousser un tag de release.
  5. Build **WebGPU optionnelle** : même code, shaders WGSL générés par `sokol-shdc`. La page charge la build WebGPU si `navigator.gpu` est disponible, sinon la build WebGL2. Le job CI produit les deux. À ne faire que si un effet le justifie (compute, nombreux draw calls).
- **Fait quand :** une partie solo tourne depuis GitHub Pages dans Chrome, Firefox et Safari, puis un joueur web rejoint une partie hébergée par un client natif via le lobby public.

### M15 - Manettes et commandes tactiles (M)
Jouer sans clavier ni souris : manette sur les trois systèmes et le web, écran tactile (surtout la version web sur téléphone et tablette). Aucun changement de protocole.
- **Tâches :**
  1. Manettes (`SDL_GameController`) : branchement à chaud, base de SDL plus un `gamecontrollerdb.txt` facultatif, stick gauche en marche analogique, stick droit pour regarder (zone morte, courbe, sensibilité, inversion), boutons liés aux actions (`pad_*` de `control.ini`, onglet Gamepad d'Options), vibration quand on est touché ; menus au focus (croix/stick, `A`, `B`) par un registre des zones cliquables dessinées (`ui.cpp`).

     *Fait :* `src/platform/gamepad.cpp`, logique pure dans `src/ui/inputlogic.h` (`tests/input_test.cpp`), aide `HK_PADTEST` (manette virtuelle de SDL) et `tests/input_smoke.py` ; détails dans BUILD.md, « Manettes ». Non vérifié : de vraies manettes (Xbox, PlayStation, Switch) et la vibration, l'API Gamepad des navigateurs (seulement une manette simulée dans Chromium).
  2. Commandes tactiles : manette à l'écran (stick gauche, zone de regard à droite, boutons), affichée au premier toucher, menus au doigt ; page web sans défilement ni zoom, bouton plein écran, indication de passer en paysage.

     *Fait :* `src/platform/touch.cpp` (disposition dans `inputlogic.h`, testée), réglages dans Options → Pad / Touch, page web (`shell.html`) ; `tests/input_smoke.py` (doigts par `HK_PADTEST`) et `tests/web_smoke.py` (téléphone simulé par Playwright, événements tactiles CDP) ; détails dans BUILD.md, « Écrans tactiles ». Non vérifié : de vrais téléphones et tablettes (iOS Safari, Android Chrome), un écran tactile natif. Reste : le clavier du téléphone pour les champs de texte (code de partie, nom).

## 4. Sécurité : vulnérabilités classées

Toutes sont déclenchables à distance. L'obstacle du checksum XOR (sock.cpp:350-364) est trivial à calculer.

**Critiques : corruption mémoire ou crash à distance**
1. sock.cpp:179-180 : `rbufpos=num*450` avec `num` lu dans le paquet, puis `CopyMemory` dans `reliablerecvdata[50000]`. Cela donne une écriture jusqu'à environ 29 Mo au-delà de l'objet.
2. sock.cpp:200-205 : `wsize` des paquets groupés n'est pas vérifié contre `recvbufsize`. Cela donne une écriture jusqu'à 65535 octets dans un `Message` de 50001 (sock.h:10-14).
3. sock.cpp:62-68, 181-188 : `dwsize` vient de l'attaquant et la copie n'est pas bornée. Ensuite, `reliablerecvbufsize` peut devenir énorme.
4. sock.cpp:154-180 : aucune longueur minimale. Un RELIABLE de 5 à 8 octets donne une taille négative, convertie en `size_t` énorme.
5. multiplay.cpp:403-410 et 418 : `strcpy(message[1000], req->name)` et `strcpy(player->name[32], ...)` sur `infosock`, avant toute connexion. Le `static Message msg` (389) garde les octets des paquets précédents, et le test de serveur plein (412) arrive après le premier strcpy.
6. multiplay.cpp:1554-1572 : `WORD len` du chat peut atteindre 65535 et est copié dans `chatmessage[1000]`.
7. multiplay.cpp:1478-1520 : ReceiveBlockBuffer avec un `numblocks` sans borne, puis rediffusion de `SendReliable(buf,pos)` hors limites. Un seul client fait tomber le serveur.
8. multiplay.cpp:1748-1760 : `GetPlayer(killer)->kills++` sans borne, plus un `sprintf(cmsg[300])` sur des noms non terminés.
9. multiplay.cpp:690-697 : `credits[credit]` est seulement testé contre `credit<0`, alors que le tableau fait `credits[10]`.
10. multiplay.cpp:1890-1894 : `sobject[update->num]`, où `num` est un int signé sans borne.

**Hautes**
11. multiplay.cpp:791-821 : PLAYERNAMES. `playernum` et `namelen` sont des char signés sans borne, et `numplayers` n'est pas borné.
12. multiplay.cpp:643-688 et 883-886, plus HandleFireGun 1650-1690 : aucun indice de joueur n'est vérifié côté client. Un DELETEPLAYER usurpé déconnecte la victime. L'index de son n'est exploitable que sous Windows (sound.cpp:176), sound_sdl.cpp:225 le borne déjà.
13. multiplay.cpp:1353-1374 et 1708 : `pnum` est fourni par le client (on peut déplacer n'importe quel joueur), et `weapon` sert sans borne dans `ammoingun[weapon]--`.
14. multiplay.cpp:296-332 et sock.cpp:118-133 : les callbacks `RECEIVED*` peuvent être rejoués depuis le réseau. Cela donne un renvoi du monde entier, un respawn ou un soin à volonté.
15. sock.cpp:156 : un RELIABLE réécrit `dest` avant même le checksum, ce qui permet de détourner une session. **Mais cette réécriture fait marcher le NAT** : il faut la sécuriser, pas la supprimer.
16. multiplay.cpp:496-500 : un SIMPLE reçu sur `infosock` est traité avec `pnum=0`, donc comme s'il venait de l'hôte.
17. multiplay.cpp:508-511 et 613-640 : les adresses source ne sont jamais vérifiées, et un `ENTER` est accepté de n'importe qui (`SetLocalNum(msg2)` sans borne, 619).
18. multiplay.cpp:280-306 et sock.cpp:315-328 : amplification. Un paquet usurpé fait envoyer le monde entier à la victime, avec des renvois sans fin.
19. multiplay.cpp:551 : `sizeof(kp)` sur un pointeur. Le serveur envoie l'adresse de pile au lieu de la struct, ce qui est une fuite utile pour contourner l'ASLR, et la fonctionnalité est cassée. Vérifié.
20. land.cpp:520-522, 610-615 et multiplay.cpp:741-768 : un LANDINFO malveillant (`numdefs`, `numgroundtex`, `numblotch` sans borne, chemins non terminés passés à `strcat`) permet une traversée de chemin.
21. multiplay.cpp:1478-1484 : construction ou destruction sans contrôle de distance, d'équipe ni de coût.
22. multiplay.cpp:362-372 et playing.cpp:385, 460 : la mort est décidée par la victime, ce qui permet l'invulnérabilité.

**Moyennes et basses**
- sock.cpp:169 : le canal reliable reste bloqué après 65535 messages.
- sock.cpp:94-98 : ACK acceptés de n'importe quelle source.
- sock.cpp:286-312 : file reliable sans limite.
- sock.cpp:267 et 322-327 : renvoi compté en frames.
- multiplay.cpp:412-446 : slots fantômes, SERVERFULL jamais envoyé, ports comparés dans des ordres d'octets différents, `bind` non vérifié, 26 ports pour 32 joueurs.
- multiplay.cpp:1296-1304 : décalage des slots dans DeletePlayer.
- multiplay.cpp:229-238 : INFO amplifiable et liste de serveurs polluable.
- sock.cpp:27 : `sizeof(this)`.
- multiplay.cpp:1940 : `candidate` non initialisé.

**Précondition pour Steam ou des serveurs publics :** les points 1 à 20 doivent être fermés (M2 et M3). ENet n'apporte aucun chiffrement ; SDR ou GameNetworkingSockets apportent le chiffrement et l'authentification.

## 5. Gestion de version du jeu pour le netplay

### 5.1 Constat
- Aucune version dans `REQUESTENTER` ni dans `INFO` (multiplaystruct.h:46-58). Rien non plus dans `project(Edifice CXX)` (CMakeLists.txt:2) ni dans le `.rc`, qui n'a pas de VERSIONINFO. La seule mention est « beta version » dans HugeKilla/Instructions.txt:1.
- `INFO` et `REQUESTENTER` sont envoyés avec la longueur `2+namelen+1` (multiplay.cpp:236, 880, 1003, 1079). **Des champs ajoutés en fin de struct ne partiraient donc jamais.** Des champs ajoutés en tête casseraient les anciens pairs, qui lisent `name` à l'offset 2.
- `Sock::Receive` renvoie la taille du message dans les cas normaux (sock.cpp:68, 189, 205), mais 1 pour les paquets consommés sans données (161, 172, 195) et `sizeof(callback)` pour les ACK (125, 147). `ServerLoop` ignore cette valeur (multiplay.cpp:395, `while(infosock.Receive(&msg,&source))`). Comme `msg` est `static` et garde les octets des paquets précédents, **on ne peut pas détecter un trailer sans connaître la longueur**.
- `SERVERFULL` est construit mais jamais envoyé (multiplay.cpp:414). Le client ne gère pas ce message, et un message SIMPLE inconnu est ignoré en silence. Un ancien client refusé renverrait donc « retrying » toutes les 2 s (multiplay.cpp:871-881).
- Un ancien serveur accepte n'importe quel `REQUESTENTER` : le contrôle **côté client, sur INFO**, est indispensable. Tous les chemins de connexion passent déjà par INFO : JoinScreenJoin (1068), IDC_JOINLOCAL (991), IDC_JOININTERNET via REQUESTINFO (972-988).

### 5.2 `version.h`, versionné dans le dépôt
```cpp
#define EDIFICE_VERSION_STRING   "0.2.0"   // informatif
#define EDIFICE_PROTOCOL_VERSION 2         // 1 = protocole historique sans trailer
#define EDIFICE_NET_MAGIC        0x31564445 // 'EDV1'
#ifndef EDIFICE_BUILD_ID
#define EDIFICE_BUILD_ID "dev"              // surchargeable par CMake
#endif
// EDIFICE_LAYOUT_HASH : constexpr combinant sizeof(SimpleMessage), PlayerPosition,
// FireGunDirection, LandInfo, LandDefs, SkySettings, ServerObject, ServerVars,
// UpdateServerObject, KilledPosition. Détecte /Zp1 et tout ajout de champ.
```
Pas de `configure_file` ni de `git describe` : le `.vcproj` ne passe pas par CMake, et le dossier n'est pas un dépôt git. À ajouter aux deux builds.

### 5.3 Format sur le fil, rétrocompatible
Trailer sérialisé octet par octet en little-endian, à l'offset `2+namelen+1`, dans `REQUESTENTER` et `INFO` :
```
uint32 magic 'EDV1' | uint16 protocol | uint32 layoutHash | uint8 verLen | char ver[verLen<=15] | (M4+) uint64 dataHash
```
- Un trailer n'est considéré comme présent que si `len >= 2+namelen+1+11` **et** que le magic correspond. Sinon, le pair est traité comme protocole 1.
- La négociation tient dans une fonction pure, `CheckPeerVersion(buf, len, &out)`, testable en M5.
- Le format des messages legacy ne change pas, et aucun nouveau type n'est ajouté pour le handshake. Seule exception : `VERSIONMISMATCH`, en SIMPLE `type2=26` (premier code libre après `GAMEOVER 25`).

### 5.4 Lecture de la taille et factorisation
- Ajouter à `Sock::Receive` un paramètre optionnel `int *len=nullptr` (sock.h:76), ou utiliser sa valeur de retour, dans `ServerLoop` (multiplay.cpp:395) et `ClientLoop` (713-715).
- `SendRequestEnter()` remplace les trois copier-coller (876, 999, 1075). `SendInfo()` remplace HandleSimpleServer REQUESTINFO (229-238). Les deux tronquent les noms à 31 et 99 octets, ce qui est nécessaire avant `GetPersonaName`, en UTF-8.

### 5.5 Comportement en cas d'écart

| Situation | Comportement |
|---|---|
| Nouveau serveur, `REQUESTENTER` sans trailer ou avec un écart de protocole ou de layout | Aucun slot alloué. Envoi réel (`infosock.dest=source; Send; Flush`) d'un SIMPLE `VERSIONMISMATCH(msg=proto serveur)` **et** d'un `CHATMESSAGE CMSG_INFO`, que les anciens binaires affichent déjà (multiplay.cpp:1567-1570) : « Serveur Edifice 0.3.0 (protocole 3) : mettez votre jeu à jour ». |
| Serveur plein | `SERVERFULL` réellement envoyé (corrige 411-415), plus le texte CMSG_INFO. |
| Nouveau client, `INFO` sans trailer | Serveur marqué « ancienne version (protocole 1) » et grisé. On refuse de le rejoindre **avant** d'envoyer `REQUESTENTER`. |
| Nouveau client, `INFO` avec un autre protocole | Grisé avec la raison « Serveur 0.3.0 (p3), vous : 0.2.0 (p2) ». Join refusé. |
| Nouveau client qui reçoit `VERSIONMISMATCH` ou `SERVERFULL` | Message dans la console et sur l'écran Join, puis `Disconnect()` et `playstate=0` pour arrêter le retry (873). |
| Ancien client qui interroge un nouveau serveur | Il ignore le trailer. Option : le serveur ajoute « [v0.2.0] » au nom affiché. |
| Écart de `dataHash` (M4) | Refus avec un code distinct, ou simple avertissement (à décider). |

### 5.6 Affichage et listes de serveurs
- **Écran Join SDL :** `localgamenames[MAXLOCALLIST][100]` (multiplay.cpp:1022) devient une struct `{name, gamever, proto, compatible, legacy, nplayers}`. Adapter `JoinScreenAddGame` (1118) et le rendu (1226) : « nom  v0.2.0 », grisé avec la raison si incompatible. `JoinScreenJoin` (1069) refuse le join.
- **Dialogue Win32 :** listbox `IDC_LOCALGAMES` (735, 993), même logique.
- **Console** au démarrage et **menu** (`DrawMenu`, ui.cpp:122) : `Edifice 0.2.0 (protocole 2, build dev)`.
- **Lobby Steam (M9) :**
  - `SetLobbyData` : `game=edifice`, `proto`, `layout`, `gamever`, `datahash`, `host=SteamID64`, `name`.
  - `AddRequestLobbyListStringFilter("game","edifice")` et `("proto", "<N>")`. C'est obligatoire avec l'App ID 480, qui est partagé.
  - Nouvelle vérification sur `LobbyEnter_t` avant `Connect()`, avec un refus explicite.
  - Le trailer continue de transiter dans `REQUESTENTER`, même sous SDR.
- **ENet (si retenu) :** `enet_host_connect(..., data=EDIFICE_PROTOCOL_VERSION)` et `enet_peer_disconnect(peer, code)` pour le motif de refus.

### 5.7 Politique d'incrément (bump)
- `EDIFICE_PROTOCOL_VERSION` doit être **identique** des deux côtés, sans négociation. On l'incrémente dans le même commit que :
  - un changement du format ou de l'ID d'un message, ou de la sémantique d'un champ ;
  - un changement de l'ordre ou du contenu de `ServerVars` ;
  - un changement de la génération du terrain ou du PRNG (M4) ;
  - un changement de quantification ou de tick rate ;
  - chaque lot de M6 ;
  - un changement de transport (M7, M8).
- `EDIFICE_LAYOUT_HASH` est calculé automatiquement. Il disparaît après M6, avec un incrément de protocole à ce moment-là.
- `EDIFICE_VERSION_STRING` suit semver et reste **informatif** : il ne motive jamais seul un refus. On l'incrémente à chaque release.
- Règle de livraison : tout incrément de protocole implique de livrer Windows et macOS ensemble.
- Les règles sont documentées en commentaire dans `version.h`.
- Avertir les testeurs : un Windows Debug en `/Zp1` sera refusé par tous les autres builds, ce qui est voulu.

### 5.8 Matrice de tests
- Ancien serveur contre nouveau client : grisé, pas de REQUESTENTER envoyé.
- Nouveau serveur contre ancien client : texte CMSG_INFO affiché, aucun slot créé.
- Nouveau contre nouveau, même protocole : la partie fonctionne.
- Protocole incrémenté sur un seul poste : `VERSIONMISMATCH` reçu, et le lobby Steam filtré.
- Win Debug `/Zp1` contre macOS : refus par le layout.
- Serveur plein : `SERVERFULL` reçu.
- Cas à couvrir en LAN, par IP, et via Steam (M9).

## 6. Risques transverses
- **Conflit avec l'agent du port :** M1, M3 et M9 touchent `JoinScreen*`, `ClientLoop` et `CMakeLists.txt`. Passer après son feu vert. M12 attend la fin du port.
- **NAT et LAN :** filtrer exactement sur ip:port, ou supprimer la réécriture de `dest`, casse Internet. Rétablir le filtre de multiplay.cpp:717 casse le broadcast.
- **Terrain :** le PRNG maison ne suffit pas si la libm diverge. Le repli est l'envoi des positions par le serveur. Les seeds et les `.sav` existants changent de décor.
- **Faux positifs de l'anti-triche :** passer d'abord par une phase de log seulement.
- **Licence :** le code est sous LGPL-3.0 et Steamworks est propriétaire. Garder le SDK hors du dépôt, en dépendance optionnelle et liée dynamiquement, avec un build sans Steam complet. Une exception peut être demandée à l'auteur (license.txt:3). À faire valider par un juriste.
- **macOS :** le hardened runtime entre en conflit avec l'overlay Steam (entitlements nécessaires). `steam_appid.txt` dépend du `chdir` d'EnterDataDir.
- **Rendu (M13) :** la physique est couplée au rendu (playing.cpp:495-503) : le nouveau backend ne doit pas changer le pas de temps. Les GPU intégrés anciens peuvent manquer de GL 3.3, d'où le maintien du backend fixe jusqu'à la bascule.
- **Web (M14) :** sans pont WebRTC côté natif, les joueurs web ne jouent qu'entre eux. Safari et Firefox ont un support WebGPU récent : rester sur WebGL2 par défaut. GitHub Pages n'envoie pas COOP/COEP : si des threads devenaient indispensables, il faudrait coi-serviceworker (rechargement à la première visite) ou un hébergement qui pose ces en-têtes.
- **Gameplay :** M11 phase 3 touche à la physique couplée au rendu (playing.cpp:495-503). À faire derrière un flag.

## 7. Décisions ouvertes pour l'utilisateur
1. **Transport Internet :**
   - (a) ENet : simple, sans chiffrement, à vendoriser dans 2 builds.
   - (b) GameNetworkingSockets (open source, BSD) : même API que Steam Networking Sockets, chiffrement, un seul code pour les builds avec et sans Steam.
   - (c) Steam SDR seul, en gardant `Sock` durci pour le LAN et l'IP directe.
   - Recommandation : (b) ou (c), pour éviter d'avoir trois transports.
2. **Garder le build Windows natif (.sln/.vcproj, WinMain, DirectSound) ?** Ou passer Windows sous CMake et SDL dès M5 ? Cela conditionne la durée de vie de `/Zp1` et de `version.h` hors CMake.
3. **App ID Steam :** 480 en développement, puis un App ID payant (Steam Direct) avant publication. Quel calendrier ?
4. **Lobbies :** FriendsOnly ou Public par défaut ?
5. **Hash des données de gameplay :** refus strict ou simple avertissement ?
6. ~~**Anciens `.sav` après M4 et M6 :** chargement legacy ou rupture assumée et annoncée ?~~ Chargement legacy (M6) : le relief, le ciel et les bâtiments sont repris ; le décor (arbres, taches) diffère depuis le générateur PCG32 du protocole 3.
7. **Numéro de version de départ :** `0.2.0` et protocole 2 ? Afficher le build ID ?
8. **HKDS (serveur dédié) :** reste-t-il en UDP pur, ou faut-il prévoir SteamGameServer plus tard ?
9. **Gestion de source :** faire un `git init` maintenant ? Le dossier n'est pas un dépôt, ce qui complique la baseline M0 et les petites PR de M12.