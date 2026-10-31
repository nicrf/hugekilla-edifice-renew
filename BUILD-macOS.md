# Edifice (HugeKilla) — build macOS

Port SDL2 + OpenGL 2.1 (profil legacy, toujours supporté par macOS, y compris Apple Silicon).

## Prérequis

```bash
brew install sdl2 cmake ninja
```

## Compiler et lancer

```bash
cmake -S . -B build -G Ninja && cmake --build build
```

```bash
open build/Edifice.app
```

Les données du jeu (`HugeKilla/`) sont copiées dans `Edifice.app/Contents/Resources/HugeKilla`.
Un autre dossier de données peut être passé en premier argument ou via `HUGEKILLA_DATA`.

## Ce qui a changé par rapport à la version Windows

- `compat/` : émulation du sous-ensemble Win32 utilisé (fichiers, recherche de fichiers, BMP, clavier VK, Winsock → sockets BSD), en-têtes de remplacement (`windows.h`, `gl/gl.h`…).
- `platform_sdl.cpp` : fenêtre, contexte GL, souris (mode relatif) et clavier SDL2 traduits en codes VK Windows ; remplace `WinMain`.
- `sound_sdl.cpp` : mixeur SDL2 avec spatialisation 3D simple ; remplace DirectSound.
- Écran « Multiplayer Game » en jeu (remplace la boîte de dialogue Win32), écran Options (touches, plein écran, VSync, distance d'affichage, souris, volume — sauvegardés dans `control.ini`) et écran Crédits.
- Corrections 64 bits / clang : `long` 64 bits dans le lecteur `.cob` (textures des armes), portée des variables de `for` (VC6), références non-const vers des temporaires dans `vector.h`, globales homonymes de `land.cpp`.

## Raccourcis

`F6` plein écran · `` ` `` console · `Échap` libérer/capturer la souris.
Les lettres suivent la disposition du clavier (comme sous Windows) : sur AZERTY, remappez Avancer/Gauche sur `z`/`q` dans Options → Controls.

## Aides au debug

- `HK_AUTOSTART=1` démarre un serveur local directement.
- `HK_MENU=options|credits|join` ouvre cet écran au démarrage.
- `HK_SHOT=fichier.ppm` (et `HK_SHOT_FRAME=N`) enregistre l'image rendue N.
