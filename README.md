# CBackup – C++17 back-upbeheer voor Linux / Raspberry Pi

CBackup is een commandlineprogramma voor het instellen, maken, controleren en terugzetten van back-ups. Het bouwt voort op het idee van de aangeleverde `backup.py` (ZIP-back-up van één map) en `install.sh` (periodieke systemd-back-up). Deze nieuwe uitvoering gebruikt C++17, libarchive en OpenSSL en kan meerdere projecten/bronnen en bestemmingen per job beheren.

**Platform:** Linux, met name Raspberry Pi OS / Debian / Ubuntu. `cmd` betekent hier een terminal/shell. Het is geen Windows-`.exe`; voor Windows is een afzonderlijke port nodig.

## Installeren

Eenmalig in de uitgepakte projectmap:

```bash
chmod +x install.sh
./install.sh
```

De installer gebruikt op Debian/Raspberry Pi OS `apt` voor CMake, C++-compiler, `libarchive-dev`, `libssl-dev` en `rclone`, compileert het programma en installeert `cbackup` in `/usr/local/bin`. De installer plant **niet** stilzwijgend taken in. Het oorspronkelijke `install.sh` bevatte zowel een Bash-shebang als de Windows-regel `@echo off`; dat is in deze installer opgelost.

Handmatig bouwen zonder installatie:

```bash
sudo apt install build-essential cmake pkg-config libarchive-dev libssl-dev rclone
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/cbackup help
```

## Snel starten

```bash
cbackup init mijnbackup
cbackup config mijnbackup add-source "$HOME/Documenten" "$HOME/Projects"
cbackup config mijnbackup set dest "$HOME/Backups"
cbackup config mijnbackup set mode auto
cbackup config mijnbackup set compression zstd
cbackup config mijnbackup set full_every 7
cbackup config mijnbackup set keep_chains 3
cbackup config mijnbackup show
cbackup run mijnbackup --dry-run
cbackup run mijnbackup
cbackup list mijnbackup
```

Standaard bewaart CBackup de config in `~/.config/cbackup/jobs/<job>.conf`. Back-ups staan in `<dest>/<job>/`. Meerdere jobs, bijvoorbeeld `code`, `foto`, `school`, kunnen verschillende instellingen hebben.

## Back-upsoorten

| Type | Commando | Betekenis |
|---|---|---|
| Volledig | `cbackup run mijnbackup --type full` | Archiveert alle geselecteerde bestanden. |
| Incrementeel | `cbackup run mijnbackup --type incremental` | Archiveert veranderingen sinds de **vorige** back-up. |
| Differentieel | `cbackup run mijnbackup --type differential` | Archiveert veranderingen ten opzichte van de **laatste volledige** back-up. |
| Automatisch | `cbackup config mijnbackup set mode auto` | Eerst full; daarna incremental; opnieuw full na `full_every` opgeslagen back-ups. |
| Spiegelkopie | `cbackup run mijnbackup --type mirror` | Maakt een **niet-gecomprimeerde** actuele kopie. Verwijderde bronbestanden verdwijnen bij de volgende spiegelrun ook uit de mirror. **Geen versiegeschiedenis.** |

De eerste back-up wordt automatisch volledig als een incrementele of differentiële basis ontbreekt. Veranderen de selectiecriteria, dan start een nieuwe volledige keten.

**Compressie:** `cbackup config mijnbackup set compression zstd` (standaard, `.tar.zst`), `gzip` (`.tar.gz`) of `none` (`.tar`). `mirror` gebruikt geen archiefcompressie. Compressie helpt doorgaans goed bij tekst en broncode, maar minder bij al gecomprimeerde JPEG-, MP4- of ZIP-bestanden.

## Bestanden en projecten selecteren

Bronnen mogen mappen of losse bestanden zijn. Om meerdere bronnen toe te voegen:

```bash
cbackup config mijnbackup add-source "$HOME/Projects/site" "$HOME/Projects/game" "$HOME/Foto's"
```

Beperk op extensies; bestanden die hier niet bij staan worden overgeslagen:

```bash
cbackup config mijnbackup add-ext cpp h py js ts json md txt pdf png jpg
cbackup config mijnbackup clear-ext
```

Selecteer op bestands- of padpatroon (tussen enkele aanhalingstekens, zodat de shell het patroon niet vervangt):

```bash
cbackup config mijnbackup add-include '*.cpp' '*.hpp' 'src/*'
cbackup config mijnbackup add-exclude '*.tmp' '*.log' 'node_modules/*' '.git/*' 'build/*'
cbackup config mijnbackup clear-include
cbackup config mijnbackup clear-exclude
```

Extensiefilter en include-patronen werken als **EN**: een bestand moet aan beide voldoen als beide zijn ingesteld. Exclude-patronen gaan voor. Globs worden toegepast op het relatieve pad en op het pad met `source_N/`-prefix; `*` mag ook submappen matchen.

De bronvolgorde bepaalt `source_0`, `source_1` enz. Bij `restore` worden die naamruimten apart gehouden, zodat twee projecten met een gelijknamig bestand niet botsen.

## Naar USB-schijf / andere harde schijf

Koppel de schijf aan via de bestandsbeheerder of `mount` en maak op de **aangekoppelde schijf** een map aan. Bijvoorbeeld:

```bash
mkdir -p /media/pi/BACKUPDISK/cbackup
cbackup config mijnbackup set usb /media/pi/BACKUPDISK/cbackup
cbackup run mijnbackup
```

Pas de exacte mountlocatie aan jouw systeem aan. CBackup controleert `/proc/self/mountinfo` en weigert een USB-kopie als de opslag niet echt gemount is; zo wordt een ongekoppelde schijf niet per ongeluk vervangen door een lokale map. De normale lokale kopie in `dest` blijft bestaan.

Verwijder de USB-locatie uit de configuratie:

```bash
cbackup config mijnbackup set usb ''
```

## Naar de cloud (Google Drive, OneDrive, Dropbox, S3, enz.)

Installeer/configureer eerst rclone:

```bash
rclone config
rclone listremotes
```

Stel dat je remote in rclone `mijncloud:` heet. Dan:

```bash
cbackup config mijnbackup set cloud 'mijncloud:backups'
cbackup run mijnbackup
```

Dat zet snapshots in `mijncloud:backups/mijnbackup/`. De app gebruikt `rclone copyto`; bestaande cloudbestanden worden niet automatisch verwijderd. Een mislukte upload kan via de volgende run of handmatig worden hervat:

```bash
cbackup replicate mijnbackup    # USB en cloud bijwerken
cbackup cloud-push mijnbackup   # volledige aanwezige lokale keten naar cloud
cbackup cloud-pull mijnbackup   # cloudarchieven terughalen naar lokale dest
```

**Privacy:** back-uparchieven zijn niet standaard versleuteld. Gebruik voor vertrouwelijke bestanden een rclone `crypt`-remote, aangemaakt via `rclone config`, en geef die crypt-remote op in `set cloud`. Bewaar de rclone-config en het crypt-wachtwoord veilig, onafhankelijk van deze back-up.

**LET OP: mirror + cloud:** de speciale mirrorstand gebruikt `rclone sync` uitsluitend op `<cloud>/<job>/mirror`. Daarbij worden bestanden die niet meer in de bronmirror staan **verwijderd** op die remote mirrorlocatie. Gebruik mirror niet als enige back-up tegen ransomware of per ongeluk verwijderen.

## Inplannen via terminal

Deze commando's schrijven systemd **gebruikersunits**. Voer ze uit als de gewone gebruiker die de bestanden kan lezen (niet met `sudo`).

```bash
cbackup schedule mijnbackup --every 10min
cbackup schedule mijnbackup --every 2h
cbackup schedule mijnbackup --daily 22:00
cbackup schedule mijnbackup --weekly Sun@02:00
cbackup schedule mijnbackup off
```

De laatste ingestelde planning vervangt de vorige. Intervalplanning start eenmaal kort na het opstarten van de **systemd-gebruikersmanager** en plant daarna de volgende run na afloop van de vorige. Kalenderplanning gebruikt de lokale tijdzone. Om gebruikersservices ook zonder actieve login te starten, is op veel systemen eenmalig nodig:

```bash
sudo loginctl enable-linger "$USER"
```

Controleer:

```bash
systemctl --user list-timers 'cbackup-*'
systemctl --user status cbackup-mijnbackup.timer
journalctl --user -u cbackup-mijnbackup.service -n 50
```

Er wordt bewust **geen automatische back-up bij afsluiten** beloofd. Tijdens shutdown kunnen USB-stations, netwerk of processen al niet beschikbaar zijn. Gebruik vaste intervallen en een aparte externe kopie voor meer betrouwbaarheid.

## Terugzetten en integriteit

```bash
cbackup list mijnbackup
cbackup verify mijnbackup 20261104_120001_000000_1234
cbackup restore mijnbackup 20261104_120001_000000_1234 --to "$HOME/Teruggezet"
```

Vervang de voorbeeld-ID door een ID uit `list`. De herstelmap mag **nog niet bestaan**. Bestanden worden teruggezet onder `Teruggezet/files/source_0/`, `source_1/` enz. Dit voorkomt overschrijven van je originele gegevens.

`verify` vergelijkt de SHA-256-hash van elk vereist archief met de manifestwaarde en controleert of de noodzakelijke herstelketen aanwezig is. Tijdens `restore` worden daarnaast de SHA-256-hashes van de herstelde **bestanden** gecontroleerd. `verify` is geen volledige uitpaktest: maak daarom regelmatig een echte proefrestore.

Wanneer een bestand is verwijderd, bestaat het niet meer in de doel-snapshot. Bij het terugzetten worden restbestanden uit tussenliggende archieven gefilterd, zodat een gemengde full/incremental/differential-keten correct wordt gereconstrueerd.

## Bewaarbeleid

`keep_chains` is het aantal **volledige ketens** dat lokaal overblijft. Een keten bestaat uit een full-back-up en alle daaropvolgende incremental/differential-back-ups tot de volgende full-back-up. De bewaarfase begint pas na een succesvolle externe replicatie. Standaard `3`; met `0` behoud je alles lokaal. Oude kopieën op USB/cloud worden **niet** automatisch verwijderd (behoud als extra veiligheidskopie, maar let op opslaggebruik).

```bash
cbackup config mijnbackup set keep_chains 5
```

## Beperkingen en veiligheidsaspecten

- Alleen gewone bestanden worden meegenomen; symlinks, speciale bestanden en lege mappen worden niet als afzonderlijke objecten bewaard. Geen volledige systeemback-up/bootbare clone.
- Dit is een back-upapp voor reguliere bestanden, geen transactionele snapshotservice. Databases en programma's die bestanden tijdens het kopiëren wijzigen vereisen een applicatiespecifieke export of een filesystem-snapshot.
- Het programma draait met de lees-/schrijfrechten van de huidige gebruiker. Systeembestanden vereisen aparte toegangsrechten, maar voer niet zomaar het hele programma als root uit.
- Beschadigde of gewijzigde archieven worden gedetecteerd met SHA-256, maar de manifests zijn **niet digitaal ondertekend**. SHA-256 is geen toegangsbeveiliging of versleuteling.
- Mirror weerspiegelt ook verwijderingen: gebruik daarnaast altijd een versiegebaseerde archiefback-up.
- Tijdens en rond stroomuitval blijft een gedeeltelijk archief met `.partial`-suffix mogelijk achter; het telt niet als geldig manifest. Herhaal de opdracht.
- Voor grote datasets wordt momenteel elk geselecteerd bestand gehasht om wijzigingen betrouwbaar te bepalen, ook als een back-up daarna weinig data toevoegt.
- Software is functioneel getest, maar niet professioneel beveiligingsgeaudit of getest op alle Raspberry Pi-distributies; voer vóór productiegebruik een eigen hersteltest uit.

## Integratietest

```bash
cmake -S . -B build && cmake --build build
./tests/integration.sh ./build/cbackup
```

De tests doen full, incremental, differential, gemengde ketens, restore, bestandsfilters, meerdere bronnen, mirror, integriteitscontrole en een gesimuleerde rclone-cloudupload/-download (geen echte cloudaccount nodig).

## Verdieping: officiële documentatie

- libarchive: https://www.libarchive.de/
- rclone copy: https://rclone.org/commands/rclone_copy/
- rclone crypt: https://rclone.org/crypt/
- systemd timers: https://www.freedesktop.org/software/systemd/man/latest/systemd.timer.html
