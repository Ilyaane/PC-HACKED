# Déploiement de NEXUS ADV à 0 € : Cloudflare Worker + D1

La source est prête pour **une tentative de compilation et de déploiement**, mais ce guide ne confirme PAS que le firmware compile, ni que la plateforme a été déployée. Les services tiers et leurs quotas peuvent évoluer.

## A. Serveur Cloudflare (compte gratuit ; aucun abonnement payant à activer)

1. Se connecter à https://dash.cloudflare.com/ . Sur iPad, Safari peut afficher le tableau de bord en version bureau.
2. Aller à **Workers & Pages** puis créer un Worker en mode module ES avec le nom `nexus-messagerie`. Mettre le contenu entier de `cloudflare/src/index.mjs` dans le Worker et enregistrer.
3. Ouvrir **D1** dans Cloudflare, créer une base `nexus-messagerie` sur l'offre gratuite.
4. Dans D1, ouvrir la console/éditeur SQL et exécuter exactement le contenu de `cloudflare/schema.sql`.
5. Dans les paramètres du Worker, **Bindings → D1 Database**, créer une liaison nommée **`DB`** et choisir la base créée. Redéployer.
6. Dans le Worker, **Settings → Variables and Secrets**, créer deux **SECRETS**, `ALICE_TOKEN` et `BOB_TOKEN`. Leur valeur doit être différente et longue (minimum 24 caractères) ; idéalement 32 octets aléatoires représentés en hexadécimal (64 caractères).
7. Ne pas mettre ces valeurs dans des variables publiques, un dépôt GitHub ou des captures d'écran. Conserver les secrets dans un gestionnaire de mots de passe.
8. Le site `https://nexus-messagerie.<identifiant>.workers.dev/health` doit répondre `{"ok":true,"version":"ADV-0.4"}`. Cette route est publique : elle valide l'exécution du Worker mais **pas** sa base D1 ni les secrets.
9. Tester `/session` avec un client HTTP et un des deux jetons. Le résultat attendu est `{ "ok": true, "user": "alice" }` pour Alice. Cette route nécessite `Authorization: Bearer <ALICE_TOKEN>`. Ne pas envoyer les jetons dans l'URL.

Sur ordinateur, `wrangler` permet aussi le déploiement par CLI, à condition d'avoir Node.js et un compte Cloudflare :

```sh
cd cloudflare
cp wrangler.toml.example wrangler.toml
# Renseigner database_id issu du tableau de bord Cloudflare.
npx wrangler d1 execute nexus-messagerie --remote --file=schema.sql
npx wrangler secret put ALICE_TOKEN
npx wrangler secret put BOB_TOKEN
npx wrangler deploy
```

**Attention :** aucun des appels ci-dessus n'a été exécuté dans un vrai compte Cloudflare lors de la création de cette archive.

## B. Pourquoi un seul service ?

Cloudflare Workers reçoit les requêtes HTTPS et Cloudflare D1 stocke les messages texte et les petits fichiers WAV. Pas besoin de Supabase, VPS, Cloudflare R2 ni d'API d'intelligence artificielle pour ce périmètre.

La limite actuelle du plan D1 Free est **500 Mo par base**, 5 Go au total sur le compte, et au maximum 2 Mo par ligne/BLOB ; cette application limite elle-même les WAV à 120 Ko et l'audio du Cardputer à environ 98 Ko. Le plan Workers Free impose ses propres quotas de requêtes. Si ces quotas sont dépassés, le service peut s'arrêter temporairement, sans frais tant que le compte reste sur le plan gratuit.

## C. Compilation du Cardputer ADV

Sur un ordinateur de confiance, installer PlatformIO (VS Code, Python ou `pip install platformio`) et exécuter :

```sh
cd firmware
pio run -e cardputer-adv
```

La compilation, **si elle réussit**, devrait écrire `.pio/build/cardputer-adv/firmware.bin` depuis le dossier `firmware/` (ou `firmware/.pio/build/cardputer-adv/firmware.bin` depuis la racine).

Le dépôt peut aussi lancer le workflow `Build NEXUS Cardputer ADV` dans GitHub Actions si le projet y est importé ; le résultat **ne devient pas pour autant un firmware validé sur matériel**.

Le fichier `.bin` n'est **pas présent dans l'archive** : la compilation n'a pas été réalisable ici en raison de l'absence de PlatformIO et d'accès au dépôt de dépendances depuis le conteneur.

## D. Préparer les deux Cardputer ADV

1. Effectuer une sauvegarde du firmware précédent et noter les options de restauration (voir documentation officielle M5Stack).
2. Installer la même version binaire compilée sur les deux Cardputer ADV, chacun avec un jeton **différent**.
3. Au premier démarrage, `W` → sélectionner le Wi-Fi → saisir le mot de passe → Entrée. Après un test de connexion réussi, le réseau sera mémorisé.
4. `C` → `1` : entrer l'URL complète `https://...workers.dev` sans `/` final ; `C` → `2` : saisir le jeton personnel ; `C` → `3` : saisir l'autre identifiant (`bob` si appareil `alice`, inversement pour le second).
5. `D` : vérifier Wi-Fi connecté, serveur OK et état microSD.
6. Envoyer un premier message `T` → texte → Entrée, puis l'autre appareil allumé doit le recevoir sous ~12 secondes. Tester aussi l'appareil récepteur éteint, puis rallumé.
7. Insérer une microSD fonctionnelle pour l'audio. `V` enregistre un vocal de trois secondes ; `U` envoie ; sur le destinataire, `P` télécharge et lit. Les WAV ne sont pas transcrits en texte.

**Important :** les chemins des menus et touches sont prévus par le code mais nécessitent une validation réelle sur le matériel. Le Cardputer ADV peut nécessiter des combinaisons Fn pour Esc.

## E. Liste de vérification pour éviter toute régression

- Clavier et affichage reconnus sur ADV (pas la variante Cardputer original).
- Wi-Fi enregistré, reconnecté après redémarrage ; NON permet de choisir un nouveau réseau ; mauvais mot de passe ne supprime pas l'ancien.
- Certificat HTTPS et horloge synchronisée ; statut correct si serveur indisponible.
- Écriture, brouillon, réception, pagination après 10 messages, reprise après coupure Wi-Fi.
- Envoi vocal, refus de réenregistrer par-dessus un vocal non envoyé ; réessai d'envoi sans doublons.
- Réception WAV, validation du fichier, lecture réelle au haut-parleur, micro et haut-parleur alternés.
- Jeton erroné, accès interdit aux vocaux d'autrui, limites de volume et de stockage, perte d'alimentation.
- Sauvegarde préalable du firmware existant, restauration possible.


## F. Particularités v0.4

- `O` affiche le statut du dernier envoi (LU/non lu), `R` réinitialise la carte microSD si elle est insérée après le démarrage.
- Dans l'historique texte, `N`/`B` parcourent les pages ; l'historique en RAM contient au maximum 8 messages récents et le journal complet est sauvegardé sur microSD tant qu'elle est montée.
- Après un envoi WAV confirmé, le fichier local `nexus_tx.wav` est supprimé pour éviter les doublons accidentels.
- **ATTENTION :** le fichier `firmware.bin` généré par PlatformIO n'est normalement **pas une image « usine » complète** à flasher à l'adresse 0x0. Il correspond à la partition application à l'adresse 0x10000 sur une partition et un bootloader compatibles. Utilisez la procédure PlatformIO/Arduino de téléchargement de la documentation officielle ou générez une image complète validée avec bootloader et table de partitions adaptés. N'écrasez pas une autre table de partitions sans sauvegarde préalable.
- Le modèle `m5stack-stamps3` et la bibliothèque M5Cardputer sont destinés au Cardputer ADV ; **ne pas flasher sur StickS3**.