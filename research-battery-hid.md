# Исследование протокола HID-запроса батареи для Razer DeathAdder V2 Pro

**Дата:** 2026-09-25  
**Цель:** Понимание протокола чтения уровня заряда батареи через HID  
**Устройство:** Razer DeathAdder V2 Pro  
**VID:** 0x1532 (Razer)  
**PID (проводной режим):** 0x007C  
**PID (беспроводной/донгл):** 0x007D  
**PID (Bluetooth):** 0x008E

---

## 1. Структура HID-отчёта Razer (90 байт)

Все устройства Razer используют универсальный 90-байтовый протокол HID-отчётов. Структура описана в `razercommon.h` (OpenRazer) и реализована в `protocol.rs` (razer-battery).

| Смещение | Поле | Размер | Описание |
|----------|------|--------|----------|
| 0 | `status` | 1 | Статус: 0x00=новый, 0x01=busy, 0x02=успех, 0x03=ошибка, 0x04=timeout, 0x05=не поддерживается |
| 1 | `transaction_id` | 1 | ID транзакции (определяет устройство/протокол) |
| 2-3 | `remaining_packets` | 2 | Оставшиеся пакеты (Big Endian) |
| 4 | `protocol_type` | 1 | Всегда 0x00 |
| 5 | `data_size` | 1 | Размер данных (≤ 80) |
| 6 | `command_class` | 1 | Класс команды |
| 7 | `command_id` | 1 | ID команды (бит 0: направление: 0→хост→девайс, 1→девайс→хост) |
| 8-87 | `arguments` | 80 | Аргументы/данные |
| 88 | `crc` | 1 | XOR от байтов 2..87 |
| 89 | `reserved` | 1 | Зарезервировано (0x00) |

**CRC:** XOR от байтов со 2 по 87 включительно.

---

## 2. Значения Transaction ID для DeathAdder V2 Pro

Из OpenRazer `razermouse_driver.c` (функция `razer_attr_read_firmware_version`):

- **DeathAdder V2 Pro (Wired 0x007C):** `transaction_id = 0x3f`
- **DeathAdder V2 Pro (Wireless 0x007D):** `transaction_id = 0x3f`

Это отличает его от более новых устройств (0x1f) и старых (0xFF).

---

## 3. Команды батареи

### 3.1. Запрос уровня заряда (CMD_GET_BATTERY)

Из `razer-battery/src/protocol.rs`:

| Поле | Значение |
|------|----------|
| `command_class` | `0x07` (CLASS_POWER) |
| `command_id` | `0x80` (CMD_GET_BATTERY) |
| `data_size` | `0x02` |

**Отправляемый 90-байтовый пакет:**

```
[0x00, tid, 0x00, 0x00, 0x00, 0x02, 0x07, 0x80, 0x00, ..., 0x00, crc, 0x00]
  status tid  rem.   prot  size  class cmd   args(80)          crc  res
```

Где `tid` = `0x3f` для DeathAdder V2 Pro.

**Пример из Exo/Docs (пакет с Synapse):**

```
SET => 00 1f 000000 0e068e 00 64 00 05e0 00 00 05e0 ... (90 байт)
```

Это иной запрос (команда `0e068e`), но сам Synapse при старте отправляет:

```
SET => 00 1f 000000 020680 ... (вероятно команда батареи)
```

По данным из razer-battery, команда `0x07/0x80` с `data_size=0x02` возвращает уровень заряда.

**Ответ (response):**

```
[0x02, tid, 0x00, 0x00, 0x00, 0x02, 0x07, 0x80, 0x00, raw_value, 0x00, ..., crc, 0x00]
```

- `response[9]` (байт 9, он же `arguments[1]`) — сырое значение батареи (0-255).

### 3.2. Преобразование в проценты (0-100%)

Из `razer-battery/src/protocol.rs`:

```rust
let raw = resp[9] as u16;
let level = ((raw * 100) / 255).min(100) as u8;
```

Формула: `проценты = min((raw_value * 100) / 255, 100)`

**Примеры:**
| Сырое значение | Проценты |
|---------------|----------|
| 255 | 100% |
| 191 | 75% |
| 128 | 50% |
| 64 | 25% |
| 0 | 0% |

### 3.3. Запрос статуса зарядки (CMD_IS_CHARGING)

| Поле | Значение |
|------|----------|
| `command_class` | `0x07` (CLASS_POWER) |
| `command_id` | `0x84` (CMD_IS_CHARGING) |
| `data_size` | `0x02` |

**Ответ:**
- `response[9]` — `0` = не заряжается, `≠0` = заряжается.

### 3.4. Альтернативная команда из захвата Synapse (Exo)

В документе Exo обнаружен запрос:

```
SET => 00 1f 000000 0e068e 00 64 00 05e0 00 00 05e0 ...
GET => 02 1f 000000 0e068e 00 64 00 05e0 00 00 05e0 ...
```

Где:
- `command_class = 0x0e`, `command_id = 0x068e` (с направлением host→device=0x0e)
- Байт `64` (100 в десятичной) — вероятно уровень заряда в процентах (100%)
- Значения `05e0` (1504) — неизвестны (возможно ёмкость батареи, напряжение или макс/мин уровни)

Это была команда при 100% заряде. Возможно, это команда получения расширенной информации о батарее, а не простой запрос процента.

---

## 4. Способ отправки: Feature Report vs Write/Read

### 4.1. Feature Report (Linux, macOS)

**Linux (hidraw):** Используются `HIDIOCSFEATURE`/`HIDIOCGFEATURE` ioctl.
**macOS (IOKit):** Используются IOService feature reports.

Первый байт буфера — Report ID (обычно `0x00`), затем 90 байт payload.

```rust
// Отправка feature report
let mut send_buf = [0u8; 91];
send_buf[0] = 0x00; // Report ID
send_buf[1..].copy_from_slice(&report); // 90 байт
hid.send_feature_report(&send_buf)?;
// Получение
let mut recv_buf = [0u8; 91];
recv_buf[0] = 0x00;
hid.get_feature_report(&mut recv_buf)?;
```

### 4.2. Output/Input Report (Windows)

**На Windows** feature reports часто не работают (ошибка 0x00000057 "parameter is incorrect"), потому что многие устройства Razer не декларируют feature reports в HID-дескрипторе.

**Рабочий метод на Windows — `hid_write` + `hid_read`:**

```rust
// Отправка 
let mut send_buf = [0u8; 91];
send_buf[0] = 0x00; // Report ID
send_buf[1..].copy_from_slice(&report);
hid.write(&send_buf)?; // hid_write — output report

// Ожидание 100мс
thread::sleep(Duration::from_millis(IO_DELAY_MS));

// Чтение ответа
let mut recv_buf = [0u8; 91];
let n = hid.read_timeout(&mut recv_buf, READ_TIMEOUT_MS)?; // 1000ms timeout
```

**Важно:** `razer-battery` пробует feature report сначала, и при неудаче падает на write/read. Так работает на всех платформах.

### 4.3. Параметры задержки и ретраев

Из `razer-battery/src/protocol.rs`:

| Параметр | Значение |
|----------|----------|
| Задержка между send/receive | 100 мс |
| Таймаут hid_read | 1000 мс |
| Макс. попыток | 5 |
| Пауза после статуса busy | 300 мс |
| Пауза после статуса 0x00 | 200 мс |
| Пауза при неизв. статусе | 200 мс |

### 4.4. Время ожидания ответа в OpenRazer (Linux kernel)

Из `razermouse_driver.h`:

- **DeathAdder V2 Pro (Wired 0x007C, Wireless 0x007D):** `RAZER_VIPER_MOUSE_RECEIVER_WAIT_US = 59900` мкс (~60 мс)
- Обычные мыши: `RAZER_MOUSE_WAIT_US = 600` мкс
- Новые приёмники: `RAZER_NEW_MOUSE_RECEIVER_WAIT_US = 31000` мкс (31 мс)

DeathAdder V2 Pro попадает в категорию Viper-мышей (большая задержка — 60 мс).

---

## 5. Какой HID-интерфейс используется

### 5.1. HID-интерфейс для батареи

Из `razer-battery/src/devices.rs`:

```rust
// Razer DeathAdder V2 Pro (Wireless), PID 0x007C
dev!("Razer DeathAdder V2 Pro (Wireless)", 0x007C, 2, 0x3f),
// Параметры: name, pid, interface, transaction_id
```

**Интерфейс: 2**
**Transaction ID: 0x3f**
**Usage Page: 0x0001, Usage: 0x0002** (стандартный HID Mouse)

**Важное примечание:** На Windows библиотека ищет сначала по полному совпадению (VID+PID+интерфейс+usage_page+usage), потом по usage, потом по интерфейсу. Порядок приоритетов платформозависим.

### 5.2. HID-интерфейс для RGB (OpenRGB)

OpenRGB использует HID API через hidapi. В документации OpenRGB указано:
- Разные интерфейсы HID используются для разных функций
- Для RGB используется **интерфейс 0** (первый HID-интерфейс)
- Для батареи используется **интерфейс 2**

Это означает, что **конфликта между OpenRGB (RGB) и чтением батареи быть не должно**, так как они работают на разных HID-интерфейсах одного USB-устройства. Каждый интерфейс — отдельный HID-девайс с отдельным путём (hidraw, устройство HID API).

---

## 6. Режимы открытия устройства

### 6.1. Режим эксклюзивности

Из `razer-battery/src/lib.rs`:

```rust
#[cfg(target_os = "macos")]
api.set_open_exclusive(false); // macOS — неэксклюзивный режим
```

**На Windows:** hidapi по умолчанию открывает с флагами:
```c
CreateFile(path, GENERIC_READ | GENERIC_WRITE, 
           FILE_SHARE_READ | FILE_SHARE_WRITE, 
           NULL, OPEN_EXISTING, 0, NULL)
```

То есть с **разделяемым доступом** (`FILE_SHARE_READ | FILE_SHARE_WRITE`). Это означает, что несколько приложений могут одновременно открыть устройство.

**Конфликт с Razer Synapse:** Synapse использует эксклюзивный доступ. Если Synapse запущен, hidapi может не открыть устройство. Документация `razer-battery` рекомендует закрывать Synapse перед использованием.

### 6.2. Эффект запроса батареи на спящую мышь

**Запрос батареи НЕ будит спящую мышь.** Если мышь находится в режиме сна для экономии энергии, запрос может вернуть:
- Статус 0x00 (нет ответа) — потребуется ретрай
- Статус 0x01 (busy)
- Последний известный уровень заряда

Драйвер OpenRazer использует до 5 ретраев с паузой 10 мс между ними. На пользовательском уровне razer-battery делает до 5 попыток с паузой 200-300 мс.

---

## 7. Подтверждённая спецификация

### 7.1. Итоговые байты для отправки на DeathAdder V2 Pro

**Запрос уровня заряда (90 байт):**

```
00 3f 00 00 00 02 07 80 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 CRC 00
```

Где CRC = XOR от байтов 2..87 (все нули → CRC = 0x00).

**Запрос статуса зарядки:**

```
00 3f 00 00 00 02 07 84 00 00 ...
```

### 7.2. Разбор ответа

```python
def parse_battery_response(response_90_bytes):
    status = response[0]          # 0x02 = успех
    tid = response[1]             # должен совпадать с запросом
    cmd_class = response[6]       # должен быть 0x07
    cmd_id = response[7]          # должен быть 0x80
    raw_value = response[9]       # сырой уровень (0-255)
    crc = response[88]            # проверка CRC XOR байт 2..87
    
    # Проверка CRC
    expected_crc = 0
    for i in range(2, 88):
        expected_crc ^= response[i]
    if expected_crc != crc:
        raise ValueError("CRC mismatch")
    
    # Конвертация в проценты
    percent = min((raw_value * 100) // 255, 100)
    return percent
```

```python
def parse_charging_response(response_90_bytes):
    raw_value = response[9]       # 0 = не заряжается, !=0 = заряжается
    return raw_value != 0
```

---

## 8. Сравнение реализации в разных проектах

| Проект | Платформа | Тип отчёта | Transaction ID | Интерфейс | CRC |
|--------|-----------|------------|----------------|-----------|-----|
| **OpenRazer** (Linux kernel) | Linux | Feature Report (USB Control) | 0x3f | Через HID драйвер | XOR байт 2..87 |
| **razer-battery** (Rust) | Windows/Linux/macOS | Feature → fallback Write/Read | 0x3f (для DA V2 Pro) | 2 | XOR байт 2..87 |
| **Exo** (C#) | Windows | Feature Report | 0x1f | 0 | XOR байт 2..87 |
| **RazeCLI** (Python) | Windows/Linux/macOS | Write/Read | 0x3f | 2 | XOR байт 2..87 |

Exo использует transaction ID 0x1f (новый протокол), а не 0x3f (который в OpenRazer для DA V2 Pro). Возможно, оба работают — 0x1f для запросов к приёмнику/доку, а 0x3f напрямую к мышке. См. захват Synapse, где используются оба значения: 0x08 на старте и 0x1f для основных запросов.

---

## 9. Полная последовательность запросов при старте Synapse (из Exo)

При подключении донгла 0x007D происходит последовательность:

1. **Handshake:** `00 08 000000 020086` → `02 08 000000 020086`
2. **Чтение серийного номера:** `00 08 000000 160082` → данные SN
3. **Проверка спаривания:** `00 08 000000 3100bf 02` → `02 08 000000 3100bf 02 01 007d 00 ffff` (показывает PID мыши 0x007D)
4. **Запрос батареи:** `00 1f 000000 0e068e 00 64 ...` → `02 1f 000000 0e068e 00 64 ...` (64 = 100%?)
5. **Запрос текущего эффекта:** `00 1f 000000 0c0f82 0104 ...`
6. **Запрос статуса:** `00 1f 000000 020084 03` (is_charging)

---

## 10. Рекомендации для реализации

1. **Transaction ID:** Использовать `0x3f` для DeathAdder V2 Pro.
2. **HID-интерфейс:** Использовать интерфейс `2`.
3. **Способ отправки (Windows):** Сначала пробовать Feature Report, при ошибке падать на Write/Read с Report ID = 0x00.
4. **Таймауты:** Минимум 60 мс между send и receive. Рекомендуется 100 мс.
5. **Ретраи:** 5 попыток при ошибках.
6. **CRC:** Обязательно проверять XOR байт 2..87.
7. **Открытие:** Использовать `FILE_SHARE_READ | FILE_SHARE_WRITE`.
8. **Synapse:** Если Synapse запущен, может блокировать открытие устройства. Рекомендовать закрывать Synapse.
9. **Спящая мышь:** Запрос не будит мышь; возможны ретраи.
10. **PID:** 0x007C (проводной) и 0x007D (беспроводной/донгл). На донгле запрос идёт к донглу, который возвращает состояние подключённой мыши.
11. **Bluetooth (0x008E):** При BT-подключении через Windows (Bluetooth LE) протокол может отличаться. Использовать стандартные HID-запросы или GATT.

---

## Источники

1. **OpenRazer** — `driver/razermouse_driver.c`, `driver/razermouse_driver.h`, `driver/razercommon.h` — GPL-2.0
2. **razer-battery** (andrzej-zuralovic) — `src/protocol.rs`, `src/devices.rs`, `src/lib.rs` — MIT
3. **Exo** (hexawyz) — `Docs/Razer DeathAdder V2 Pro.md` — reverse-engineering документация
4. **RazeCLI** (compilererrors) — README
5. **razer-battery-report** (xzeldon) — README
6. **OpenRGB** (CalcProgrammer1) — README, документация