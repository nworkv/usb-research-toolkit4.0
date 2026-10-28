## 0.3.9 — автоматический Raw Gadget campaign runner

- Добавлен `scripts/usb-gadget-campaign.py`: генерация seeded-мутаций и последовательный запуск профилей через `usb-connect` на Raspberry Pi.
- Каждый случай получает отдельный JSONL trace, stdout/stderr, run-id, SHA-256 и временные метки; сохраняются `campaign.jsonl` и `summary.json`.
- Штатная остановка профиля выполняется через SIGINT с ограниченным ожиданием и эскалацией только при зависании процесса.
- Добавлен dry-run для проверки плана без подключения USB; реальный запуск требует явных `--allow-research` и `--confirm-lab`.
- Добавлена необязательная временная корреляция с изменениями размеров PCAPNG в каталоге захватов. Это не является обнаружением или доказательством BSOD.

# История изменений

## 0.3.8 — Wireshark-only host

- Добавлена настройка SMB-шары на Raspberry Pi для прямой записи PCAPNG с Wireshark.
- Добавлен `scripts/pcap-watch.py`: автоматическое обнаружение стабильных PCAP/PCAPNG, анализ через `tshark` на Pi, SHA-256 и JSONL-индекс.
- Добавлена инструкция `docs/wireshark-only-host.md`.
- Тестовый Windows-хост не требует агента, Python, PowerShell-скриптов или служб проекта.
- Ограничение: USB-трасса не заменяет системный журнал Windows и не подтверждает BSOD/kernel panic сама по себе.

# Изменения v0.3.7 — передача журналов хоста

- Добавлен `scripts/host-log-agent.py`: автоматическая запись локального JSONL-журнала на тестируемом Windows/Linux-хосте, heartbeat и передача событий на Raspberry Pi.
- Добавлен `scripts/host-log-receiver.py`: приём с токен-аутентификацией и создание `host-HOSTNAME.log` + `host-HOSTNAME.jsonl`; watchdog пишет событие при потере heartbeat.
- Windows-агент опрашивает System Event Log (Kernel-Power, BugCheck, unexpected shutdown и события запуска/остановки EventLog); Linux-агент следует за `journalctl` для warning и выше.
- `usb-fuzz.py campaign` поддерживает `--host-log-settle`, а распознавание сигнатур расширено для Windows Event ID 41/1001/6008.
- Добавлены документация и тест транспортного слоя.

# История изменений USB Research Toolkit

## v0.3.5

- Raw Gadget: `USB_RAW_EVENT_DISCONNECT` больше не трактуется как доказанный физический обрыв. Это важно для DWC2, который может выдавать событие DISCONNECT при сбросе шины.
- Raw Gadget: при RESET/DISCONNECT остановка endpoint workers выполняется до отключения endpoints; `EBUSY` от endpoint, который UDC уже отключил самостоятельно, записывается как `ENDPOINT_ALREADY_INACTIVE`, не завершает event loop.
- Raw Gadget: обработчики endpoint запускаются после успешного завершения status stage `SET_CONFIGURATION`, чтобы не ставить IN/OUT transfer до завершения управления конфигурацией.
- Анализатор отдельно классифицирует неоднозначный `USB_RESET_OR_DISCONNECT`, не объявляя физический обрыв без достаточных доказательств.
- Документация уточняет, почему захват через usbmon на самой Pi обычно не показывает USB Gadget линию; захват надо делать на тестовом хосте или аппаратным USB-анализатором.

# История изменений

## 0.3.4 — Raw Gadget lifecycle и диагностика захвата
- Изменён порядок остановки endpoints: прервать и join worker-потоки до `USB_RAW_IOCTL_EP_DISABLE`, чтобы не просить ядро отключить endpoint при queued URB.
- `USB_RAW_EVENT_DISCONNECT` в режиме DWC2 обрабатывается как recoverable reset/disconnect; цикл Raw Gadget продолжает ждать повторную enumeration.
- JSONL trace-файл очищается при начале нового запуска, чтобы исключить смешивание разных run-id.
- HID Mouse Report Descriptor согласован с четырёхбайтовой IN-payload: добавлен wheel usage.
- `usb-lab capture` предупреждает, если запуск происходит на машине с UDC: host-stack capture нужно проводить на отдельном хосте.
- Остаётся требуемой физическая проверка на конкретном Raspberry Pi/ядре; исходный код и тесты проверены локально.


## 0.3.3 — исправление Raw Gadget endpoint cleanup

- Исправлена передача endpoint handle в `USB_RAW_IOCTL_EP_DISABLE`: вместо указателя передаётся числовое значение, как ожидает обработчик ioctl в Linux Raw Gadget.
- Добавлена попытка отменить зависший endpoint I/O с помощью сигнала worker-потоку, повторное отключение после завершения worker и ошибка вместо молчаливого забывания активного endpoint.
- Добавлен явный журнал `ENDPOINT_DISABLE_RETRIED` и диагностические поля для устойчивых ошибок отключения.
- Обработчик исключений теперь закрывает `/dev/raw-gadget`, даже если очистка endpoint сама завершилась ошибкой.
- Проверены сборка и автоматические тесты в Linux; физическая проверка на Pi ещё требуется.

## 0.3.2 — чистая сборка репозитория

- Включено полное дерево исходников, шаблонов, документации и тестов.
- Удалены случайные служебные файлы ремонта из дистрибутива.
- Исправлены и повторно проверены `scripts/usb-lab.py`, `scripts/normalize-tshark.py`, `tests/test_normalizer.py`, `tests/test_usb_lab.py`.
- Исправлена совместимость нормализатора с именами поля `bmRequestType` в TShark.
- Версия проекта в CMake повышена до 0.3.2.

## Ограничения

Физическая работа на конкретной Raspberry Pi 4, захват через USBPcap на Windows 10 и наличие Raw Gadget/UDC в целевом ядре требуют отдельной проверки в реальной среде. См. `README.md`.

## 0.3.6 — reproducible USB profile fuzz campaigns

- Added `scripts/usb-fuzz.py` for seeded JSON-profile mutations and bounded sequential campaigns.
- Added per-profile SHA-256, manifest, JSONL campaign log, summary report, and reproducer directories for suspected failures.
- Added `usb-lab.py fuzz` subcommand.
- Execution of an external runner requires `--confirm-lab`; generation alone is offline.
- Added a deterministic-generation regression test to CTest.
- Host crash signatures are heuristic evidence from a supplied appended host log, not proof by themselves.


## 0.4.0 — очередь Wireshark review и повтор конфигураций

- После кампании анализирует PCAP/PCAPNG через `tshark` и создаёт `review-queue.json` для случаев с возможными аномалиями либо недостаточными данными захвата.
- Записывает Unix-временные метки начала/окончания каждого случая для корреляции с кадрами Wireshark.
- По умолчанию продолжает кампанию после ошибки отдельного случая; `--stop-on-error` включает остановку.
- Добавлены `--replay-from` и `--replay-case` для повторного запуска сохранённого профиля в отдельный каталог результатов.
- Добавлен `scripts/review-usb-campaign.py`, чтобы повторно проанализировать PCAPNG после остановки/сохранения Wireshark.
- Очередь — эвристическая: PCAPNG сам по себе не доказывает kernel crash и не гарантирует побайтовую проверку всех отправленных дескрипторов.
