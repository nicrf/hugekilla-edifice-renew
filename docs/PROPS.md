# Rochers et arbres qui bloquent (props)

Les arbres et les gros rochers ajoutés par la végétation Ultra (#90) étaient purement
visuels : on les traversait, les balles aussi, et un joueur Classic ne les voyait pas.
Ils deviennent des obstacles du jeu, les mêmes pour tous : on marche autour des troncs
et sur les rochers, les balles (celles du serveur aussi) et les grenades s'y arrêtent,
on ne construit pas dedans, et Classic les dessine. Toujours le protocole 5 : une
capacité de plus (`HELLOCAP_PROPS`), comme les phases de M11 (`docs/M11.md`).

## Placement : calculé, pas transmis

`src/game/props.cpp` (`PropsGenerate`, sans état du jeu, testé par
`tests/props_test.cpp`) place les props d'après le relief seul, à la fin de
`CreateLand`, sur le serveur comme sur chaque client :

- **arbres** : au plus un par case de 8 unités ; un hachage de la case et de la graine
  du monde (`LandDefs.randomseed`) décide s'il y en a un (55 % dans les forêts d'un
  bruit périodique, 4 % ailleurs) et où dans la case ; refusé dans l'eau ou près
  d'elle (hauteur < eau + 1,5), sur une pente forte, sur les sommets (> eau + 60) ;
  conifère au-dessus d'eau + 30, sinon conifère ou feuillu ;
- **rochers** : au plus un par case de 8 unités, plus probables sur les pentes et les
  rives, de taille 0,9 à 2,5 (les petites pierres restent des décorations Ultra) ;
  jamais dans un arbre ni dans un autre rocher ;
- rien à moins de 3 unités des objets de décor de la carte (`GetScenery`).

Sur une carte aléatoire, de l'ordre de 400 à 500 props (deux tiers d'arbres) pour 512 × 512 unités.
Une carte sauvegardée (`savemap`, `loadmap`, Server Setup) reconstruit son relief à
partir de ses `LandDefs` : elle retrouve les mêmes props sans rien dans le fichier.
**Le format des cartes ne change pas** (format 2), les anciennes cartes ont des props
au chargement.

Le calcul n'utilise que des opérations exactes en flottant (hachages entiers, + − × ÷,
`sqrtf`) et interdit la fusion `a*b+c` (`#pragma`, clang et GCC sur arm64) : à relief
identique, résultat identique sur macOS, Linux et Windows. `tests/props_test.cpp`
vérifie une valeur de référence du hachage sur un relief synthétique, sur les trois OS
de la CI. Le relief lui-même vient du générateur du monde (protocole 3), déjà commun.

**Vérification** : `PropsHash` (le nombre, les types, les graines et les positions
arrondies au 1/16, pas les hauteurs) est envoyé par le serveur dans `PROPSMODE` ; le
client le compare au sien et l'écrit dans sa console (« rocks and trees: N, same as the
server's » ou « differ from the server's »). Une différence ne bloque rien (seul le
serveur décide des coups) mais le dirait.

## Ce qui est là, ce qui ne l'est pas

Un prop n'est pas là (ni collision, ni dessin) tant que :

- un bloc le traverse (carte chargée avec des bâtiments à cet endroit, bloc posé par un
  client modifié) ;
- une station posée est à moins de 6 unités, ou un autre objet du serveur posé (drapeau
  lâché) à moins de 3 : leur emplacement reste dégagé.

Les deux ne dépendent que de ce que tout le monde partage déjà (blocs, objets du
serveur) : serveur et clients sont d'accord, au délai du réseau près. Recalculé 5 fois
par seconde (`PropsRefresh`).

La réapparition (`Play`, position au hasard) retire jusqu'à 20 fois une position à plus
de 1,5 unité de tout prop.

## Collisions

Tout passe par `src/game/propsgame.cpp`, une grille de cases de 16 unités, le monde qui
se répète tous les 512 :

| Qui | Troncs (cylindre : rayon 0,28 à 0,49, hauteur 4,5 à 10,5) | Rochers (demi-ellipsoïde : rayon 0,8 à 2,9, hauteur 0,4 à 1,9) |
|---|---|---|
| Joueurs (`Play`) | repoussés hors du cylindre, vitesse vers le tronc annulée (`PropsPushPlayer`, rayon du joueur 0,5) | sol (`GetHeight`) : on monte sur les petits, les grands sont des murs comme un bloc |
| Balles (`TickObjects`, client et serveur) | segment de chaque pas contre le cylindre : la balle s'arrête au point d'entrée, avec l'impact | idem contre le dôme, et `GetHeight` |
| Coups du serveur (M11 3a) | `AcBulletTick` ne teste que la partie du pas avant le prop : pas de dégâts derrière | idem |
| Grenades | rebondissent sur le tronc (vitesse horizontale réfléchie, × 0,6) | sol (`GetHeight`) : s'y arrêtent comme sur le relief |
| Construction (`PlaceStructure`) | aperçu rouge et refus (« A rock or a tree is in the way ») si un bloc ou son poteau touche un prop | idem |

Les couronnes des arbres ne bloquent rien (on tire à travers le feuillage).

## Dessin

- **Classic** (`src/render/propsdraw.cpp`) : maillages simples faits au premier dessin,
  aux formes d'Ultra (conifère en cônes sur un tronc, feuillu en boules bosselées,
  rochers en sphères déformées à fond plat), textures procédurales (écorce, aiguilles,
  feuilles, roche, `TEX_PROPS0`), lumière cuite dans les couleurs ; une version légère
  au-delà de 70 unités, rien au-delà de 300 (le brouillard finit à 400).
- **Ultra** (`ultra_veg.inl`) : la même liste (`RUltraScene.props`), avec ses
  maillages PBR, le vent, les ombres et les versions lointaines. Ultra ajoute seulement
  des buissons et de petites pierres, visuels. Avec la végétation Ultra sur Off, les
  maillages Classic sont dessinés (`RMAT_MODEL`) : ce qui bloque se voit toujours.

## Réseau : protocole 5, une capacité

- `HELLOCAP_PROPS` (64) dans l'octet de capacités de REQUESTENTER : « j'ai les props
  et j'obéis à PROPSMODE ». Dans INFO : le serveur a les props (`props 1`).
  (128 est réservé à la grenade fumigène.)
- `PROPSMODE` (SIMPLE 30, `multiplaystruct.h`) : `msg` 1 = les props bloquent, 0 = il
  n'y en a pas ; `msg2` = le `PropsHash` du serveur. Envoyé à l'entrée en jeu (après
  les blocs, à chaque nouvelle manche aussi) et quand `props` change à la console, aux
  seuls clients qui ont annoncé la capacité. Un client sans réponse (ancien serveur)
  n'a pas de props.
- Réglage du serveur : `props` dans `sv.ini` (défaut 1), la console, Server Setup
  « Rocks and trees : On/Off », et `HK_AC_VARS="props=0"` en test.

| Serveur \ client | ancien (sans `HELLOCAP_PROPS`) | nouveau |
|---|---|---|
| **ancien** | comme avant | pas de PROPSMODE : pas de props (ni dessin ni collision) |
| **nouveau, `props 1`** | **refusé** : VERSIONMISMATCH + « This server has rocks and trees: update the game » (un 0.5.x affiche « update the game ») | props partout, hachages comparés |
| **nouveau, `props 0`** | rejoint comme avant | PROPSMODE 0 : pas de props |

Le refus est envoyé avant que l'adresse soit prouvée (comme celui d'une autre version) :
environ 65 octets pour une demande d'au moins 17, bornés par `ac_ipenter`. Changer
`props` en cours de partie ne déconnecte pas les anciens clients déjà là (ils n'ont pas
les props) ; le refus ne vaut qu'à l'arrivée.

## Tests

- `tests/props_test.cpp` (ctest `props`) : même relief → mêmes props et même hachage,
  autre graine → autre hachage, valeur de référence ; arbres au sec, rochers au plus
  sur la rive, rien dans le décor, aucun rocher dans un autre prop, nombre raisonnable ;
  mer → rien ; balle à travers un tronc (point d'entrée), à côté, au-dessus, trop
  courte, partie de l'intérieur, plongeant par le haut ; dôme d'un rocher ; joueur
  contre un tronc (direction et profondeur), au-dessus (jetpack) ; blocs à côté, à
  travers, au-dessus, coin hors du rayon ; bord du monde (`PropWrap`, grille).
- `tests/net_props.py` (`run_netplay_tests.sh`, `net_props@on` et `net_props@off`) :
  INFO annonce la capacité ; un client sans elle est refusé avec le texte, sans slot ;
  trois clients reçoivent PROPSMODE 1 et le hachage du journal du serveur
  (`HK_PROPS_LOG=1` : la liste des props sur stderr) ; une instance du jeu (`HK_JOIN`)
  construit les mêmes props (nombre, hachage, chaque position, taille et hauteur) ;
  derrière un arbre isolé, 12 tirs du serveur ne touchent pas la victime alors que les
  mêmes tirs 4 unités à côté la touchent. Avec `props 0` : pas de capacité dans INFO,
  un ancien client rejoint, un nouveau reçoit PROPSMODE 0.
- `tests/net_handshake.py` (aussi lancé par `tests/check_package.py` dans la CI) : un
  client de la même version avec `HELLOCAP_PROPS` entre ; sans, il est refusé avec le
  texte si INFO annonce les props, accepté sinon. Les autres tests réseau rejoignent
  comme des builds plus anciens : `run_netplay_tests.sh` lance leur serveur avec
  `props 0`.

## Limites

- Les props ne sont pas détruits (les grenades ne les cassent pas) et ne font pas
  d'ombre en Classic.
- Un client dont le relief différerait d'un bit de celui du serveur à un seuil
  (hauteur ou pente juste à la limite) aurait un prop de plus ou de moins : le hachage
  le dit dans la console, le serveur décide quand même des coups.
- Un ancien client déjà en jeu quand on passe `props` à 1 à la console ne les a pas.
