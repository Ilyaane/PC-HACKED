# KVXputer avec MQTT V5 intégré, Cardputer ADV

## Objet et provenance

MQTT V5 est intégré au menu de KVXputer pour le Cardputer ADV.

- **KVXputer** original : `kvx17/kvxputer`, commit épinglé `c2ff56f154c35eb13fcfab845fb30dcecf372449`. Les menus et applications d'origine sont conservés.
- **MQTT** : version technique **v0.2.3**, appelée **V5** par l'utilisateur, issue du code de `nexus-mqtt-standalone-20261103`.

**Attention :** la présente branche est un projet de portage. Ne pas considérer l'absence d'erreur dans les tests statiques comme preuve que le matériel est validé.

## Menu et ergonomie

KVXputer garde la grille de trois colonnes sur deux rangées, avec ordre de parcours **par colonnes**. La tuile MQTT suit Config et affiche une bulle de messagerie.

- **FN + ESC** : sortie directe de MQTT et retour à la grille KVXputer. **ESC seul** revient dans les écrans du module.
- **MQTT V5** : quatre cases Messages, Historique, Réseau et Réglages; UI héritée de v0.2.3, touches et flèches gérées par KVXputer.

MQTT utilise le programme Arduino et le gestionnaire Wi-Fi de KVXputer. La sauvegarde de la configuration MQTT demande une **reconnexion asynchrone** du service, pas `ESP.restart()`.

## Réseau et affichage

La grille KVXputer ne redessine intégralement qu'au changement de page ou au retour de module. MQTT utilise une tâche FreeRTOS pour TLS, MQTT et les reconnexions avec délai. L'écran est mis à jour sur changement d'état, action clavier ou message reçu, et non par effacement complet à intervalle fixe.

Les appels réseau réels restent **à valider sur Cardputer ADV**, notamment lorsque le broker MQTT est inaccessible, pendant la reconnexion et lors de changements rapides de Wi-Fi.

Le SDK ESP32-S3 utilisé réserve des buffers TLS internes de 16 Kio. L’historique est chargé à l’ouverture ou à la réception du premier message, et les tampons du scanner BLE sont libérés à la fermeture du scan. Le diagnostic TLS affiche le plus grand bloc contigu et le journal série enregistre aussi la RAM totale et la marge de pile.

## Configuration, stockage, secrets

**Configuration MQTT normale :** `/mqtt/config.ini` sur la carte microSD, selon les champs `wifi_ssid`, `wifi_password`, `host`, `port`, `username`, `password`, `device_id`, `peer_id`. Une configuration fournie à la compilation peut servir de valeur initiale, mais le fichier INI existant sur microSD est prioritaire. Le certificat de confiance TLS de MQTT est attendu en `/mqtt/ca.pem`. Historique et boîte d'envoi MQTT : `/mqtt/history.jsonl`, `/mqtt/outbox.jsonl`.

**Pas de NVS pour MQTT** : pas de `Preferences`, pas d'enregistrement de jetons MQTT en flash de préférences. La gestion de l'association Wi-Fi est celle de KVXputer, pas une seconde connexion imposée par le module MQTT. La configuration `config.ini` peut néanmoins contenir des identifiants en clair : protégez la carte microSD.


## Construction reproductible

```sh
git clone https://github.com/kvx17/kvxputer.git kvxputer
git -C kvxputer checkout c2ff56f154c35eb13fcfab845fb30dcecf372449
python kvx-nexus-neon/tools/test_overlay.py
python kvx-nexus-neon/tools/install_overlay.py --kvx kvxputer
cd kvxputer
pio run -e m5stack-cardputer
```

Dans la CI, les scripts de compatibilité KVX existants sont appliqués avant la compilation. La taille de l'application est contrôlée face à la partition `APP` réelle, et l'empreinte SHA-256 est imprimée. L'image d'application ne doit pas être flashée à l'adresse `0x0`; un flash complet est un artefact distinct. La CI construit et vérifie le binaire sans publier les artefacts de livraison.

## Tests et limites

Les tests de structure vérifient : une tuile MQTT, insertion idempotente, absence de second programme Arduino, navigation FN+ESC, préservation du fichier INI MQTT, absence d'écriture NVS MQTT, utilisation du Wi-Fi partagé, suppression du redémarrage global et absence de rafraîchissement complet sur simple poll inactif.

**Non certifié par de simples tests statiques** : compilation finale et taille d'image tant que la CI n'a pas terminé, affichage et clavier physiques, broker hors ligne, reconnexions réelles, deux Cardputers, stabilité longue durée, usages des anciennes applications KVX. Une fois la CI aboutie, des essais sur l'appareil restent indispensables avant qualification « définitive ».


## Licence

KVXputer original est sous AGPL-3.0-or-later : conserver ses avis et respecter les obligations du logiciel dérivé.
