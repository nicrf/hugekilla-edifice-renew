# Edifice (HugeKilla) — Renew

Remise en état du jeu **Edifice**, alias *HugeKilla* : un FPS multijoueur de 2000 en capture du drapeau, où l'on bâtit des forts bloc par bloc sur des terrains générés aléatoirement, avec jetpack et deltaplane.
Le jeu tourne désormais sur **macOS, Linux et Windows**, grâce à SDL2 et sokol_gfx (Metal, Direct3D 11, OpenGL 4.1).

## Hommage aux auteurs originaux

Edifice a été créé par **Nelson Planting** (programmation) et **John Thomas** (modèles et graphismes) sous la bannière *Nelso & Particle*, en 2000.
Nelson a ensuite publié tout le code source sous licence LGPL. Il souhaitait que des morceaux soient réutilisés, améliorés et rendus à la communauté open source. Ce dépôt existe pour honorer ce souhait.

> Le code d'origine est toujours disponible sur SourceForge :
> **https://sourceforge.net/p/hugekilla/code/HEAD/tree/HugeKilla/**

Le premier commit de ce dépôt est une importation fidèle de la révision SVN r5. Tout le reste est construit par-dessus, et l'historique permet de comparer avec l'original. Les crédits sont aussi affichés dans le jeu (menu principal → *Credits*).

Merci, Nelson et John.

## Ce qui a changé

- **Multiplateforme** : SDL2 (fenêtre, entrées, audio) compilée en statique, rendu par sokol_gfx sur l'API de chaque système (Metal sous macOS, Direct3D 11 sous Windows, OpenGL 4.1 sous Linux), au même rendu que l'OpenGL 2.1 d'origine, qui reste disponible le temps d'une release (`build.py --gl`). Plus aucune dépendance à installer pour jouer.
- **Corrections** : plantages et erreurs 64 bits (textures des armes, portée des boucles VC6, casse des fichiers sous Linux…).
- **Nouveaux écrans** : configuration du serveur (ciel, relief, décor, captures, temps, avec choix aléatoire), multijoueur en jeu (IP ou LAN), options (touches, vidéo, audio, nom du joueur, manette), crédits.
- **Manettes** : toute manette que SDL connaît (Xbox, PlayStation, Switch Pro, la plupart des manettes USB/Bluetooth), branchée à chaud, en jeu comme dans les menus.
- **Builds** : un seul `build.py` pour les trois OS. Les releases sont générées automatiquement par GitHub Actions à chaque tag `v*`.

La suite est décrite dans la [feuille de route](docs/ROADMAP.md) : netplay sécurisé et versionné, restructuration du code, « Rejoindre via Steam ».

## Jouer

Téléchargez l'archive de votre système dans les [Releases](https://github.com/nicrf/hugekilla-edifice-renew/releases).

- **macOS** : l'app n'est pas notariée. Au premier lancement, faites clic droit → *Ouvrir*.
- **Linux** : décompressez l'archive puis lancez `./Edifice`. Le dossier `data/` doit rester à côté.
- **Windows** : décompressez l'archive puis lancez `Edifice.exe`.

**Jouer par Internet** : l'hôte choisit une visibilité « Internet » dans *Server Setup* (publique, ou amis/privée avec un code de partie à partager), les autres ouvrent l'onglet *Internet* de l'écran *Join* (ou tapent le code). La connexion est directe et chiffrée entre joueurs, ou passe par le relais du serveur de parties quand rien d'autre ne passe : aucun port à ouvrir sur la box.

`F6` plein écran · `` ` `` console · `Échap` libérer/capturer la souris. En mode bac à sable, la console permet de construire sans limite : tapez `gamemode 1`.

**Manette** (réglages et boutons dans *Options → Gamepad*) : stick gauche se déplacer, stick droit regarder, `RT` tirer, `LT` jetpack/planeur, `A` sauter, `B` prendre/poser un objet (station, drapeau), `X` recharger, `Y` construire, `LB`/`RB` arme précédente/suivante (en posant une structure : la tourner), `R3` zoom, `Back` scores, croix haut/gauche/bas parler/parler à l'équipe/push-to-talk, `Start` menu. Dans les menus : croix ou stick gauche pour choisir, `A` valider, `B` retour. Une manette inconnue de SDL se déclare dans un `gamecontrollerdb.txt` du dossier des réglages (voir BUILD.md).

## Compiler

```bash
python3 build.py --run
```

Détails, prérequis et options : [BUILD.md](BUILD.md).

## Licence

GNU LGPL v3 : voir [license.txt](license.txt) et [lgpl-3.0.txt](lgpl-3.0.txt), comme le projet d'origine. Le texte d'origine de l'auteur est dans [readme.txt](readme.txt).

Bibliothèques incluses dans les archives (leurs licences sont dans le dossier `licenses/`) : [SDL2](https://libsdl.org) (zlib), [Opus](https://opus-codec.org) (BSD), [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) de Valve (BSD-3), [Protocol Buffers](https://protobuf.dev) (BSD-3), [Abseil](https://abseil.io) (Apache 2.0), [OpenSSL](https://www.openssl.org) (Apache 2.0), [libdatachannel](https://github.com/paullouisageneau/libdatachannel) et [libjuice](https://github.com/paullouisageneau/libjuice) (MPL 2.0, pour que les joueurs du navigateur rejoignent les parties des versions de bureau), [usrsctp](https://github.com/sctplab/usrsctp) (BSD-3), [plog](https://github.com/SergiusTheBest/plog) (MIT), [stb_image](https://github.com/nothings/stb) (domaine public / MIT), [sokol_gfx](https://github.com/floooh/sokol) (zlib), et [curl](https://curl.se) sous Windows.
