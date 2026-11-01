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

Le compose publie l'API (8080/tcp), la signalisation TCP (10000/tcp, pour les anciennes versions du jeu), STUN/TURN (3478/udp) et la plage de ports du trafic relayé (49160-49200/udp, à ouvrir aussi dans le pare-feu). Mettre un reverse proxy HTTPS devant le port 8080 et passer `EDIFICE_TRUST_PROXY=1` (voir ci-dessous).

### Derrière Caddy (HTTPS)

La signalisation passe aussi par l'API, en WebSocket (`GET /v1/signal`) : derrière le reverse proxy HTTPS, elle est chiffrée (`wss://`) sans certificat ni port de plus. Caddy relaie les WebSocket d'office avec `reverse_proxy`, rien à ajouter :

```caddyfile
lobby.example.org {
	reverse_proxy 127.0.0.1:8080   # ou lobby:8080 si Caddy est dans le même réseau Docker
}
```

Caddy écrit l'adresse du joueur dans `X-Forwarded-For` (en remplaçant ce que le client a pu y mettre, ou en l'ajoutant à droite) et le schéma dans `X-Forwarded-Proto` ; avec `EDIFICE_TRUST_PROXY=1`, le lobby prend **l'entrée la plus à droite** (celle qu'a écrite le proxy), jamais la première, qu'un client peut inventer. Avec plusieurs proxys en chaîne (CDN puis Caddy…), lister leurs adresses dans `EDIFICE_TRUSTED_PROXIES` (IP ou CIDR, séparés par des virgules) : le lobby saute alors ces entrées depuis la droite, et ignore l'en-tête si la connexion ne vient pas de l'un d'eux. Sans la liste, un seul proxy, celui qui se connecte au lobby.

Les sessions donnent `signaling_url` (`wss://lobby.example.org/v1/signal`, déduit de la requête : `Host` et `X-Forwarded-Proto` ; `EDIFICE_PUBLIC_URL=https://lobby.example.org` le fixe) en plus de `signaling` (le relais TCP). Les versions du jeu qui le connaissent prennent le WebSocket ; les anciennes, le port 10000. Quand plus aucune ancienne version ne joue, couper le relais TCP (`EDIFICE_SIGNAL=off`) et fermer le port 10000 (compose et pare-feu).

Une partie annoncée depuis une adresse privée (10/8, 172.16/12, 192.168/16, 127/8, 169.254/16, 100.64/10, ::1, fc00::/7, fe80::/10) est refusée (400, avec le message d'erreur) : personne sur Internet ne pourrait la joindre. Derrière un proxy, c'est presque toujours `EDIFICE_TRUST_PROXY` qui manque (le lobby voit alors l'adresse du proxy) : le journal du lobby le dit. Pour un lobby de LAN ou de test : `-allow-private`.

Sans reverse proxy, le lobby peut servir l'API en HTTPS lui-même : `-tls-cert`/`-tls-key`.

Ou avec Docker seul :

```bash
docker run -p 8080:8080 -p 10000:10000 -p 3478:3478/udp -e EDIFICE_PUBLIC_HOST=lobby.example.org -e EDIFICE_TURN_RELAY_IP=203.0.113.10 -e EDIFICE_TURN_SECRET=change-me ghcr.io/nicrf/edifice-lobby:latest
```

| Option | Variable | Défaut | Rôle |
|---|---|---|---|
| `-http` | `EDIFICE_HTTP` | `:8080` | API HTTP |
| `-signal` | `EDIFICE_SIGNAL` | `:10000` | signalisation TCP ; `off` = seulement le WebSocket de l'API (`GET /v1/signal`) |
| `-turn` | `EDIFICE_TURN` | `:3478` | STUN/TURN (UDP) |
| `-public-host` | `EDIFICE_PUBLIC_HOST` | — | nom ou IP que les joueurs utilisent pour joindre ce serveur (URLs STUN/TURN) |
| `-turn-relay-ip` | `EDIFICE_TURN_RELAY_IP` | — | IPv4 publique du relais ; vide = TURN désactivé (STUN public de Google à la place) |
| `-turn-secret` | `EDIFICE_TURN_SECRET` | aléatoire | secret des identifiants TURN (à fixer si plusieurs instances) |
| `-turn-relay-ports` | `EDIFICE_TURN_RELAY_PORTS` | — | plage UDP du trafic relayé (ex. `49160-49200`), indispensable derrière les redirections de ports de Docker |
| `-trust-proxy` | `EDIFICE_TRUST_PROXY=1` | non | IP du client lue dans `X-Forwarded-For` (entrée la plus à droite), schéma dans `X-Forwarded-Proto` (derrière un reverse proxy) |
| `-trusted-proxies` | `EDIFICE_TRUSTED_PROXIES` | — | avec `-trust-proxy` : IP/CIDR des proxys en chaîne (vide : un seul, celui qui se connecte) |
| `-public-url` | `EDIFICE_PUBLIC_URL` | — | URL publique de l'API (`https://lobby.example.org`) pour `signaling_url` ; vide = déduite de chaque requête |
| `-allow-private` | `EDIFICE_ALLOW_PRIVATE=1` | non | accepte les parties annoncées depuis une adresse privée ou de bouclage (LAN, tests) |
| `-tls-cert`, `-tls-key` | `EDIFICE_TLS_CERT`, `EDIFICE_TLS_KEY` | — | sert l'API en HTTPS (sans reverse proxy) |
| `-ttl` | — | `60s` | une partie disparaît si son hôte ne l'a pas renouvelée depuis ce délai |
| `-rate-limit` | — | `30` | requêtes d'écriture et recherches par code autorisées par minute et par IP (rafale : un tiers) |

Pour le relais TURN, les ports UDP des allocations doivent aussi être joignables : fixer leur plage avec `-turn-relay-ports` et l'ouvrir (le compose le fait).

## API

| Requête | Réponse |
|---|---|
| `POST /v1/sessions` | `identity`, `token`, serveurs STUN/TURN avec identifiants temporaires (12 h), `signaling` (`hôte:port` du relais de signalisation TCP, avec `-public-host`), `signaling_url` (le même en WebSocket, `wss://hôte/v1/signal`) |
| `POST /v1/servers` `{name, port, version, protocol, players, maxplayers, mode, visibility, identity, session}` | `id`, `token`, `refresh_seconds` |
| `PUT /v1/servers/{id}` (même corps, `Authorization: Bearer <token>`) | 204 : partie renouvelée |
| `DELETE /v1/servers/{id}` (`Authorization: Bearer <token>`) | 204 |
| `GET /v1/servers?protocol=N` | `{"servers": [...]}` : parties publiques, les plus peuplées d'abord |
| `GET /v1/servers/{id}` | une partie, même privée (partagée par son id : le **code de partie**) |
| `GET /v1/signal` (`Upgrade: websocket`) | relais de signalisation en WebSocket (voir plus bas) |
| `GET /healthz` | `ok` |

L'adresse d'une partie est **l'IP vue par le serveur**, jamais celle déclarée par l'hôte, et doit être publique (sauf `-allow-private`). De même, `identity` (à laquelle les joueurs se connectent en P2P) n'est listée que si `session` est le jeton de la session de cette identité : personne ne peut annoncer une partie sous l'identité d'un autre (sans preuve, la partie est listée sans identité ; le jeton n'est jamais renvoyé). `visibility` vaut `public` (listée), `friends` ou `private` (non listées, joignables par leur id). L'id est le **code de partie** : 6 caractères (chiffres et majuscules, sans 0/O ni 1/I) à se transmettre ; la recherche ignore la casse, les tirets et les espaces, et compte dans la limite de requêtes par IP pour qu'on ne puisse pas énumérer les parties privées. Limites : 4 parties par IP, requêtes d'écriture limitées par IP, corps de 4 Ko maximum, textes réduits à l'ASCII imprimable.

## Signalisation

Une ligne par message, le protocole de l'exemple de Valve (`examples/trivial_signaling_server.py` de GameNetworkingSockets), avec authentification, sur deux transports : **WebSocket** sur l'API (`GET /v1/signal`, une ligne par trame texte, sans `\n` ; chiffré derrière HTTPS) ou **TCP** en clair (port 10000, pour les anciennes versions du jeu ; le jeton y passe en clair). Les deux se parlent : un joueur en WebSocket joint un joueur en TCP.

1. première ligne : `IDENTITÉ JETON` (d'une session `POST /v1/sessions`), sinon `ERROR unauthorized` et déconnexion ; en WebSocket aussi, dans la première trame et non dans un en-tête, pour qu'aucun journal de proxy ne l'enregistre ;
2. ensuite : `DESTINATAIRE CHARGE` → relayé au destinataire sous la forme `EXPÉDITEUR CHARGE`.

Le jeu (`src/net/gnssignal.cpp`) y envoie les signaux de GameNetworkingSockets en hexadécimal, comme l'exemple de Valve, et vérifie que chaque signal reçu se dit bien venir de l'expéditeur garanti par le serveur. Lignes de 4 Ko maximum, 50 messages par seconde et par connexion (au-delà, ignorés : la signalisation ne garantit pas la livraison), déconnexion après 5 minutes d'inactivité. En WebSocket : trames masquées et non fragmentées (sinon fermeture), pings répondus ; les ouvertures comptent dans la limite de requêtes par IP.

## Tests

```bash
go test -race ./...
```

Sur macOS récent avec un Go antérieur à 1.22, ajouter `-ldflags=-linkmode=external`.
