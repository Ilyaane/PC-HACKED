# KVXputer + Nexus V10 + MQTT V5, Cardputer ADV

## Objet et provenance

Projet d'intégration **native** de deux applications dans KVXputer, sans changer de firmware ni de bootloader :

- **KVXputer** original : `kvx17/kvxputer`, commit épinglé `c2ff56f154c35eb13fcfab845fb30dcecf372449`. Les menus et applications d'origine sont conservés.
- **Nexus** : profil de référence **V10 du 3 novembre 2026**, sans Morse ni vocal. L'image autonome est archivée dans `Nexus_version.zip` (SHA-256 du binaire applicatif `92e71f6a4937bf68b6791102fec4b0a00f167402cacde37a435b918606dbfd11`). Cette archive est une référence, pas un binaire exécuté par le module.
- **MQTT** : version technique **v0.2.3**, appelée **V5** par l'utilisateur, issue du code de `nexus-mqtt-standalone-20261103`.

**Attention :** la présente branche est un projet de portage. Ne pas considérer l'absence d'erreur dans les tests statiques comme preuve que le matériel est validé.

## Menu et ergonomie

KVXputer garde la grille de trois colonnes sur deux rangées, avec ordre de parcours **par colonnes**. Sur la dernière page, Nexus est suivi immédiatement de MQTT : **MQTT se place dans la case juste en dessous de Nexus** (positions 21 et 22 si les 20 entrées de base sont visibles).

- **Nexus V10** : écran d'accueil en carrousel horizontal à flèches, couleurs néon et quatre sections : Messages, Écrire, Wi-Fi et Contact. Gauche/Droite parcourent le carrousel; Haut/Bas peuvent également changer la sélection. La lecture et l'envoi des messages passent par le protocole HTTPS existant, avec clé d'idempotence pour les réessais.
- **FN + ESC** : sortie directe de Nexus ou MQTT et retour à la grille KVXputer. **ESC seul** revient dans les écrans du module.
- **MQTT V5** : quatre cases Messages, Historique, Réseau et Réglages; UI héritée de v0.2.3, touches et flèches gérées par KVXputer.

Les modules n'utilisent aucun second `setup()`, `loop()`, flashage ou redémarrage pour passer d'une application à l'autre. La sauvegarde de la configuration MQTT demande une **reconnexion asynchrone** du service, pas `ESP.restart()`.

## Réseau et affichage

Nexus conserve un poll HTTPS en tâche indépendante pour vérifier de nouveaux messages, **sans repeindre l'écran à chaque poll silencieux**. La grille KVXputer ne redessine intégralement qu'au changement de page ou au retour de module. MQTT utilise une tâche FreeRTOS pour TLS, MQTT et les reconnexions avec délai. L'écran est mis à jour sur changement d'état, action clavier ou message reçu, et non par effacement complet à intervalle fixe.

Les appels réseau réels restent **à valider sur Cardputer ADV**, notamment lorsque le broker MQTT est inaccessible, pendant la reconnexion et lors de changements rapides de Wi-Fi.

## Configuration, stockage, secrets

**Configuration MQTT normale :** `/mqtt/config.ini` sur la carte microSD, selon les champs `wifi_ssid`, `wifi_password`, `host`, `port`, `username`, `password`, `device_id`, `peer_id`. Une configuration fournie à la compilation peut servir de valeur initiale, mais le fichier INI existant sur microSD est prioritaire. Le certificat de confiance TLS de MQTT est attendu en `/mqtt/ca.pem`. Historique et boîte d'envoi MQTT : `/mqtt/history.jsonl`, `/mqtt/outbox.jsonl`.

**Pas de NVS pour MQTT** : pas de `Preferences`, pas d'enregistrement de jetons MQTT en flash de préférences. La gestion de l'association Wi-Fi est celle de KVXputer, pas une seconde connexion imposée par le module MQTT. La configuration `config.ini` peut néanmoins contenir des identifiants en clair : protégez la carte microSD.

**Nexus** : serveur HTTPS via certificat CA, jeton injecté dans l'arbre temporaire de compilation par `NEXUS_SHARED_TOKEN`; aucune valeur réelle dans les sources publiques. La version Nexus native conserve une préférence NVS pour le contact, **pas pour le jeton**.

## Construction reproductible

```sh
git clone https://github.com/kvx17/kvxputer.git kvxputer
git -C kvxputer checkout c2ff56f154c35eb13fcfab845fb30dcecf372449
python kvx-nexus-neon/tools/test_overlay.py
python kvx-nexus-neon/tools/install_overlay.py --kvx kvxputer --nexus .
cd kvxputer
pio run -e m5stack-cardputer
```

Dans la CI, les scripts de compatibilité KVX existants sont appliqués avant la compilation. La taille de l'application est contrôlée face à la partition `APP` réelle, et l'empreinte SHA-256 est imprimée. L'image d'application ne doit pas être flashée à l'adresse `0x0`; un flash complet est un artefact distinct. Ne publiez pas de binaire comportant un jeton compilé; la CI chiffre l'artefact de livraison.

## Tests et limites

Les tests de structure vérifient : deux entrées de menu, insertion idempotente, absence de second programme Arduino, navigation FN+ESC, préservation du fichier INI MQTT, absence d'écriture NVS MQTT, utilisation du Wi-Fi partagé, suppression du redémarrage global et absence de rafraîchissement complet sur simple poll inactif.

**Non certifié par de simples tests statiques** : compilation finale et taille d'image tant que la CI n'a pas terminé, affichage et clavier physiques, broker hors ligne, reconnexions réelles, deux Cardputers, stabilité longue durée, usages des anciennes applications KVX. Une fois la CI aboutie, des essais sur l'appareil restent indispensables avant qualification « définitive ».

**Écart fonctionnel connu :** l'application Nexus native reprend la **messagerie texte, la navigation et l'esthétique V10**, mais ne reprend pas encore la totalité des pages utilitaires, archives et paramètres du binaire autonome V10. Ne pas présenter ce portage comme une copie intégrale certifiée du firmware autonome.

## Licence

KVXputer original est sous AGPL-3.0-or-later : conserver ses avis et respecter les obligations du logiciel dérivé.
