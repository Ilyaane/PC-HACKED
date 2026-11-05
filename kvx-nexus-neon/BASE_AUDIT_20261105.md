# Audit et correctifs KVXputer de base, 5 novembre 2026

## Périmètre et méthode

Base de référence : `kvx17/kvxputer` au commit `c2ff56f154c35eb13fcfab845fb30dcecf372449`.
Branche d'intégration : `kvx-base-ble-suite-stability-20261105`.
Les sources originales KVXputer, Nexus V10 et MQTT V5 restent inchangées dans leurs branches respectives.

La livraison s'effectue au moyen de correctifs appliqués à un checkout temporaire du firmware, puis d'une compilation PlatformIO complète. Il n'existe toujours qu'un seul programme Arduino `setup()/loop()`. Les deux modules native Nexus V10 et MQTT V5 sont conservés et les menus KVX existants ne sont pas retirés.

## Bugs objectivement identifiés dans les sources

| Sous-système | Défaut observé dans le code | Correction dans cette branche |
|---|---|---|
| BLE Suite / RAM | `initBLE()` était réexécuté alors que NimBLE était déjà initialisé ; chaque appel ajoutait une réservation `uiRamEnterHeavy()`. `deinitBLE()` n'appelait pas son inverse. | Initialisation idempotente ; libération `uiRamLeaveHeavy()` à la fin de la session et après échec d'initialisation |
| BLE Suite / entrée | `BleSuiteMenu()` ignorait le booléen retourné par l'initialisation BLE et pouvait afficher un menu inutilisable. | Abandon explicite du module en cas d'échec |
| BLE Suite / affichage | `(tftHeight-80)/25` laisse seulement 2 lignes sur un écran 135 px. | Police 1x, lignes 12 px, 6 options visibles dans le menu principal **et les sous-menus** sans chevauchement avec le pied de page |
| BLE Suite / accueil | Écran d'accueil bloquant pendant 2000 ms. | Réduction à 120 ms |
| BLE Suite / navigation | Les sous-menus avaient les mêmes lignes de 25 px et ne vérifiaient pas les listes vides ; l'écran de la suite ignorait les demandes globales de sortie. | Sous-menus compacts, retour sûr sur menu vide, sortie forcée prise en compte et diagnostic RAM série |
| BLE Suite / mémoire des scans | `std::vector::clear()` conserve normalement sa capacité ; les tableaux d'appareils scannés pouvaient garder de la RAM allouée en quittant le module. | `shrink_to_fit()` des six vecteurs, après nettoyage protégé par mutex |
| BLE commun / erreur de scanner | Après échec d'obtention de l'objet `BLEScan`, la fonction retournait sans libérer explicitement sa réservation de ressources. | Passage par `stopBLEStack()` avant retour d'erreur |
| Bluetooth / Déconnecter | L'option utilisait une procédure de fermeture différente du nettoyage commun ; le nettoyage dépendait du drapeau `is_ble_inited`, pas toujours présent pour HID. | Procédure unique `stopBLEStack()`, prenant également en compte `BLEConnected` |

## Correctifs déjà acquis et conservés des branches précédentes

- Nexus V10 intégré sans Morse ni vocal ; navigation et sortie par FN + ESC.
- MQTT V5/v0.2.3 : microSD initialisée avant lecture de `/mqtt/config.ini`, édition des 8 paramètres par le clavier KVX, erreur de sauvegarde expliquée.
- Nexus : suppression du seuil de 43 Ko de RAM totale et prévention d'un état bloqué après échec de création de tâche.
- Menus KVX : densité de texte 1x dans les menus partagés (même taille que BLE).
- Gestion Wi-Fi KVX : scripts existants de correction du scan et des tâches de reconnexion.

## Vérification et critères d'acceptation

1. Tests statiques de l'intégration Nexus/MQTT et test API Cloudflare.
2. Application des patchs seulement sur les sources épinglées ; toute divergence fait échouer le CI.
3. Tests d'invariants BLE : les 12 intitulés de menu restent présents, les appels d'initialisation/libération de RAM sont équilibrés dans le gestionnaire, échec d'initialisation traité, nettoyage du scanner.
4. Compilation totale de KVXputer pour `m5stack-cardputer`, puis contrôle taille/partition/SHA-256 de l'image.
5. Tests matériels encore indispensables : plusieurs entrées/sorties de BLE Suite ; BLE puis Nexus ; BLE puis MQTT ; scan avec de nombreux résultats ; mémoire libre après fermeture ; appui FN+ESC ; broker hors ligne ; réseau Wi-Fi perdu ; test régression des fonctions préexistantes sur appareils autorisés.

## Limites et risques résiduels

Le code source inspecté contient également de nombreux composants radio et sous-menus spécialisés. Une compilation, même réussie, ne prouve pas leur fonctionnement individuel. Aucun essai matériel, mesure de consommation RAM en situation réelle, test BLE avec des accessoires ou test de charge n'a été exécuté dans cet audit. Les modules de sécurité sans fil, dont BLE Suite, peuvent avoir des usages nécessitant un cadre autorisé : aucune capacité offensive n'a été ajoutée ou étendue dans ces correctifs.

**Statut :** correctifs enregistrés ; qualification finale uniquement après succès complet CI et tests physiques. Ne pas remplacer le firmware précédemment transmis avant la validation.
