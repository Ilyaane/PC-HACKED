# NEXUS Messagerie pour Cardputer ADV — v0.6 (code partagé)

Projet **M5Stack Cardputer ADV** (ESP32-S3, 8 Mo de flash). La v0.6 conserve l'interface menus et le Wi-Fi mémorisé de v0.5 et ajoute l'option d'un code partagé entre deux appareils. Le serveur Cloudflare Workers + D1 **NEXUS** a été déployé et testé avec les jetons existants à l'adresse `https://nexus-messagerie.ilyaane-patel1.workers.dev`. Le nouveau firmware v0.6 doit être validé par la compilation GitHub Actions et par des essais réels sur Cardputer ADV avant d'être considéré comme testé matériellement.

## Menus et navigation

L'accueil contient un menu noir avec texte blanc et sélection cyan, défilant par cinq choix visibles :

1. Messages reçus
2. Écrire un message
3. Messages envoyés
4. Enregistrer un message vocal WAV
5. Envoyer le message vocal
6. Écouter un message vocal
7. Réseaux Wi-Fi
8. Paramètres NEXUS
9. Diagnostics
10. Détecter la microSD
11. Effacer un vocal en attente

**Naviguer** : `Fn + ;` = haut, `Fn + .` = bas, `Entrée` = choisir. `Fn + \`` = retour (Esc). Dans l'historique, `Fn + ,` et `Fn + /` changent de page. Ces combinaisons correspondent au clavier réel de M5Cardputer 1.1.1; ce modèle n'a pas de touches flèches indépendantes. Les lettres T/C/W/etc. ne sont plus requises pour se déplacer dans les menus. Un témoin Wi-Fi et le dernier état de connexion sont affichés.

Le firmware conserve les fonctions de la version précédente (texte, reçu/envoyé, audio WAV, microSD, brouillons persistants, HTTPS, accusés de lecture).

## Réseaux Wi-Fi

- Le firmware conserve **jusqu'à cinq réseaux** et leurs mots de passe en NVS (migration de l'ancien réseau enregistré).
- Au démarrage, il réessaie le dernier réseau, puis les autres déjà enregistrés si nécessaire.
- Un réseau connu apparaît avec `[MEM]` lors de la recherche Wi-Fi : **Entrée reconnecte sans retaper le mot de passe**.
- Sur un réseau inconnu, le mot de passe apparaît **en clair pendant la saisie**, sans étoiles. Évite de le saisir devant des personnes qui ne doivent pas le connaître.
- La suppression du réseau courant demande confirmation. Les autres réseaux mémorisés restent intacts.

**Important à la mise à jour :** flasher uniquement `firmware.bin` à l'adresse **0x10000** et **ne pas effacer la mémoire complète** permet normalement de conserver les données NVS (Wi-Fi et configuration). L'image complète `NEXUS_ADV_full_flash_0x0.bin` écrite à `0x0` peut effacer les réglages NVS; refaire alors la configuration.

## Deux Cardputer, un même code

Dans le mode partagé, **le même code de connexion est saisi sur les deux appareils**, dès 10 caractères. Il ne doit **pas** être publié dans le code source. Le serveur identifie ensuite les demandes grâce à un champ `X-Nexus-User` dérivé du destinataire configuré sur chaque Cardputer.

- Cardputer 1 : code partagé, **correspondant** `bob` ; l'application se présente comme **alice**.
- Cardputer 2 : même code partagé, **correspondant** `alice` ; l'application se présente comme **bob**.
- Les deux appareils ont la même adresse HTTPS `https://nexus-messagerie.ilyaane-patel1.workers.dev`.

### Activation du mode partagé dans GitHub

Le seul nouveau secret GitHub nécessaire est **`NEXUS_SHARED_TOKEN`**. Entrer la valeur du code choisi dans GitHub > dépôt PC-HACKED > Settings > Secrets and variables > Actions. Le code ne doit pas être ajouté à un fichier suivi par Git. Ensuite relancer le workflow **NEXUS Server - Cloudflare deploy and smoke tests**.

**Tant que `NEXUS_SHARED_TOKEN` n'est pas renseigné**, le serveur fonctionne avec ses anciens jetons distincts `NEXUS_ALICE_TOKEN` et `NEXUS_BOB_TOKEN` pour préserver les installations existantes. **Quand le mode partagé est activé**, les anciens jetons ne donnent plus accès au serveur.

### Limite importante de sécurité

Un code facile à deviner et connu des deux appareils est **moins sûr** que deux jetons aléatoires. Chaque personne connaissant le code peut demander à agir comme Alice **ou** comme Bob, car le rôle est déclaré par l'appareil et non prouvé cryptographiquement. **Ne pas utiliser pour des échanges confidentiels ou sensibles**, et privilégier un code aléatoire plus long si les appareils communiquent sur Internet. Les messages ne sont pas chiffrés de bout en bout. Les mots de passe Wi-Fi restent stockés en NVS et peuvent être lus en cas d'accès physique.

## Compilation et vérification

Workflow : `.github/workflows/nexus-adv-build.yml`. Il lance :
- les tests API Node/SQLite,
- les vérifications statiques,
- la compilation PlatformIO M5Cardputer ADV,
- le contrôle de l'en-tête ESP32 et la création des deux fichiers firmware.

L'artifact GitHub contient `firmware.bin` (pour l'offset 0x10000) et `NEXUS_ADV_full_flash_0x0.bin` (pour 0x0). Les tests automatiques ne prouvent pas la qualité de l'écran sur un matériel physique. Tester les flèches, la connexion sans ressaisie du Wi-Fi, la saisie visible et l'envoi/réception texte et vocal sur **deux appareils**.

Le projet NEXUS utilise le Worker `nexus-messagerie` et la base D1 `nexus-messagerie`. Il ne doit pas toucher au projet Déclic Cloudflare Pages.
