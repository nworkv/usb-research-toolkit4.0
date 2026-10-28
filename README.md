# USB Research Toolkit 0.4.1



## Сбор USB-трассы, когда на тестовом хосте разрешён только Wireshark

Используйте [docs/wireshark-only-host.md](docs/wireshark-only-host.md): Raspberry Pi раздаёт SMB-шару, Wireshark на Windows сохраняет PCAPNG прямо на неё, а `scripts/pcap-watch.py` на Pi автоматически индексирует завершённые файлы. На тестовом хосте не запускаются агент проекта или дополнительные скрипты. PCAPNG — это USB-трасса, не Windows Event Log и не доказательство BSOD.

Исследовательский стенд для USB 2.0: генерация вариативных профилей устройств, эмуляция USB-периферии на Raspberry Pi 4, запись USB-захвата на тестовом хосте и сравнение наблюдений устройства с журналом Linux/Windows.

> **Статус проекта:** исследовательский прототип. C++-утилиты и автоматические тесты собираются локально. Физическое подключение к вашему Raspberry Pi 4 и тест на Windows 10 должны быть проверены отдельно. Raw Gadget — низкоуровневый отладочный интерфейс ядра Linux, а не универсальный production backend.

## Исправления v0.4.1

- `--mutations N` означает максимум N изменений полей на один профиль, а не случайную массовую порчу всего дерева.
- Числовые значения остаются в пределах ширины поля, чтобы сериализуемые исследовательские варианты могли быть переданы устройству.
- В режиме `research` семантически некорректные, но сериализуемые дескрипторы могут пройти к Raw Gadget только при явном `--allow-research`. Ошибки сериализации/ширины поля по-прежнему блокируют запуск. Используйте исключительно собственный изолированный стенд.
- `START_FAILED` теперь явно попадает в очередь ручной проверки, а не теряется при анализе PCAPNG. Такой случай не считается реакцией Windows: устройство не запустилось.

## 1. Самое главное — одна утилита `usb-lab`

Удобная оболочка объединяет типовые действия и доступна на Python 3.10+:

- `profiles` — список встроенных типов USB-устройств;
- `generate` — выпуск вариативных профилей из шаблона или своего JSON;
- `validate` — проверка профиля;
- `probe` и `device` — диагностика и запуск USB Gadget на Raspberry Pi/Linux;
- `capture` — захват host-stack в PCAPNG через Wireshark/TShark;
- `normalize` — перевод PCAP/PCAPNG в JSONL;
- `compare` — сопоставление журнала Raspberry Pi с PCAP или нормализованным журналом и создание JSON + Markdown отчётов.

Запустить интерактивное меню из корня репозитория:

```bash
./usb-lab
# или
python scripts/usb-lab.py
```

На Windows используйте `usb-lab.cmd` или `py -3`:

```powershell
py -3 .\scripts\usb-lab.py
```

## 2. Что умеет проект

### Генерация USB-профилей

Встроенные шаблоны:

| Короткое имя | Шаблон | Назначение и ограничения |
|---|---|---|
| `vendor` | `vendor-bulk` | Vendor-specific устройство с Bulk IN/OUT для тестов передачи данных |
| `cdc` | `cdc-acm-serial` | Профиль CDC ACM с коммуникационным и data-интерфейсами; проверяйте class-запросы и привязку драйвера на целевом хосте |
| `hid` | `hid-mouse` | HID-мышь, которая передаёт нейтральный отчёт без движения |
| `mass-storage` | `msc-bot-descriptor-only` | **Только дескрипторы** Mass Storage BOT; не является готовым диском, потому что SCSI/BOT обработчик и backing storage пока не реализованы |

Шаблоны хранятся в `profiles/templates/`. Это лабораторные примеры, а не полные копии конкретных коммерческих моделей.

Генератор изменяет выбранные поля на базе шаблона: PID и ревизию, manufacturer/product/serial строки, потребление питания, адреса endpoints, размер пакета, интервал, `bcdUSB`, классы/подклассы/протоколы устройства и интерфейсов, EP0 packet size, тип передачи endpoint, атрибуты и значение конфигурации. В режиме `safe` пересчитываются предусмотренные структурные поля; фиксированный `--seed` делает набор воспроизводимым.

`safe` означает корректную структуру и диапазоны полей согласно правилам текущего валидатора, но **не гарантирует совместимость с классом устройства**: например, мутация `interface-class` может изменить выбор драйвера хоста. Для обычных профилей используйте набор полей по умолчанию. Чтобы варьировать расширенный набор полей, задайте `--fields all`; проверяйте результат `usb-config validate` и используйте исследовательские изменения только на изолированном стенде.

### Эмуляция устройства

`usb-connect` работает на Raspberry Pi/Linux через Raw Gadget. Он обрабатывает базовые стандартные control-запросы, читает/возвращает доступные данные endpoint и пишет события в JSONL. Реальная работоспособность зависит от ядра, UDC и возможностей endpoint.

### Захват и сравнение

На тестовом компьютере `usb-lab capture` сохраняет `.pcapng`. `usb-lab compare` может самостоятельно нормализовать такой файл и выпустить два отчёта:

- JSON: полные счётчики, пары событий, непарные сигнатуры, ошибки и явные события отключения;
- Markdown: краткий читаемый вывод на русском языке.

Итоговые статусы:

- `CORRELATED` — найденные сравнимые сигнатуры совпали;
- `PARTIAL_CORRELATION` — есть совпадения и непарные наблюдения;
- `NO_CORRELATION` — сравнимые сигнатуры не сопоставлены;
- `DISCONNECT_OBSERVED` — устройство явно зарегистрировало событие отключения;
- `ERROR_OR_STALL_OBSERVED` — в журнале обнаружена ошибка/STALL/тайм-аут;
- `NO_HOST_EVIDENCE` — нормализованный захват пуст;
- `POSSIBLE_DISCONNECT` — некоторые host status коды совместимы с исчезновением устройства, но требуют проверки.

Эти статусы помогают локализовать проблему, но не должны толковаться шире, чем позволяет источник данных.

## 3. Сборка

### Требования

- C++20-компилятор;
- CMake 3.20+;
- Python 3.10+;
- Wireshark/TShark — для захвата и импорта PCAP/PCAPNG;
- Linux и ядро с USB Gadget/UDC/Raw Gadget — для запуска эмулируемого устройства на Raspberry Pi.
- Необязательно: `libusb-1.0` development package для `usb-host-test`.

На Raspberry Pi OS/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake python3
# Необязательно, для usb-host-test:
sudo apt install libusb-1.0-0-dev pkg-config
```

На Windows 10 установите CMake, Visual Studio с C++ workload, Python 3 и Wireshark с USBPcap. `usb-connect` запускается на Raspberry Pi/Linux; на Windows используются генератор/валидатор и host capture/analyzer.

Собрать проект и выполнить тесты:

```bash
python scripts/usb-lab.py build
```

Эквивалентные команды CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Проверить окружение:

```bash
python scripts/usb-lab.py doctor
```

## 4. Работа с профилями

### Посмотреть список

```bash
python scripts/usb-lab.py profiles
python scripts/usb-lab.py show --type cdc
```

Можно посмотреть JSON напрямую в `profiles/templates/`.

### Сгенерировать варианты распространённых типов

10 разных лабораторных вариантов CDC ACM:

```bash
python scripts/usb-lab.py generate \
  --type cdc --count 10 --seed 42 \
  --mode safe --out-dir profiles/generated/cdc-session
```

20 вариантов HID:

```bash
python scripts/usb-lab.py generate \
  --type hid --count 20 --seed 12345 \
  --mode safe --out-dir profiles/generated/hid-session
```

Профили Mass Storage (дескрипторы; **не подключайте, ожидая рабочий диск**):

```bash
python scripts/usb-lab.py generate \
  --type mass-storage --count 5 --seed 77 \
  --mode safe --out-dir profiles/generated/msc-descriptor-tests
```

Варианты собственного профиля:

```bash
python scripts/usb-lab.py generate \
  --base profiles/valid/minimal-device.json \
  --count 20 --seed 20261023 \
  --fields pid,revision,strings,power,endpoint-addresses,packet-size,interval \
  --mode safe --out-dir profiles/generated/my-device
```

Группы `--fields`: `pid`, `revision`, `strings`, `manufacturer`, `product`, `serial`, `power`, `endpoint-addresses`, `packet-size`, `interval`, `bcd-usb`, `device-class`, `device-subclass`, `device-protocol`, `interface-class`, `interface-subclass`, `interface-protocol`, `ep0-packet-size`, `endpoint-transfer-type`, `configuration-attributes`, `configuration-value`. Значение `all` включает все группы; список можно передать через запятую.

Режим `research` намеренно вводит одно структурное несоответствие в профиль и поэтому может приводить к отказу enumeration:

```bash
python scripts/usb-lab.py generate \
  --type vendor --count 8 --seed 42 \
  --mode research --out-dir profiles/generated/research-cases
```

Используйте Research mode только на изолированном тестовом хосте, которым вы управляете. Для активации такого профиля `usb-connect` требует явный флаг `--allow-research`.

Проверить профиль:

```bash
python scripts/usb-lab.py validate profiles/valid/minimal-device.json
```

**VID/PID.** Значения шаблонов служат примером. Перед подключением замените их идентификаторами, использование которых разрешено для вашей лаборатории. Не выдавайте себя за существующее коммерческое устройство и не подключайте намеренно некорректные профили к чужим машинам.

## 5. Настройка Raspberry Pi 4

### Подключение

Для режима USB device на Raspberry Pi 4 Model B используется **USB-C-разъём самой платы** — подключите его кабелем передачи данных к USB-порту тестового хоста. USB-A разъёмы Raspberry Pi остаются host-портами для периферии. При питании Pi от USB хоста проверьте, что порт обеспечивает достаточную мощность; при нестабильном питании используйте подходящий вариант питания/измерительный USB-хаб.

### Диагностика

```bash
python scripts/usb-lab.py probe
```

Скрипт ничего не меняет в загрузочной конфигурации. Проверьте `/boot/firmware/config.txt` (на старых образах путь может отличаться) и режим DWC2. При необходимости параметр может выглядеть так:

```ini
dtoverlay=dwc2,dr_mode=peripheral
```

Не добавляйте строку вслепую, если есть существующий overlay или функция gadget уже управляет тем же контроллером. После изменений может потребоваться перезагрузка.

Raw Gadget должен быть включён в ядре (`CONFIG_USB_RAW_GADGET`) и доступен как `/dev/raw-gadget`; UDC должен отображаться под `/sys/class/udc`. В новых образах Raspberry Pi OS сервис `rpi-usb-gadget` может уже занимать контроллер — перед запуском стенда выясните, какой gadget привязан, и не запускайте два конфликтующих backend одновременно.

Команда `probe` выведет имена драйвера и устройства, которые понадобятся `usb-connect`. После проверки запустите корректный профиль:

```bash
sudo python scripts/usb-lab.py device \
  --profile profiles/valid/minimal-device.json \
  --udc-driver '<имя-драйвера-из-probe>' \
  --udc-device '<имя-UDC-из-probe>' \
  --trace runs/test-001/device.jsonl \
  --run-id test-001
```

Остановить — `Ctrl+C`. Для генерации сначала используйте `safe` профиль, который проходит `usb-config validate`. Не используйте Research-профиль, пока не проверили восстановление платы по Ethernet/Wi-Fi/UART: ошибочная конфигурация может перестать перечисляться на тестовом хосте.

## 6. Захват PCAPNG на хосте

Захват запускается **на тестовом компьютере**, а не на Raspberry Pi.

### Linux

Установите Wireshark/TShark, подключите Pi к USB-C хоста и посмотрите интерфейсы:

```bash
tshark -D
```

Для Linux USB capture обычно подходит `usbmon0` для всех шин либо `usbmonN` для конкретной шины. Запустите захват:

```bash
python scripts/usb-lab.py capture \
  --out runs/test-001/host-linux.pcapng --interface usbmon0
```

Если интерфейс не указан, откроется список для выбора. На Linux захват может потребовать `sudo` или настроенных прав на capture.

### Windows 10

Установите Wireshark с компонентом USBPcap. В PowerShell:

```powershell
py -3 .\scripts\usb-lab.py capture `
  --out runs\test-001\host-windows.pcapng
```

Выберите USBPcap-интерфейс, соответствующий контроллеру, к которому подключён Pi. Перечень интерфейсов также показывает команда `tshark -D`. Начните захват **до подключения/переподключения устройства**, затем выполните эксперимент и остановите запись `Ctrl+C`.

Полный bus capture может включать другие USB-устройства. По возможности используйте отдельный тестовый порт/контроллер и проверяйте device address в захвате.

## 7. Сравнение с журналом Raspberry Pi

`compare` понимает нормализованный JSONL и исходные `.pcap`/`.pcapng`. Для PCAPNG нормализация выполняется автоматически, требуется TShark.

### Автоматически из PCAPNG

Linux:

```bash
python scripts/usb-lab.py compare \
  --device-trace runs/test-001/device.jsonl \
  --host runs/test-001/host-linux.pcapng \
  --source linux_host \
  --out runs/test-001/report.json
```

Windows:

```powershell
py -3 .\scripts\usb-lab.py compare `
  --device-trace runs\test-001\device.jsonl `
  --host runs\test-001\host-windows.pcapng `
  --source windows_host `
  --out runs\test-001\report.json
```

Рядом с JSON будут созданы:

- `report.md` — отчёт на русском для быстрого просмотра;
- `report.host-normalized.jsonl` — нормализованные события хоста.

### Вручную: PCAPNG → JSONL → сравнение

```bash
python scripts/usb-lab.py normalize \
  --pcap runs/test-001/host-linux.pcapng \
  --out runs/test-001/host.jsonl --source linux_host

./build/usb-trace summarize runs/test-001/device.jsonl
./build/usb-trace summarize runs/test-001/host.jsonl
./build/usb-trace compare runs/test-001/device.jsonl runs/test-001/host.jsonl
```

Если нормализатор сообщает, что поля USB недоступны в вашей версии TShark, проверьте список полей:

```bash
tshark -G fields | grep -E 'usb\.(setup|endpoint_address|data_len|capdata)'
```

На PowerShell используйте `Select-String` вместо `grep`.

## 8. Как интерпретировать расхождения

Система сопоставляет:

- control SETUP-запросы по `bmRequestType`, `bRequest`, `wValue`, `wIndex`, `wLength`;
- передачи endpoint, когда в обоих источниках есть endpoint, направление, фактическая длина и идентичные доступные байты payload.

Непарное наблюдение значит «в другом журнале не найдено событие с такой сигнатурой», а не автоматически «пакет потерян». Причиной могут быть разные точки наблюдения, неполный capture, адрес устройства, пропущенные payload, повторная передача или разный уровень представления.

**Важное ограничение USB:** `usbmon` в Linux и USBPcap в Windows обычно регистрируют передачи на уровне USB host stack/URB. Они не являются заменой аппаратному анализатору USB 2.0 и не доказывают прохождение каждого физического токена, ACK/NAK и CRC. Для выводов о фактических транзакциях на проводе требуется подходящий аппаратный USB-анализатор.

Явный `USB_DISCONNECT` в журнале Raw Gadget — сильное наблюдение о том, что отключение сообщил контроллер/ядро. Но пустой host capture сам по себе не доказывает разрыв: сначала проверьте выбранный интерфейс, наличие захвата до подключения и доступность USBPcap/usbmon.

## 9. Что пока не реализовано

- Mass Storage шаблон описывает дескрипторы, но не реализует BOT CBW/CSW, SCSI и образ диска; поэтому не появится полноценный рабочий USB-диск.
- Isochronous передача в Raw Gadget backend не реализована.
- Alternate setting ограничены текущей обработкой; перед использованием не-нулевых вариантов их поддержку нужно дописать и проверить.
- CDC ACM является лабораторным профилем, а не гарантированно совместимым серийным адаптером для всех ОС.
- Программа не обещает определять физическую потерю каждого пакета только по софтовому pcap.
- Возможности endpoint и допустимые дескрипторы зависят от конкретного UDC; успешная валидация JSON не гарантирует, что контроллер может выполнить конфигурацию.

## 10. Структура репозитория

```text
apps/                 C++ CLI: usb-config, usb-connect, usb-trace, usb-profile-gen
include/              общие типы и интерфейсы
src/                  компилятор дескрипторов, Raw Gadget, trace
profiles/templates/   шаблоны vendor, CDC ACM, HID mouse, MSC BOT descriptors
profiles/valid/       базовый корректный профиль
profiles/research/    профиль для демонстрации намеренной ошибки
scripts/usb-lab.py    меню и единая командная оболочка
scripts/              захват, нормализация PCAPNG и настройка Pi
schemas/              JSON Schema
runs/                 каталоги журналов и отчётов (локальные результаты)
tests/                C++ и Python тесты
```


## Передача системных журналов с хоста на Raspberry Pi

Добавлены `scripts/host-log-agent.py` (запускается на тестируемом Linux/Windows-хосте) и `scripts/host-log-receiver.py` (запускается на Raspberry Pi). Агент непрерывно формирует локальный JSONL и пересылает события на Pi; receiver автоматически создаёт текстовый `host-HOSTNAME.log` для сигнатурного анализа и структурированный `host-HOSTNAME.jsonl`. Настройка, запуск и ограничения описаны в [docs/host-log-collection.md](docs/host-log-collection.md). В кампании используйте `--host-log runs/host-logs/host-HOSTNAME.log --host-log-settle 6`.

## Лабораторный fuzz-модуль (добавлено в сборку toolkit)

`scripts/usb-fuzz.py` создаёт воспроизводимые мутации JSON-профилей и, при явном разрешении, запускает последовательную кампанию через предоставленную команду-runner. Генерация по умолчанию не подключает USB-устройство к хосту.

Генерация 100 вариантов с фиксированным seed:

```bash
python scripts/usb-fuzz.py generate \
  --base profiles/templates/hid-mouse.json \
  --count 100 --seed 20261028 --mutations 3 \
  --out-dir runs/fuzz/hid-100
```

То же через общую оболочку:

```bash
python scripts/usb-lab.py fuzz generate --base profiles/templates/hid-mouse.json --count 100 --seed 20261028 --out-dir runs/fuzz/hid-100
```

`manifest.json` сохраняет seed, хеш исходного профиля, SHA-256 каждого варианта и описание изменений. Повторный запуск с теми же входными данными даёт те же профили. Варианты получают `validation.mode=research` и `calculated_fields=manual`; перед реальным запуском проверьте, что профиль поддерживается текущим backend/UDC.

Для кампании укажите **свой одноразовый тестовый runner**, который принимает путь профиля через `{profile}`, завершает каждый тест с кодом 0 при нормальном результате и ненулевым кодом при подозрении на сбой:

```bash
python scripts/usb-fuzz.py campaign \
  --base profiles/templates/hid-mouse.json --count 100 --seed 20261028 \
  --out-dir runs/fuzz/hid-campaign --runner 'python scripts/my_lab_runner.py --profile {profile}' \
  --timeout 30 --confirm-lab --stop-on-crash
```

`--confirm-lab` обязателен для исполнения runner и означает, что целевой хост изолирован и принадлежит исследователю. `--host-log PATH` позволяет наблюдать **новые строки** в доступном файле журнала; сигнатуры вроде kernel panic/bugcheck помечаются как `SUSPECTED_FAILURE`. Тайм-аут, ненулевой exit code или сигнатура создают `reproducers/case-NNNNN/` с профилем, SHA-256, результатом runner и краткой инструкцией повторения. Отчёт кампании сохраняется в `campaign.jsonl` и `summary.json`.

**Ограничения:** модуль не может сам доказать сбой удалённой Windows/Linux-системы без надёжного внешнего монитора. Отсутствие строки в host log не означает отсутствия сбоя; тайм-аут/ошибка runner — только подозрение. Подключайте эмулируемое устройство только к выделенному тестовому хосту/виртуальной среде и не запускайте кампанию на рабочей системе.

### Автоматическая кампания на Raspberry Pi (v0.4.1)

Добавлен `scripts/usb-gadget-campaign.py`: он генерирует мутации и последовательно запускает каждый профиль через `usb-connect`, держит его подключённым заданное время, посылает `SIGINT` для штатного отключения, пишет отдельную трассу и сохраняет `campaign.jsonl`/`summary.json`. Windows-хосту по-прежнему нужен только Wireshark/USBPcap; runner работает на Pi.

Сначала соберите toolkit и узнайте точные значения UDC:

```bash
sudo python3 scripts/usb-lab.py build
sudo python3 scripts/usb-lab.py probe
```

Сначала проверьте план без подключения устройства:

```bash
python3 scripts/usb-gadget-campaign.py \
  --base profiles/templates/hid-mouse.json \
  --count 3 --seed 20261028 --mutations 1 \
  --udc-driver 'ИМЯ_ДРАЙВЕРА' --udc-device 'ИМЯ_UDC' \
  --out-dir runs/fuzz/smoke --dry-run
```

Реальный короткий прогон в отдельной лаборатории:

```bash
sudo python3 scripts/usb-gadget-campaign.py \
  --base profiles/templates/hid-mouse.json \
  --count 5 --seed 20261028 --mutations 1 \
  --udc-driver 'ИМЯ_ДРАЙВЕРА' --udc-device 'ИМЯ_UDC' \
  --dwell-seconds 10 --settle-seconds 3 \
  --capture-dir /srv/usb-captures \
  --out-dir runs/fuzz/hid-smoke \
  --allow-research --confirm-lab
```

`--allow-research` допускает профили с ручными/исследовательскими дескрипторами; `--confirm-lab` подтверждает, что устройство подключено только к собственному изолированному тестовому хосту. `--capture-dir` не управляет Wireshark, а лишь фиксирует изменившиеся PCAPNG-файлы и их размеры для временной корреляции. Запустите захват в Wireshark до кампании и остановите после неё. Начните с 3–5 случаев и короткой выдержки. При ошибке запуска кампания по умолчанию останавливается; `--continue-on-error` разрешает продолжать, но используйте его только если понимаете причину ошибки.

Runner не может доказать BSOD по PCAPNG и не может надёжно обнаружить зависание Windows без внешнего сигнала. Он автоматизирует мутации, подключение/отключение устройства и сбор локальных трасс, а не подтверждение системного сбоя. Физическую совместимость с конкретным Pi/UDC нужно проверить на стенде.

## 11. Полезные ссылки

- Спецификация USB 2.0: <https://www.usb.org/document-library/usb-20-specification>
- Linux USB Raw Gadget: <https://docs.kernel.org/usb/raw-gadget.html>
- Linux USB Gadget API: <https://docs.kernel.org/driver-api/usb/gadget.html>
- Raspberry Pi — USB Gadget mode, поддерживаемые разъёмы: <https://www.raspberrypi.com/news/usb-gadget-mode-in-raspberry-pi-os-ssh-over-usb/>
- Wireshark — USB capture: <https://wiki.wireshark.org/CaptureSetup/USB>


## 12. Исправления выпуска 0.3.2

- Собран полный чистый архив исходного проекта, без маркеров конфликтов Git.
- Исправлены скрипт нормализации TShark и тестовые файлы для сравнения USB-захватов.
- Нормализатор проверяет доступные поля TShark через `tshark -G fields` и поддерживает современное имя `usb.bmRequestType`, а также старое имя, если оно присутствует в установленной версии.
- Python-скрипты проверены через `py_compile`; автоматические C++/Python-тесты запускаются через CTest.

Для обновления после неудачного Git merge рекомендуется распаковать этот ZIP в новую папку и перенести туда только собственные профили и результаты из `runs/`. Не распаковывайте поверх папки с незавершённым слиянием Git.


## 13. Исправление Raw Gadget endpoint cleanup в версии 0.3.3

В версии 0.3.3 исправлена передача handle в `USB_RAW_IOCTL_EP_DISABLE`: backend передаёт числовой handle как аргумент ioctl, как ожидает реализация Linux Raw Gadget. Ранее передавался адрес переменной с handle, из-за чего ядро интерпретировало адрес как индекс endpoint и возвращало `EBUSY`; endpoint оставался включённым, а следующая enumeration могла завершаться `EBUSY` для `0x81`.

Также добавлены отмена ожидающего endpoint I/O через thread-directed `SIGUSR1`, повтор попытки отключения после завершения worker и сохранение ошибки, если endpoint всё ещё не удалось отключить. Это исправление прошло сборку и автоматические тесты в Linux, но требует отдельной проверки на конкретном UDC/Raspberry Pi.


## 14. v0.3.4: Raw Gadget lifecycle и пустой захват Wireshark

В v0.3.4 исправлен порядок остановки endpoint worker: сначала выставляется stop, worker ioctl прерываются сигналом `SIGUSR1` и присоединяются (`join`), после чего вызывается `USB_RAW_IOCTL_EP_DISABLE`. Ранее backend сначала пытался отключить endpoint, пока `USB_RAW_IOCTL_EP_WRITE/READ` ещё мог держать URB в очереди; Raw Gadget может отклонить такой disable.

`USB_RAW_EVENT_DISCONNECT` больше не считается безусловным завершением процесса. Документация Raw Gadget указывает, что драйвер `dwc2` может сообщать bus reset как `USB_RAW_EVENT_DISCONNECT`; backend очищает worker/endpoints и продолжает ждать повторную enumeration.

Каждый новый `TraceLogger` теперь начинает новый JSONL-файл, а не дописывает предыдущий запуск в файл с повторяющимися sequence numbers. Для каждого запуска используйте отдельные `run-id` и имена файлов.

Профиль HID Mouse обновлён: Report Descriptor теперь описывает 4-байтовый отчёт (кнопки, X, Y и колесо), что соответствует 4-байтовой IN-payload и `wMaxPacketSize=4`.

**Почему Wireshark может быть пустым:** журнал Raw Gadget уже содержит `CONTROL_REQUEST` и `CONTROL_RESPONSE_COMPLETE` — значит, хост отправлял setup-запросы и устройство на них отвечало. Если Wireshark ничего не показывает, проверьте точку захвата. USBPcap/usbmon должны работать на отдельном тестовом HOST-компьютере, который подключён к Raspberry Pi. `usbmon` на самой Raspberry Pi обычно видит её HOST-контроллеры (например, USB-A/xHCI), но не трафик линии, на которой Pi выступает USB-Gadget через UDC. На Linux запустите `sudo modprobe usbmon`, затем `tshark -D` и захватывайте `usbmon0` или интерфейс конкретной шины. На Windows выберите USBPcap-интерфейс того host controller, к которому физически подключена Pi. Начните захват до переподключения кабеля.

Все изменения используют исключительно Linux USB Raw Gadget (`/dev/raw-gadget`); ConfigFS не применяется.

## v0.3.5: диагностика пустого захвата и Raw Gadget reset

### Почему Wireshark может показывать пустой USB-захват

`Raw Gadget` работает на стороне **USB device controller (UDC)**. Linux `usbmon` на Raspberry Pi наблюдает USB-транзакции, которые инициируют USB **host**-контроллеры самой Pi (например, её USB-A порты), а не обязательно кабель, на котором Pi играет роль USB gadget. Для программного PCAPNG запускайте захват на отдельном тестовом хосте: на Linux — `usbmonN`, на Windows — соответствующий `USBPcapN` интерфейс того контроллера, куда физически включена Pi. Начинайте capture до отключения/повторного подключения устройства и временно снимите display/capture filters. `usbmon`/USBPcap фиксируют URB/host-stack transfers, а не каждый физический token/ACK/NAK/CRC; для этого нужен аппаратный USB-анализатор.

### Изменения Raw Gadget

- Endpoint worker-потоки теперь останавливаются и join-ятся до `USB_RAW_IOCTL_EP_DISABLE`.
- На `USB_RAW_EVENT_RESET` и `USB_RAW_EVENT_DISCONNECT` UDC может уже отключить endpoints. `EBUSY` в такой уборке теперь отмечается как `ENDPOINT_ALREADY_INACTIVE`, а не считается фатальным; event loop продолжает ждать enumeration. Особенно важно для DWC2, который может сообщить о reset через `DISCONNECT`.
- IN/OUT worker-потоки запускаются только после завершения status stage `SET_CONFIGURATION`.
- В сравнительном отчёте `USB_RESET_OR_DISCONNECT` не считается доказанным физическим отключением кабеля.
- Новый HID mouse профиль согласует HID report length с 62-байтным report descriptor и 4-байтным отчётом (buttons, X/Y, wheel).

### Проверка трафика

На Pi после запуска `usb-connect` журнал `device.jsonl` должен содержать `CONTROL_REQUEST` и `CONTROL_RESPONSE_COMPLETE` при enumeration, затем `SET_CONFIGURATION`, `DEVICE_IN_TRANSFER_QUEUED` и `DEVICE_IN_TRANSFER_COMPLETE` когда хост опрашивает interrupt IN endpoint. Если control-запросы есть в журнале Raw Gadget, но Wireshark пуст, сначала проверьте точку захвата/выбранный USBPcap или usbmon bus. Если нет `DEVICE_IN_TRANSFER_COMPLETE`, смотрите `DEVICE_IN_TRANSFER_ERROR`, reset/disconnect events и статус устройства в host OS.

### Короткая проверка, если в Wireshark ноль USB-кадров

1. Убедитесь, что кабель USB-C Pi подключён к тестовому хосту, а не к USB-A порту самой Pi.
2. На Windows запустите Wireshark от имени администратора, откройте Capture Options и выберите `USBPcapN`, соответствующий host-контроллеру физического порта. Начните capture до отключения/подключения кабеля. Без display filter проверьте VID/PID профиля (`1209:1200` для включённого шаблона HID в этом проекте; `1209:0012` встречался в прежнем тесте).
3. На отдельном Linux-хосте выполните `sudo modprobe usbmon`, `tshark -D`, затем запишите capture с интерфейса `usbmonN`, который соответствует шине, где появился USB-девайс. `usbmon0` охватывает все host-шины.
4. Не используйте `usbmon` на самой Pi для наблюдения DWC2 UDC-линии: он не является аппаратным ответвителем gadget-трафика. Для анализа реальных USB-токенов используйте аппаратный USB 2.0 анализатор.
5. В `device.jsonl` наличие `CONTROL_REQUEST` подтверждает, что запрос хоста дошёл до Raw Gadget. Наличие `CONTROL_RESPONSE_COMPLETE` подтверждает завершение программной операции ответа, но не является само по себе физическим анализом ACK/CRC. После настройки конфигурации ожидайте `DEVICE_IN_TRANSFER_QUEUED`; при опросе interrupt IN хостом — `DEVICE_IN_TRANSFER_COMPLETE`.


## v0.3.6: воспроизводимые мутации и кампании

- Добавлен `scripts/usb-fuzz.py`: seeded-поколение мутаций USB JSON-профилей, SHA-256, manifest и журнал последовательной кампании.
- Добавлена команда оболочки `usb-lab.py fuzz ...`.
- Кампания запускает только явно указанный одноразовый runner и требует `--confirm-lab`; тайм-ауты, ненулевой код возврата и распознанные host-log signatures сохраняются как подозрительные случаи с точным профилем для повторения.
- Добавлены Python-тесты детерминизма генератора в CTest.
- Сигнатуры в host log являются эвристикой, а не самостоятельным доказательством сбоя ОС.


### Очередь дополнительной проверки (v0.4.1)

- После кампании runner создаёт `review-queue.json`: случаи с USB reset/disconnect/stall/timeout/error, признаками malformed/invalid, отсутствием кадров в окне теста или недостаточными данными.
- Для анализа после сохранения PCAPNG запустите `scripts/review-usb-campaign.py`; это позволяет сначала завершить кампанию, затем остановить/сохранить захват Wireshark и повторно построить очередь.
- Повтор выбранного случая выполняется через `--replay-from <каталог кампании> --replay-case <номер>` и использует исходный `case-NNNNN.json`, а не создаёт новую мутацию.
- Анализ является эвристическим и требует синхронизации часов Pi/Windows. PCAPNG не доказывает BSOD/kernel panic и не гарантирует автоматическое побайтовое сравнение каждого ответа с профилем.
