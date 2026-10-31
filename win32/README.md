# win32/

- `hugekilla.rc`, `resource.h`, `*.ico` : ressources Windows, compilées par le CMake principal sous Windows. `resource.h` est aussi inclus par le code C++ (IDs de dialogues), d'où la présence de `win32/` dans le chemin d'inclusion sur tous les OS.
- `hugekilla.sln`, `hugekilla.vcproj` : **legacy**, projet Visual Studio 2003 d'origine conservé pour référence. Ses chemins pointent vers l'ancienne arborescence plate et ne sont plus maintenus : utiliser `python build.py` ou CMake (voir `../BUILD.md`).
