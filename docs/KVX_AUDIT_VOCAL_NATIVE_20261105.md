# KVXputer ADV + Nexus vocal : audit d'integration et protocole de validation
Date : 5 novembre 2026. Branche : `kvx-core-stabilisation-rebuild-20261105`.

## Etat de la source
- Une seule application KVXputer native ; Nexus et MQTT restent deux modules distincts dans le menu.
- Nexus conserve la messagerie texte HTTPS, les contacts Alice/Bob, la navigation et la police compacte.
- Nouveau moteur vocal dans `kvx-nexus-neon/overlay/src/menu/nexus_neon/nexus_voice.cpp`, sans seconde pile M5Cardputer ni module Morse.
- Capture via `mic_record_wav_to_path` de KVX, rééchantillonnage par blocs de 48 kHz vers 16 kHz, mono PCM 16 bits ; brouillon WAV sur microSD, maximum 120000 octets.
- Lecture via `playAudioFile` de KVX. Envoi `POST /voice/:destinataire` avec `X-Client-Id` ; téléchargement `GET /voice/:id` en flux borné et vérification RIFF/WAVE avant lecture.
- Annulation de téléchargement best effort ; conservation du brouillon tant que le serveur ne fournit pas d'identifiant de message.
- Protection des brouillons et identifiants de réessai par contact ; évite de mélanger l'ancienne réponse réseau et le contact courant.
- Le correctif MQTT de masquage des mots de passe et du fichier INI borné reste conservé.

## Points non certifiés et risques résiduels
- **Les tests de compilation et d'intégrité ne remplacent jamais les tests sur le Cardputer ADV physique.**
- Micro ES8311/SPM1423, haut-parleur, microSD montée, timeouts réels, contiguïté du heap et durée des tâches doivent être validés sur l'appareil.
- La capture est un traitement bloquant de courte durée, et le port audio hérite de la stabilité des pilotes KVX existants.
- La sortie du module pendant un envoi HTTPS peut laisser la tâche réseau finir après le retour au menu. Vérifier les accès concurrents microSD avec MQTT et les autres modules ; sans essai, absence de conflit non garantie.
- Les écrans secondaires du Nexus autonome ne sont pas encore portés intégralement.
- La présence de fonctions radio historiques dans KVXputer ne vaut pas validation fonctionnelle de chaque module. Aucun changement de comportement perturbateur radio n'a été ajouté pour cette intégration.
- Le binaire contenant le secret `NEXUS_SHARED_TOKEN` ne doit pas être publié en clair dans un artefact GitHub public. Les builds CI utilisent un chiffrement ; un `.bin.enc` n'est pas flashable directement.
- Pas de test de bout en bout réussi documenté pour un envoi ou une réception vocale entre deux appareils ; ne pas qualifier cette version de « finale stable » sans ce résultat.

## Tests matériels encore requis
1. Démarrer sur Cardputer ADV et ouvrir successivement KVX, Nexus, MQTT et les anciennes applications.
2. Exercer ESC, FN+ESC, entrée/sortie rapide, réinitialisation, scan Wi-Fi annulé, faible batterie et carte microSD retirée.
3. Enregistrer, annuler, écouter et envoyer plusieurs WAV, en cas de silence, sans réseau et avec un stockage presque plein.
4. Tester deux appareils avec contacts distincts, réception de vocaux, coupures HTTPS, confirmations, réessais et absence de doublons.
5. Mesurer la mémoire disponible avant/après utilisation, les piles de tâches et l'interopérabilité audio, MQTT, BLE et Wi-Fi.
6. Vérifier checksums, partitions réelles et flashage d'une image privée avant remise d'un `.bin` installable.

## Suivi GitHub
- Issue de validation : https://github.com/Ilyaane/PC-HACKED/issues/8
- Workflow : https://github.com/Ilyaane/PC-HACKED/actions/workflows/kvx-nexus-neon.yml
