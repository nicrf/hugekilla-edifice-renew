# Edifice (HugeKilla) — Renew

Remise en état du jeu **Edifice**, alias *HugeKilla* : un FPS multijoueur de 2000 en capture du drapeau, où l'on bâtit des forts bloc par bloc sur des terrains générés aléatoirement, avec jetpack et deltaplane.
Le jeu tourne désormais sur **macOS, Linux et Windows**, grâce à SDL2 et OpenGL.

## Hommage aux auteurs originaux

Edifice a été créé par **Nelson Planting** (programmation) et **John Thomas** (modèles et graphismes) sous la bannière *Nelso & Particle*, en 2000.
Nelson a ensuite publié tout le code source sous licence LGPL. Il souhaitait que des morceaux soient réutilisés, améliorés et rendus à la communauté open source. Ce dépôt existe pour honorer ce souhait.

> Le code d'origine est toujours disponible sur SourceForge :
> **https://sourceforge.net/p/hugekilla/code/HEAD/tree/HugeKilla/**

Le premier commit de ce dépôt est une importation fidèle de la révision SVN r5. Tout le reste est construit par-dessus, et l'historique permet de comparer avec l'original. Les crédits sont aussi affichés dans le jeu (menu principal → *Credits*).

Merci, Nelson et John.

## Ce qui a changé

- **Multiplateforme** : SDL2 (fenêtre, entrées, audio) compilée en statique, OpenGL 2.1 conservé. Plus aucune dépendance à installer pour jouer.
- **Corrections** : plantages et erreurs 64 bits (textures des armes, portée des boucles VC6, casse des fichiers sous Linux…).
- **Nouveaux écrans** : configuration du serveur (ciel, relief, décor, captures, temps, avec choix aléatoire), multijoueur en jeu (IP ou LAN), options (touches, vidéo, audio, nom du joueur), crédits.
- **Builds** : un seul `build.py` pour les trois OS. Les releases sont générées automatiquement par GitHub Actions à chaque tag `v*`.

La suite est décrite dans la [feuille de route](docs/ROADMAP.md) : netplay sécurisé et versionné, restructuration du code, « Rejoindre via Steam ».

## Jouer

Téléchargez l'archive de votre système dans les [Releases](https://github.com/nicrf/hugekilla-edifice-renew/releases).

- **macOS** : l'app n'est pas notariée. Au premier lancement, faites clic droit → *Ouvrir*.
- **Linux** : décompressez l'archive puis lancez `./Edifice`. Le dossier `data/` doit rester à côté.
- **Windows** : décompressez l'archive puis lancez `Edifice.exe`.

`F6` plein écran · `` ` `` console · `Échap` libérer/capturer la souris. En mode bac à sable, la console permet de construire sans limite : tapez `gamemode 1`.

## Compiler

```bash
python3 build.py --run
```

Détails, prérequis et options : [BUILD.md](BUILD.md).

## Licence

GNU LGPL v3 : voir [license.txt](license.txt) et [lgpl-3.0.txt](lgpl-3.0.txt), comme le projet d'origine. Le texte d'origine de l'auteur est dans [readme.txt](readme.txt).
