# CBackup 1.3 — Nederlandse snelstart

CBackup maakt back-ups van bestanden en mappen. Je kunt een volledige, incrementele, differentiële of spiegelback-up kiezen. Terugzetten werkt met een snapshot-ID en kan voor alles, één bestand of een map. Voor een herstel op de oorspronkelijke plek krijg je ook een ID waarmee je dat herstel weer ongedaan kunt maken.

## Installeren en beginnen

Linux, Raspberry Pi OS en macOS:

```sh
cd src
bash install.sh
```

Windows 10/11, vanuit PowerShell:

```powershell
cd src
.\install.ps1
```

Kies **Default** voor de eenvoudige installatie en geef je bron en opslaglocatie op. De installer controleert de compiler en benodigde bibliotheken, bouwt het programma en maakt de gekozen back-uptaak aan. **Advanced** laat je de instellingen in één keer kiezen. Op macOS moeten de Apple Command Line Tools en Homebrew beschikbaar zijn; de installer geeft aan wat je zo nodig eerst moet afronden.

De standaard is: automatisch starten bij aanmelden, elke **2 uur** een incrementele back-up, zstd-compressie en **90 dagen** bewaren (ongeveer drie maanden). De eerste back-up is volledig. Periodieke volledige back-ups zorgen ervoor dat oude ketens veilig kunnen worden opgeruimd. Bestanden uit een vereiste herstelketen kunnen daardoor iets langer bewaard blijven dan 90 dagen.

Engels is de standaardtaal. Kies Nederlands met:

```sh
cbackup language nl
cbackup help
cbackup settings
```

De taalkeuze vertaalt de hulp en beschrijving van instellingen. Sommige uitgebreide meldingen blijven Engels of Nederlands.

## Wat kun je bij Advanced kiezen?

Je kunt meerdere bronmappen of losse bestanden toevoegen, de opslaglocatie kiezen en het back-uptype, de compressie en het niveau instellen. Ook kun je bepalen hoe vaak een volledige back-up wordt gemaakt, hoeveel dagen en ketens je bewaart, en of je alle geschikte bestanden, instellingen of installatiebestanden wilt selecteren.

Verder kies je filters op extensie/patroon, eigen uitsluitingen, het overslaan van opnieuw installeerbare dependencies, USB- en cloudkopieën, starten bij aanmelden, controleren op wijzigingen na opslaan en het controle-interval. Planning kan handmatig, bij aanmelden, met een interval, dagelijks op een tijdstip of wekelijks op een dag met tijd. Versleuteling, de naam van de omgevingsvariabele voor het wachtwoord, de maximale loggrootte en de terminaltaal zijn ook instelbaar.

Een versleutelde taak blijft vanuit de installer zonder automatische planning totdat je het geheim veilig aan de scheduler hebt doorgegeven. De installer slaat het wachtwoord niet op. Een wachtwoord dat alleen in je terminal staat, is meestal niet beschikbaar voor een al draaiende achtergrondplanner.

## Later instellingen wijzigen

Je kunt de taak na installatie blijven aanpassen zonder opnieuw te installeren. Vervang `mijnbackup` door je eigen taaknaam en gebruik je eigen paden:

```sh
cbackup config mijnbackup show
cbackup settings
cbackup config mijnbackup set dest /pad/naar/backups
cbackup config mijnbackup add-source /pad/naar/project /pad/naar/instellingen.ini
cbackup config mijnbackup remove-source /pad/naar/oude-bron
cbackup config mijnbackup set mode auto
cbackup config mijnbackup set compression zstd:3
cbackup config mijnbackup set keep_days 90
cbackup config mijnbackup set profile settings
cbackup config mijnbackup set profile all
cbackup config mijnbackup add-exclude prive-map '*.tmp'
cbackup config mijnbackup clear-exclude
cbackup config mijnbackup add-ext cpp h json
cbackup config mijnbackup clear-ext
cbackup config mijnbackup set usb /pad/op/aangekoppelde/usb-schijf
cbackup config mijnbackup set cloud 'mijnremote:backups'
cbackup schedule mijnbackup --every 2h
cbackup schedule mijnbackup --daily 22:00
cbackup schedule mijnbackup --weekly Sun@02:00
cbackup schedule mijnbackup off
cbackup config mijnbackup set autostart false
```

Elk planningscommando vervangt de vorige planning. Voor een cloudkopie moet je eerst `rclone config` uitvoeren. Een andere `dest` kiezen verplaatst bestaande archieven niet; je selecteert daarmee welke opslaglocatie volgende opdrachten gebruiken. De bestemming moet buiten je bronmappen liggen.

`node_modules`, `.venv` en andere veelgebruikte dependency-/cachemappen worden standaard overgeslagen. Projectinstellingen en lockfiles blijven geschikt voor back-up. Bewaar bijvoorbeeld `package.json` én `package-lock.json`, zodat je later met `npm ci` de vastgelegde versies kunt installeren. Dat werkt alleen zolang de benodigde pakketten en passende runtime beschikbaar zijn. Je kunt deze uitsluiting uitzetten met `cbackup config mijnbackup set exclude_dependencies false`.

## Back-up maken, bekijken en terugzetten

```sh
cbackup run mijnbackup
cbackup ids mijnbackup
cbackup changes mijnbackup SNAPSHOT_ID
cbackup verify mijnbackup SNAPSHOT_ID
cbackup restore mijnbackup SNAPSHOT_ID --to /pad/naar/nieuwe-herstelmap
cbackup restore mijnbackup SNAPSHOT_ID --only rapport.docx --to /pad/naar/nieuwe-bestandsmap
cbackup restore mijnbackup SNAPSHOT_ID --only project/submap --to /pad/naar/nieuwe-mappenmap
cbackup restore mijnbackup SNAPSHOT_ID --original --dry-run
cbackup restore mijnbackup SNAPSHOT_ID --original --yes
cbackup undo-list mijnbackup
cbackup undo mijnbackup HERSTEL_ID --dry-run
cbackup undo mijnbackup HERSTEL_ID --yes
cbackup log mijnbackup --tail 20
```

Voor `--to` moet je een nieuwe map opgeven. De bestanden staan daarna onder `files/source_0/`, `files/source_1/`, enzovoort. `--original` zet bestanden op hun oorspronkelijke plek terug en bewaart overschreven versies. Extra bestanden die nu bestaan, blijven staan. Het **herstel-ID voor undo** is een ander ID dan het snapshot-ID. Undo weigert bestanden te overschrijven die je na het herstel opnieuw hebt veranderd.

## Meldingen, proefherstel, opslagbudget en offline kopie

Deze vier functies zijn ingebouwd en tijdens Geavanceerd installeren instelbaar. Meldingen staan standaard aan, een proefherstel gebeurt na een succesvolle run zodra de zeven dagen verstreken zijn, en een offlineherinnering wordt na veertien dagen gegeven. Het opslagbudget staat standaard op onbeperkt.

```sh
cbackup health mijnbackup
cbackup alerts mijnbackup
cbackup config mijnbackup set notifications true
cbackup config mijnbackup set desktop_notifications true
cbackup config mijnbackup set stale_hours 24
cbackup config mijnbackup set notify_repeat_hours 24
cbackup config mijnbackup set test_restore_days 7
cbackup config mijnbackup set test_restore_max_mb 64
cbackup test-restore mijnbackup
cbackup run mijnbackup --dry-run
cbackup config mijnbackup set storage_quota_mb 20480
cbackup config mijnbackup set offline_reminder_days 14
cbackup offline-confirm mijnbackup SNAPSHOT_ID
```

Een mislukte run wordt vastgelegd. `health` meldt ook wanneer een succesvolle run te lang uitblijft. De status en meldingen blijven bewaard onder `~/.config/cbackup/state/NAAM/`, apart van de back-upschijf. Dezelfde melding wordt standaard hoogstens eens per 24 uur herhaald. `notifications false` schakelt de meldingen uit; `health` blijft problemen tonen. Het commando geeft afsluitcode 0 bij een gezonde status, 2 als aandacht nodig is en 1 bij een uitvoeringsfout.

Nieuwe planning maakt ook een lichte gezondheidscontrole per uur. Voor bestaande planning: `cbackup schedule mijnbackup on`. `schedule off` verwijdert beide taken. Dit vereist een werkende scheduler en gebruikerssessie; op een uitgeschakelde computer kan geen melding verschijnen. Optionele bureaubladmeldingen gebruiken Windows msg.exe, macOS osascript of Linux notify-send. Zonder die voorziening blijft de melding in het lokale logboek beschikbaar.

Het proefherstel kiest telkens een volgend bestand van maximaal `test_restore_max_mb` MiB, herstelt het in een tijdelijke map en controleert de grootte en SHA-256. Die map wordt opgeruimd. Een mislukt proefherstel blijft als probleem zichtbaar tot een test wel slaagt. Als alle bestanden te groot zijn, wordt dat gemeld; verhoog dan de limiet. Versleutelde archieven vereisen het wachtwoord en genoeg tijdelijke ruimte. Mirror heeft geen archiefketen en wordt niet automatisch op deze manier getest. Eén voorbeeldbestand geeft geen garantie voor de hele back-up.

Het opslagbudget wordt op de echte archiefgrootte gecontroleerd. Oude ketens worden alleen verwijderd wanneer het bewaarbeleid dat toestaat. Past een nieuwe back-up daarna niet, dan stopt die run en blijven de bestaande herstelketens behouden. De schatting zie je met `--dry-run`. Het budget telt lokale archieven, manifests en mirror; tijdelijke bestanden, logboeken, undo-kopieën en externe kopieën vallen erbuiten. Er is tijdens het maken dus extra vrije ruimte nodig. `storage_quota_mb 0` schakelt het budget uit.

Maak de offline kopie zelf en koppel die los voordat je `offline-confirm` uitvoert. Dit commando controleert de lokale herstelketen en registreert jouw bevestiging; het maakt of controleert de losse kopie niet. Een gewone USB-kopie geldt niet automatisch als offline. `offline_reminder_days 0` schakelt herinneringen uit; `test_restore_days 0` schakelt automatisch proefherstel uit.

## Wat is gecontroleerd?

De Windows x64-code is in deze sessie succesvol gecompileerd en **25 groepen functionele tests zijn tijdens ontwikkeling geslaagd (19 algemeen + 6 onderhoud)** met het echte Windows-programma. Ook de laatste build, met de extra ruimteschatting bij mirror --dry-run, compileert. Windows Application Control blokkeerde de laatste hertest met fout 4551, ook buiten de sandbox; die laatste hertest is dus nog niet bevestigd. Daarbij zijn onder meer de back-uptypes, verwijderde bestanden, Unicode-bestandsnamen, gedeeltelijk herstel, undo en conflicten, versleuteling en beschadiging, bewaarbeleid, compressie en de watcher getest. De extra controles testen begrensde meldingen, verlopen successtatus, een onbereikbare bestemming, offlineherinneringen en bevestiging, proefherstel met roterende bestanden, opruimen van tijdelijke data, beschadigde archieven, voorbeeldlimieten en veilige opslagquota. De installers zijn op Bash-syntax en PowerShell-parsing gecontroleerd. De installers zijn hier niet uitgevoerd en er zijn geen achtergrondtaken op deze computer aangemaakt.

Automatische planning via de native scheduler, cloudaccounts en kopieën naar een verwisselbare schijf moeten nog op de bedoelde computer worden getest. Linux, macOS en Raspberry Pi zijn hier nog niet functioneel getest. De meegeleverde CI is ingesteld voor Windows, Linux en macOS, maar is in deze sessie niet uitgevoerd.

Dit is een back-up voor gewone bestanden, geen opstartbare schijfkopie of volledige systeemhersteloplossing. Lees voor versleuteling, platformbeperkingen en meer voorbeelden de [uitgebreide handleiding](README.md). Maak vóór gebruik met belangrijke gegevens een echt proefherstel naar een nieuwe map.
