# Intégration Nexus Néon texte dans KVXputer (Cardputer ADV)

## But

Une **21e application native**, nommée **Nexus Neon**, est ajoutée après Config, sur la dernière page de la grille KVXputer (3 colonnes × 2 rangées). Les 20 applications existantes restent dans leur ordre d'origine et les trois dernières cases restent vides. L'icône utilise les mêmes méthodes de dessin, couleurs de thème et limites d'écran que le menu KVXputer.

La version intégrée dérive de **Nexus_Néon_No_Morse_No_Micro.bin** mais **ne charge pas le binaire** : les éléments utiles ont été portés depuis le code source (API HTTPS, destinataire, envoi texte avec identifiant idempotent, consultation des messages, interface).

**Ce qui est présent :** messages texte Alice/Bob, brouillon conservé après erreur, confirmation avant envoi, actualisation HTTPS non bloquante, saisie via le clavier KVXputer, navigation directe avec ; . , / (sans Fn), icône dédiée, retour propre au menu, réutilisation du gestionnaire Wi-Fi KVXputer.

**Ce qui n'est PAS présent :** Morse, microphone, messages vocaux, MQTT, second firmware, second setup/loop, stockage du jeton en NVS.

## Installation pour développement

La branche du projet qui contient l'overlay est séparée du firmware Nexus existant. Elle ne modifie ni les fichiers originaux KVXputer chez kvx17, ni leurs fonctions existantes.

Prérequis : clone local de la version amont épinglée `c2ff56f154c35eb13fcfab845fb30dcecf372449`, et copie de ce dépôt utilisateur contenant `nexus-adv/firmware/include/ca_roots.h`.

```sh
python kvx-nexus-neon/tools/test_overlay.py
python kvx-nexus-neon/tools/install_overlay.py --kvx ./kvxputer --nexus .
cd kvxputer
pio run -e m5stack-cardputer
```

L'injection d'un jeton compilé, si nécessaire, utilise l'environnement `NEXUS_SHARED_TOKEN` et un fichier d'en-tête généré **uniquement dans le répertoire temporaire de compilation**. Le script vérifie l'empreinte attendue du secret, sans afficher sa valeur. Sans secret, le firmware compile mais la communication distante reste volontairement désactivée. Ne publiez jamais un binaire contenant un jeton partagé.

## Vérifications

`tools/test_overlay.py` vérifie l'installation répétable, l'emplacement de la case et les composants utilisés. GitHub Actions doit également compiler KVXputer complet et contrôler le fichier résultant.

**Essais physiques non réalisables ici :** affichage réel, boutons, Wi-Fi réel, deux appareils, serveur Cloudflare de production, intégrité du flash sur le Cardputer ADV. Une compilation réussie ne vaut pas validation complète sur matériel.

## Sources et licence

* KVXputer : https://github.com/kvx17/kvxputer (AGPL-3.0-or-later)
* Base Nexus Néon : code du dossier `nexus-adv/` de ce dépôt.
* Le firmware combiné est un travail dérivé soumis à la licence AGPL-3.0 applicable à KVXputer, et doit conserver les avis d'attribution.
