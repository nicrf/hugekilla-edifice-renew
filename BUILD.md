# Edifice (HugeKilla) — compilation

Un seul `CMakeLists.txt` pour les trois OS, et un seul point d'entrée : `build.py` (Python 3, bibliothèque standard uniquement).

| OS | Chemin de code | Résultat |
|----|----------------|----------|
| macOS | SDL2 + OpenGL 2.1 legacy (aussi sur Apple Silicon), Win32 émulé par `src/compat` | `Edifice.app` (données dans `Contents/Resources/data`) |
| Linux | SDL2 + OpenGL/GLU, même chemin que macOS | `Edifice` + dossier `data/` à côté |
| Windows (MSVC) | chemin Win32 natif d'origine : `WinMain`, DirectSound, `win32/hugekilla.rc` | `Edifice.exe` + contenu de `data/` à côté |

## Prérequis

- **macOS** : `xcode-select --install`, puis `brew install cmake sdl2 ninja`
- **Linux (Debian/Ubuntu)** : `sudo apt install build-essential cmake ninja-build libsdl2-dev libglu1-mesa-dev`
  (Fedora : `sudo dnf install gcc-c++ cmake ninja-build SDL2-devel mesa-libGLU-devel`)
- **Windows** : Visual Studio 2019/2022 avec la charge « Développement Desktop en C++ » (inclut CMake), et Python 3. `build.py` prend le Visual Studio installé le plus récent. MinGW (MSYS2 ou compilation croisée) est aussi accepté par `CMakeLists.txt` (`-fpermissive`, lien statique), sans être testé régulièrement.

## Compiler, lancer, empaqueter

```bash
python3 build.py                     # Release par défaut -> build/<os>-release
python3 build.py --config Debug      # -> build/<os>-debug
python3 build.py --clean             # repart d'un dossier de build vide
python3 build.py --run               # compile puis lance le jeu (arguments en plus transmis au jeu)
python3 build.py --package           # dist/Edifice-<version>-<os>.zip (macOS, Windows) ou .tar.gz (Linux)
```

Chaque archive contient le jeu, `readme.txt`, `license.txt` et `lgpl-3.0.txt`. Sous macOS, `Edifice.app` est autonome : l'étape post-build (`cmake/macos_bundle_fixup.cmake`) copie la `libSDL2` de Homebrew dans `Contents/Frameworks`, réécrit le chemin de chargement (`@executable_path/../Frameworks`) puis signe le bundle en ad hoc (`codesign -v --deep --strict` passe). Le zip se lance donc sans Homebrew. Il n'est pas notarié : au premier lancement d'une copie téléchargée, ouvrez-le par clic droit → Ouvrir. `--clean` supprime aussi les restes de l'ancien build à plat (`build/CMakeCache.txt`…).

(`py build.py ...` sous Windows.) Générateur : Ninja s'il est installé, sinon celui par défaut de CMake ; sous Windows, Visual Studio en 32 bits (`Win32`) comme la version d'origine (`EDIFICE_ARCH=x64` pour du 64 bits ; `--generator` pour forcer un autre générateur).
La version vient de `project(Edifice VERSION …)` dans `CMakeLists.txt`.

CMake directement, si on préfère :

```bash
cmake --preset macos-release && cmake --build --preset macos-release   # ou linux-release
cmake --preset windows-vs && cmake --build --preset windows-release    # Windows, Visual Studio 2022
cmake --preset windows-vs2019 && cmake --build --preset windows-vs2019-release   # Visual Studio 2019
```

Les presets macOS/Linux exigent Ninja (`ninja-build`) ; `build.py` se rabat sur Make s'il est absent. Pour une autre version de Visual Studio, utilisez `build.py` (générateur par défaut).

## Arborescence

```
src/game/      boucle de jeu, terrain (land*), structures, physique, config, vector, StdAfx.h, hugekilla.cpp (code commun + WinMain sous _WIN32)
src/render/    rendu OpenGL (redrawing), textures, modèles .cob (ase)
src/net/       sockets UDP (sock), protocole et serveur/client (multiplay*)
src/ui/        menus en jeu, console, écran Options (SDL)
src/audio/     sound.cpp (DirectSound, Windows) / sound_sdl.cpp (SDL, autres OS)
src/platform/  platform_sdl.cpp : main SDL (fenêtre, contexte GL, entrées) hors Windows
src/compat/    émulation du sous-ensemble Win32 (win32compat.*) et en-têtes de remplacement (include/windows.h, gl/gl.h…), hors Windows
win32/         hugekilla.rc, resource.h (inclus par le code), icônes ; .sln/.vcproj d'origine conservés pour référence (legacy, chemins non mis à jour)
data/          données du jeu (ini, textures, sons, objets, Structures) — ex-HugeKilla/
```

Les sources incluent leurs en-têtes par nom nu (`"vector.h"`, `<StdAfx.h>`…) : tous les dossiers `src/*` (et `win32/` pour `resource.h`) sont dans le chemin d'inclusion.

## Données

Le jeu charge ses données avec des chemins relatifs. Hors Windows, `EnterDataDir` (`src/platform/platform_sdl.cpp`) cherche `control.ini`, dans l'ordre : premier argument, `HUGEKILLA_DATA`, `<dossier de l'exécutable>/data` (= `Contents/Resources/data` dans le bundle), puis `data/` du dépôt (`HK_SOURCE_DATA_DIR`).
`HK_SOURCE_DATA_DIR` n'est défini qu'en Debug : un binaire Release déplacé loin de son `data/` ne va jamais écrire dans le `data/control.ini` suivi par git.
Sous Windows, le code d'origine lit le répertoire courant : le contenu de `data/` est copié à côté de `Edifice.exe` (et c'est le répertoire de débogage dans Visual Studio).

À chaque build, `data/` est recopié à côté de l'exécutable (Windows, Linux ; `cmake/copy_data.cmake`), sauf `control.ini` et `net.ini`, que le jeu réécrit (écrans Options et Multiplayer) : ils ne sont copiés que s'ils manquent, donc les réglages locaux survivent à une recompilation. Supprimez-les du dossier de build pour revenir aux valeurs par défaut. Sous macOS, le bundle est régénéré depuis `data/` et ces réglages repartent des valeurs du dépôt.

Installation Linux : `cmake --install build/linux-release --prefix ~/Games/Edifice` donne `Edifice` + `data/` dans un dossier inscriptible (c'est aussi le préfixe par défaut). N'installez pas sous `/usr/local` : le jeu écrit `control.ini` dans son dossier de données.

Noms de fichiers : les données ont été créées sous Windows, insensible à la casse. Hors Windows, `HKPath` (`src/compat/win32compat.cpp`) retrouve un chemin composante par composante sans tenir compte de la casse quand le chemin exact n'existe pas (utile sur Linux et sur APFS sensible à la casse). Les fichiers du dépôt portent les noms en minuscules que cherche le code (`land.ini`, `object.ini`, `wall3.bmp`).

Fins de ligne : les `.ini` et `Structures/*.txt` sont en CRLF et le code s'appuie sur le `\r`. `.gitattributes` marque `data/**` en `-text` pour que git les stocke tels quels malgré `core.autocrlf=input`.

## Ce qui a changé par rapport à la version Windows d'origine

- `src/compat/` : émulation du sous-ensemble Win32 utilisé (fichiers, recherche de fichiers, BMP, clavier VK, Winsock → sockets BSD), en-têtes de remplacement (`windows.h`, `gl/gl.h`…).
- `src/platform/platform_sdl.cpp` : fenêtre, contexte GL, souris (mode relatif) et clavier SDL2 traduits en codes VK Windows ; remplace `WinMain`.
- `src/audio/sound_sdl.cpp` : mixeur SDL2 avec spatialisation 3D simple ; remplace DirectSound.
- Écran « Multiplayer Game » en jeu (remplace la boîte de dialogue Win32), écran Options (touches, plein écran, VSync, distance d'affichage, souris, volume — sauvegardés dans `control.ini`) et écran Crédits.
- Corrections 64 bits / clang : `long` 64 bits dans le lecteur `.cob` (textures des armes), portée des variables de `for` (VC6), références non-const vers des temporaires dans `vector.h`, globales homonymes de `land.cpp`.
- Build Windows via CMake : alignement des structs par défaut dans toutes les configs. La config Debug du `.vcproj` utilisait `/Zp1`, qui rendait une build Debug Windows incompatible en réseau avec les autres (constat de `docs/plans.json`) ; la Debug MSVC utilise désormais l'alignement naturel, comme la Release. CRT statique comme la Release d'origine.

## Raccourcis

`F6` plein écran · `` ` `` console · `Échap` libérer/capturer la souris.
Les lettres suivent la disposition du clavier (comme sous Windows) : sur AZERTY, remappez Avancer/Gauche sur `z`/`q` dans Options → Controls.

## Aides au debug (SDL)

- `HK_AUTOSTART=1` démarre un serveur local directement.
- `HK_MENU=options|options2|credits|join` ouvre cet écran au démarrage.
- `HK_SHOT=fichier.ppm` (et `HK_SHOT_FRAME=N`) enregistre l'image rendue N.

## État de vérification

- macOS (arm64, Apple clang) : compilé et testé.
- Linux et Windows/MSVC : `CMakeLists.txt` et `build.py` écrits pour ces OS mais **pas encore compilés** ; à valider (CI GitHub Actions prévue au jalon M5).
