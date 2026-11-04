#pragma once
// UTF-8 CLI help catalog. Command names and config keys are deliberately stable.
// Legacy diagnostics fall back to English; this is not a full gettext catalog.
#include <algorithm>
#include <cctype>
#include <ostream>
#include <string>
#include <vector>

namespace cb_i18n {
struct Catalog {
    const char *code, *name;
    const char *intro, *defaults, *sources, *dest, *mode, *compression;
    const char *retention, *schedule, *autostart, *filters, *dependencies;
    const char *encryption, *profile, *logging, *restore;
};
inline const std::vector<Catalog> &catalogs() {
    static const std::vector<Catalog> data = {
        {"en", "English",
         "File backups and recovery by snapshot ID.",
         "Defaults: every 2 hours, incremental chains, zstd, 90-day retention, automatic startup.",
         "Files or folders to protect; multiple sources are allowed.",
         "Local backup folder; USB adds an external copy; cloud uses an rclone remote.",
         "auto: incremental with periodic full backups; full: all files; incremental: since previous; differential: since full; mirror: current copy, including deletions.",
         "Archive format and compression level; 0 selects the format default.",
         "Minimum recoverable days and minimum full chains; necessary ancestors remain until their whole chain can expire. keep_days=0 uses only chain count; keep_chains=0 keeps everything.",
         "Manual, interval, daily, weekly or startup; times use the local timezone.",
         "Enable or disable startup of scheduled background backups. Watch mode polls for changes; choose the interval in seconds.",
         "Extension/include filters select files; exclude patterns take precedence. Custom exclusions are optional.",
         "Skip reinstallable dependency folders, keep manifests and lockfiles. Restore exact versions with the saved lockfile; missing lockfiles cannot guarantee exact versions.",
         "none or aes-256-gcm. Supply the password through password_env; losing it makes encrypted backups unrecoverable. Do not put passwords in command arguments.",
         "all: every selected file; settings: configuration files; installers: installation files and dependency manifests. Profiles filter files, not operating system images.",
         "Read job logs; log_max_mb bounds log rotation. Snapshot IDs identify restore points; changes lists added, modified and deleted paths.",
         "Restore all files or --only a file/folder. --original preserves overwritten versions; undo reverses the recorded restore. Verify and try --dry-run first."},
        {"nl", "Nederlands",
         "Bestandsback-ups en herstel via een snapshot-ID.",
         "Standaard: elke 2 uur, incrementele ketens, zstd, 90 dagen bewaren, automatisch starten.",
         "Bestanden of mappen om te beschermen; meerdere bronnen zijn mogelijk.",
         "Lokale back-upmap; USB voegt een externe kopie toe; cloud gebruikt een rclone-remote.",
         "auto: incrementeel met periodiek volledig; full: alle bestanden; incremental: sinds vorige; differential: sinds volledig; mirror: actuele kopie inclusief verwijderingen.",
         "Archieftype en compressieniveau; 0 gebruikt het standaardniveau van het type.",
         "Minimaal herstelbare dagen en volledige ketens; benodigde voorgangers blijven tot de hele keten mag vervallen. keep_days=0 gebruikt alleen het ketenaantal; keep_chains=0 bewaart alles.",
         "Handmatig, interval, dagelijks, wekelijks of bij opstarten; tijden zijn lokaal.",
         "Automatisch starten van geplande achtergrondback-ups aan/uit. Watch controleert periodiek wijzigingen; kies het interval in seconden.",
         "Extensie- en include-filters selecteren bestanden; uitsluitpatronen gaan voor. Eigen uitsluitingen zijn optioneel.",
         "Herinstalleerbare afhankelijkheidsmappen overslaan, manifests en lockfiles bewaren. Exacte versies herstellen vereist het opgeslagen lockfile; zonder lockfile geen versiegarantie.",
         "none of aes-256-gcm. Wachtwoord via password_env; bij verlies zijn versleutelde back-ups niet herstelbaar. Zet wachtwoorden niet in opdrachtargumenten.",
         "all: alle geselecteerde bestanden; settings: configuratie; installers: installatiebestanden en afhankelijkheidsmanifests. Dit maakt geen besturingssysteemimage.",
         "Joblog lezen; log_max_mb begrenst logrotatie. Snapshot-ID's geven herstelpunten; changes toont toegevoegde, gewijzigde en verwijderde paden.",
         "Alles herstellen of --only voor een bestand/map. --original bewaart overschreven versies; undo draait de vastgelegde herstelactie terug. Controleer eerst met verify en --dry-run."},
        {"es", "Español",
         "Copias de archivos y recuperación mediante ID de instantánea.",
         "Predeterminado: cada 2 horas, cadenas incrementales, zstd, conservar 90 días, inicio automático.",
         "Archivos o carpetas que proteger; admite varios orígenes.",
         "Carpeta local; USB añade una copia externa; cloud utiliza un destino rclone.",
         "auto: incremental con completas periódicas; full: todos; incremental: desde la anterior; differential: desde la completa; mirror: copia actual con eliminaciones.",
         "Formato y nivel de compresión; 0 usa el nivel predeterminado.",
         "Días recuperables y cadenas completas mínimos; los antecesores necesarios se conservan hasta poder eliminar la cadena. keep_days=0 usa solo cadenas; keep_chains=0 conserva todo.",
         "Manual, por intervalo, diario, semanal o al iniciar; hora local.",
         "Activar/desactivar copias programadas al iniciar. Watch comprueba cambios periódicamente; intervalo en segundos.",
         "Extensiones e inclusiones seleccionan archivos; las exclusiones tienen prioridad y son opcionales.",
         "Omitir dependencias reinstalables, conservar manifiestos y archivos de bloqueo. Las versiones exactas requieren el archivo de bloqueo guardado.",
         "none o aes-256-gcm. Contraseña mediante password_env; si se pierde no se puede recuperar. No escriba contraseñas como argumentos.",
         "all: todos los seleccionados; settings: configuración; installers: instaladores y manifiestos. No crea imágenes del sistema operativo.",
         "Consultar registros; log_max_mb limita su rotación. Los ID identifican puntos de restauración; changes muestra archivos añadidos, modificados y eliminados.",
         "Restaurar todo o usar --only para archivo/carpeta. --original guarda versiones reemplazadas; undo revierte la restauración registrada. Pruebe verify y --dry-run."},
        {"de", "Deutsch",
         "Dateisicherungen und Wiederherstellung per Snapshot-ID.",
         "Standard: alle 2 Stunden, inkrementelle Ketten, zstd, 90 Tage Aufbewahrung, Autostart.",
         "Zu sichernde Dateien oder Ordner; mehrere Quellen sind möglich.",
         "Lokaler Sicherungsordner; USB ergänzt eine externe Kopie; cloud verwendet ein rclone-Ziel.",
         "auto: inkrementell mit regelmäßiger Vollsicherung; full: alle Dateien; incremental: seit letzter Sicherung; differential: seit Vollsicherung; mirror: aktuelle Kopie einschließlich Löschungen.",
         "Archivformat und Komprimierungsstufe; 0 verwendet den Standardwert.",
         "Mindestanzahl wiederherstellbarer Tage und vollständiger Ketten; benötigte Vorgänger bleiben bis zum Ablauf der ganzen Kette. keep_days=0 nutzt nur die Kettenanzahl; keep_chains=0 behält alles.",
         "Manuell, Intervall, täglich, wöchentlich oder beim Start; lokale Zeit.",
         "Geplante Hintergrundsicherungen beim Start aktivieren/deaktivieren. Watch prüft Änderungen regelmäßig; Intervall in Sekunden.",
         "Dateiendungen und Einschlussmuster wählen Dateien aus; optionale Ausschlussmuster haben Vorrang.",
         "Neu installierbare Abhängigkeitsordner auslassen, Manifeste und Sperrdateien behalten. Exakte Versionen erfordern die gespeicherte Sperrdatei.",
         "none oder aes-256-gcm. Passwort über password_env; ohne Passwort keine Wiederherstellung. Passwörter nicht als Befehlsargument angeben.",
         "all: alle ausgewählten Dateien; settings: Konfiguration; installers: Installationsdateien und Manifeste. Kein Betriebssystemabbild.",
         "Protokolle lesen; log_max_mb begrenzt die Rotation. Snapshot-IDs bezeichnen Wiederherstellungspunkte; changes zeigt neue, geänderte und gelöschte Pfade.",
         "Alles oder mit --only Datei/Ordner wiederherstellen. --original bewahrt ersetzte Versionen; undo macht die protokollierte Wiederherstellung rückgängig. Zuerst verify und --dry-run."},
        {"fr", "Français",
         "Sauvegardes de fichiers et restauration par identifiant.", "Par défaut : toutes les 2 heures, incrémental, zstd, 90 jours, démarrage automatique.",
         "Fichiers ou dossiers à protéger, plusieurs sources possibles.", "Dossier local ; USB ajoute une copie externe ; cloud utilise rclone.",
         "auto : incrémental avec sauvegardes complètes périodiques ; full : tout ; incremental : depuis la précédente ; differential : depuis la complète ; mirror : copie actuelle avec suppressions.",
         "Format et niveau de compression ; 0 utilise le niveau par défaut.", "Durée et nombre minimal de chaînes complètes ; les ancêtres nécessaires sont conservés. keep_days=0 utilise seulement les chaînes ; keep_chains=0 conserve tout.",
         "Manuel, intervalle, quotidien, hebdomadaire ou au démarrage ; heure locale.", "Activer le démarrage des tâches planifiées. Watch recherche périodiquement les changements ; intervalle en secondes.",
         "Extensions et inclusions sélectionnent les fichiers ; les exclusions facultatives sont prioritaires.", "Ignorer les dépendances réinstallables, conserver manifestes et fichiers de verrouillage ; versions exactes uniquement avec verrouillage enregistré.",
         "none ou aes-256-gcm ; mot de passe via password_env. Sans ce mot de passe, restauration impossible ; ne pas le passer comme argument.",
         "all : tout ; settings : configuration ; installers : installateurs et manifestes. Ce ne sont pas des images système.",
         "Journaux avec rotation limitée par log_max_mb ; changes indique les ajouts, modifications et suppressions pour les identifiants.",
         "Tout restaurer ou --only fichier/dossier. --original conserve les versions remplacées ; undo annule la restauration enregistrée. Essayer verify et --dry-run."},
        {"pt", "Português",
         "Cópias de arquivos e recuperação por ID.", "Padrão: a cada 2 horas, incremental, zstd, 90 dias, início automático.",
         "Arquivos ou pastas a proteger; várias origens permitidas.", "Pasta local; USB adiciona cópia externa; cloud usa rclone.",
         "auto: incremental com completas periódicas; full: tudo; incremental: desde a anterior; differential: desde a completa; mirror: cópia atual com exclusões.",
         "Formato e nível de compressão; 0 usa o padrão.", "Dias recuperáveis e cadeias completas mínimos; antecessores necessários permanecem. keep_days=0 usa somente cadeias; keep_chains=0 guarda tudo.",
         "Manual, intervalo, diário, semanal ou na inicialização; horário local.", "Ativar tarefas no início. Watch verifica alterações periodicamente; intervalo em segundos.",
         "Extensões e inclusões selecionam arquivos; exclusões opcionais têm prioridade.", "Ignorar dependências reinstaláveis e guardar manifestos e arquivos de bloqueio; versões exatas precisam do bloqueio salvo.",
         "none ou aes-256-gcm; senha via password_env. Sem a senha não há recuperação; não use senhas como argumentos.",
         "all: tudo; settings: configuração; installers: instaladores e manifestos. Não são imagens do sistema.",
         "Consultar registros; log_max_mb limita a rotação. changes mostra adições, alterações e exclusões por ID.",
         "Restaurar tudo ou --only arquivo/pasta. --original guarda versões substituídas; undo desfaz a restauração registrada. Teste verify e --dry-run."},
        {"it", "Italiano",
         "Backup di file e ripristino tramite ID.", "Predefiniti: ogni 2 ore, incrementale, zstd, 90 giorni, avvio automatico.",
         "File o cartelle da proteggere; sono consentite più sorgenti.", "Cartella locale; USB aggiunge una copia esterna; cloud usa rclone.",
         "auto: incrementale con completi periodici; full: tutto; incremental: dal precedente; differential: dal completo; mirror: copia attuale con eliminazioni.",
         "Formato e livello di compressione; 0 usa il valore predefinito.", "Giorni ripristinabili e catene complete minimi; gli antenati necessari restano. keep_days=0 usa solo le catene; keep_chains=0 conserva tutto.",
         "Manuale, intervallo, giornaliero, settimanale o all'avvio; ora locale.", "Attivare i processi all'avvio. Watch controlla periodicamente le modifiche; intervallo in secondi.",
         "Estensioni e inclusioni selezionano file; le esclusioni facoltative hanno precedenza.", "Saltare dipendenze reinstallabili, conservare manifesti e file di blocco; versioni esatte richiedono il blocco salvato.",
         "none o aes-256-gcm; password tramite password_env. Senza password nessun ripristino; non passarla come argomento.",
         "all: tutto; settings: configurazione; installers: installatori e manifesti. Non crea immagini del sistema.",
         "Leggere i registri; log_max_mb limita la rotazione. changes mostra aggiunte, modifiche e rimozioni per ID.",
         "Ripristinare tutto o --only file/cartella. --original conserva versioni sostituite; undo annulla il ripristino registrato. Provare verify e --dry-run."},
        {"ru", "Русский",
         "Резервное копирование файлов и восстановление по ID.", "По умолчанию: каждые 2 часа, инкрементные цепочки, zstd, хранение 90 дней, автозапуск.",
         "Защищаемые файлы или папки; можно выбрать несколько источников.", "Локальная папка; USB добавляет внешнюю копию; cloud использует rclone.",
         "auto: инкрементные с периодическими полными; full: всё; incremental: после предыдущей; differential: после полной; mirror: актуальная копия с удалениями.",
         "Формат и уровень сжатия; 0 выбирает стандартный уровень.", "Минимум дней восстановления и полных цепочек; нужные предшественники сохраняются. keep_days=0 учитывает только цепочки; keep_chains=0 сохраняет всё.",
         "Вручную, интервал, ежедневно, еженедельно или при запуске; местное время.", "Включить запуск фоновых заданий. Watch периодически проверяет изменения; интервал в секундах.",
         "Расширения и включения выбирают файлы; необязательные исключения имеют приоритет.", "Пропускать переустанавливаемые зависимости, сохранять манифесты и lock-файлы; точные версии требуют сохранённого lock-файла.",
         "none или aes-256-gcm; пароль через password_env. Без пароля восстановление невозможно; не передавайте его в аргументах.",
         "all: всё; settings: настройки; installers: установщики и манифесты. Это не образ операционной системы.",
         "Просмотр журналов; log_max_mb ограничивает ротацию. changes показывает добавленные, изменённые и удалённые пути по ID.",
         "Восстановить всё или --only файл/папку. --original сохраняет заменяемые версии; undo отменяет записанное восстановление. Сначала verify и --dry-run."},
        {"uk", "Українська",
         "Резервування файлів і відновлення за ID.", "Типово: кожні 2 години, інкрементні ланцюжки, zstd, зберігання 90 днів, автозапуск.",
         "Файли або папки для захисту; можна обрати кілька джерел.", "Локальна папка; USB додає зовнішню копію; cloud використовує rclone.",
         "auto: інкрементні з періодичними повними; full: усе; incremental: після попередньої; differential: після повної; mirror: поточна копія з видаленнями.",
         "Формат і рівень стиснення; 0 обирає типовий рівень.", "Мінімум днів відновлення та повних ланцюжків; потрібні попередники залишаються. keep_days=0 враховує лише ланцюжки; keep_chains=0 зберігає все.",
         "Вручну, інтервал, щодня, щотижня або під час запуску; місцевий час.", "Увімкнути фонові завдання під час запуску. Watch періодично перевіряє зміни; інтервал у секундах.",
         "Розширення та включення обирають файли; необов'язкові виключення мають пріоритет.", "Пропускати перевстановлювані залежності, зберігати маніфести й lock-файли; точні версії потребують збереженого lock-файлу.",
         "none або aes-256-gcm; пароль через password_env. Без пароля відновлення неможливе; не передавайте його в аргументах.",
         "all: усе; settings: налаштування; installers: інсталятори й маніфести. Це не образ операційної системи.",
         "Перегляд журналів; log_max_mb обмежує ротацію. changes показує додані, змінені й видалені шляхи за ID.",
         "Відновити все або --only файл/папку. --original зберігає замінені версії; undo скасовує записане відновлення. Спочатку verify і --dry-run."},
        {"pl", "Polski",
         "Kopie plików i odzyskiwanie według ID.", "Domyślnie: co 2 godziny, łańcuchy przyrostowe, zstd, 90 dni, automatyczny start.",
         "Chronione pliki lub foldery; można podać wiele źródeł.", "Folder lokalny; USB dodaje kopię zewnętrzną; cloud używa rclone.",
         "auto: przyrostowe z okresowymi pełnymi; full: wszystko; incremental: od poprzedniej; differential: od pełnej; mirror: aktualna kopia z usunięciami.",
         "Format i poziom kompresji; 0 wybiera poziom domyślny.", "Minimum dni odtwarzania i pełnych łańcuchów; wymagani poprzednicy zostają. keep_days=0 uwzględnia tylko łańcuchy; keep_chains=0 zachowuje wszystko.",
         "Ręcznie, interwał, codziennie, co tydzień lub przy starcie; czas lokalny.", "Włączyć zadania przy starcie. Watch okresowo sprawdza zmiany; interwał w sekundach.",
         "Rozszerzenia i włączenia wybierają pliki; opcjonalne wykluczenia mają pierwszeństwo.", "Pomijać odtwarzalne zależności, zachować manifesty i pliki blokad; dokładne wersje wymagają zapisanej blokady.",
         "none lub aes-256-gcm; hasło przez password_env. Bez hasła odzyskanie niemożliwe; nie podawaj go w argumentach.",
         "all: wszystko; settings: konfiguracja; installers: instalatory i manifesty. Nie tworzy obrazu systemu.",
         "Odczyt dzienników; log_max_mb ogranicza rotację. changes pokazuje dodane, zmienione i usunięte ścieżki według ID.",
         "Odtworzyć wszystko lub --only plik/folder. --original zachowuje zastępowane wersje; undo cofa zapisane odtworzenie. Najpierw verify i --dry-run."},
        {"zh", "中文",
         "文件备份和通过快照 ID 恢复。", "默认：每 2 小时增量备份，zstd，保留 90 天，自动启动。",
         "需要保护的文件或文件夹；可设置多个来源。", "本地备份目录；USB 增加外部副本；cloud 使用 rclone。",
         "auto：增量并定期完整备份；full：全部；incremental：自上次备份；differential：自完整备份；mirror：包含删除的当前镜像。",
         "压缩格式和级别；0 使用格式默认值。", "可恢复天数和完整备份链的最低数量；所需前序备份会保留。keep_days=0 仅按链数；keep_chains=0 全部保留。",
         "手动、间隔、每天、每周或启动时；使用本地时间。", "启用或禁用计划任务自动启动。Watch 定期检查变化；间隔单位为秒。",
         "扩展名和包含规则选择文件；可选的排除规则优先。", "跳过可重新安装的依赖目录，保留清单和锁文件；精确版本需要已保存的锁文件。",
         "none 或 aes-256-gcm；通过 password_env 提供密码。丢失密码则无法恢复；不要将密码放入命令参数。",
         "all：全部；settings：配置；installers：安装文件和清单。这不是操作系统镜像。",
         "查看日志；log_max_mb 限制日志轮换。changes 按 ID 显示新增、修改和删除的路径。",
         "恢复全部或用 --only 选择文件/目录。--original 保留被覆盖的版本；undo 撤销已记录的恢复。先尝试 verify 和 --dry-run。"},
        {"ja", "日本語",
         "ファイルのバックアップと ID による復元。", "初期設定：2 時間ごと、増分、zstd、90 日保持、自動起動。",
         "保護するファイルやフォルダー。複数指定できます。", "ローカル保存先。USB は外部コピー、cloud は rclone を使用します。",
         "auto：増分と定期的な完全保存。full：全体。incremental：前回以降。differential：完全保存以降。mirror：削除も反映した現在のコピー。",
         "圧縮形式とレベル。0 は形式の既定値です。", "復元可能日数と完全チェーンの最低数。必要な先行保存は保持されます。keep_days=0 はチェーン数のみ、keep_chains=0 はすべて保持。",
         "手動、間隔、毎日、毎週、起動時。時刻はローカル時間です。", "起動時の予定タスクを有効・無効にします。Watch は定期的に変更を確認。間隔は秒です。",
         "拡張子と包含規則で選択。任意の除外規則を優先します。", "再インストール可能な依存フォルダーを省き、マニフェストとロックファイルを保存。正確なバージョンにはロックが必要です。",
         "none または aes-256-gcm。password_env でパスワードを指定。紛失すると復元できません。引数にパスワードを記載しないでください。",
         "all：すべて。settings：設定。installers：インストーラーとマニフェスト。OS イメージではありません。",
         "ログを表示。log_max_mb はローテーション上限。changes は ID ごとの追加・変更・削除を表示します。",
         "全体または --only でファイル・フォルダーを復元。--original は上書き前を保存。undo は記録された復元を取り消します。まず verify と --dry-run。"},
        {"ko", "한국어",
         "파일 백업 및 ID로 복구.", "기본값: 2시간마다 증분 백업, zstd, 90일 보관, 자동 시작.",
         "보호할 파일 또는 폴더. 여러 원본을 지정할 수 있습니다.", "로컬 폴더. USB는 외부 복사본, cloud는 rclone을 사용합니다.",
         "auto: 증분 및 주기적 전체; full: 전체; incremental: 이전 이후; differential: 전체 이후; mirror: 삭제를 반영하는 현재 사본.",
         "압축 형식과 수준. 0은 기본 수준입니다.", "최소 복구 가능 일수와 전체 체인 수. 필요한 이전 백업은 유지합니다. keep_days=0은 체인 수만 적용, keep_chains=0은 모두 보관합니다.",
         "수동, 간격, 매일, 매주 또는 시작 시. 현지 시간 사용.", "시작 시 예약 작업 활성화/해제. Watch는 주기적으로 변경 확인. 간격은 초 단위입니다.",
         "확장자와 포함 규칙으로 선택하며 선택적 제외 규칙이 우선합니다.", "재설치 가능한 종속 폴더는 생략하고 매니페스트와 잠금 파일을 저장합니다. 정확한 버전에는 저장된 잠금 파일이 필요합니다.",
         "none 또는 aes-256-gcm. password_env로 암호 지정. 암호 분실 시 복구 불가. 명령 인수에 암호를 넣지 마세요.",
         "all: 전체; settings: 설정; installers: 설치 파일과 매니페스트. 운영체제 이미지는 아닙니다.",
         "로그 확인. log_max_mb는 로그 순환 크기. changes는 ID별 추가, 변경, 삭제 경로를 표시합니다.",
         "전체 또는 --only 파일/폴더 복구. --original은 덮어쓴 버전을 보관하고 undo는 기록된 복구를 취소합니다. 먼저 verify 및 --dry-run."},
        {"hi", "हिन्दी",
         "फ़ाइल बैकअप और ID से पुनर्स्थापना।", "डिफ़ॉल्ट: हर 2 घंटे, वृद्धिशील श्रृंखला, zstd, 90 दिन, स्वतः शुरुआत।",
         "सुरक्षित करने वाली फ़ाइलें या फ़ोल्डर; कई स्रोत संभव हैं।", "स्थानीय फ़ोल्डर; USB बाहरी प्रति जोड़ता है; cloud में rclone।",
         "auto: वृद्धिशील और समय-समय पर पूर्ण; full: सभी; incremental: पिछले के बाद; differential: पूर्ण के बाद; mirror: हटाने सहित वर्तमान प्रति।",
         "संपीड़न प्रारूप और स्तर; 0 से डिफ़ॉल्ट स्तर।", "न्यूनतम पुनर्स्थापना दिन और पूर्ण श्रृंखलाएँ; आवश्यक पुराने बैकअप रखे जाते हैं। keep_days=0 केवल श्रृंखला गिनता है; keep_chains=0 सभी रखता है।",
         "मैन्युअल, अंतराल, दैनिक, साप्ताहिक या शुरुआत पर; स्थानीय समय।", "शुरुआत पर निर्धारित कार्य चालू/बंद। Watch समय-समय पर बदलाव जाँचता है; अंतराल सेकंड में।",
         "एक्सटेंशन और समावेशन फ़ाइलें चुनते हैं; वैकल्पिक बहिष्करण पहले लागू होते हैं।", "फिर स्थापित हो सकने वाली निर्भरताएँ छोड़ें, मैनिफ़ेस्ट और लॉकफ़ाइल रखें; ठीक वही संस्करण लॉकफ़ाइल से मिलते हैं।",
         "none या aes-256-gcm; password_env से पासवर्ड। पासवर्ड खोने पर पुनर्स्थापना असंभव; इसे कमांड तर्क में न दें।",
         "all: सभी; settings: कॉन्फ़िगरेशन; installers: इंस्टॉलर और मैनिफ़ेस्ट। यह ऑपरेटिंग सिस्टम इमेज नहीं है।",
         "लॉग देखें; log_max_mb रोटेशन सीमा। changes ID के अनुसार जोड़े, बदले और हटाए गए पथ दिखाता है।",
         "सब या --only फ़ाइल/फ़ोल्डर बहाल करें। --original पुरानी प्रतियाँ रखता है; undo दर्ज बहाली उलटता है। पहले verify और --dry-run।"},
        {"ar", "العربية",
         "نسخ الملفات واستعادتها بمعرّف اللقطة.", "الافتراضي: كل ساعتين، نسخ تزايدي، zstd، حفظ 90 يوماً، بدء تلقائي.",
         "الملفات أو المجلدات المطلوب حمايتها؛ يمكن اختيار مصادر متعددة.", "مجلد محلي؛ USB يضيف نسخة خارجية؛ cloud يستخدم rclone.",
         "auto: تزايدي مع كامل دوري؛ full: الكل؛ incremental: منذ السابق؛ differential: منذ الكامل؛ mirror: نسخة حالية تشمل الحذف.",
         "صيغة الضغط ومستواه؛ 0 للمستوى الافتراضي.", "الحد الأدنى لأيام الاستعادة والسلاسل الكاملة؛ تبقى النسخ السابقة اللازمة. keep_days=0 يحسب السلاسل فقط؛ keep_chains=0 يحفظ الكل.",
         "يدوي أو فترة أو يومي أو أسبوعي أو عند البدء؛ بالتوقيت المحلي.", "تفعيل المهام المجدولة عند البدء. Watch يفحص التغييرات دورياً؛ الفترة بالثواني.",
         "الامتدادات وأنماط التضمين تختار الملفات؛ لأنماط الاستبعاد الاختيارية الأولوية.", "تجاهل الاعتماديات القابلة لإعادة التثبيت وحفظ بياناتها وملفات القفل؛ الإصدارات الدقيقة تتطلب ملف القفل المحفوظ.",
         "none أو aes-256-gcm؛ كلمة المرور عبر password_env. فقدانها يمنع الاستعادة؛ لا تضعها في معاملات الأمر.",
         "all: الكل؛ settings: الإعدادات؛ installers: ملفات التثبيت والبيانات. ليست صورة لنظام التشغيل.",
         "عرض السجلات؛ log_max_mb يحد تدويرها. changes يعرض المسارات المضافة والمعدلة والمحذوفة حسب المعرّف.",
         "استعادة الكل أو --only لملف/مجلد. --original يحفظ الإصدارات المستبدلة؛ undo يعكس الاستعادة المسجلة. جرّب verify و --dry-run أولاً."},
        {"bn", "বাংলা",
         "ফাইল ব্যাকআপ এবং ID দিয়ে পুনরুদ্ধার।", "ডিফল্ট: প্রতি ২ ঘণ্টায়, ইনক্রিমেন্টাল, zstd, ৯০ দিন রাখা, স্বয়ংক্রিয় চালু।",
         "সুরক্ষিত ফাইল বা ফোল্ডার; একাধিক উৎস সম্ভব।", "স্থানীয় ফোল্ডার; USB বাহ্যিক কপি যোগ করে; cloud এ rclone।",
         "auto: পর্যায়ক্রমিক পূর্ণসহ ইনক্রিমেন্টাল; full: সব; incremental: আগের পর; differential: পূর্ণের পর; mirror: মুছে ফেলা সহ বর্তমান কপি।",
         "কম্প্রেশন ধরন ও মাত্রা; 0 ডিফল্ট মাত্রা।", "ন্যূনতম পুনরুদ্ধারযোগ্য দিন ও পূর্ণ চেইন; প্রয়োজনীয় পূর্বসূরি থাকে। keep_days=0 শুধু চেইন গণনা; keep_chains=0 সব রাখে।",
         "ম্যানুয়াল, বিরতি, দৈনিক, সাপ্তাহিক বা শুরুতে; স্থানীয় সময়।", "শুরুতে নির্ধারিত কাজ চালু/বন্ধ। Watch পর্যায়ক্রমে পরিবর্তন দেখে; বিরতি সেকেন্ডে।",
         "এক্সটেনশন ও অন্তর্ভুক্তি ফাইল বেছে নেয়; ঐচ্ছিক বর্জন অগ্রাধিকার পায়।", "পুনঃস্থাপনযোগ্য নির্ভরতা বাদ দিয়ে ম্যানিফেস্ট ও লকফাইল রাখুন; নির্দিষ্ট সংস্করণের জন্য সংরক্ষিত লকফাইল দরকার।",
         "none বা aes-256-gcm; password_env দিয়ে পাসওয়ার্ড। হারালে পুনরুদ্ধার অসম্ভব; কমান্ড আর্গুমেন্টে দেবেন না।",
         "all: সব; settings: কনফিগারেশন; installers: ইনস্টলার ও ম্যানিফেস্ট। অপারেটিং সিস্টেম ইমেজ নয়।",
         "লগ দেখুন; log_max_mb রোটেশন সীমা। changes ID অনুযায়ী যোগ, পরিবর্তন ও মুছে ফেলা পথ দেখায়।",
         "সব বা --only ফাইল/ফোল্ডার ফিরিয়ে আনুন। --original আগের সংস্করণ রাখে; undo নথিভুক্ত পুনরুদ্ধার ফেরায়। আগে verify ও --dry-run।"},
        {"ur", "اردو",
         "فائل بیک اپ اور ID سے بحالی۔", "طے شدہ: ہر 2 گھنٹے، انکریمنٹل، zstd، 90 دن، خودکار آغاز۔",
         "محفوظ کرنے والی فائلیں یا فولڈر؛ متعدد ذرائع ممکن ہیں۔", "مقامی فولڈر؛ USB بیرونی نقل؛ cloud کے لیے rclone۔",
         "auto: انکریمنٹل اور وقفے سے مکمل؛ full: سب؛ incremental: پچھلے کے بعد؛ differential: مکمل کے بعد؛ mirror: حذف سمیت موجودہ نقل۔",
         "کمپریشن قسم اور سطح؛ 0 طے شدہ سطح۔", "بحالی کے کم از کم دن اور مکمل زنجیریں؛ ضروری پچھلی نقول برقرار۔ keep_days=0 صرف زنجیریں گنتا ہے؛ keep_chains=0 سب رکھتا ہے۔",
         "دستی، وقفہ، روزانہ، ہفتہ وار یا آغاز پر؛ مقامی وقت۔", "آغاز پر مقررہ کام چلائیں یا بند کریں۔ Watch وقفے سے تبدیلی دیکھتا ہے؛ وقفہ سیکنڈ میں۔",
         "ایکسٹینشن اور شمولیت فائلیں منتخب کرتے ہیں؛ اختیاری اخراج کو ترجیح۔", "دوبارہ نصب ہونے والی وابستگیاں چھوڑیں، مینی فیسٹ اور لاک فائل رکھیں؛ عین ورژن کے لیے محفوظ لاک فائل ضروری۔",
         "none یا aes-256-gcm؛ پاس ورڈ بذریعہ password_env۔ پاس ورڈ کھونے پر بحالی ناممکن؛ کمانڈ آرگومنٹ میں نہ دیں۔",
         "all: سب؛ settings: ترتیبات؛ installers: تنصیب فائلیں اور مینی فیسٹ۔ یہ آپریٹنگ سسٹم امیج نہیں۔",
         "لاگ دیکھیں؛ log_max_mb گردش کی حد۔ changes ID کے مطابق شامل، بدلے اور حذف شدہ راستے دکھاتا ہے۔",
         "سب یا --only فائل/فولڈر بحال کریں۔ --original پرانی نقول محفوظ؛ undo درج شدہ بحالی واپس کرتا ہے۔ پہلے verify اور --dry-run۔"},
        {"id", "Bahasa Indonesia",
         "Cadangan berkas dan pemulihan berdasarkan ID.", "Bawaan: setiap 2 jam, inkremental, zstd, simpan 90 hari, mulai otomatis.",
         "Berkas atau folder yang dilindungi; beberapa sumber diizinkan.", "Folder lokal; USB menambah salinan eksternal; cloud menggunakan rclone.",
         "auto: inkremental dengan penuh berkala; full: semua; incremental: sejak sebelumnya; differential: sejak penuh; mirror: salinan terkini termasuk penghapusan.",
         "Format dan tingkat kompresi; 0 memakai bawaan.", "Minimum hari pemulihan dan rantai penuh; pendahulu yang diperlukan tetap disimpan. keep_days=0 hanya menghitung rantai; keep_chains=0 menyimpan semua.",
         "Manual, interval, harian, mingguan atau saat mulai; waktu lokal.", "Aktifkan tugas saat mulai. Watch memeriksa perubahan berkala; interval dalam detik.",
         "Ekstensi dan penyertaan memilih berkas; pengecualian opsional diutamakan.", "Lewati dependensi yang dapat dipasang ulang, simpan manifes dan lockfile; versi tepat memerlukan lockfile tersimpan.",
         "none atau aes-256-gcm; sandi melalui password_env. Kehilangan sandi menghalangi pemulihan; jangan taruh di argumen perintah.",
         "all: semua; settings: konfigurasi; installers: pemasang dan manifes. Bukan citra sistem operasi.",
         "Baca log; log_max_mb membatasi rotasi. changes menampilkan jalur ditambah, diubah dan dihapus per ID.",
         "Pulihkan semua atau --only berkas/folder. --original menyimpan versi tertimpa; undo membatalkan pemulihan tercatat. Coba verify dan --dry-run dahulu."},
        {"tr", "Türkçe",
         "Dosya yedekleme ve kimlikle geri yükleme.", "Varsayılan: 2 saatte bir, artımlı, zstd, 90 gün saklama, otomatik başlatma.",
         "Korunacak dosya veya klasörler; birden çok kaynak olabilir.", "Yerel klasör; USB harici kopya ekler; cloud rclone kullanır.",
         "auto: dönemsel tam ile artımlı; full: tümü; incremental: öncekinden beri; differential: tamdan beri; mirror: silmelerle güncel kopya.",
         "Sıkıştırma biçimi ve düzeyi; 0 varsayılan düzeydir.", "En az geri yükleme günü ve tam zincir sayısı; gereken öncekiler korunur. keep_days=0 yalnız zincir sayısı; keep_chains=0 tümünü saklar.",
         "Elle, aralıklı, günlük, haftalık veya başlangıçta; yerel saat.", "Başlangıçta görevleri aç/kapat. Watch değişiklikleri düzenli denetler; aralık saniyedir.",
         "Uzantı ve dahil etme dosya seçer; isteğe bağlı hariç tutma önceliklidir.", "Yeniden kurulabilir bağımlılıkları atla, bildirim ve kilit dosyalarını sakla; tam sürümler için saklı kilit dosyası gerekir.",
         "none veya aes-256-gcm; parola password_env ile. Parola kaybolursa kurtarma yapılamaz; komut argümanına yazmayın.",
         "all: tümü; settings: ayarlar; installers: kurulumlar ve bildirimler. İşletim sistemi imajı değildir.",
         "Günlükleri oku; log_max_mb döndürme sınırı. changes kimliğe göre eklenen, değişen ve silinen yolları gösterir.",
         "Tümünü veya --only dosya/klasör geri yükle. --original eski sürümleri korur; undo kayıtlı geri yüklemeyi geri alır. Önce verify ve --dry-run."},
        {"vi", "Tiếng Việt",
         "Sao lưu tệp và khôi phục theo ID.", "Mặc định: mỗi 2 giờ, gia tăng, zstd, giữ 90 ngày, tự khởi động.",
         "Tệp hoặc thư mục cần bảo vệ; có thể dùng nhiều nguồn.", "Thư mục cục bộ; USB thêm bản ngoài; cloud dùng rclone.",
         "auto: gia tăng và định kỳ đầy đủ; full: tất cả; incremental: từ lần trước; differential: từ bản đầy đủ; mirror: bản hiện tại gồm xóa.",
         "Định dạng và mức nén; 0 dùng mặc định.", "Số ngày phục hồi và chuỗi đầy đủ tối thiểu; giữ bản trước cần thiết. keep_days=0 chỉ đếm chuỗi; keep_chains=0 giữ tất cả.",
         "Thủ công, khoảng thời gian, hằng ngày, tuần hoặc khi khởi động; giờ địa phương.", "Bật/tắt tác vụ khi khởi động. Watch kiểm tra thay đổi định kỳ; khoảng tính bằng giây.",
         "Phần mở rộng và quy tắc bao gồm chọn tệp; loại trừ tùy chọn được ưu tiên.", "Bỏ phụ thuộc cài lại được, giữ manifest và tệp khóa; đúng phiên bản cần tệp khóa đã lưu.",
         "none hoặc aes-256-gcm; mật khẩu qua password_env. Mất mật khẩu không thể phục hồi; không đặt trong đối số lệnh.",
         "all: tất cả; settings: cấu hình; installers: bộ cài và manifest. Không phải ảnh hệ điều hành.",
         "Xem nhật ký; log_max_mb giới hạn luân chuyển. changes hiển thị đường dẫn thêm, sửa, xóa theo ID.",
         "Khôi phục tất cả hoặc --only tệp/thư mục. --original giữ bản bị ghi đè; undo hoàn tác khôi phục đã ghi. Thử verify và --dry-run trước."},
        {"ta", "தமிழ்",
         "கோப்பு காப்பு மற்றும் ID மூலம் மீட்பு.", "இயல்பு: 2 மணிக்கு ஒருமுறை, அதிகரிப்பு, zstd, 90 நாள் சேமிப்பு, தானியங்கி தொடக்கம்.",
         "பாதுகாக்க வேண்டிய கோப்புகள் அல்லது அடைவுகள்; பல மூலங்கள் அனுமதி.", "உள்ளூர் அடைவு; USB வெளிப்புற நகல்; cloud க்கு rclone.",
         "auto: அதிகரிப்பு மற்றும் காலமுறை முழு; full: அனைத்தும்; incremental: முந்தையதிலிருந்து; differential: முழுவிலிருந்து; mirror: நீக்கங்கள் உட்பட தற்போதைய நகல்.",
         "சுருக்க வடிவம் மற்றும் நிலை; 0 இயல்புநிலை.", "குறைந்தபட்ச மீட்பு நாட்கள் மற்றும் முழு சங்கிலிகள்; தேவையான முந்தையவை இருக்கும். keep_days=0 சங்கிலி எண்ணிக்கை மட்டும்; keep_chains=0 அனைத்தும் சேமிக்கும்.",
         "கைமுறை, இடைவெளி, தினசரி, வாரம் அல்லது தொடக்கத்தில்; உள்ளூர் நேரம்.", "தொடக்கப் பணிகளை இயக்க/அணைக்க. Watch மாற்றங்களை இடைவெளியில் பார்க்கும்; இடைவெளி நொடிகளில்.",
         "நீட்சிகள் மற்றும் சேர்ப்பு கோப்புகளைத் தேரும்; விருப்ப விலக்குகள் முன்னுரிமை.", "மீண்டும் நிறுவக்கூடிய சார்புகளைத் தவிர்த்து manifest மற்றும் lock கோப்புகளைச் சேமிக்கவும்; அதே பதிப்புக்கு சேமித்த lock தேவை.",
         "none அல்லது aes-256-gcm; password_env மூலம் கடவுச்சொல். தொலைந்தால் மீட்க முடியாது; கட்டளை வாதத்தில் சேர்க்க வேண்டாம்.",
         "all: அனைத்தும்; settings: அமைப்புகள்; installers: நிறுவிகள் மற்றும் manifest. இயக்குதளப் படிமம் அல்ல.",
         "பதிவுகளைக் காண்க; log_max_mb சுழற்சி வரம்பு. changes ID மூலம் சேர்த்த, மாற்றிய, நீக்கிய பாதைகளைக் காட்டும்.",
         "அனைத்தும் அல்லது --only கோப்பு/அடைவு மீட்க. --original பழைய பதிப்பைச் சேமிக்கும்; undo பதிவான மீட்பை மாற்றும். முதலில் verify மற்றும் --dry-run."},
        {"te", "తెలుగు",
         "ఫైల్ బ్యాకప్ మరియు ID ద్వారా పునరుద్ధరణ.", "ప్రామాణికం: ప్రతి 2 గంటలు, ఇంక్రిమెంటల్, zstd, 90 రోజుల నిల్వ, ఆటోమేటిక్ ప్రారంభం.",
         "రక్షించవలసిన ఫైళ్లు లేదా ఫోల్డర్లు; అనేక మూలాలు సాధ్యం.", "స్థానిక ఫోల్డర్; USB బాహ్య కాపీ; cloud కు rclone.",
         "auto: ఇంక్రిమెంటల్ మరియు కాలానుగుణ పూర్తి; full: అన్నీ; incremental: గతం తర్వాత; differential: పూర్తి తర్వాత; mirror: తొలగింపులతో ప్రస్తుత కాపీ.",
         "కుదింపు ఆకృతి మరియు స్థాయి; 0 ప్రామాణిక స్థాయి.", "కనీస పునరుద్ధరణ రోజులు మరియు పూర్తి గొలుసులు; అవసరమైన గత కాపీలు ఉంటాయి. keep_days=0 గొలుసుల సంఖ్య మాత్రమే; keep_chains=0 అన్నీ ఉంచుతుంది.",
         "మాన్యువల్, విరామం, రోజువారీ, వారానికోసారి లేదా ప్రారంభంలో; స్థానిక సమయం.", "ప్రారంభ పనులు ఆన్/ఆఫ్. Watch మార్పులను తరచుగా చూస్తుంది; విరామం సెకన్లలో.",
         "పొడిగింపులు మరియు చేర్పు ఫైళ్లు ఎంచుకుంటాయి; ఐచ్ఛిక మినహాయింపులకు ప్రాధాన్యం.", "మళ్లీ ఇన్‌స్టాల్ చేయగల ఆధారాలను వదిలి manifest మరియు lock ఫైళ్లు ఉంచండి; అదే వెర్షనుకు సేవ్ చేసిన lock అవసరం.",
         "none లేదా aes-256-gcm; password_env ద్వారా పాస్‌వర్డ్. పోతే పునరుద్ధరణ అసాధ్యం; కమాండ్ ఆర్గ్యుమెంట్లలో ఇవ్వవద్దు.",
         "all: అన్నీ; settings: సెట్టింగులు; installers: ఇన్‌స్టాలర్లు మరియు manifest. ఆపరేటింగ్ సిస్టం ఇమేజ్ కాదు.",
         "లాగ్ చూడండి; log_max_mb రొటేషన్ పరిమితి. changes ID ప్రకారం చేర్చిన, మార్చిన, తొలగించిన మార్గాలు చూపుతుంది.",
         "అన్నీ లేదా --only ఫైల్/ఫోల్డర్ పునరుద్ధరించండి. --original పాత వెర్షన్లు ఉంచుతుంది; undo నమోదైన పునరుద్ధరణను వెనక్కి చేస్తుంది. ముందు verify మరియు --dry-run."},
        {"mr", "मराठी",
         "फाइल बॅकअप आणि ID द्वारे पुनर्स्थापना.", "मूलभूत: दर २ तासांनी, वाढीव बॅकअप, zstd, ९० दिवस, स्वयंचलित प्रारंभ.",
         "सुरक्षित करण्याच्या फाइल किंवा फोल्डर; अनेक स्रोत शक्य.", "स्थानिक फोल्डर; USB बाह्य प्रत जोडतो; cloud साठी rclone.",
         "auto: वाढीव व नियमित पूर्ण; full: सर्व; incremental: मागीलपासून; differential: पूर्णपासून; mirror: हटवण्यांसह वर्तमान प्रत.",
         "संक्षेप प्रकार आणि पातळी; 0 मूलभूत पातळी.", "किमान पुनर्स्थापना दिवस व पूर्ण साखळ्या; आवश्यक पूर्वीच्या प्रती राहतात. keep_days=0 फक्त साखळ्या मोजतो; keep_chains=0 सर्व ठेवतो.",
         "हाताने, अंतर, दररोज, साप्ताहिक किंवा प्रारंभी; स्थानिक वेळ.", "प्रारंभी नियोजित कार्य चालू/बंद. Watch नियमित बदल तपासतो; अंतर सेकंदात.",
         "विस्तार व समावेश फाइल निवडतात; ऐच्छिक वगळण्यांना प्राधान्य.", "पुन्हा बसवता येणारी अवलंबने वगळून manifest आणि lock फाइल ठेवा; अचूक आवृत्तीसाठी साठवलेली lock फाइल आवश्यक.",
         "none किंवा aes-256-gcm; password_env द्वारे संकेतशब्द. हरवल्यास पुनर्स्थापना अशक्य; कमांड तर्कात देऊ नका.",
         "all: सर्व; settings: संरचना; installers: स्थापक व manifest. ऑपरेटिंग सिस्टम प्रतिमा नाही.",
         "नोंदी पहा; log_max_mb आवर्तन मर्यादा. changes ID नुसार जोडलेले, बदललेले आणि हटवलेले मार्ग दाखवतो.",
         "सर्व किंवा --only फाइल/फोल्डर पुनर्स्थापित करा. --original जुन्या आवृत्त्या ठेवतो; undo नोंदवलेली पुनर्स्थापना उलटवतो. आधी verify आणि --dry-run."},
        {"fa", "فارسی",
         "پشتیبان‌گیری فایل و بازیابی با شناسه.", "پیش‌فرض: هر ۲ ساعت، افزایشی، zstd، نگهداری ۹۰ روز، شروع خودکار.",
         "فایل‌ها یا پوشه‌های محافظت‌شده؛ چند منبع مجاز است.", "پوشه محلی؛ USB نسخه خارجی اضافه می‌کند؛ cloud از rclone استفاده می‌کند.",
         "auto: افزایشی با کامل دوره‌ای؛ full: همه؛ incremental: از قبلی؛ differential: از کامل؛ mirror: نسخه فعلی شامل حذف‌ها.",
         "قالب و سطح فشرده‌سازی؛ 0 سطح پیش‌فرض.", "حداقل روزهای بازیابی و زنجیره‌های کامل؛ پیشینیان لازم می‌مانند. keep_days=0 فقط تعداد زنجیره؛ keep_chains=0 حفظ همه.",
         "دستی، فاصله‌ای، روزانه، هفتگی یا هنگام شروع؛ زمان محلی.", "فعال/غیرفعال کردن کارها هنگام شروع. Watch تغییرات را دوره‌ای بررسی می‌کند؛ فاصله به ثانیه.",
         "پسوند و الگوهای شمول فایل انتخاب می‌کنند؛ استثناهای اختیاری اولویت دارند.", "وابستگی‌های نصب‌شدنی را رد و manifest و فایل قفل را حفظ کنید؛ نسخه دقیق به قفل ذخیره‌شده نیاز دارد.",
         "none یا aes-256-gcm؛ رمز با password_env. گم‌شدن رمز بازیابی را ناممکن می‌کند؛ در آرگومان فرمان ننویسید.",
         "all: همه؛ settings: تنظیمات؛ installers: نصب‌کننده و manifest. تصویر سیستم عامل نیست.",
         "نمایش گزارش‌ها؛ log_max_mb حد چرخش. changes مسیرهای افزوده، تغییرکرده و حذف‌شده را بر اساس شناسه نشان می‌دهد.",
         "همه یا --only فایل/پوشه را بازیابی کنید. --original نسخه قبلی را نگه می‌دارد؛ undo بازیابی ثبت‌شده را برمی‌گرداند. ابتدا verify و --dry-run."},
        {"sw", "Kiswahili",
         "Hifadhi faili na urejeshe kwa ID.", "Chaguo-msingi: kila saa 2, nyongeza, zstd, siku 90, kuanza kiotomatiki.",
         "Faili au folda za kulinda; vyanzo vingi vinaruhusiwa.", "Folda ya ndani; USB huongeza nakala ya nje; cloud hutumia rclone.",
         "auto: nyongeza na kamili mara kwa mara; full: yote; incremental: tangu iliyopita; differential: tangu kamili; mirror: nakala ya sasa pamoja na ufutaji.",
         "Muundo na kiwango cha kubana; 0 huchagua chaguo-msingi.", "Siku za chini za kurejesha na minyororo kamili; nakala za awali zinazohitajika hubaki. keep_days=0 huhesabu minyororo tu; keep_chains=0 huhifadhi yote.",
         "Kwa mkono, muda, kila siku, wiki au kuanza; saa za eneo.", "Washa/zima kazi zinazoanza kiotomatiki. Watch hukagua mabadiliko mara kwa mara; muda ni sekunde.",
         "Viendelezi na ujumuishaji huchagua faili; vizuizi vya hiari vina kipaumbele.", "Ruka vitegemezi vinavyoweza kusakinishwa tena; hifadhi manifest na lockfile; matoleo sahihi yanahitaji lockfile iliyohifadhiwa.",
         "none au aes-256-gcm; nenosiri kupitia password_env. Likipotea urejeshaji hauwezekani; usiliweke kama hoja ya amri.",
         "all: yote; settings: mipangilio; installers: visakinishi na manifest. Si picha ya mfumo wa uendeshaji.",
         "Soma kumbukumbu; log_max_mb hupunguza mzunguko. changes huonyesha njia zilizoongezwa, kubadilishwa na kufutwa kwa ID.",
         "Rejesha yote au --only faili/folda. --original huhifadhi matoleo yaliyobadilishwa; undo hutengua urejeshaji uliorekodiwa. Jaribu verify na --dry-run kwanza."},
        {"th", "ไทย",
         "สำรองไฟล์และกู้คืนด้วย ID สแนปช็อต", "ค่าเริ่มต้น: ทุก 2 ชั่วโมง แบบเพิ่มเฉพาะส่วน zstd เก็บ 90 วัน เริ่มอัตโนมัติ",
         "ไฟล์หรือโฟลเดอร์ที่ต้องการป้องกัน เลือกหลายแหล่งได้", "โฟลเดอร์ในเครื่อง USB เพิ่มสำเนาภายนอก cloud ใช้ rclone",
         "auto: เพิ่มเฉพาะส่วนพร้อมสำรองเต็มเป็นระยะ; full: ทั้งหมด; incremental: จากครั้งก่อน; differential: จากชุดเต็ม; mirror: สำเนาปัจจุบันรวมการลบ",
         "รูปแบบและระดับบีบอัด 0 ใช้ค่ามาตรฐาน", "จำนวนวันกู้คืนและสายสำรองเต็มขั้นต่ำ เก็บชุดก่อนหน้าที่จำเป็น keep_days=0 ใช้จำนวนสายเท่านั้น keep_chains=0 เก็บทั้งหมด",
         "สั่งเอง ตามช่วงเวลา รายวัน รายสัปดาห์ หรือเมื่อเริ่ม ใช้เวลาท้องถิ่น", "เปิด/ปิดงานเมื่อเริ่มระบบ Watch ตรวจการเปลี่ยนแปลงเป็นระยะ หน่วยวินาที",
         "นามสกุลและกฎรวมเลือกไฟล์ กฎยกเว้นที่เลือกเองมีลำดับก่อน", "ข้ามไลบรารีที่ติดตั้งใหม่ได้ เก็บ manifest และ lockfile เวอร์ชันเดิมต้องใช้ lockfile ที่บันทึกไว้",
         "none หรือ aes-256-gcm รหัสผ่านผ่าน password_env หากหายจะกู้ไม่ได้ อย่าใส่รหัสผ่านในอาร์กิวเมนต์คำสั่ง",
         "all: ทั้งหมด; settings: การตั้งค่า; installers: ตัวติดตั้งและ manifest ไม่ใช่อิมเมจระบบปฏิบัติการ",
         "อ่านบันทึก log_max_mb จำกัดการหมุนเวียน changes แสดงพาธเพิ่ม แก้ไข และลบตาม ID",
         "กู้ทั้งหมดหรือ --only ไฟล์/โฟลเดอร์ --original เก็บเวอร์ชันที่ถูกทับ undo ย้อนการกู้ที่บันทึกไว้ ลอง verify และ --dry-run ก่อน"},
        {"ro", "Română",
         "Copii de fișiere și restaurare prin ID.", "Implicit: la 2 ore, incremental, zstd, 90 de zile, pornire automată.",
         "Fișiere sau dosare protejate; sunt permise surse multiple.", "Dosar local; USB adaugă o copie externă; cloud folosește rclone.",
         "auto: incremental cu complete periodice; full: tot; incremental: de la precedenta; differential: de la completă; mirror: copie actuală cu ștergeri.",
         "Format și nivel de compresie; 0 folosește valoarea implicită.", "Minimum de zile recuperabile și lanțuri complete; predecesorii necesari rămân. keep_days=0 folosește doar lanțuri; keep_chains=0 păstrează tot.",
         "Manual, interval, zilnic, săptămânal sau la pornire; ora locală.", "Activare/dezactivare la pornire. Watch verifică periodic schimbările; interval în secunde.",
         "Extensiile și includerile selectează fișiere; excluderile opționale au prioritate.", "Omitere dependențe reinstalabile, păstrare manifeste și fișiere de blocare; versiunile exacte necesită blocarea salvată.",
         "none sau aes-256-gcm; parolă prin password_env. Fără parolă nu există recuperare; nu o puneți în argumentele comenzii.",
         "all: tot; settings: configurație; installers: instalatoare și manifeste. Nu este imagine de sistem.",
         "Citire jurnale; log_max_mb limitează rotația. changes arată căi adăugate, modificate și șterse după ID.",
         "Restaurare totală sau --only fișier/dosar. --original păstrează versiunile înlocuite; undo inversează restaurarea înregistrată. Încercați verify și --dry-run."}
    };
    return data;
}
inline std::string &language_storage() { static std::string code = "en"; return code; }
inline std::string language() { return language_storage(); }
inline bool set_language(std::string code) {
    std::transform(code.begin(),code.end(),code.begin(),[](unsigned char c){return char(std::tolower(c));});
    // Accept common locale forms, e.g. nl_NL.UTF-8 and pt-BR.
    code=code.substr(0,code.find_first_of("_-."));
    for(const auto &c:catalogs()) if(code==c.code) { language_storage()=code;return true; }
    return false;
}
inline const Catalog &current() {
    for(const auto &c:catalogs()) if(language()==c.code) return c;
    return catalogs().front();
}
inline void languages(std::ostream &out) {
    for(const auto &c:catalogs()) out<<c.code<<"  "<<c.name<<(language()==c.code?" *":"")<<'\n';
}
inline void settings(std::ostream &out) {
    const auto &c=current();
    out<<c.defaults<<"\n\n";
    const auto line=[&](const char *keys,const char *description){out<<keys<<"\n  "<<description<<"\n";};
    line("source / add-source / remove-source",c.sources);
    line("dest / usb / cloud",c.dest);
    line("mode = auto|full|incremental|differential|mirror; full_every = 12",c.mode);
    line("compression = zstd|gzip|xz|bzip2|lz4|zip|none; compression_level = 0",c.compression);
    line("keep_days = 90; keep_chains = 1",c.retention);
    out<<"  keep_days=0: chain count only; keep_chains=0: keep all\n";
    line("schedule: off | --every 2h | --daily 22:00 | --weekly Sun@02:00 | --startup",c.schedule);
    line("autostart = true|false; watch = false; watch_seconds = 30",c.autostart);
    line("ext / include / exclude; add-exclude / clear-exclude",c.filters);
    line("exclude_dependencies = true|false",c.dependencies);
    line("encryption = none|aes-256-gcm; password_env = CBACKUP_PASSWORD",c.encryption);
    line("profile = all|settings|installers",c.profile);
    line("log_max_mb = 5; log / ids / changes",c.logging);
    const bool nl=language()=="nl";
    line("notifications = true; desktop_notifications = false; notify_repeat_hours = 24",
         nl?"Meldingen in stderr en alerts; optioneel op het bureaublad. Herhalingen worden begrensd.":"Alerts to stderr and the local alerts log; optional desktop display. Repeated conditions are throttled.");
    line("stale_hours = 24; health / alerts",
         nl?"Waarschuw wanneer een succesvolle run te lang uitblijft; 0 schakelt de ouderdomscontrole uit.":"Report failed runs or no recent successful run; 0 disables the stale threshold. Scheduled jobs get an hourly health check.");
    line("test_restore_days = 7; test_restore_max_mb = 64; test-restore",
         nl?"Periodiek een roterend voorbeeldbestand tijdelijk herstellen en hashen; 0 dagen schakelt automatisch testen uit.":"Periodically restore a rotating sample into temporary storage and verify SHA-256; 0 days disables automatic tests. The limit is per sample file.");
    line("storage_quota_mb = 0",
         nl?"Budget in MiB voor archieven, manifests en mirror; 0 = onbeperkt. Overschrijding stopt de nieuwe back-up; vereiste ketens blijven behouden.":"Committed archives, manifests and mirror budget in MiB; 0 = unlimited. Estimates are advisory; compressed size is checked exactly. Required chains remain protected.");
    line("offline_reminder_days = 14; offline-confirm JOB SNAPSHOT_ID",
         nl?"Herinner aan een losse offline kopie; bevestig zelf na kopieren en loskoppelen. 0 = uit.":"Remind when a separate offline copy is due; acknowledge manually after copying and disconnecting. 0 disables reminders.");
    line("restore / --to / --original / --only / undo / verify / --dry-run",c.restore);
    out<<"language: cbackup language CODE | cbackup --lang CODE help\n";
}
inline void help(std::ostream &out,const std::string &version) {
    const auto &c=current();
    out<<"CBackup "<<version<<"\n"<<c.intro<<"\n"<<c.defaults<<R"HELP(

  cbackup init JOB [SOURCE ...] [--no-schedule]
  cbackup config JOB show
  cbackup config JOB set KEY VALUE
  cbackup config JOB add-source PATH [PATH ...] | remove-source PATH
  cbackup config JOB add-ext EXT [...] | clear-ext
  cbackup config JOB add-include GLOB [...] | clear-include
  cbackup config JOB add-exclude GLOB [...] | clear-exclude
  cbackup settings
  cbackup compression

  cbackup run JOB [--type full|incremental|differential|mirror]
                  [--compression TYPE[:LEVEL]] [--dry-run]
  cbackup watch JOB
  cbackup schedule JOB --every 2h | --hourly | --daily 22:00 | --weekly Sun@02:00 | --startup
  cbackup schedule JOB on | off

  cbackup ids JOB
  cbackup list JOB [ID|DATE]
  cbackup changes JOB ID [ID]
  cbackup log JOB [--tail N]
  cbackup verify JOB ID
  cbackup health JOB
  cbackup alerts JOB
  cbackup test-restore JOB
  cbackup offline-confirm JOB SNAPSHOT_ID
  cbackup restore JOB ID --to NEW_FOLDER [--only FILE_OR_FOLDER] [--dry-run]
  cbackup restore JOB ID --original [--only FILE_OR_FOLDER] [--yes] [--dry-run]
  cbackup undo-list JOB
  cbackup undo JOB RESTORE_ID [--yes] [--dry-run]
  cbackup dependencies JOB ID --to NEW_FOLDER

  cbackup replicate JOB [--check]
  cbackup cloud-push JOB | cloud-pull JOB
  cbackup language [CODE]
  cbackup --lang CODE help

)HELP";
    settings(out);
}

// Common legacy diagnostics. The English source form is accepted as well.
inline std::string tr(const std::string &text) {
    struct Pair {const char *nl,*en;};
    static const Pair messages[] = {
        {"FOUT: ","ERROR: "},{"Jobnaam ontbreekt (zie: cbackup help)","Missing job name (see: cbackup help)"},
        {"Ongeldige jobnaam","Invalid job name"},{"Te veel argumenten","Too many arguments"},
        {"Lege waarde niet toegestaan","Empty value is not allowed"},{"Bron niet gevonden","Source not found"},
        {"Ongeldige mode","Invalid mode"},{"Type ontbreekt","Missing backup type"},
        {"Compressietype ontbreekt","Missing compression type"},{"Geef minimaal één pad op","Specify at least one path"},
        {"Geef minimaal één waarde op","Specify at least one value"},{"Gebruik set sleutel waarde","Usage: set KEY VALUE"},
        {"Gebruik remove-source PAD","Usage: remove-source PATH"},{"--only mist een patroon","--only requires a pattern"},
        {"--to mist een map","--to requires a folder"},{"Configuratie opgeslagen: ","Configuration saved: "},
        {"Onbekend commando: ","Unknown command: "},{"Onbekende optie: ","Unknown option: "},
        {"Job bestaat niet: ","Job does not exist: "},{"HOME ontbreekt","HOME is not set"},
        {"Ongeldige regel in jobconfig","Invalid job configuration line"},
        {"Ongeldige planning","Invalid schedule"},{"Ongeldige interval","Invalid interval"},
        {"Gebruik HH:MM","Use HH:MM"},{"Gebruik Mon..Sun","Use Mon..Sun"}
    };
    for(const auto &p:messages) {
        if(language()=="nl" && text==p.en) return p.nl;
        if(language()!="nl" && text==p.nl) return p.en;
    }
    return text;
}
} // namespace cb_i18n
