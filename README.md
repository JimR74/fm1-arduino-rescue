# fm1-arduino-rescue

Récupérer un **M-VAVE FM-1 bloqué** (écran noir, aucun périphérique USB) avec un **Arduino Uno ou Nano à 3 $** (ATmega328P) et quelques résistances, sans XIAO RP2040, Flipper Zero ni FM-1 Transporter.

> ⚠️ **Expérimental, à vos risques.** Testé avec succès **une seule fois** (14 novembre 2026), sur un FM-1 avec une flash `0x856014` et un clone d'Uno CH340. Non testé sur la flash plus récente UC25HQ80 (`0xB36014`).

## Principe

La puce du FM-1 (JieLi WL82 / AC791N) a un bootloader en ROM que rien ne peut effacer. Pour l'y faire entrer quand le firmware est mort, il faut envoyer au démarrage une **clé lente (~50 kHz) sur les lignes D+ et D−**. Ce n'est pas de l'USB, donc un simple GPIO d'ATmega suffit.

1. L'Arduino envoie la clé `0x16EF` (D+ = horloge, D− = données, bit de poids fort en premier), détecte la réponse de la puce, puis émule ~1 s d'impulsions SOF.
2. On déplace le câble du FM-1 vers le PC **sans l'éteindre**. Windows voit alors un disque `WL82 UBOOT1.00` (`4C4A:8057`).
3. Un outil existant ([FM1 Recovery](https://github.com/SikkenWorks/FM1recovery), [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker), [M-Flasher](https://github.com/jvitkauskas/m-flasher)) sauvegarde la flash et réécrit le firmware officiel V15.

Le sketch **n'écrit rien** dans le FM-1.

## Matériel

| Pièce | Quantité |
|---|---|
| Arduino Uno ou Nano (ATmega328P, 16 MHz) | 1 |
| Résistance 2,2 kΩ (rouge-rouge-rouge) | 2 |
| Résistance 3,3 kΩ (orange-orange-rouge) | 2 |
| Résistance 470 Ω (jaune-violet-brun) | 1 |
| Câble USB-C à sacrifier (bout coupé) | 1 |
| Câble USB-C de données intact | 1 |

## Câblage

Schéma en couleur : [`docs/cablage.html`](docs/cablage.html)

```
Arduino D2 ─[2.2k]─┬──────── D+ (souvent vert)   ── + fil direct vers A0
                   ├─[3.3k]─ GND
Arduino D4 ─[470Ω]─┘          (même nœud D+)
Arduino D3 ─[2.2k]─┬──────── D− (souvent blanc)
                   └─[3.3k]─ GND
Arduino GND ────────────────  GND du câble (fil de drain nu / blindage)
VBUS (rouge) et CC (bleu) : isolés, NON branchés
```

- Les ponts 2,2 k / 3,3 k ramènent le 5 V de l'ATmega à environ 3,0 V sur les lignes USB du FM-1. **Ne les omettez pas.**
- La broche **3.3V** de l'Arduino ne remplace **pas** les résistances, parce que les GPIO restent en 5 V.
- Vérifiez les couleurs des fils au multimètre : D+ = A6/B6, D− = A7/B7 sur la prise USB-C.

## Procédure

1. Téléversez `fm1_uboot_key_nano/fm1_uboot_key_nano.ino` (carte « Arduino Uno », ou « Arduino Nano » avec « ATmega328P (Old Bootloader) » pour les clones). Ouvrez le moniteur série à **115200 bauds**. Si le texte est lisible, l'horloge est bien à 16 MHz.
2. **Vérifications, FM-1 non branché** : `t` → `0.00 V -> BAS`. Avec A0 relié brièvement au 3.3V : `t` → environ `3.3 V -> HAUT`.
3. Branchez le câble coupé au FM-1 **éteint**, avec le câble intact déjà branché au PC.
4. Envoyez `g`, puis **allumez le FM-1** dans les 15 s.
5. Attendez `REPONSE detectee` puis `Calibration terminee` (LED 13 allumée en continu, environ 2 s après l'allumage).
6. **Sans éteindre le FM-1**, passez-le sur le câble intact branché au PC.
7. Vérifiez avec `Get-Disk` (PowerShell) que `WL82 UBOOT1.00` apparaît. Sinon, éteignez le FM-1 et recommencez, en essayant un autre port.
8. Lancez **FM1 Recovery en administrateur**, Simple Mode, `RESTORE`.

Commandes série : `g` = clé + calibration, `c` = calibration seule, `t` = lecture de D+, `h` = aide.

## Piège Windows : « Transfer failed: [WinError 5] Accès refusé »

Même en administrateur, l'**Accès contrôlé aux dossiers** de Windows Defender (protection contre les ransomwares) a bloqué les commandes SCSI vers le disque UBOOT. L'outil ouvrait bien le périphérique, mais l'envoi des commandes échouait.

**Solution :** dans Sécurité Windows, ouvrez Protection contre les virus et menaces, puis Gérer la protection contre les ransomwares, **désactivez temporairement** l'Accès contrôlé aux dossiers, faites la récupération, puis **réactivez-le**.

## Revenir au firmware officiel V15 sans Arduino (FM-1 qui démarre encore)

Le site de M-VAVE ne propose plus que le **V16** (`FM-1.fwsc`, 2026-10-21), sans numéro de version dans le nom du fichier. FM1 Recovery contient le V15 officiel vérifié et le décompresse dans :

`%LOCALAPPDATA%\SikkenWorks\FM1-Recovery\tools-<…>\FM-1.fwsc`
(SHA-256 `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`)

Testé le 14 novembre 2026 : sous Felucca, ce fichier installé avec **M-UPGRADE** (l'outil de M-VAVE) a remis le V15. Ensuite, l'installateur d'un autre firmware (KARMA) a pu détecter le FM-1. Les combinaisons OCT− à l'allumage et OCT−/OCT+ pendant 5 s n'ont **pas** mis le FM-1 en mode boot sous ce Felucca.

## Résultat de l'essai réussi (14 novembre 2026)

- La clé a été acceptée après environ 1 000 paquets (≈ 0,5 s). Calibration : 1 501 ms.
- Premier essai de changement de câble : `Périphérique USB inconnu (échec de demande de descripteur)`. Deuxième essai : `WL82 UBOOT1.00` détecté.
- FM1 Recovery 1.0k : `Chip key 0x980F`, `Flash ID 0x856014`, sauvegarde complète réussie, V15 réécrit et vérifié octet par octet.
- Ensuite, Felucca s'est installé normalement par l'installateur web.

## Limites connues / à tester

- Une seule réussite, sur un seul FM-1. Il faut d'autres retours, en particulier sur la flash UC25HQ80.
- La branche `SWAP_POLARITY 1` n'a jamais été nécessaire ni testée.
- La lecture de D+ passe par l'ADC (A0). Une première version utilisait le comparateur analogique sur D7 et donnait de fausses lectures « HAUT ».
- Une zone de démarrage endommagée (`0x0000–0x3FFF`) n'est pas récupérable par cette méthode, comme avec les autres outils.

## Crédits

- [merthsoft/flipper-fm1-recovery](https://github.com/merthsoft/flipper-fm1-recovery) (Unlicense) : timing de la clé et de la calibration, dont ce sketch est un portage AVR.
- [kagaimiq/jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) : documentation de la clé USB_KEY JieLi.
- [kurogedelic/FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter), [Quixotic7/MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker), [SikkenWorks/FM1recovery](https://github.com/SikkenWorks/FM1recovery), [The Firmware Hub](https://thefirmwarehub.com/).

## Licence

À décider avant publication.
