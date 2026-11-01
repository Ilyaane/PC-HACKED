# NEXUS Messagerie pour Cardputer ADV — v0.8

**NEXUS** est une application autonome légère pour M5Stack Cardputer ADV, développée sous PlatformIO / Arduino. La version v0.8 conserve la messagerie texte, la messagerie vocale WAV, le serveur Cloudflare D1, les jetons partagés et le Wi-Fi mémorisé. Elle réorganise complètement les menus, ajoute des archives vocales sur microSD et des outils Morse.

## Interface à cinq rubriques

L'écran d'accueil a cinq tuiles horizontales avec icônes vectorielles dessinées par le firmware : **Messages**, **Vocaux**, **Morse**, **Wi-Fi** et **Système**.

- **Accueil :** `,` = gauche, `/` = droite, **Entrée** = ouvrir la rubrique.
- **Dans les rubriques :** `;` = monter, `.` = descendre, **Entrée** = ouvrir l'action.
- **Retour :** **OPT**.
- Ces touches fonctionnent sans Fn **uniquement dans les menus**; dans la saisie texte, la ponctuation conserve sa signification.
- L'interface est noire, blanche et cyan avec icônes originales et l'en-tête « NEXUS » (le numéro v0.8 apparaît uniquement dans la documentation).

La rubrique **Messages** regroupe réception, rédaction et envoi. **Vocaux** regroupe enregistrement, envoi, archives et lecture. **Wi-Fi** gère les réseaux mémorisés. **Système** regroupe configuration, diagnostics et microSD.

## Archives automatiques de messages vocaux reçus

À réception de chaque nouveau message vocal, NEXUS télécharge automatiquement son fichier WAV depuis le serveur **sur la microSD**, le valide, puis l'enregistre sans remplacer les vocaux précédents. Les fichiers sont rangés par date (fuseau Réunion UTC+04:00), expéditeur et identifiant.

Exemple :

```
/NEXUS/
  MESSAGES_VOCAUX/
    INDEX.txt
    RECUS/
      2026/
        11/
          01/
            alice/
              2026-11-01_13-45-22_alice_ID42.wav
            bob/
              2026-11-01_14-01-08_bob_ID43.wav
```

Les messages sont indexés dans `INDEX.txt`. La liste sur l'appareil affiche les **24 derniers éléments** pour limiter l'usage de RAM; les WAV plus anciens restent sur la carte SD et sont accessibles depuis un ordinateur.

- Le téléchargement se fait dans un fichier temporaire `.part`; une validation d'en-tête et de taille précède le renommage final.
- Si l'enregistrement de la voix échoue (microSD pleine, déconnectée, erreur réseau), NEXUS **ne fait pas avancer le curseur de réception**, ce qui permet une nouvelle tentative plutôt que de perdre le vocal.
- Le dossier et l'index sont créés automatiquement au premier vocal reçu. Le nom utilise l'heure transmise par le serveur; si celle-ci est invalide, classement sous `DATE_INCONNUE`.
- Il faut une microSD correctement formatée. Sans microSD, pas d'archive locale vocale.
- Les WAV sont des fichiers audio ordinaires **non chiffrés sur la carte SD**; protéger la carte si les messages sont privés.

## Outils Morse

- **Écrire en Morse** : convertir un texte en points et tirets affichés.
- **Jouer le Morse** : générer des sons au haut-parleur à partir du texte.
- **Décoder son du micro (bêta)** : essayer de reconnaître des impulsions sonores de type 880 Hz enregistrées au micro pendant une courte durée. Ce mode est **expérimental** : son efficacité dépend du volume, du bruit ambiant et de la cadence; il ne permet pas de comprendre toutes les sources audio ou les enregistrements quelconques.

Les alphabets A–Z, les chiffres 0–9 et plusieurs signes de ponctuation sont gérés. Les sons sont des créations synthétiques originales, et non de la musique protégée.

## Wi-Fi, paramètres et sécurité

Cinq Wi-Fi peuvent être mémorisés. La reconnexion utilise les mots de passe sauvegardés dans la mémoire NVS interne, pas dans la microSD. La saisie Wi-Fi est visible sans étoiles. Le firmware relit les valeurs critiques après enregistrement pour signaler une erreur NVS.

Les deux Cardputer utilisent la même adresse : `https://nexus-messagerie.ilyaane-patel1.workers.dev`. Dans le mode partagé :
- appareil Alice : destinataire `bob`,
- appareil Bob : destinataire `alice`,
- le secret de connexion est défini exclusivement dans GitHub Actions comme `NEXUS_SHARED_TOKEN`, jamais dans ce dépôt.

Un code partagé facile à deviner **n'offre pas une authentification indépendante** entre les deux appareils. La messagerie n'utilise pas de chiffrement de bout en bout.

## M5 Launcher / WebUI : quel fichier utiliser ?

Ce projet est pensé pour fonctionner à côté de Bruce, KVX Puter et des autres firmwares chargés par M5 Launcher sur microSD, sans dépendance à leur code et sans module LoRa ou GPS.

- Dans un dossier de microSD `ADV Nexus`, enregistrer de préférence **`firmware.bin`** (application uniquement) et l'installer par la fonction habituelle de M5 Launcher / WebUI.
- L'autre fichier, **`NEXUS_ADV_full_flash_0x0.bin`**, comprend chargeur de démarrage, partitions et application; **ne pas le lancer comme une application ordinaire depuis la liste M5 Launcher**. Il sert aux opérations complètes de reflash depuis un outil compatible en mode USB.
- Ne pas changer la table des partitions ni effacer complètement la mémoire du Cardputer déjà fonctionnel.
- Selon la table de partitions active et la méthode utilisée par Launcher, le changement de firmware peut affecter les réglages NVS. Vérifier Wi-Fi, code et destinataire après installation et redémarrage.

## Vérifications et limites

Le workflow GitHub `.github/workflows/nexus-adv-build.yml` exécute les tests API Cloudflare, les vérifications statiques du projet, la compilation ESP32-S3 par PlatformIO et les contrôles des images binaires. Un succès de compilation **ne prouve pas** le fonctionnement physique du microphone, du haut-parleur, du comportement d'écran ou de la microSD sur les deux appareils.

Tests matériels à effectuer : menu horizontal/vertical, absence de clignotement, connexion Wi-Fi et préservation NVS sur les deux Cardputer, envoi d'un vocal Alice → Bob, présence de son WAV dans le dossier de la bonne date sur microSD, lecture de l'archive, Morse texte et audio puis décodage expérimental. Déclic et son hébergement Cloudflare Pages sont totalement extérieurs au projet NEXUS.
