# edifice-lobby

Petit service en un seul binaire Go pour jouer à Edifice par Internet :

- **liste des parties** : les hôtes annoncent leur partie, les joueurs la voient dans l'écran *Join* ;
- **signalisation P2P** pour GameNetworkingSockets : relaie les messages qui permettent à deux joueurs de se connecter directement à travers leurs box ;
- **STUN/TURN** (via [pion/turn](https://github.com/pion/turn)) : découverte d'adresse, et relais quand le P2P direct échoue. Le trafic relayé reste chiffré de bout en bout par GameNetworkingSockets.

Licence MIT (programme séparé du jeu, qui reste sous LGPL).

## Lancer

```bash
go run . -public-host lobby.example.org -turn-relay-ip 203.0.113.10
```

Avec Docker Compose (recommandé) :

```bash
cd server && cp .env.example .env
```

Éditer `.env` (nom public, IP publique du relais, secret : `openssl rand -hex 32`), puis :

```bash
docker compose pull && docker compose up -d
```

L'image est construite par GitHub Actions et publiée sur `ghcr.io/nicrf/edifice-lobby` (amd64 et arm64) : `latest` suit `main`, chaque release a son tag (`0.4.0`, `0.4`). Pour l'utiliser sans le compose :

```bash
docker pull ghcr.io/nicrf/edifice-lobby:latest
```

Pour construire l'image soi-même à la place : `docker compose build`.

Le compose publie l'API (8080/tcp), la signalisation (10000/tcp), STUN/TURN (3478/udp) et la plage de ports du trafic relayé (49160-49200/udp, à ouvrir aussi dans le pare-feu). Mettre un reverse proxy HTTPS devant le port 8080 et passer `EDIFICE_TRUST_PROXY=1`.

Ou avec Docker seul :

```bash
docker run -p 8080:8080 -p 10000:10000 -p 3478:3478/udp -e EDIFICE_PUBLIC_HOST=lobby.example.org -e EDIFICE_TURN_RELAY_IP=203.0.113.10 -e EDIFICE_TURN_SECRET=change-me ghcr.io/nicrf/edifice-lobby:latest
```

| Option | Variable | Défaut | Rôle |
|---|---|---|---|
| `-http` | `EDIFICE_HTTP` | `:8080` | API HTTP |
| `-signal` | `EDIFICE_SIGNAL` | `:10000` | signalisation (TCP) |
| `-turn` | `EDIFICE_TURN` | `:3478` | STUN/TURN (UDP) |
| `-public-host` | `EDIFICE_PUBLIC_HOST` | — | nom ou IP que les joueurs utilisent pour joindre ce serveur (URLs STUN/TURN) |
| `-turn-relay-ip` | `EDIFICE_TURN_RELAY_IP` | — | IPv4 publique du relais ; vide = TURN désactivé (STUN public de Google à la place) |
| `-turn-secret` | `EDIFICE_TURN_SECRET` | aléatoire | secret des identifiants TURN (à fixer si plusieurs instances) |
| `-turn-relay-ports` | `EDIFICE_TURN_RELAY_PORTS` | — | plage UDP du trafic relayé (ex. `49160-49200`), indispensable derrière les redirections de ports de Docker |
| `-trust-proxy` | `EDIFICE_TRUST_PROXY=1` | non | IP du client lue dans `X-Forwarded-For` (derrière un reverse proxy) |
| `-ttl` | — | `60s` | une partie disparaît si son hôte ne l'a pas renouvelée depuis ce délai |
| `-rate-limit` | — | `30` | requêtes d'écriture et recherches par code autorisées par minute et par IP (rafale : un tiers) |

Pour le relais TURN, les ports UDP des allocations doivent aussi être joignables : fixer leur plage avec `-turn-relay-ports` et l'ouvrir (le compose le fait).

## API

| Requête | Réponse |
|---|---|
| `POST /v1/sessions` | `identity`, `token`, serveurs STUN/TURN avec identifiants temporaires (12 h), `signaling` (`hôte:port` du relais de signalisation, avec `-public-host`) |
| `POST /v1/servers` `{name, port, version, protocol, players, maxplayers, mode, visibility, identity, session}` | `id`, `token`, `refresh_seconds` |
| `PUT /v1/servers/{id}` (même corps, `Authorization: Bearer <token>`) | 204 : partie renouvelée |
| `DELETE /v1/servers/{id}` (`Authorization: Bearer <token>`) | 204 |
| `GET /v1/servers?protocol=N` | `{"servers": [...]}` : parties publiques, les plus peuplées d'abord |
| `GET /v1/servers/{id}` | une partie, même privée (partagée par son id : le **code de partie**) |
| `GET /healthz` | `ok` |

L'adresse d'une partie est **l'IP vue par le serveur**, jamais celle déclarée par l'hôte. De même, `identity` (à laquelle les joueurs se connectent en P2P) n'est listée que si `session` est le jeton de la session de cette identité : personne ne peut annoncer une partie sous l'identité d'un autre (sans preuve, la partie est listée sans identité ; le jeton n'est jamais renvoyé). `visibility` vaut `public` (listée), `friends` ou `private` (non listées, joignables par leur id). L'id est le **code de partie** : 6 caractères (chiffres et majuscules, sans 0/O ni 1/I) à se transmettre ; la recherche ignore la casse, les tirets et les espaces, et compte dans la limite de requêtes par IP pour qu'on ne puisse pas énumérer les parties privées. Limites : 4 parties par IP, requêtes d'écriture limitées par IP, corps de 4 Ko maximum, textes réduits à l'ASCII imprimable.

## Signalisation

TCP, une ligne par message, le protocole de l'exemple de Valve (`examples/trivial_signaling_server.py` de GameNetworkingSockets), avec authentification :

1. première ligne : `IDENTITÉ JETON` (d'une session `POST /v1/sessions`), sinon `ERROR unauthorized` et déconnexion ;
2. ensuite : `DESTINATAIRE CHARGE` → relayé au destinataire sous la forme `EXPÉDITEUR CHARGE`.

Le jeu (`src/net/gnssignal.cpp`) y envoie les signaux de GameNetworkingSockets en hexadécimal, comme l'exemple de Valve, et vérifie que chaque signal reçu se dit bien venir de l'expéditeur garanti par le serveur. Lignes de 4 Ko maximum, 50 messages par seconde et par connexion (au-delà, ignorés : la signalisation ne garantit pas la livraison), déconnexion après 5 minutes d'inactivité.

## Tests

```bash
go test -race ./...
```

Sur macOS récent avec un Go antérieur à 1.22, ajouter `-ldflags=-linkmode=external`.
