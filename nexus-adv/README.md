# NEXUS Messagerie pour Cardputer ADV

**Version candidate : v1.0. Le firmware reste à tester sur les deux Cardputer ADV réels.** Ne pas confondre les fichiers déjà installés par M5 Launcher et le code de développement. Aucune nouvelle version n'est déposée automatiquement sur Google Drive.

## Accueil et navigation

NEXUS doit rester un petit firmware indépendant, compatible avec le fonctionnement habituel de M5 Launcher et les autres firmwares stockés sur microSD. L'accueil affiche le nom **NEXUS**, sans numéro de version, avec six tuiles et icônes vectorielles originales : **Messages, Vocaux, Morse, Wi-Fi, Système, Autres**.

Sur l'accueil, la touche physique `,` va à gauche, `/` va à droite, et **Entrée** ouvre la rubrique. Dans chaque rubrique, `;` monte, `.` descend et **Entrée** valide. **OPT** revient en arrière. **Fn n'est pas nécessaire dans les menus**, mais ces mêmes caractères restent utilisables dans les champs de saisie. Les tuiles et les sélections sont rafraîchies partiellement pour limiter les clignotements.

Les rubriques :
- **Messages :** réception, nouveau message, messages envoyés.
- **Vocaux :** enregistrer, envoyer, archives microSD, écouter et effacer le brouillon.
- **Morse :** texte en Morse, son, décodage micro expérimental, entraînement auditif, vitesse.
- **Wi-Fi :** réseau mémorisé, sélection et diagnostic.
- **Système :** paramètres, diagnostics, **notification sonore activée/désactivée** (réglage conservé en NVS), détection microSD.
- **Autres :** recherche de messages TXT archivés, diagnostic batterie en millivolts et pourcentage estimé, espace microSD, version.

## Alertes sonores facultatives (v1.0)

Dans **Système → Son notification : ON/OFF**, Entrée active ou coupe le bref signal au haut-parleur lors d'un **nouveau** message non lu. Le choix est conservé dans la mémoire NVS, même sans carte microSD. À l'ouverture, l'appareil synchronise le compteur et évite de faire sonner indéfiniment un même message déjà signalé. Les messages restent accessibles quand le son est désactivé.

## Recherche hors ligne (v1.0)

Dans **Autres → Rechercher dans textes**, taper au moins deux caractères puis Entrée. La recherche ne lit que les fichiers `.txt` archivés sous `/NEXUS/MESSAGES_TEXTE/`, sans envoyer la requête à Internet. Elle n'est pas sensible aux majuscules/minuscules, et parcourt les sous-dossiers d'archives, dans la limite de 12 000 fichiers par recherche pour garder l'appareil réactif. La liste affiche les 24 dernières correspondances retenues ; si une limite est atteinte, le résultat l'indique. Entrée ouvre un message, `,` et `/` changent de page ; OPT revient aux résultats.

## Batterie (v1.0)

Dans **Autres → Batterie**, M5Stack `Power.getBatteryVoltage()` permet d'afficher la **tension en mV**. `Power.getBatteryLevel()` fournit le **pourcentage estimé**, lissé avec les mesures précédentes pour réduire les fluctuations. Entrée actualise. Le Cardputer ADV **ne mesure pas le courant de charge** et ne possède pas de mesure exacte du pourcentage : une valeur 100 % n'est pas une preuve de pleine charge. Comparer les appareils surtout **débranchés**, avec la même version de firmware.

Documentation fabricant : https://docs.m5stack.com/en/arduino/m5cardputer/battery

## Messages non lus et réception après arrêt

Le Worker Cloudflare conserve les messages même si le Cardputer est éteint. L'API authentifiée `GET /unread` renvoie le **nombre de messages non lus** et un aperçu du plus récent, pour le rôle Alice ou Bob. Le firmware de développement actualise ce compteur au démarrage après connexion Wi-Fi, puis environ toutes les 12 secondes. Une pastille et un aperçu apparaissent sur l'accueil. Le compteur est mis à jour après accusé de lecture et sa dernière valeur est gardée en NVS.

Le Wi-Fi, l'adresse du serveur, le jeton partagé et le destinataire sont enregistrés dans la **NVS interne**, indépendamment de la microSD. Sans microSD, un Cardputer connecté peut recevoir des messages et afficher le compteur, mais **ne garde pas l'historique local après redémarrage**. Il retélécharge les messages depuis le serveur lors de la reconnexion.

## Archive des messages sur microSD (v1.0)

Quand une microSD correctement formatée est présente, NEXUS conserve les anciens vocaux et les textes envoyés/reçus dans des dossiers séparés par année, mois, jour, expéditeur et identifiant. Le fichier `/nexus_history.txt` reste compatible avec les anciennes versions pour charger les **24 derniers messages** dans l'interface, tandis que les archives plus anciennes restent sur la carte.

```
/NEXUS/
  MESSAGES_VOCAUX/
    INDEX.txt
    RECUS/2026/11/01/alice/2026-11-01_13-45-22_alice_ID42.wav
  MESSAGES_TEXTE/
    RECUS/2026/11/01/alice/2026-11-01_13-46-00_alice_ID43.txt
    ENVOYES/2026/11/01/bob/2026-11-01_13-47-00_bob_ID44.txt
/nexus_history.txt
```

L'horodatage de classement utilise l'heure du serveur et le fuseau de La Réunion (UTC+04:00). Lorsqu'un téléchargement vocal ou l'écriture d'un texte reçu sur microSD échoue, le curseur de réception n'avance pas : l'application réessaie. La sauvegarde des fichiers s'effectue d'abord sous `.part` avant renommage. Les WAV et TXT ne sont **pas chiffrés** sur la microSD.

## Diagnostics et sécurité

La rubrique **Diagnostics** affiche à nouveau Wi-Fi, adresse IP locale, RAM libre (Heap), état du serveur, état NVS, rôle Alice/Bob et nombre de messages non lus. L'écriture des paramètres critiques est vérifiée par une relecture NVS, mais la persistance réelle doit être testée après arrêt et redémarrage.

Les deux Cardputer utilisent la même URL : `https://nexus-messagerie.ilyaane-patel1.workers.dev`. En mode code partagé, Alice configure son destinataire `bob` et Bob `alice`. La clé serveur `NEXUS_SHARED_TOKEN` reste dans GitHub Actions. Le rôle transmis par l'appareil n'est pas une deuxième preuve d'authentification. Le code partagé facile à deviner et l'absence de chiffrement de bout en bout ne conviennent pas aux échanges confidentiels.

## M5 Launcher / WebUI

Stocker le fichier d'**application** `firmware.bin` dans ton dossier personnel `ADV Nexus` sur microSD et installer normalement via M5 Launcher/WebUI. Ne jamais traiter l'image complète `NEXUS_ADV_full_flash_0x0.bin` comme une simple application. Les partitions et les méthodes de flashage peuvent affecter la NVS. Préserver le Cardputer qui fonctionne et ne pas effacer son flash sans sauvegarde.

Le projet NEXUS est indépendant de Déclic, des firmwares Gmail/ChatGPT futurs, de GPS/LoRa et des firmwares HID.

## Tests requis pour valider le matériel

1. Tuiles gauche/droite puis listes haut/bas, sans Fn ni clignotement gênant.
2. Saisie Wi-Fi et mot de passe en clair dans l'écran prévu; retour, accents et ponctuation.
3. Conservation Wi-Fi, URL, code, rôle après deux redémarrages sur **chacun** des Cardputer.
4. Message Alice → Bob envoyé pendant que Bob est éteint : reconnecter Bob, vérifier la pastille non lue, l'aperçu et le message dans la réception; ensuite vérifier l'accusé de lecture.
5. Refaire Bob → Alice et tester plusieurs messages arrivés hors ligne.
6. Avec microSD, vérifier les archives TXT/WAV, noms datés, fichiers complets et relecture après redémarrage.
7. Sans microSD, vérifier la connexion et le compteur non lu sans plantage.
8. Morse texte, haut-parleur, quiz et décodage micro expérimental.
9. Diagnostics IP, RAM, état NVS et serveur.
10. Vérifier que M5 Launcher peut conserver et réinstaller les autres firmwares.

GitHub Actions vérifie les tests API, le source statique, la compilation PlatformIO M5Cardputer ADV et les en-têtes des images ESP32. **Ces résultats ne remplacent pas les essais physiques ni les tests de réception Wi-Fi/voix sur les deux appareils.** L'état actuel est développement, pas certification « sans bug ».


## Réinstallation complète demandée (attention)

Le fichier **`NEXUS_ADV_full_flash_0x0.bin`** est une image complète prévue pour être écrite à **0x0** avec un outil USB compatible ESP32-S3, **pas à 0x10000** et **pas depuis l'option d'installation normale de M5 Launcher**.

Si l'outil efface entièrement le flash avant installation :
- L'application et l'ancienne table de partitions sont remplacées ; les paramètres **NVS** sont effacés. Il faudra ressaisir le Wi-Fi, l'adresse HTTPS, le code du serveur et le correspondant Alice/Bob.
- **M5 Launcher installé en flash peut être remplacé** par NEXUS. Les fichiers `.bin` stockés sur microSD restent physiquement sur la carte, mais sans M5 Launcher ils ne seront plus accessibles de la même manière. Il faudra réinstaller M5 Launcher si tu veux à nouveau passer d'un firmware à l'autre depuis son interface.
- Toute donnée enregistrée dans les partitions du flash interne peut être perdue. Sauvegarder et retirer la microSD avant les opérations de réinstallation si nécessaire. Préserver au minimum une copie des fichiers, et tester d'abord sur le Cardputer problématique.

**Option préférable si tu souhaites conserver M5 Launcher :** remplacer seulement la version NEXUS installée, en utilisant le fichier d'application `firmware.bin` via le mécanisme de M5 Launcher ; effacer uniquement l'installation de NEXUS si le lanceur le permet. Une réinstallation générale n'est pas une garantie de supprimer les problèmes de batterie ou de sauvegarde NVS.

Les tests automatisés vérifient le code, la compilation, les images ESP32 et les API du serveur, mais **pas** les vrais messages audio, la charge batterie, la relecture des archives ni les interactions clavier sur ton matériel.
