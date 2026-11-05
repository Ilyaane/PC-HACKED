# KVXputer / Cardputer ADV : audit et recompilation de stabilisation
Date de l'audit : 5 novembre 2026.

## Sources de travail
- KVXputer original : commit `c2ff56f154c35eb13fcfab845fb30dcecf372449` de `kvx17/kvxputer`.
- Fusion précédente : `kvx-core-ble-suite-nexus-wifi-20261105`.
- Correctifs de cette étape : `kvx-core-stabilisation-rebuild-20261105`.
- L'intégration est native : une application Arduino, Nexus text-only V10 et MQTT V0.2.3/V5 dans les menus.

## Autres défauts relevés dans les sources
1. **Wi-Fi auto après échec** : `wifiConnectTask` prenait un bail de RAM (mode UI lourd) puis quittait sans libération quand aucun réseau ne se connectait. Correction : appelle `wifiDisconnect()` sur le chemin d'échec.
2. **Wi-Fi manuel** : certains chemins sans connexion réussie quittaient `wifiConnectMenu` sans restaurer le bail de RAM. Correction : nettoyage explicite sur échec sans couper une connexion établie.
3. **Double libération BLE/Wi-Fi** : `BLEStateManager::deinitBLE` peut libérer son bail tandis que `stopBLEStack` libérait aussi un bail sans vérifier s'il lui appartenait. Correction : distinction de la possession du bail par le scanner générique.
4. **Initialisation BLE considérée réussie sans preuve** : `BLEStateManager::initBLE` ignorait le booléen de `NimBLEDevice::init` et pouvait réutiliser un indicateur d'initialisation périmé. Correction : vérifier le booléen et l'état réel, désactiver proprement l'application si l'initialisation échoue.
5. **Utilisation de pointeurs vers un instantané BLE supprimé** : `ScannerData::getSnapshot` renvoyait un pointeur nu alors que `addDevice` et `clear` pouvaient libérer le cache. Correction : références propriétaires `std::shared_ptr` et accès au cache protégés par mutex.
6. **MQTT et Wi-Fi partagé** : la version fusionnée ne propose pas la connexion au réseau depuis sa page « Réseau ». Correction : ENTREE invoque le menu Wi-Fi natif KVX, avec retour dans MQTT; ESC revient au menu MQTT.
7. **Polices disproportionnées** : écrans de connexion Wi-Fi et menus KVX affichés plus grands que le menu BLE 1×. Correctifs précédents maintenus.

## Limites de l'audit
- Le firmware complet comporte de nombreuses applications : cet audit cible les ressources communes, menus, connexion réseau, configuration MQTT et détection BLE passive. Il n'établit pas que tous les bugs du dépôt ont été trouvés.
- Les fonctions d'attaque, de brouillage, d'injection ou de perturbation radio n'ont pas été améliorées par cet audit et leur comportement n'est pas certifié.
- Pas d'appareil physique accessible pour une connexion Wi-Fi, un broker MQTT, TLS, un second Cardputer, une batterie faible ou un appui FN+ESC.
- L'étape CI vérifie les tests statiques, la compilation PlatformIO et l'intégrité de l'image, mais pas les comportements matériels.
- La variante Nexus embarquée conserve le chat texte, mais tous les écrans secondaires de l'original Nexus V10 ne sont pas portés.
- Un `.bin` généré par ce workflow est une **image d'application**, pas une image flash complète pour adresse `0x0`. 
- Le téléchargement GitHub CI est chiffré pour ne pas exposer le jeton compilé; un lien CI ne doit pas être présenté comme un `.bin` brut.

## Contrôles
Voir l'exécution GitHub Actions pour le dernier commit :
https://github.com/Ilyaane/PC-HACKED/actions/runs/42058020422

Le dossier précédent `docs/KVX_AUDIT_CORE_20261105.md` et l'ancienne branche sont conservés.
