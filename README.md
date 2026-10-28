# USB Research Toolkit 0.4.1

Документация на русском языке для лабораторного исследования USB-профилей на Raspberry Pi с использованием **только Wireshark + USBPcap на Windows-хосте**. Фаззер, Raw Gadget, `tshark`, анализ PCAPNG и все отчёты работают на Raspberry Pi/Linux.

> **Используйте только собственный изолированный тестовый стенд.** Некорректные USB-дескрипторы могут приводить к отказу перечисления, переподключениям, зависанию USB-устройства или сбоям тестового хоста. Перед длинной кампанией сначала выполните базовый тест и короткий прогон. Не подключайте тестовое устройство к рабочему компьютеру.

## 1. Что делает проект

1. Генерирует вариации JSON-профиля USB-устройства с фиксированным seed.
2. Запускает профили по одному на Raspberry Pi через Linux Raw Gadget.
3. Сохраняет отдельную трассу и stdout/stderr для каждого случая.
4. Сопоставляет время случаев с кадрами PCAP/PCAPNG, захваченными Wireshark/USBPcap на Windows.
5. Создаёт очередь случаев для дополнительного ручного просмотра.
6. Позволяет повторно запустить ранее сохранённый `case-NNNNN.json` после завершения кампании.

**Важно:** `--count 1000` означает тысячу сгенерированных вариантов одного исходного профиля, например HID Mouse, а не тысячу разных физических устройств или классов USB. Уникальность дескрипторов нужно проверять отдельно. `--mutations N` задаёт максимум N изменений полей на профиль; число фактических изменений может быть меньше.

Wireshark/USBPcap показывает USB-трафик, видимый на host stack. Это не системный журнал Windows и не доказательство BSOD/kernel panic. `COMPLETED` означает, что процесс Gadget продержался заданное время и штатно остановился; это не означает, что Windows приняла профиль или работала без ошибок.

## 2. Требования

### Raspberry Pi / Linux

- Raspberry Pi с поддерживаемым USB Device Controller (UDC), подключённый к тестовому ПК портом, способным работать в режиме USB device/peripheral.
- Ядро с Raw Gadget и доступный `/dev/raw-gadget`.
- Python 3.10+, CMake 3.20+, C++20-компилятор.
- `tshark` для разбора сохранённых PCAPNG.
- Samba, если Wireshark будет сохранять захваты на общую папку Pi.

На Raspberry Pi OS/Debian зависимости можно установить так:

```bash
sudo apt update
sudo apt install build-essential cmake python3 samba tshark
```

### Windows test host

- Wireshark с компонентом USBPcap.
- Никакой USB Research Toolkit, Python-агент или дополнительная служба на Windows не требуется.

## 3. Собрать проект и проверить окружение

В корне репозитория на Raspberry Pi:

```bash
cd ~/usb-research-toolkit4.0
python3 scripts/usb-lab.py build
python3 scripts/usb-lab.py doctor
```

`build` собирает C++-утилиты и выполняет CTest. `doctor` проверяет наличие `tshark`, `usb-connect`, `/dev/raw-gadget` и UDC.

Дополнительные проверки:

```bash
ls -l /dev/raw-gadget
ls -l /sys/class/udc/
sudo build/usb-connect probe
```

В выводе `probe` найди **точные** значения `UDC device name` и `Raw Gadget driver name from uevent`. Подставляй их в `--udc-device` и `--udc-driver` соответственно. Например, на некоторых стендах оба значения равны `fe980000.usb`, но не полагайся на это без проверки.

Проверь исходный профиль:

```bash
python3 scripts/usb-lab.py validate profiles/templates/hid-mouse.json --mode strict
```

Если сборка, `probe`, Raw Gadget или проверка исходного профиля завершились ошибкой, **не начинай кампанию** — сначала устрани эту ошибку.

## 4. Настроить папку для Wireshark PCAPNG

Подробное описание находится в [docs/wireshark-only-host.md](docs/wireshark-only-host.md).

Если SMB-шара ещё не настроена, на Pi выполни один раз, заменив `nworkv` на существующего Linux-пользователя, которому разрешена запись:

```bash
sudo bash scripts/setup-wireshark-share.sh /srv/usb-captures nworkv
```

Скрипт устанавливает Samba и `tshark`. Если он сообщает:

```text
A [usb-captures] share already exists in /etc/samba/smb.conf
```

он не добавляет дублирующую секцию и завершается с сообщением о необходимости ручной проверки. Это не доказывает, что существующая шара настроена правильно. Проверь её конфигурацию:

```bash
sudo testparm -s
sudo grep -A12 '^\[usb-captures\]' /etc/samba/smb.conf
ls -ld /srv/usb-captures
```

Не добавляй вторую секцию `[usb-captures]`. Исправь существующую конфигурацию вручную, если путь, пользователи или права не совпадают.

Узнай IP Raspberry Pi:

```bash
hostname -I
```

В Проводнике Windows открой, например:

```text
\\192.168.1.20\usb-captures
```

Замени IP на адрес своей Pi и используй настроенные учётные данные Samba. Держи SMB только в лабораторной сети и не публикуй TCP/445 в Интернет.

### Выбор интерфейса Wireshark

1. Подключи Raspberry Pi к USB-порту выделенного Windows-хоста кабелем с линиями данных.
2. Открой Wireshark и выбери интерфейс `USBPcapN`, относящийся к тому Root Hub/контроллеру, куда подключена Pi.
3. Начни захват **до** запуска `usb-connect`/кампании. Убери display filter, если он скрывает пакеты.
4. После теста останови захват и сохрани уникальный `.pcapng` в папку текущей кампании на SMB-шаре.

USBPcap должен работать на отдельном host-компьютере Windows. `usbmon` на самой Pi обычно показывает передачи, инициированные её USB host-контроллерами, и не заменяет захват host-side трафика на ПК, где Pi выступает USB Gadget.

## 5. Базовый тест без мутаций

Перед фаззингом проверь, что Raw Gadget вообще способен запустить обычный профиль. Начни Wireshark capture на Windows, а на Pi запусти (замени имена UDC на значения `probe`):

```bash
mkdir -p runs/fuzz/baseline
sudo build/usb-connect run \
  profiles/valid/minimal-device.json \
  --backend raw-gadget \
  --udc-driver 'ИМЯ_ДРАЙВЕРА_ИЗ_PROBE' \
  --udc-device 'ИМЯ_UDC_ИЗ_PROBE' \
  --speed high \
  --trace runs/fuzz/baseline/device.jsonl \
  --run-id baseline-check
```

Дождись `Raw Gadget running`, подожди 10–15 секунд и останови `Ctrl+C`. Затем останови и сохрани захват в Wireshark. Проверь журнал:

```bash
less runs/fuzz/baseline/device.jsonl
```

События `UDC_BOUND`, `CONTROL_REQUEST` и `CONTROL_RESPONSE_COMPLETE` подтверждают, что Raw Gadget видит привязку контроллера и управляющие запросы. Если они есть в журнале Pi, но PCAPNG пустой, прежде всего проверь интерфейс USBPcap/Root Hub, кабель, фильтры и время начала захвата. `not attached` в sysfs после остановки Gadget ожидаем и сам по себе не указывает на неисправность.

## 6. Сначала сделай dry-run

`--dry-run` генерирует профили и план, но не запускает USB Gadget. Создай новый каталог с уникальной меткой, чтобы не перезаписать старые результаты:

```bash
cd ~/usb-research-toolkit4.0

UDC_DRIVER='ИМЯ_ДРАЙВЕРА_ИЗ_PROBE'
UDC_DEVICE='ИМЯ_UDC_ИЗ_PROBE'
STAMP=$(date +%Y%m%d-%H%M%S)
PLAN="runs/fuzz/hid-plan-$STAMP"

python3 scripts/usb-gadget-campaign.py \
  --base profiles/templates/hid-mouse.json \
  --count 5 \
  --seed 20261028 \
  --mutations 1 \
  --udc-driver "$UDC_DRIVER" \
  --udc-device "$UDC_DEVICE" \
  --out-dir "$PLAN" \
  --dry-run
```

В этом терминале переменные `UDC_DRIVER`, `UDC_DEVICE` и `PLAN` сохраняются до его закрытия. Команда выведет путь к плану.

Основные файлы плана:

- `case-00001.json`, `case-00002.json`, ... — сохранённые профили;
- `manifest.json` — seed, хеш базового профиля, SHA-256 каждого варианта и список мутаций;
- `campaign.jsonl` — записи `DRY_RUN_ONLY`;
- `summary.json` — подтверждение, что профили сгенерированы, а запусков было `0`.

### Вывести список изменений каждого профиля

```bash
python3 - "$PLAN/manifest.json" <<'PY' > "$PLAN/changes.txt"
import json, sys
from pathlib import Path

manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print("seed:", manifest.get("seed"))
print("base:", manifest.get("base"))
print("base_sha256:", manifest.get("base_sha256"))
print("count:", manifest.get("count"))
for case in manifest.get("cases", []):
    print(f"\ncase-{case['case']:05d}  status={case.get('status')}  sha256={case.get('sha256')}")
    changes = case.get("changes") or []
    if changes:
        for change in changes:
            print("  " + change)
    else:
        print("  Изменения полей не зарегистрированы")
PY

less "$PLAN/changes.txt"
```

Выйти из `less`: клавиша `q`.

Отчёт отражает мутации, зарегистрированные генератором. Если нужна гарантия, что все варианты отличаются по содержимому USB-дескрипторов, дополнительно проверяй уникальность профилей: номера случаев и `profile_name` сами по себе не делают дескрипторы уникальными. Для большого запуска проверь небольшой dry-run и отчёт прежде, чем подключать тестовый хост.

## 7. Короткая реальная кампания на 3–5 случаев

Для настоящего запуска обязательны `--allow-research` и `--confirm-lab`. Эти флаги предназначены только для изолированного стенда с разрешённым тестированием.

Сначала подготовь каталоги и переменные в том же терминале:

```bash
STAMP=$(date +%Y%m%d-%H%M%S)
OUT="runs/fuzz/hid-smoke-$STAMP"
CAPTURE_DIR="/srv/usb-captures/hid-smoke-$STAMP"

sudo install -d -m 2770 -o "$USER" -g "$(id -gn)" "$CAPTURE_DIR"
echo "OUT=$OUT"
echo "CAPTURE_DIR=$CAPTURE_DIR"
```

Если Samba использует отдельного пользователя, назначь владельца каталога именно этому пользователю, чтобы Windows могла сохранять файлы.

На Windows начни новый USBPcap capture. На Pi запусти:

```bash
sudo python3 scripts/usb-gadget-campaign.py \
  --base profiles/templates/hid-mouse.json \
  --count 5 \
  --seed 20261028 \
  --mutations 1 \
  --udc-driver "$UDC_DRIVER" \
  --udc-device "$UDC_DEVICE" \
  --dwell-seconds 10 \
  --settle-seconds 3 \
  --capture-dir "$CAPTURE_DIR" \
  --out-dir "$OUT" \
  --allow-research \
  --confirm-lab
```

По умолчанию runner продолжает после ошибки отдельного случая. Если хочешь остановить кампанию при первой ошибке запуска/раннем завершении, добавь `--stop-on-error`.

После завершения останови Wireshark и сохрани файл, например:

```text
\\192.168.1.20\usb-captures\hid-smoke-ВРЕМЕННАЯ_МЕТКА\campaign.pcapng
```

Используй IP и имя каталога из своего запуска. Проверь, что Pi видит файл:

```bash
find "$CAPTURE_DIR" -maxdepth 1 -type f -printf '%f %s bytes\n'
```

## 8. Запустить кампанию на 50 или 1000 вариантов

Тот же runner поддерживает до 1000 случаев на одну кампанию. Сначала пройди короткий прогон выше и убедись, что профили реально доходят до Raw Gadget. Пример для 50 вариантов:

```bash
STAMP=$(date +%Y%m%d-%H%M%S)
OUT="runs/fuzz/hid-50-$STAMP"
CAPTURE_DIR="/srv/usb-captures/hid-50-$STAMP"
sudo install -d -m 2770 -o "$USER" -g "$(id -gn)" "$CAPTURE_DIR"
echo "OUT=$OUT"
echo "CAPTURE_DIR=$CAPTURE_DIR"

sudo python3 scripts/usb-gadget-campaign.py \
  --base profiles/templates/hid-mouse.json \
  --count 50 --seed 20261028 --mutations 1 \
  --udc-driver "$UDC_DRIVER" --udc-device "$UDC_DEVICE" \
  --dwell-seconds 10 --settle-seconds 3 \
  --capture-dir "$CAPTURE_DIR" --out-dir "$OUT" \
  --allow-research --confirm-lab
```

Для 1000 вариантов измени `--count 50` на `--count 1000` и создай отдельный новый `OUT`/`CAPTURE_DIR`. При `--dwell-seconds 10` и `--settle-seconds 3` номинальное время только на выдержку/паузы — примерно 3 часа 37 минут; запуск, остановка, ошибки и разбор могут увеличить общее время. Для запуска нужна свободная консоль; не прерывай процесс, если не хочешь получить статус `INTERRUPTED`.

**Не запускай тысячу случаев, если smoke-тест всё ещё даёт `START_FAILED`, не видно USB-запросов или не настроен корректный PCAPNG capture.** Отказ Windows принять отдельный некорректный профиль может быть ожидаем, но это нужно отличать от ошибки запуска самого Gadget.

## 9. Посмотреть результаты кампании

Скрипт печатает итоговый каталог и путь к `summary.json`. Поскольку команда запускается через `sudo`, после завершения исправь права на каталог результатов:

```bash
sudo chown -R "$USER:$(id -gn)" "$OUT"
```

### Общая сводка

```bash
python3 -m json.tool "$OUT/summary.json" | less
```

Наиболее важные поля:

- `generated` — число сгенерированных профилей;
- `executed` — число обработанных runner случаев;
- `completed` — процесс Gadget выдержал заданное время и штатно завершился;
- `startup_errors` — ошибки запуска/раннее завершение/ошибка остановки;
- `interrupted_cases` — случаи, прерванные пользователем;
- `review_required`, `review_queue` — размер и путь к очереди ручной проверки.

### Результат каждого случая

```bash
less "$OUT/campaign.jsonl"
```

Каждая строка — отдельная JSON-запись. В ней есть номер случая, путь профиля, SHA-256, временные метки, код завершения, статус, пути к логам и список изменившихся файлов захвата.

Для краткого списка статусов:

```bash
python3 - "$OUT/campaign.jsonl" <<'PY'
import json, sys
from pathlib import Path
for line in Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    if not line.strip(): continue
    r = json.loads(line)
    print(f"case-{r.get('case', 0):05d}  {r.get('status')}  "
          f"{r.get('duration_seconds', '?')} s  "
          f"sha256={r.get('profile_sha256', '?')}")
PY
```

### Какие поля менялись

В `manifest.json` сохраняется массив `changes` для каждого `case`. Создай читаемый отчёт по реальной кампании:

```bash
python3 - "$OUT/manifest.json" <<'PY' > "$OUT/changes.txt"
import json, sys
from pathlib import Path
m = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
for c in m.get("cases", []):
    print(f"\ncase-{c['case']:05d}  sha256={c.get('sha256')}")
    changes = c.get("changes") or []
    for change in changes:
        print("  " + change)
    if not changes:
        print("  Изменения полей не зарегистрированы")
PY

less "$OUT/changes.txt"
```

### Трассы и ошибки

```bash
find "$OUT/traces" -maxdepth 1 -type f -print
```

Для конкретного случая `N` используй номер с ведущими нулями:

```bash
cat "$OUT/traces/case-00001.stdout.log"
cat "$OUT/traces/case-00001.stderr.log"
less "$OUT/traces/case-00001.device.jsonl"
```

В `device.jsonl` ищи `UDC_BOUND`, `CONTROL_REQUEST`, `CONTROL_RESPONSE_COMPLETE`, `CONTROL_STALL`, `DEVICE_IN_TRANSFER_ERROR`, `USB_SUSPEND`, reset/disconnect события. `START_FAILED` — это прежде всего проблема запуска/валидации на Pi, не свидетельство сбоя Windows.

## 10. Анализ PCAPNG и очередь проверки

После окончания кампании обязательно останови Wireshark и сохрани PCAPNG в папку, указанную `--capture-dir`. Убедись, что файл есть на Pi. Затем запусти повторный анализ **после сохранения файла**:

```bash
python3 scripts/review-usb-campaign.py \
  --campaign-dir "$OUT" \
  --capture-dir "$CAPTURE_DIR"
```

Открой очередь:

```bash
python3 -m json.tool "$OUT/review-queue.json" | less
```

В очередь могут попадать:

- `GADGET_START_FAILED` — профиль не запустился на Raspberry Pi;
- `NO_CAPTURE_FRAMES_IN_CASE_WINDOW` — в захвате не нашлось кадров во временном окне случая;
- `WIRESHARK_ANOMALY_TEXT` — в Protocol/Info обнаружен текст, похожий на reset/disconnect/stall/timeout;
- `POSSIBLE_DECODE_OR_DESCRIPTOR_MISMATCH` — Wireshark описывает кадр как malformed/invalid/undecoded;
- `TSHARK_NOT_FOUND`, ошибка чтения PCAPNG или неидентифицированный USB-трафик.

Это **эвристический список для ручной проверки**, а не вердикт об уязвимости. Временная корреляция зависит от синхронизации часов Pi и Windows. Если PCAPNG был сохранён только после остановки кампании, первоначальный runner мог не зарегистрировать его изменение; повторный `review-usb-campaign.py` нужен именно для анализа уже сохранённого файла.

Чтобы проверить отдельный захват вручную:

```bash
tshark -r "$CAPTURE_DIR/campaign.pcapng" -q -z io,phs
```

Путь заменяй на реальное имя файла. Для просмотра кадров открой PCAPNG в Wireshark на Windows или используй `tshark` на Pi.

## 11. Повторить подозрительный профиль после кампании

Например, чтобы повторить случай `3`, нужно сохранить исходный каталог кампании в переменной `OUT`, как в том же терминале, либо вручную задать его точный путь. Повтор использует сохранённый `case-00003.json`, а не создаёт новую мутацию.

Создай отдельный каталог повторного прогона и папку для его захвата:

```bash
CASE=3
STAMP=$(date +%Y%m%d-%H%M%S)
REPLAY_OUT="runs/fuzz/replay-case-$CASE-$STAMP"
REPLAY_CAPTURE_DIR="/srv/usb-captures/replay-case-$CASE-$STAMP"
sudo install -d -m 2770 -o "$USER" -g "$(id -gn)" "$REPLAY_CAPTURE_DIR"
echo "REPLAY_OUT=$REPLAY_OUT"
echo "REPLAY_CAPTURE_DIR=$REPLAY_CAPTURE_DIR"
```

Начни новый USBPcap capture в Wireshark и сохрани его в `REPLAY_CAPTURE_DIR`. Затем на Pi:

```bash
sudo python3 scripts/usb-gadget-campaign.py \
  --base profiles/templates/hid-mouse.json \
  --replay-from "$OUT" --replay-case "$CASE" \
  --udc-driver "$UDC_DRIVER" --udc-device "$UDC_DEVICE" \
  --dwell-seconds 10 --settle-seconds 0 \
  --capture-dir "$REPLAY_CAPTURE_DIR" \
  --out-dir "$REPLAY_OUT" \
  --allow-research --confirm-lab
```

После окончания останови и сохрани захват, затем:

```bash
sudo chown -R "$USER:$(id -gn)" "$REPLAY_OUT"
python3 scripts/review-usb-campaign.py \
  --campaign-dir "$REPLAY_OUT" \
  --capture-dir "$REPLAY_CAPTURE_DIR"
```

Не перезаписывай исходную кампанию или PCAPNG: отдельные каталоги сохраняют воспроизводимость.

## 12. Частые проблемы

### `START_FAILED`

Смотри `$OUT/traces/case-NNNNN.stderr.log`. Частые причины: невалидный/неподдерживаемый профиль, ошибка Raw Gadget, неверные имена UDC, неподдерживаемая скорость или ошибка дескриптора. Кампания не должна рассматриваться как тест реакции Windows, если gadget не запустился.

### `PermissionError` при создании отчёта

Кампания запускалась через `sudo` и файлы принадлежат `root`. Выполни:

```bash
sudo chown -R "$USER:$(id -gn)" "$OUT"
```

### В `device.jsonl` есть `CONTROL_REQUEST`, но Wireshark пуст

Проверь, что захват идёт на отдельном Windows-хосте и на правильном USBPcap Root Hub, что кабель подключён к этому контроллеру и захват начался до теста. Убери display filter. События в `device.jsonl` не гарантируют, что был выбран правильный интерфейс захвата.

### `review-queue.json` пустой

Убедись, что повторный анализ выполнен после сохранения PCAPNG и что путь `--capture-dir` указывает на каталог с этим файлом. Проверь наличие `tshark` командой `which tshark`. Пустая очередь не означает, что хост точно не столкнулся с проблемой.

### `not attached` в `/sys/class/udc/.../state`

Проверь состояние во время работающего `usb-connect`, например во втором терминале через `watch -n 0.5 cat /sys/class/udc/ИМЯ_UDC/state`. После остановки Gadget `not attached` — ожидаемо.

## 13. Как трактовать результаты и ограничения

| Наблюдение | Интерпретация |
|---|---|
| Windows отклонила устройство/enum не завершился | Само по себе не означает уязвимость |
| `COMPLETED` | Процесс Gadget отработал заданное время; здоровье хоста этим не подтверждено |
| `START_FAILED` | Тест не дошёл до полноценного прогона; расследуй локальный журнал Pi |
| USB reset, STALL или timeout | Требует контекста кадра и поведения устройства/хоста |
| Нет кадров в PCAPNG | Может указывать на неверный интерфейс, неподходящее окно времени или отсутствие захвата |
| Хост перестал отвечать по сети | Только косвенный признак, не доказательство BSOD |
| Нет подозрительных строк в Wireshark | Не доказывает отсутствие сбоя или byte-for-byte совпадение |

Проект не устанавливает Windows-агент и не собирает Windows Event Log. Для подтверждения BSOD/kernel crash потребуются независимые подтверждения, например сохранённый дамп или системный журнал, но они вне конфигурации «на хосте только Wireshark». Анализатор этого проекта не делает полного побайтового сравнения каждого ответа с намеренным содержимым JSON — очередь проверки помогает отобрать случаи для ручного исследования.

## 14. Другие команды `usb-lab`

```bash
python3 scripts/usb-lab.py profiles
python3 scripts/usb-lab.py show --type hid
python3 scripts/usb-lab.py validate profiles/valid/minimal-device.json --mode strict
python3 scripts/usb-lab.py generate --type hid --count 10 --seed 42 --mode safe --out-dir profiles/generated/hid-session
```

Другие встроенные шаблоны находятся в `profiles/templates/`:

- `hid-mouse.json` — HID Mouse;
- `cdc-acm-serial.json` — лабораторный профиль CDC ACM;
- `vendor-bulk.json` — vendor-specific Bulk;
- `msc-bot-descriptor-only.json` — только дескрипторы Mass Storage; полноценный BOT/SCSI накопитель не реализован.

Полезные документы:

- [Захват Wireshark при ограничении «только Wireshark на Windows»](docs/wireshark-only-host.md)
- [Архитектура проекта](docs/architecture.md)
- [Ограничения Windows](docs/windows10.md)
- [Сбор host logs для другой конфигурации стенда](docs/host-log-collection.md) — не требуется при использовании только Wireshark на Windows.

Полезные внешние документы:

- [Linux USB Raw Gadget](https://docs.kernel.org/usb/raw-gadget.html)
- [Linux USB Gadget API](https://docs.kernel.org/driver-api/usb/gadget.html)
- [Wireshark USB capture](https://wiki.wireshark.org/CaptureSetup/USB)
- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification)
