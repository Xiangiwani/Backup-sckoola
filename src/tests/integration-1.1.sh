#!/usr/bin/env bash
# Integratietest voor CBackup. Gebruik:
#   ./tests/integration.sh ./build/cbackup [PAD_NAAR_OUDE_CBACKUP]
# Raakt je echte configuratie niet aan: HOME, systemctl en rclone worden gesimuleerd.
# Het tweede argument (optioneel) is een oudere cbackup-versie, om te testen dat oude back-ups
# met de nieuwe versie terug te zetten en voort te zetten zijn.
set -euo pipefail
B=$(readlink -f "${1:?Gebruik: integration.sh PAD_NAAR_CBACKUP [OUDE_CBACKUP]}")
OLD=${2:+$(readlink -f "$2")}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export HOME=$T/home FAKE_CLOUD=$T/cloud
mkdir -p "$HOME" "$T/bin" "$FAKE_CLOUD"
export PATH=$T/bin:$PATH

# ---- nep-systemctl: onthoudt welke timers 'actief' zijn
cat > "$T/bin/systemctl" <<'EOF'
#!/usr/bin/env bash
echo "systemctl $*" >> "$HOME/systemctl.log"
case "$2" in
  is-active) [[ -f "$HOME/.timer-on-$4" ]] && exit 0 || exit 3 ;;
  enable)    touch "$HOME/.timer-on-$4" ;;
  disable)   rm -f "$HOME/.timer-on-$4" ;;
esac
exit 0
EOF
# ---- nep-rclone: remote:pad -> $FAKE_CLOUD/pad
cat > "$T/bin/rclone" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
echo "rclone $*" >> "$HOME/rclone.log"
map(){ if [[ "$1" == *:* ]]; then echo "$FAKE_CLOUD/${1#*:}"; else echo "$1"; fi; }
cmd=$1; src=$(map "$2"); dst=$(map "$3"); shift 3; ff=""
while [[ $# -gt 0 ]]; do case $1 in --files-from) ff=$2; shift 2;; *) shift;; esac; done
case $cmd in
  copy)   mkdir -p "$dst"
          if [[ -n $ff ]]; then while IFS= read -r f; do [[ -z $f ]] || cp "$src/$f" "$dst/$f"; done < "$ff"
          else cp -r "$src/." "$dst/"; fi ;;
  copyto) mkdir -p "$(dirname "$dst")"; cp "$src" "$dst" ;;
  sync)   rm -rf "$dst"; mkdir -p "$dst"; cp -r "$src/." "$dst/" ;;
  *) echo "nep-rclone: onbekend commando $cmd" >&2; exit 2 ;;
esac
EOF
chmod +x "$T/bin/systemctl" "$T/bin/rclone"

PASS=0
ok(){ PASS=$((PASS+1)); echo "OK   $*"; }
die(){ echo "FOUT $*" >&2; exit 1; }
must_fail(){ local what=$1; shift; if "$@" >"$T/last.out" 2>&1; then cat "$T/last.out"; die "verwachtte een fout: $what"; fi; }
# Lege mappen worden niet bewaard (alleen bestanden); vergelijk daarom een kopie zonder lege mappen.
same(){ local x; x=$(mktemp -d "$T/cmp.XXXX"); cp -a "$1/." "$x/"; find "$x" -mindepth 1 -type d -empty -delete
        diff -r "$x" "$2" >/dev/null || { diff -r "$x" "$2" || true; die "$3"; }; rm -rf "$x"; }
newest(){ ls "$1" | sed -n 's/^snap_\(.*\)\.manifest$/\1/p' | sort | tail -1; }
count_snaps(){ ls "$1" | grep -c '\.manifest$' || true; }
NL=(zondag maandag dinsdag woensdag donderdag vrijdag zaterdag)
nlday(){ echo "${NL[$(date -d "$1" +%w)]}"; }
# lokale datum+tijd -> snapshot-ID (UTC), bijv. snapid "8 days ago" 10:00 1
snapid(){ echo "$(date -u -d "@$(date -d "$(date -d "$1" +%F) $2" +%s)" +%Y%m%d_%H%M%S)_000000_$3"; }
rename_snap(){ # jobmap oudID nieuwID
  local d=$1 old=$2 new=$3 f
  for f in "$d"/snap_*.manifest; do sed -i "s/$old/$new/g" "$f"; done
  for f in "$d"/snap_"$old"*; do mv "$f" "${f/$old/$new}"; done
}
BK=$HOME/backups

# ========================================================== 1. init + standaardplanning
mkdir -p "$T/src/sub"; echo alpha > "$T/src/a.txt"; echo "int main(){}" > "$T/src/sub/b.cpp"
"$B" init std "$T/src" >/dev/null
grep -q '^schedule="hourly"$' "$HOME/.config/cbackup/jobs/std.conf" || die "init zet geen uurlijkse planning"
grep -q '^compression="zstd"$' "$HOME/.config/cbackup/jobs/std.conf" || die "init gebruikt geen zstd"
grep -q '^OnCalendar=hourly$' "$HOME/.config/systemd/user/cbackup-std.timer" || die "timer niet uurlijks"
grep -q "run std$" "$HOME/.config/systemd/user/cbackup-std.service" || die "service roept run niet aan"
"$B" config std show | grep -q "Planning: elk uur (op het hele uur) - timer actief" || die "show meldt timer niet als actief"
ok "init: standaard elk uur, incrementeel (auto), zstd"

"$B" init leeg >/dev/null
[[ ! -f "$HOME/.config/systemd/user/cbackup-leeg.timer" ]] || die "timer zonder bron aangemaakt"
"$B" config leeg add-source "$T/src" >/dev/null
[[ -f "$HOME/.config/systemd/user/cbackup-leeg.timer" ]] || die "timer niet actief na add-source"
ok "init zonder bron: planning wordt actief bij add-source"

"$B" init geenplan "$T/src" --no-schedule >/dev/null
[[ ! -f "$HOME/.config/systemd/user/cbackup-geenplan.timer" ]] || die "--no-schedule maakte toch een timer"
ok "init --no-schedule"

"$B" schedule leeg off >/dev/null
[[ ! -f "$HOME/.config/systemd/user/cbackup-leeg.timer" ]] || die "schedule off liet timer staan"
grep -q '^schedule=""$' "$HOME/.config/cbackup/jobs/leeg.conf" || die "schedule off niet in config"
"$B" schedule leeg --every 10min >/dev/null
grep -q '^OnUnitInactiveSec=10min$' "$HOME/.config/systemd/user/cbackup-leeg.timer" || die "--every niet in timer"
"$B" schedule leeg --daily 22:00 >/dev/null
grep -q '^OnCalendar=\*-\*-\* 22:00:00$' "$HOME/.config/systemd/user/cbackup-leeg.timer" || die "--daily niet in timer"
"$B" schedule leeg off >/dev/null; "$B" schedule leeg on >/dev/null
grep -q '^OnCalendar=hourly$' "$HOME/.config/systemd/user/cbackup-leeg.timer" || die "schedule on is niet uurlijks"
must_fail "ongeldige tijd" "$B" schedule leeg --daily 25:00
ok "schedule off/on/--every/--daily"

# ========================================================== 2. full / incremental / differential + snelle metadata-controle
J=$BK/std
"$B" run std >"$T/o"; grep -q "Type: full" "$T/o" || die "eerste run niet full"
cp -a "$T/src" "$T/s1"
echo beta >> "$T/src/a.txt"; "$B" run std >"$T/o"
grep -q "Type: incremental, bestanden: 2, in archief: 1" "$T/o" || { cat "$T/o"; die "incremental telt fout"; }
grep -q "gehasht: 1, ongewijzigd volgens metadata: 1" "$T/o" || { cat "$T/o"; die "metadata-cache niet gebruikt"; }
"$B" run std >"$T/o"; grep -q "Geen wijzigingen" "$T/o" || die "geen-wijzigingen niet herkend"
grep -q "gehasht: 0" "$T/o" || die "ongewijzigde bestanden toch gehasht"
# Inhoud wijzigen met gelijke grootte en teruggezette mtime: ctime verraadt de wijziging.
cp -p "$T/src/a.txt" "$T/ref"; printf 'ALPHA\nBETA\n' > "$T/src/a.txt"; touch -r "$T/ref" "$T/src/a.txt"
"$B" run std >"$T/o"; grep -q "in archief: 1" "$T/o" || { cat "$T/o"; die "stille wijziging (zelfde grootte/mtime) gemist"; }
cp -a "$T/src" "$T/s3"
echo gamma > "$T/src/c.txt"; "$B" run std --type differential >"$T/o"; grep -q "Type: differential" "$T/o" || die "differential"
rm "$T/src/sub/b.cpp"; "$B" run std >"$T/o"; grep -q "verdwenen: 1" "$T/o" || die "verwijdering niet gezien"
cp -a "$T/src" "$T/s5"
"$B" restore std laatste --to "$T/r_last" >/dev/null; same "$T/s5" "$T/r_last/files/source_0" "restore laatste"
FIRST=$(ls "$J" | sed -n 's/^snap_\(.*\)\.manifest$/\1/p' | sort | head -1)
"$B" restore std "$FIRST" --to "$T/r_first" >/dev/null; same "$T/s1" "$T/r_first/files/source_0" "restore op ID"
[[ -f "$T/r_last/bronnen.txt" ]] && grep -q "$T/src" "$T/r_last/bronnen.txt" || die "bronnen.txt ontbreekt"
must_fail "bestaande doelmap" "$B" restore std laatste --to "$T/r_last"
ok "full/incremental/differential, metadata-cache, stille wijziging, restore op ID/laatste"

# ========================================================== 3. terugzetten op dag en tijd
mkdir -p "$T/tsrc"; echo "versie 1" > "$T/tsrc/doc.txt"
"$B" init tijd "$T/tsrc" --no-schedule >/dev/null
"$B" run tijd >/dev/null; S1=$(newest "$BK/tijd"); cp -a "$T/tsrc" "$T/t1"
echo "versie 2" > "$T/tsrc/doc.txt"; echo nieuw > "$T/tsrc/extra.txt"; "$B" run tijd >/dev/null; S2=$(newest "$BK/tijd"); cp -a "$T/tsrc" "$T/t2"
echo "versie 3" > "$T/tsrc/doc.txt"; "$B" run tijd >/dev/null; S3=$(newest "$BK/tijd"); cp -a "$T/tsrc" "$T/t3"
# Verplaats S1 naar 8 dagen geleden 10:00 en S2 naar eergisteren 10:00 (lokale tijd); S3 blijft 'nu'.
N1=$(snapid "8 days ago" 10:00 1); N2=$(snapid "2 days ago" 10:00 2)
rename_snap "$BK/tijd" "$S1" "$N1"; rename_snap "$BK/tijd" "$S2" "$N2"
"$B" verify tijd laatste >/dev/null || die "keten na hernoemen ongeldig"
rs(){ local d; d=$(mktemp -u "$T/rt.XXXXXX")
      "$B" restore tijd "$@" --to "$d" > "$d.out" 2>&1 || { cat "$d.out" >&2; die "restore $*"; }
      cp "$d.out" "$T/rt_last.out"; echo "$d/files/source_0"; }
same "$T/t1" "$(rs "$(date -d '8 days ago' +%F)")"            "datum JJJJ-MM-DD"
same "$T/t1" "$(rs "$(date -d '8 days ago' +%d-%m-%Y)")"      "datum DD-MM-JJJJ"
same "$T/t2" "$(rs eergisteren)"                                "eergisteren"
same "$T/t2" "$(rs gisteren)"                                   "gisteren (geen snapshot die dag)"
grep -q "Let op: op" "$T/rt_last.out" || die "geen melding bij dag zonder snapshot"
same "$T/t2" "$(rs "$(nlday '2 days ago')")"                    "weekdag ($(nlday '2 days ago'))"
same "$T/t3" "$(rs "$(nlday today)")"                           "weekdag van vandaag"
same "$T/t1" "$(rs vorige "$(nlday today)")"                    "vorige $(nlday today)"
same "$T/t3" "$(rs vandaag)"                                    "vandaag"
same "$T/t1" "$(rs "$(date -d '2 days ago' +%F)" 09:00)"        "dag + tijd voor de snapshot"
same "$T/t2" "$(rs "$(date -d '2 days ago' +%F) om 10:00")"     "dag + tijd van de snapshot"
same "$T/t1" "$(rs 3 dagen geleden)"                            "3 dagen geleden"
same "$T/t2" "$(rs 1 uur geleden)"                              "1 uur geleden"
same "$T/t3" "$(rs latest)"                                     "latest"
must_fail "voor de oudste back-up" "$B" restore tijd 9 dagen geleden --to "$T/nee1"
must_fail "datum in de toekomst" "$B" restore tijd "$(date -d tomorrow +%F)" --to "$T/nee2"
must_fail "onzin" "$B" restore tijd blauwdag --to "$T/nee3"
"$B" list tijd gisteren > "$T/o"; grep -q "wordt teruggezet" "$T/o" || die "list WANNEER markeert niets"
"$B" verify tijd eergisteren > /dev/null || die "verify op dag"
ok "terugzetten op datum, weekdag, gisteren/eergisteren, vorige <dag>, tijd, 'X geleden'"

# ========================================================== 4. --only en --original
must_fail "--only zonder match (b.cpp is verwijderd)" "$B" restore std laatste --only 'sub/*' --to "$T/r_only2"
"$B" restore std "$FIRST" --only b.cpp --to "$T/r_only3" >/dev/null
[[ -f "$T/r_only3/files/source_0/sub/b.cpp" && ! -e "$T/r_only3/files/source_0/a.txt" ]] || die "--only op bestandsnaam"
"$B" restore std "$FIRST" --only sub --to "$T/r_only4" >/dev/null
[[ -f "$T/r_only4/files/source_0/sub/b.cpp" ]] || die "--only op map"
ok "--only (patroon, bestandsnaam, map)"

mkdir -p "$T/osrc/d"; echo "goed" > "$T/osrc/d/x.txt"; echo "ook goed" > "$T/osrc/y.txt"; echo "los" > "$T/losbestand.txt"
"$B" init orig "$T/osrc" "$T/losbestand.txt" --no-schedule >/dev/null; "$B" run orig >/dev/null
echo "kapot" > "$T/osrc/d/x.txt"; rm "$T/osrc/y.txt"; echo "na de back-up" > "$T/osrc/z.txt"; echo "weg" > "$T/losbestand.txt"
must_fail "--original zonder --yes en zonder terminal" "$B" restore orig laatste --original </dev/null
[[ "$(cat "$T/osrc/d/x.txt")" == kapot ]] || die "--original zonder bevestiging wijzigde toch iets"
"$B" restore orig laatste --original --dry-run > "$T/o"
grep -q "overschrijven (nu anders):           2" "$T/o" || { cat "$T/o"; die "dry-run telt fout"; }
[[ "$(cat "$T/osrc/d/x.txt")" == kapot ]] || die "dry-run wijzigde iets"
"$B" restore orig laatste --original --yes > "$T/o"
[[ "$(cat "$T/osrc/d/x.txt")" == goed && "$(cat "$T/osrc/y.txt")" == "ook goed" && "$(cat "$T/losbestand.txt")" == los ]] || die "--original zette niet goed terug"
[[ -f "$T/osrc/z.txt" ]] || die "--original verwijderde een nieuwer bestand"
grep -rqx kapot "$BK/orig/vervangen" || die "vorige versie niet bewaard in vervangen/"
"$B" restore orig laatste --original --yes > "$T/o"; grep -q "niets te doen" "$T/o" || die "tweede --original niet idempotent"
ok "--original: bevestiging, dry-run, losse bronbestanden, vorige versies bewaard, niets verwijderd"

# ========================================================== 5. compressietypes en niveaus
mkdir -p "$T/csrc"; for i in $(seq 1 200); do echo "regel $i van een tekstbestand dat goed comprimeert"; done > "$T/csrc/tekst.txt"; head -c 5000 /dev/urandom > "$T/csrc/bin.dat"
"$B" init comp "$T/csrc" --no-schedule >/dev/null
declare -A EXT=([zstd]=.tar.zst [gzip]=.tar.gz [xz]=.tar.xz [bzip2]=.tar.bz2 [lz4]=.tar.lz4 [zip]=.zip [none]=.tar)
for c in zstd gzip xz bzip2 lz4 zip none; do
  "$B" config comp set compression "$c" >/dev/null
  "$B" run comp --type full >/dev/null
  id=$(newest "$BK/comp"); [[ -f "$BK/comp/snap_$id${EXT[$c]}" ]] || die "$c: archief heeft geen extensie ${EXT[$c]}"
  "$B" restore comp laatste --to "$T/rc_$c" >/dev/null; same "$T/csrc" "$T/rc_$c/files/source_0" "restore $c"
done
if command -v unzip >/dev/null; then
  zipid=$(ls "$BK/comp" | grep '\.zip$' | head -1); unzip -l "$BK/comp/$zipid" | grep -q "files/source_0/tekst.txt" || die "zip niet leesbaar met unzip"
fi
"$B" config comp set compression xz:9 >/dev/null; grep -q '^compression_level=9$' "$HOME/.config/cbackup/jobs/comp.conf" || die "niveau niet opgeslagen"
"$B" config comp set compression zstd:19 >/dev/null; "$B" run comp --type full >/dev/null
"$B" config comp set compression_level 1 >/dev/null; grep -q '^compression_level=1$' "$HOME/.config/cbackup/jobs/comp.conf" || die "compression_level"
"$B" config comp set compression gzip >/dev/null; grep -q '^compression_level=0$' "$HOME/.config/cbackup/jobs/comp.conf" || die "niveau niet gereset bij ander type"
must_fail "zstd:23" "$B" config comp set compression zstd:23
must_fail "none:3"  "$B" config comp set compression none:3
must_fail "rar"     "$B" config comp set compression rar
# Eenmalig ander type via run; config blijft gzip. Gemengde keten: gzip full + zip incr + xz incr.
"$B" run comp --type full >/dev/null
echo extra >> "$T/csrc/tekst.txt"; "$B" run comp --compression zip >/dev/null; [[ $(newest "$BK/comp") ]] && ls "$BK/comp" | grep -q "$(newest "$BK/comp")\.zip$" || die "run --compression zip"
echo meer >> "$T/csrc/tekst.txt"; "$B" run comp --compression xz:3 >/dev/null; ls "$BK/comp" | grep -q "$(newest "$BK/comp")\.tar\.xz$" || die "run --compression xz:3"
grep -q '^compression="gzip"$' "$HOME/.config/cbackup/jobs/comp.conf" || die "run --compression wijzigde de config"
"$B" restore comp laatste --to "$T/rc_mix" >/dev/null; same "$T/csrc" "$T/rc_mix/files/source_0" "gemengde compressieketen"
"$B" compression | grep -q "^  zip " || die "compressie-overzicht"
ok "compressie: zstd gzip xz bzip2 lz4 zip none, niveaus, eenmalige keuze, gemengde keten"

# ========================================================== 6. bewaarbeleid (keep_chains + keep_days)
mkdir -p "$T/bsrc"; echo x > "$T/bsrc/f"
"$B" init bewaar "$T/bsrc" --no-schedule >/dev/null
"$B" run bewaar --type full >/dev/null; P1=$(newest "$BK/bewaar")
"$B" run bewaar --type full >/dev/null; P2=$(newest "$BK/bewaar")
rename_snap "$BK/bewaar" "$P1" "$(snapid '20 days ago' 10:00 1)"; rename_snap "$BK/bewaar" "$P2" "$(snapid '10 days ago' 10:00 2)"
"$B" config bewaar set keep_chains 1 >/dev/null
"$B" run bewaar --type full >/dev/null
[[ $(count_snaps "$BK/bewaar") -eq 3 ]] || die "keep_days 14 verwijderde te veel"
"$B" config bewaar set keep_days 5 >/dev/null
"$B" run bewaar --type full >/dev/null
[[ $(count_snaps "$BK/bewaar") -eq 3 ]] || die "keep_days 5: verwacht 3 snapshots, gevonden $(count_snaps "$BK/bewaar")"
"$B" restore bewaar "$(date -d '6 days ago' +%F)" --to "$T/rb" >/dev/null || die "moment binnen keep_days niet meer terug te zetten"
"$B" config bewaar set keep_days 0 >/dev/null
"$B" run bewaar --type full >/dev/null
[[ $(count_snaps "$BK/bewaar") -eq 1 ]] || die "keep_days 0 + keep_chains 1: verwacht 1 snapshot"
ok "bewaarbeleid: keep_days beschermt, keep_chains ruimt op"

# ========================================================== 7. cloud (nep-rclone), mirror, integriteit
"$B" config std set cloud fake:backups >/dev/null
: > "$HOME/rclone.log"; echo delta >> "$T/src/a.txt"; "$B" run std >/dev/null
[[ $(grep -c "rclone copy" "$HOME/rclone.log") -eq 2 ]] || { cat "$HOME/rclone.log"; die "verwacht 2 rclone-aanroepen per run"; }
[[ $(ls "$FAKE_CLOUD/backups/std" | grep -c manifest) -eq $(count_snaps "$J") ]] || die "niet alle snapshots in de cloud"
cp -a "$T/src" "$T/s_cloud"; rm -rf "$J"; "$B" cloud-pull std >/dev/null
"$B" restore std laatste --to "$T/r_cloud" >/dev/null; same "$T/s_cloud" "$T/r_cloud/files/source_0" "restore na cloud-pull"
"$B" config std set cloud '' >/dev/null
ok "cloud: upload in 2 aanroepen, cloud-pull + restore"

"$B" run std --type mirror >/dev/null; same "$T/src" "$J/mirror/files/source_0" "mirror"
rm "$T/src/c.txt"; "$B" run std --type mirror >/dev/null; [[ ! -e "$J/mirror/files/source_0/c.txt" ]] || die "mirror hield verwijderd bestand"
ok "mirror"

arc=$(ls "$J" | grep -E '^snap_.*\.tar\.zst$' | sort | tail -1); printf x >> "$J/$arc"
must_fail "beschadigd archief" "$B" verify std laatste
must_fail "restore uit beschadigd archief" "$B" restore std laatste --to "$T/r_bad"
ok "integriteitscontrole"

# ========================================================== 8. oude config zonder nieuwe sleutels
cat > "$HOME/.config/cbackup/jobs/oudconf.conf" <<EOF
# cbackup 1.0; wijzig via cbackup config
dest="$BK"
usb=""
cloud=""
mode="auto"
compression="gzip"
full_every=7
keep_chains=3
source="$T/src"
EOF
"$B" config oudconf show > "$T/o"; grep -q "Planning: uit" "$T/o" && grep -q "minimaal 14 dagen" "$T/o" || { cat "$T/o"; die "oude config"; }
ok "oude configbestanden blijven werken"

# ========================================================== 9. (optioneel) back-ups van een oudere versie
if [[ -n "$OLD" ]]; then
  mkdir -p "$T/vsrc"; echo een > "$T/vsrc/1.txt"
  "$OLD" init oud >/dev/null; "$OLD" config oud add-source "$T/vsrc" >/dev/null
  "$OLD" run oud >/dev/null; V1=$(newest "$BK/oud"); cp -a "$T/vsrc" "$T/v1"
  echo twee > "$T/vsrc/2.txt"; "$OLD" run oud >/dev/null; cp -a "$T/vsrc" "$T/v2"
  head -1 "$BK/oud/snap_$V1.manifest" | grep -q "CBACKUP.1" || die "oude versie schreef geen v1-manifest"
  "$B" restore oud laatste --to "$T/rv2" >/dev/null; same "$T/v2" "$T/rv2/files/source_0" "oude back-up terugzetten"
  "$B" restore oud vandaag --to "$T/rv2b" >/dev/null; same "$T/v2" "$T/rv2b/files/source_0" "oude back-up op dag"
  echo drie > "$T/vsrc/3.txt"; "$B" run oud > "$T/o"; grep -q "Type: incremental" "$T/o" || die "nieuwe versie zet oude keten niet voort"
  "$B" restore oud laatste --to "$T/rv3" >/dev/null; same "$T/vsrc" "$T/rv3/files/source_0" "gemengde v1/v2-keten"
  echo kapot > "$T/vsrc/1.txt"; "$B" restore oud "$V1" --original --yes >/dev/null
  [[ "$(cat "$T/vsrc/1.txt")" == een ]] || die "--original met v1-manifest"
  ok "back-ups van de oude versie: terugzetten, voortzetten, --original"
fi

echo
echo "Alle $PASS testgroepen geslaagd."
