# Audit de stabilisation KVXputer + Nexus V10 + MQTT V5
Date : 5 novembre 2026. Carte cible : M5Stack Cardputer ADV.

## Règles de ce chantier
- Préserver les anciens firmwares, sources, menus et fonctionnalités.
- Une seule application embarquée native, sans chargeur de firmwares secondaires.
- Les modules Nexus et MQTT gardent leurs interfaces indépendantes et leur navigation.
- Ne pas exposer les identifiants réseau et jetons de service dans les sources publiques.
- Aucun résultat de CI ne remplace un essai physique.

## Corrections de la branche
1. **MQTT V5** : montage explicite de la microSD avant de lire les fichiers INI/CA, contrôle d'écriture temporaire, saisie via le clavier natif, état d'erreur visible et formulaire non réinitialisé en silence.
2. **Nexus V10** : réduction des faux refus « RAM insuffisante », contrôle de la mémoire contiguë disponible, gestion de l'échec d'allocation d'une réponse réseau, protection contre l'état réseau bloqué.
3. **Menus KVX** : police 1× identique à BLE pour les menus d'options et listes de fichiers communs, sans changer les écrans métiers personnalisés.
4. **Wi-Fi partagé** : page de recherche/connexion compacte, recherche asynchrone et bornée, touche ESC pour annuler une recherche ou une connexion, gestion Wi-Fi toujours centralisée par KVX.
5. **BLE Suite** : arrêt propre quand l'initialisation manque de mémoire, nettoyage de la réservation de RAM après fermeture, compatibilité avec l'arrêt BLE demandé par le gestionnaire Wi-Fi, écran compact (~6 lignes sur 135 pixels), accueil raccourci.
6. **Scanner BLE passif** : maximum 64 appareils conservés, libération de la capacité des vecteurs quand les données sont effacées, sélection basée sur la taille du snapshot plutôt que sur un vecteur pouvant avoir changé.
7. **Qualité** : installation du patch dans une copie éphémère d'un commit KVX épinglé, tests statiques, contrôles d'intégration, compilation PlatformIO entière et contrôle de la partition de l'application.

## Tests réalisés et limites
- Contrôles des ancrages des scripts : effectués sur les sources KVX épinglées.
- Contrôles statiques de Nexus/MQTT et test de contrat Cloudflare : validés sur la nouvelle branche lors de la première exécution CI.
- Nouveau contrôle des invariants BLE/Wi-Fi : exécuté dans le job de construction avant compilation (son statut final figure dans GitHub Actions).
- **Compilation / image ESP32 : voir GitHub Actions**, notamment le run 42055234988, ne pas supposer sa réussite avant la conclusion.
- **Non vérifiés physiquement** : clavier réel, transition BLE/Wi-Fi, association Wi-Fi, broker MQTT injoignable, redémarrage sous faible RAM, latences, stabilité sur une heure, tous les menus et toutes les anciennes applications.

## Points de vigilance non certifiés
- BLE Suite est volumineux et contient beaucoup de sous-écrans personnalisés. La réduction 1× couvre ses deux menus principaux, pas toutes ses vues de détail. Les fonctions de transmission active ne sont ni ajoutées ni modifiées dans ce chantier.
- Le code de connectivité et de BLE nécessite des tests avec plusieurs appareils autorisés et avec les cartes mémoire utilisées en pratique.
- Le compilateur ne prouve pas qu'aucun gel ou problème matériel ne subsiste.
- Le firmware corrigé ne doit pas remplacer la version précédente avant une compilation complète et une validation matérielle suffisante.
