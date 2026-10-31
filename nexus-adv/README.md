# NEXUS Messagerie — Édition Cardputer ADV — v0.4.1

**Statut : SOURCES EXPÉRIMENTALES. Aucun `.bin` certifié ni serveur public en fonctionnement.**

Cette archive contient un projet réservé au **M5Stack Cardputer ADV (K132-Adv, ESP32-S3FN8, 8 Mo de flash, sans PSRAM)**. Elle ne comprend **aucun firmware StickS3** et ne remplace pas Bruce automatiquement.

## Le projet, en une phrase

Deux Cardputer ADV équipés chacun du même logiciel, de leur propre jeton de connexion, d'un Wi-Fi avec Internet et d'une microSD peuvent échanger des **messages écrits et des fichiers vocaux WAV** par l'intermédiaire d'un serveur Cloudflare Worker relié à D1. Le serveur garde les messages quand l'autre appareil est éteint.

## Dossiers de l'archive

- `firmware/src/main.cpp` : interface écran/clavier, Wi-Fi mémorisé, texte, WAV, réception différée.
- `firmware/include/ca_roots.h` : 3 certificats CA publics pour les connexions HTTPS vérifiées.
- `firmware/platformio.ini` : configuration ESP32-S3 8 Mo sans PSRAM et taille maximale de partition d'application.
- `cloudflare/src/index.mjs` : API de messagerie Cloudflare Worker.
- `cloudflare/schema.sql` : schéma D1, avec identifiants de retransmission uniques.
- `cloudflare/tests/worker.test.mjs` : tests d'API Node.js + SQLite en mémoire.
- `.github/workflows/build-adv.yml` : pipeline **à lancer dans votre dépôt GitHub** pour tenter la compilation et récupérer un `.bin` si la compilation réussit.
- `tests/static_checks.py` : vérifications de cohérence textuelle, **pas** un compilateur.
- `docs/DEPLOIEMENT.md` et `docs/PROTOCOLE_TESTS_CARDPUTER_ADV.md` : configuration et scénarios de test.
- `docs/VERIFICATION.md` : résultat honnête des tests de cette livraison.

## Écran et touches prévus

| Touche | Fonction |
|---|---|
| `T` | Composer un message au clavier ; Entrée : envoyer. En cas d'échec après une tentative réelle, le brouillon reste et peut être renvoyé au retour du réseau. |
| `I` | Lire les 8 derniers messages en cache (et les pages de texte de chaque message) ; flèches Haut/Bas : naviguer ; N/B : page de texte suivante/précédente. |
| `O` | Voir l'état du dernier message envoyé (lu / non lu), actualiser avec Entrée. |
| `V` | Enregistrer un WAV mono PCM 16 kHz / 16 bits d'environ 3 secondes sur microSD, **uniquement après appui manuel**. |
| `U` | Envoyer ou réessayer le vocal ; sa copie temporaire locale est effacée si l'envoi est confirmé. |
| `P` | Télécharger si nécessaire le dernier WAV reçu et le lire au haut-parleur. |
| `W` | Choisir Wi-Fi mémorisé : Entrée=Oui, N=Non, F=oublier avec confirmation. |
| `C` | Régler URL serveur HTTPS, jeton personnel et correspondant (alice/bob). |
| `R` | Retenter le montage d'une microSD insérée après le démarrage. |
| `D` | Diagnostic réseau, mémoire et microSD. |
| `Z` | Supprimer un vocal non envoyé (confirmation nécessaire). |
| `Esc` | Retour accueil. |

Les touches exactes, les combinaisons Fn, les timings audio, la reconnexion Wi-Fi et l'affichage **exigent validation sur Cardputer ADV physique**.

## Ce qui ne nécessite aucun service payant

- Textes et vocaux WAV transmis et conservés via Cloudflare Workers Free + D1 Free, **dans les limites des quotas**.
- Pas de transcription de la voix en texte, pas de synthèse vocale par IA, pas de coût d'API d'IA.
- Pas de deuxième serveur, ni d'ordinateur allumé au domicile.

Le prix fixe du logiciel est nul ; le Wi-Fi/accès Internet, les éventuels achats de microSD ou de matériel et la disponibilité des offres tierces restent hors du projet. N'activer aucun forfait facturé.

## Sécurité et confidentialité

L'API exige deux secrets **différents**, un par appareil, et n'envoie les messages qu'au destinataire autorisé. Les communications utilisent HTTPS avec vérification d'autorité de certification. **Il n'y a PAS de chiffrement de bout en bout :** les données sont lisibles par l'administrateur du serveur Cloudflare/D1. Les secrets enregistrés sur le Cardputer via NVS ne sont pas chiffrés contre une personne ayant accès au matériel. À utiliser pour des messages ordinaires, pas pour des secrets critiques.

## À ne pas faire

- Ne pas flasher les sources `.cpp` comme un `.bin` : ce ne sont pas les mêmes formats.
- Ne pas installer cette version sur un Cardputer original ou un StickS3.
- Ne pas publier ou transmettre les valeurs de `ALICE_TOKEN`, `BOB_TOKEN` ou le mot de passe Wi-Fi.
- Ne pas annoncer « sans bugs » ou « prêt à flasher » sans compilations et essais matériels.

**Prochaine étape concrète :** lire `docs/DEPLOIEMENT.md`, lancer la compilation PlatformIO ou le workflow GitHub Actions, puis suivre `docs/PROTOCOLE_TESTS_CARDPUTER_ADV.md`. La création du serveur seule ne rend pas les appareils opérationnels.


### Mise à jour v0.4.1
Corrections supplémentaires: le changement Wi-Fi annulé restaure les paramètres mémorisés; le diagnostic distingue Wi-Fi connecté du serveur inaccessible; un échec d’écriture de l’historique sur microSD n’avance plus le curseur persistant; récupération améliorée du WAV précédent lors d’un remplacement interrompu. `build_adv.sh` permet une tentative de compilation sur une machine qui possède PlatformIO. **Aucun firmware .bin compilé ici.**