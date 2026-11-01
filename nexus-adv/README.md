# NEXUS Messagerie pour Cardputer ADV — v0.5 (interface menus)

Projet **M5Stack Cardputer ADV** (ESP32-S3, 8 Mo de flash). La version v0.5 améliore l'interface et le Wi-Fi. Le serveur Cloudflare Workers + D1 **NEXUS** a été déployé et testé avec les jetons existants à l'adresse `https://nexus-messagerie.ilyaane-patel1.workers.dev`. Le nouveau firmware v0.5 doit être validé par la compilation GitHub Actions et par des essais réels sur Cardputer ADV avant d'être considéré comme testé matériellement.

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

## Deux appareils et jetons

Les deux Cardputer utilisent la même URL serveur. Le premier utilise le jeton Alice avec destinataire `bob`, et le deuxième le jeton Bob avec destinataire `alice`. Ces jetons sont saisis manuellement via **Paramètres NEXUS**.

Le serveur et le firmware acceptent désormais des jetons **d'au moins 12 caractères** et les anciens jetons longs restent valides. Pour réellement passer aux nouveaux jetons courts, il faut mettre à jour **les deux secrets GitHub Actions** `NEXUS_ALICE_TOKEN` et `NEXUS_BOB_TOKEN`, redéployer le serveur et saisir les mêmes valeurs sur les appareils. Aucun jeton privé n'est inscrit dans ce dépôt.

Privilégier 12 caractères **difficiles à deviner**, plutôt qu'un mot courant ou un code public. Les jetons sont des identifiants, **pas** un chiffrement de bout en bout. Les messages restent lisibles par l'administrateur du serveur et les mots de passe Wi-Fi stockés en NVS ne sont pas protégés contre un accès physique au matériel.

## Compilation et vérification

Workflow : `.github/workflows/nexus-adv-build.yml`. Il lance :
- les tests API Node/SQLite,
- les vérifications statiques,
- la compilation PlatformIO M5Cardputer ADV,
- le contrôle de l'en-tête ESP32 et la création des deux fichiers firmware.

L'artifact GitHub contient `firmware.bin` (pour l'offset 0x10000) et `NEXUS_ADV_full_flash_0x0.bin` (pour 0x0). Les tests automatiques ne prouvent pas la qualité de l'écran sur un matériel physique. Tester les flèches, la connexion sans ressaisie du Wi-Fi, la saisie visible et l'envoi/réception texte et vocal sur **deux appareils**.

Le projet NEXUS utilise le Worker `nexus-messagerie` et la base D1 `nexus-messagerie`. Il ne doit pas toucher au projet Déclic Cloudflare Pages.
