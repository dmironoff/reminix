# ReMinix — путеводитель по сообщениям IPC

> Дополнительный пункт этапа 0.2 (`docs/modernization.md`, п. 7, ч. 1), продолжение
> `docs/types-audit.md`. Описаны все типы полезной нагрузки сообщений из
> `minix/include/minix/ipc.h` (256 типов), общие раскладки `mess_1`…`mess_10` и их
> псевдонимы из `com.h`/`vboxif.h`, а также протоколы, которые пользуются общими
> полями напрямую. Для каждого поля — назначение по коду отправителя и получателя
> и пометка, если ширина или смысл поля зависят от архитектуры или аппаратуры.
> Цель — исходные данные для проектирования переносимой системы сообщений
> (на 64 битах 128 из 256 типов не помещаются в 56 байт, `types-audit.md` §5).
> Дата: 2026-10-02, дерево — `038580154`. Код не менялся.

**Итог.** 256 типов в 9 разделах (§4–§9), 129 псевдонимов общих раскладок и
протоколы на общих полях (PCI, `IOMMU_MAP`). Пометки «64»: **А** — 132 поля,
**Р** — 90, **С** — 62, **В** — 116, **Ж** — 30, **У** — 23, **Ф** — 16 (списки
**Ф**, **У**, **Ж** — §11). Найдено 13 ошибок и нестыковок протоколов (§12),
в том числе переполнение буфера ядра в `do_vsafecopy`; 10 мёртвых типов и ряд
мёртвых полей (§13). Выводы для новой системы сообщений — §14.

## 1. Как читать

Каждый тип описан разделом `#### mess_…` (строка в `ipc.h`), затем:

- **размер** полезной нагрузки на i386 и на x86_64 (по clang, с LP64-моделью;
  на i386 всегда 56 — это проверяет `_ASSERT_MSG_SIZE`);
- **направление** (кто → кому), **вызовы** — значения `m_type`, с которыми тип
  используется, **отправитель** и **получатель** (функции и файлы), **ответ**;
- **таблица полей**: имя (вложенные поля — с `↳`), тип, смещение на i386 /
  x86_64, назначение, пометка в колонке «64».

Пометки колонки «64» (поле может иметь несколько):

| Пометка | Значение | Что случится на 64 битах / при > 4 ГБ |
|---|---|---|
| **А** | виртуальный адрес (указатель, `vir_bytes`-адрес) | поле растёт до 8 байт; раскладка сообщения меняется |
| **Ф** | физический адрес (любого типа) | ширина зависит от платформы и PAE/LPAE; на ILP32 > 4 ГБ не выразить |
| **Р** | размер/длина/смещение в памяти шириной в слово (`size_t`, `ssize_t`, `vir_bytes`-длина) | поле растёт до 8 байт |
| **С** | `long`, `unsigned long`, `key_t` и пр. машинное слово, не адрес | поле растёт до 8 байт |
| **У** | адрес или размер памяти в поле **узкой фиксированной** ширины (`int`, `u32_t`, `unsigned`) | раскладка не меняется, но значение **обрезается** — тихая ошибка |
| **Ж** | аппаратно-зависимое значение: порт ввода-вывода, IRQ, регистр устройства или CPU, CR3/TTBR, флаги PTE, BAR, MMIO, значения регистров процессора | смысл и ширина зависят от архитектуры или устройства |
| **В** | поле фиксированной ширины, у которого на x86_64 другое смещение из-за выравнивания (`uint64_t` после 4-байтовых полей) | только информационно |

Пустая колонка — поле от архитектуры не зависит. Сводная таблица типов — §10, сводка пометок — §11.

## 2. Устройство сообщения

```c
typedef struct noxfer_message {
	endpoint_t m_source;	/* кто отправил — заполняет ЯДРО при доставке */
	int m_type;		/* код запроса, или код результата в ответе */
	union {			/* полезная нагрузка, ровно 56 байт */
		mess_u8 m_u8; … mess_1 m_m1; … mess_lc_vfs_readwrite m_lc_vfs_readwrite; …
		u8_t size[56];
	};
} message __ALIGNED(16);	/* 64 байта; проверка sizeof(message) == 64 */
```

- **`m_source`** записывает ядро при доставке (`mini_send`/`mini_receive`,
  `minix/kernel/proc.c`); отправитель его не задаёт — подделать отправителя нельзя.
  Для `notify` — endpoint источника уведомления (`HARDWARE`, `SYSTEM`, `CLOCK`, …).
- **`m_type`** — в запросе код вызова (`§3`), в ответе — результат: `OK` (0),
  отрицательный код ошибки (`-E…`) или, для части протоколов, собственный код
  ответа (`CDEV_REPLY`, `BDEV_REPLY`, `SDEV_REPLY`, `NDEV_*_REPLY`, …).
  Обёртки: `_syscall()` (`minix/lib/libc/sys/syscall.c`) кладёт номер в `m_type`,
  делает `ipc_sendrec()` и превращает отрицательный `m_type` ответа в `errno`;
  `_taskcall()` (libsys) — то же, но возвращает отрицательный код;
  `_kernel_call()` (`minix/lib/libsys/kernel_call.c`) — номер `SYS_*` в `m_type`,
  ловушка `do_kernel_call`, результат в `m_type` (повтор при `ENOTREADY`).
- **Полезная нагрузка** — один из типов `mess_*`. Структуры строго 56 байт: каждая
  заканчивается полем `padding[N]`, `_ASSERT_MSG_SIZE` проверяет размер при сборке.
  Большие данные сообщением не передаются — через **гранты** (`cp_grant_id_t`,
  `minix/include/minix/safecopies.h`): отправитель создаёт грант на свою память,
  получатель копирует `sys_safecopyfrom/to()` по номеру гранта.
- Размер сообщения зашит в ассемблер: ядро копирует сообщение к/от процесса
  фиксированным числом слов (`copy_msg_from_user`/`copy_msg_to_user`,
  `minix/kernel/arch/i386/klib.S` — 16 × 4 байта; `arch/earm/klib.S` — 2 × 32 байта).

### 2.1 Примитивы IPC

Номера — `minix/include/minix/ipcconst.h`; вход в ядро — пользовательские заглушки
`minix/kernel/arch/i386/usermapped_glo_ipc.S` (через `int`, `sysenter` или
`syscall` — выбирает ядро; регистры: `eax` — endpoint адресата/источника, `ebx` —
адрес сообщения, `ecx` — код операции; для `SENDA` `ebx` — таблица, `eax` — число
элементов; статус `RECEIVE` возвращается в `ebx`), разбор — `do_ipc(reg_t r1, r2,
r3)` в `minix/kernel/proc.c`. Адрес сообщения передаётся в регистре `reg_t`
(32 бита — `types-audit.md`, «reg_t»).

| Операция | Номер | Что делает |
|---|---|---|
| `SEND` | 1 | блокирующая отправка |
| `RECEIVE` | 2 | блокирующий приём (от конкретного endpoint или `ANY`); статус — номер операции отправителя и флаги (`IPC_STATUS_CALL`, `IPC_FLG_MSG_FROM_KERNEL`) |
| `SENDREC` | 3 | отправка и ожидание ответа от того же адресата (обычный «вызов») |
| `NOTIFY` | 4 | неблокирующее уведомление без данных: у получателя взводится бит, доставка — сообщением `NOTIFY_MESSAGE`, собранным ядром (§2.2) |
| `SENDNB` | 5 | неблокирующая отправка (ошибка, если получатель не ждёт) |
| `MINIX_KERNINFO` | 6 | получить адрес `struct minix_kerninfo` в своём АП |
| `SENDA` | 16 | асинхронная отправка: таблица `asynmsg_t` в памяти отправителя (§2.3) |
| (kernel call) | — | отдельный вектор: сообщение `SYS_*` обрабатывает задача `SYSTEM` внутри ядра (`do_kernel_call` → `kernel_call()`, `minix/kernel/system.c`) |

### 2.2 Уведомления

`notify(dst)` не несёт данных; ядро помечает бит отправителя у получателя и при
приёме строит сообщение (`BuildNotifyMessage`, `minix/kernel/proc.c:99`):
`m_type = NOTIFY_MESSAGE` (0x1000), `m_source` — источник, полезная нагрузка —
`mess_notify`: `timestamp` (монотонное время, тики), для `HARDWARE` — маска
ожидающих прерываний (`interrupts`), для `SYSTEM` — ожидающие сигналы
(`sigset`). Признак уведомления — `is_ipc_notify(status)` (статус `RECEIVE`).

### 2.3 Асинхронные сообщения

```c
typedef struct asynmsg { unsigned flags; endpoint_t dst; int result; message msg; } asynmsg_t;
```

Таблица лежит в памяти отправителя; ядро читает её при доставке (`AMF_VALID`,
`AMF_DONE`, `AMF_NOTIFY`, `AMF_NOREPLY`, `AMF_NOTIFY_ERR`). Используется RS, PM, VFS,
драйверами для ответов, которые нельзя блокировать. Адрес таблицы — в `struct priv`
ядра (`s_asyntab`, `vir_bytes`). **А**: адрес таблицы и `size_t count` в регистрах.

## 3. Пространства номеров `m_type`

Номер запроса однозначно определяет протокол; база диапазона — в `com.h`
(`callnr.h` — для системных вызовов libc). Запросы (`*_RQ_BASE`) и ответы
(`*_RS_BASE`) часто разведены на 0x80.

| База | Значение | Протокол | Заголовок |
|---|---|---|---|
| `PM_BASE` | 0x000 | системные вызовы → PM (`PM_*`, `NR_PM_CALLS` = 48) | `callnr.h` |
| `VFS_BASE` | 0x100 | системные вызовы → VFS (`VFS_*`, `NR_VFS_CALLS` = 64) | `callnr.h` |
| `DL_RQ_BASE`/`DL_RS_BASE` | 0x200/0x280 | старый сетевой протокол (`DL_*`) | `com.h` |
| `BUSC_RQ_BASE`/`BUSC_RS_BASE` | 0x300/0x380 | шина: PCI (`BUSC_PCI_*`), I2C (`BUSC_I2C_*`) | `com.h` |
| `CDEV_RQ_BASE`/`CDEV_RS_BASE` | 0x400/0x480 | символьные устройства (`CDEV_*`) | `com.h` |
| `BDEV_RQ_BASE`/`BDEV_RS_BASE` | 0x500/0x580 | блочные устройства (`BDEV_*`) | `com.h` |
| `KERNEL_CALL` | 0x600 | вызовы ядра (`SYS_*`) | `com.h` |
| `RS_RQ_BASE` | 0x700 | RS (`RS_*`) | `com.h` |
| `DS_RQ_BASE` | 0x800 | DS (`DS_*`) | `com.h` |
| `VFS_PM_RQ_BASE`/`VFS_PM_RS_BASE` | 0x900/0x980 | VFS ↔ PM | `com.h` |
| `FS_BASE` | 0xA00 | VFS → ФС (`REQ_*`, `vfsif.h`) | `com.h` |
| `VFS_TRANSACTION_BASE` | 0xB00 | номер транзакции в ответах VFS-клиентам | `com.h` |
| `VM_RQ_BASE` | 0xC00 | VM (`VM_*`) | `com.h` |
| `IPC_BASE` | 0xD00 | сервер IPC (SysV) | `com.h` |
| `COMMON_RQ_BASE`/`COMMON_RS_BASE` | 0xE00/0xE80 | общие для всех служб (gcov, инъекция ошибок, MIB) | `com.h` |
| `SCHEDULING_BASE` | 0xF00 | планировщик | `com.h` |
| `NOTIFY_MESSAGE` | 0x1000 | уведомления (§2.2) | `com.h` |
| `USB_BASE` | 0x1100 | USB | `com.h` |
| `DEVMAN_BASE` | 0x1200 | менеджер устройств | `com.h` |
| `TTY_RQ_BASE` | 0x1300 | TTY (служебные) | `com.h` |
| `RTCDEV_RQ_BASE`/`RTCDEV_RS_BASE` | 0x1400/0x1480 | часы реального времени | `com.h` |
| `INPUT_RQ_BASE`/`INPUT_RS_BASE` | 0x1500/0x1580 | ввод (клавиатура/мышь) | `com.h` |
| `VBOX_RQ_BASE`/`VBOX_RS_BASE` | 0x1600/0x1680 | VirtualBox | `vboxif.h` |
| `PTYFS_BASE` | 0x1700 | PTY ↔ PTYFS | `com.h` |
| `MIB_BASE` | 0x1800 | MIB (sysctl) | `com.h` |
| `SDEV_RQ_BASE`/`SDEV_RS_BASE` | 0x1900/0x1980 | сокеты (`SDEV_*`) | `com.h` |
| `NDEV_RQ_BASE`/`NDEV_RS_BASE` | 0x1A00/0x1A80 | сетевые драйверы (`NDEV_*`) | `com.h` |

## 4. Общие раскладки (`mess_1`…`mess_10`, `mess_u*`)

Наследие MINIX 2/3: безымянные «универсальные» поля, которым протоколы дают имена
через `#define` (псевдонимы ниже и в разделах протоколов) или используют напрямую
(PCI, `amddev`, `IOMMU_MAP` в fxp/rtl8139/libaudiodriver — §9). Доступ —
`m.m1_i1` (= `m_m1.m1i1`, `ipc.h:2678`–`2737`). Новые протоколы ими не пользуются.

| Тип | ipc.h | i386 → x86_64 | Поля (смещение i386 / x86_64) | 64 |
|---|---|---|---|---|
| `mess_u8` | 17 | 56 → 56 | `data[56]` (`uint8_t`) | — |
| `mess_u16` | 22 | 56 → 56 | `data[28]` (`uint16_t`) | — |
| `mess_u32` | 27 | 56 → 56 | `data[14]` (`uint32_t`) | — |
| `mess_u64` | 32 | 56 → 56 | `data[7]` (`uint64_t`) | — |
| `mess_1` | 37 | 56 → **80** | `m1ull1` u64 0/0; `m1i1`, `m1i2`, `m1i3` int 8,12,16 / 8,12,16; `m1p1`…`m1p4` `char *` 20,24,28,32 / 24,32,40,48 | `m1p*` — **А** |
| `mess_2` | 45 | 56 → **72** | `m2ll1` i64 0/0; `m2i1`…`m2i3` int 8,12,16; `m2l1`, `m2l2` `long` 20,24 / 24,32; `m2p1` `char *` 28/40; `sigset` 32/48 (16 байт); `m2s1` short 48/64 | `m2l*` — **С**; `m2p1` — **А** |
| `mess_3` | 56 | 56 → **64** | `m3i1`, `m3i2` int 0,4; `m3p1` `char *` 8/8; `m3ca1[44]` 12/16 | `m3p1` — **А** |
| `mess_4` | 63 | 56 → **80** | `m4ll1` i64 0/0; `m4l1`…`m4l5` `long` 8,12,16,20,24 / 8,16,24,32,40 | `m4l*` — **С** |
| `mess_7` | 70 | 56 → **72** | `m7i1`…`m7i5` int 0…16; `m7p1`, `m7p2` `char *` 20,24 / 24,32 | `m7p*` — **А** |
| `mess_9` | 77 | 56 → **80** | `m9ull1`, `m9ull2` u64 0,8; `m9l1`…`m9l5` `long` 16…32 / 16…48; `m9s1`…`m9s4` short 36…42 / 56…62 | `m9l*` — **С** |
| `mess_10` | 85 | 56 → **72** | `m10ull1` u64 0; `m10i1`…`m10i4` int 8…20; `m10l1`…`m10l3` `long` 24,28,32 / 24,32,40 | `m10l*` — **С** |

`mess_5`, `mess_6`, `mess_8` в ReMinix нет. Использование: 129 псевдонимов
(116 в `com.h`, 7 в `vboxif.h`, 6 в тесте `sys_vumap`), из них чаще всего — `m2_i1`
(11), `m1_i1` и `m2_l1` (по 9), `m2_l2` (8); прямые обращения к `m*_*` — 21 файл
(PCI в libsys и драйвере шины, `amddev`, fxp, rtl8139, libaudiodriver). Таблицы
псевдонимов — в разделах протоколов (§5–§9), где они применяются.

Главное для переработки: в общих раскладках смысл поля задаётся только именем
псевдонима, а тип — общий для всех протоколов (`long` для адресов, `int` для
физических адресов и длин, `char *` для чего угодно). Здесь сосредоточены поля
**У** — адреса в `int` (`SVMCTL_PTROOT` = `m1_i3`, `VFS_PM_PS_STR` = `m7_i5`,
`SVMCTL_MRG_ADDR` = `m2_i2`, …).

## 5. Вызовы ядра (libsys → ядро) и сообщения ядра

#### `mess_krn_lsys_schedule` — ipc.h:261

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_krn_lsys_schedule`.

Направление: ядро → планировщик (сервер `sched`), асинхронное сообщение «от имени» процесса (`m_source` = endpoint процесса, исчерпавшего квант; флаг `IPC_FLG_MSG_FROM_KERNEL`). Вызовы (`m_type`): `SCHEDULING_NO_QUANTUM`. Отправитель: `notify_scheduler()` (`minix/kernel/proc.c:1893`, через `mini_send(..., FROM_KERNEL)`). Получатель: `minix/servers/sched/main.c:68` → `do_noquantum()` (`minix/servers/sched/schedule.c:87`). Ответ: нет (sched не отвечает ядру). Несмотря на имя `krn_lsys`, получатель — сервер, libsys не участвует.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `acnt_queue` | `time_t` | 0 / 0 | Время, проведённое процессом в очереди готовых с последнего сброса учёта, в **миллисекундах** (не секундах, несмотря на `time_t`): `cpu_time_2_ms(p_accounting.time_in_queue)` (`proc.c:1894`; `cpu_time_2_ms` возвращает `unsigned`). sched не читает. |  |
| `acnt_deqs` | `unsigned long` | 8 / 8 | Число снятий процесса с очереди (`p_accounting.dequeues`, `proc.c:1895`). sched не читает. | С |
| `acnt_ipc_sync` | `unsigned long` | 12 / 16 | Число синхронных IPC (`p_accounting.ipc_sync`). sched не читает. | С |
| `acnt_ipc_async` | `unsigned long` | 16 / 24 | Число асинхронных IPC (`p_accounting.ipc_async`). sched не читает. | С |
| `acnt_preempt` | `unsigned long` | 20 / 32 | Число вытеснений (`p_accounting.preempted`). sched не читает. | С |
| `acnt_cpu` | `__uint32_t` | 24 / 40 | Номер CPU (`cpuid`), на котором истёк квант (`proc.c:1899`). sched не читает. | В |
| `acnt_cpu_load` | `__uint32_t` | 28 / 44 | Загрузка этого CPU в процентах 0..100 с прошлого вызова (`cpu_load()`, `minix/kernel/arch/i386/arch_clock.c:414`). sched не читает. | В |

Замечания: `do_noquantum()` использует только `m_source`; все поля учёта — мёртвый груз (заполняются ядром, нигде не читаются). При переработке — либо убрать, либо сделать `u64_t`/`u32_t`.

#### `mess_krn_lsys_sys_devio` — ipc.h:275

Размер: i386 56, x86_64 56. Член объединения: `m_krn_lsys_sys_devio`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_DEVIO` с `_DIO_INPUT`. Отправитель: `do_devio()` (`minix/kernel/system/do_devio.c:74-83`). Получатель: `sys_in()` (`minix/lib/libsys/arch/i386/sys_in.c`). Запрос — `mess_lsys_krn_sys_devio`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `value` | `__uint32_t` | 0 / 0 | Значение, прочитанное из порта ввода-вывода `inb/inw/inl` (`do_devio.c:74-83`); для байта/слова — младшие 8/16 бит. | Ж |

#### `mess_krn_lsys_sys_fork` — ipc.h:282

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_krn_lsys_sys_fork`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_FORK`. Отправитель: `do_fork()` (`minix/kernel/system/do_fork.c:111-112`). Получатель: `sys_fork()` (`minix/lib/libsys/sys_fork.c`), вызывает VM (`minix/servers/vm/fork.c:89`). Запрос — `mess_lsys_krn_sys_fork`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Новый endpoint потомка (слот + увеличенное поколение), `do_fork.c:111`. |  |
| `msgaddr` | `vir_bytes` | 4 / 8 | Виртуальный адрес буфера сообщения родителя (`rpp->p_delivermsg_vir`, `do_fork.c:112`) — у потомка тот же адрес; VM делает страницу записываемой у обоих (`minix/servers/vm/fork.c:102-106`). | А |

#### `mess_krn_lsys_sys_getwhoami` — ipc.h:290

Размер: i386 56, x86_64 56. Член объединения: `m_krn_lsys_sys_getwhoami`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_GETINFO` с `request = GET_WHOAMI`. Отправитель: `do_getinfo()` (`minix/kernel/system/do_getinfo.c:132-141`). Получатель: `sys_whoami()` (`minix/lib/libsys/sys_getinfo.c`). Запрос — `mess_lsys_krn_sys_getinfo`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Endpoint вызывающего (`caller->p_endpoint`, `do_getinfo.c:134`). |  |
| `privflags` | `int` | 4 / 4 | Флаги привилегий вызывающего `priv(caller)->s_flags` (`SYS_PROC`, `PREEMPTIBLE`, `CHECK_IO_PORT`, …), `do_getinfo.c:139`. |  |
| `initflags` | `int` | 8 / 8 | Флаги инициализации `priv(caller)->s_init_flags` (флаги SEF/RS), `do_getinfo.c:140`. |  |
| `name` | `char[44]` | 12 / 12 | Имя процесса `p_name`, обрезано до `min(44, PROC_NAME_LEN)-1` и завершено NUL (`do_getinfo.c:135-138`). |  |

#### `mess_krn_lsys_sys_irqctl` — ipc.h:299

Размер: i386 56, x86_64 56. Член объединения: `m_krn_lsys_sys_irqctl`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_IRQCTL` с `IRQ_SETPOLICY`. Отправитель: `do_irqctl()` (`minix/kernel/system/do_irqctl.c:119`). Получатель: `sys_irqctl()` (`minix/lib/libsys/sys_irqctl.c`), читает только при `IRQ_SETPOLICY`. Запрос — `mess_lsys_krn_sys_irqctl`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `hook_id` | `int` | 0 / 0 | Индекс выделенного ядром IRQ-хука + 1 (1..`NR_IRQ_HOOKS`), `do_irqctl.c:119`; драйвер передаёт его затем в `IRQ_ENABLE/DISABLE/RMPOLICY`. |  |

#### `mess_krn_lsys_sys_times` — ipc.h:306

Размер: i386 56, x86_64 56. Член объединения: `m_krn_lsys_sys_times`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_TIMES`. Отправитель: `do_times()` (`minix/kernel/system/do_times.c:37-42`). Получатель: `sys_times()` (`minix/lib/libsys/sys_times.c`; вызывают PM `forkexit.c:306`, `misc.c:429`, `sef.c:362,373`). Запрос — `mess_lsys_krn_sys_times`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `real_ticks` | `clock_t` | 0 / 0 | Реальное время в тиках `get_realtime()` (`do_times.c:41`). `sys_times()` его **не возвращает** — мёртвое поле. |  |
| `boot_ticks` | `clock_t` | 4 / 4 | Монотонное время с загрузки в тиках `get_monotonic()` (`do_times.c:40`); в `sys_times()` — `*uptime`. |  |
| `user_time` | `clock_t` | 8 / 8 | Пользовательское время процесса в тиках `p_user_time` (`do_times.c:37`); заполняется только если `endpt` ≠ `NONE` и валиден, иначе мусор. |  |
| `system_time` | `clock_t` | 12 / 12 | Системное время процесса в тиках `p_sys_time` (`do_times.c:38`); то же условие. |  |
| `boot_time` | `time_t` | 16 / 16 | Время загрузки, секунды с 1970 (`get_boottime()`, `do_times.c:42`). |  |

Замечания: `clock_t` = `unsigned int` (32 бита на всех портах), счётчики тиков переполняются при HZ=1000 через ~49 дней — не вопрос 64 бит, но вопрос переработки.

#### `mess_krn_lsys_sys_trace` — ipc.h:317

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_krn_lsys_sys_trace`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_TRACE`. Отправитель: `do_trace()` (`minix/kernel/system/do_trace.c:97-199`). Получатель: `sys_trace()` (`minix/lib/libsys/sys_trace.c`, вызывает PM для ptrace). Запрос — `mess_lsys_krn_sys_trace`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `data` | `long` | 0 / 0 | Результат: слово `long` из памяти трассируемого (`T_GETINS/T_GETDATA`, `do_trace.c:96-102`), слово из `struct proc`/`struct priv` — в т.ч. сохранённые регистры CPU (`T_GETUSER`, `do_trace.c:109-123`), байт (`T_READB_INS`); 0 для остальных запросов. | С, Ж |

#### `mess_krn_lsys_sys_umap` — ipc.h:324

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_krn_lsys_sys_umap`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_UMAP` и `SYS_UMAP_REMOTE`. Отправитель: `do_umap_remote()` (`minix/kernel/system/do_umap_remote.c:111`; `do_umap()` вызывает его же). Получатель: `sys_umap()` (`minix/lib/libsys/sys_umap.c`), `sys_umap_remote()` (`minix/lib/libsys/arch/i386/sys_umap_remote.c`). Запрос — `mess_lsys_krn_sys_umap`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `dst_addr` | `phys_bytes` | 0 / 0 | Физический адрес, соответствующий виртуальному адресу/гранту (`vm_lookup()`, `do_umap_remote.c:94-111`); используется драйверами для DMA. Перекрывает `src_endpt`/`segment` запроса. | Ф |

#### `mess_krn_lsys_sys_vumap` — ipc.h:331

Размер: i386 56, x86_64 56. Член объединения: `m_krn_lsys_sys_vumap`.

Направление: ядро → libsys (ответ на kernel call). Вызовы (`m_type`): ответ на `SYS_VUMAP`. Отправитель: `do_vumap()` (`minix/kernel/system/do_vumap.c:128`). Получатель: `sys_vumap()` (`minix/lib/libsys/sys_vumap.c`). Запрос — `mess_lsys_krn_sys_vumap`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pcount` | `int` | 0 / 0 | Число заполненных элементов выходного вектора `struct vumap_phys` (≤ `pmax`, ≤ `MAPVEC_NR`), `do_vumap.c:128`. |  |

#### `mess_lsys_krn_readbios` — ipc.h:1074

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_krn_readbios`.

Направление: libsys → ядро (kernel call, только i386). Вызовы (`m_type`): `SYS_READBIOS`. Отправитель: `sys_readbios()` (`minix/lib/libsys/arch/i386/sys_readbios.c`; вызывают `memory`, `printer`, `readclock`, `tty/console`). Получатель: `do_readbios()` (`minix/kernel/arch/i386/do_readbios.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `size` | `size_t` | 0 / 0 | Число копируемых байт (`do_readbios.c:18,36`). | Р |
| `addr` | `phys_bytes` | 4 / 8 | Физический адрес в BIOS-области; допускаются только [0, 0x4FF] и [0x90000, 0xFFFFF] (`do_readbios.c:21,32-34`, `memory.h:29-33,118-119`); копирование с `proc_nr_e = NONE` (физическая адресация). | Ф, Ж |
| `buf` | `vir_bytes` | 8 / 16 | Виртуальный адрес буфера в адресном пространстве вызывающего (`dst.proc_nr_e = m_source`), `do_readbios.c:22`. | А |

#### `mess_lsys_kern_safecopy` — ipc.h:1083

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_kern_safecopy`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_SAFECOPYFROM`, `SYS_SAFECOPYTO`. Отправитель: `sys_safecopyfrom()`, `sys_safecopyto()` (`minix/lib/libsys/sys_safecopy.c`). Получатель: `do_safecopy_from()`, `do_safecopy_to()` (`minix/kernel/system/do_safecopy.c:377-394`) → `safecopy()`. Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `from_to` | `endpoint_t` | 0 / 0 | Endpoint грантодателя (источник для FROM, приёмник для TO), `do_safecopy.c:379,390`. |  |
| `gid` | `cp_grant_id_t` | 4 / 4 | Идентификатор гранта в таблице грантодателя. |  |
| `offset` | `size_t` | 8 / 8 | Смещение внутри гранта в байтах (проверяется `verify_grant()`). | Р |
| `address` | `void *` | 12 / 16 | Виртуальный адрес буфера в пространстве вызывающего (`(vir_bytes)` в `do_safecopy.c:382,393`). | А |
| `bytes` | `size_t` | 16 / 24 | Число копируемых байт. | Р |

Замечания: `sys_safecopy*()` принимают `vir_bytes offset`, а поле `size_t` — на всех ABI одной ширины, но разных типов; лучше единый тип.

#### `mess_lsys_krn_schedctl` — ipc.h:1093

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_schedctl`.

Направление: сервер планирования → ядро (kernel call). Вызовы (`m_type`): `SYS_SCHEDCTL`. Отправитель: `sys_schedctl()` (`minix/lib/libsys/sys_schedctl.c`; единственный вызов — `minix/servers/sched/schedule.c:218` с `flags = 0`). Получатель: `do_schedctl()` (`minix/kernel/system/do_schedctl.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `flags` | `__uint32_t` | 0 / 0 | 0 — вызывающий становится планировщиком процесса (`p_scheduler = caller`); `SCHEDCTL_FLAG_KERNEL` — планирует ядро, процесс сразу ставится в очередь; иные биты → `EINVAL` (`do_schedctl.c:16-21,28-43`). |  |
| `endpoint` | `endpoint_t` | 4 / 4 | Процесс, чьё планирование настраивается (`do_schedctl.c:23`). |  |
| `priority` | `int` | 8 / 8 | Только при `SCHEDCTL_FLAG_KERNEL`: очередь приоритета `TASK_Q..NR_SCHED_QUEUES` или −1 (не менять), `sched_proc()` в `minix/kernel/system.c:645`. |  |
| `quantum` | `int` | 12 / 12 | Только при `SCHEDCTL_FLAG_KERNEL`: квант в мс (≥1) или −1. |  |
| `cpu` | `int` | 16 / 16 | Только при `SCHEDCTL_FLAG_KERNEL`: номер CPU (SMP) или −1. |  |

Замечания: ветка `SCHEDCTL_FLAG_KERNEL` в дереве не используется (никто не передаёт флаг).

#### `mess_lsys_krn_schedule` — ipc.h:1104

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_schedule`.

Направление: сервер планирования → ядро (kernel call). Вызовы (`m_type`): `SYS_SCHEDULE`. Отправитель: `sys_schedule()` (`minix/lib/libsys/sys_schedule.c`; вызывает `minix/servers/sched/schedule.c:321`). Получатель: `do_schedule()` (`minix/kernel/system/do_schedule.c`) → `sched_proc()` (`minix/kernel/system.c:642`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpoint` | `endpoint_t` | 0 / 0 | Планируемый процесс; вызывающий обязан быть его `p_scheduler`, иначе `EPERM` (`do_schedule.c:14-20`). |  |
| `quantum` | `int` | 4 / 4 | Квант в мс (≥1) или −1 — не менять; ядро переводит в такты CPU `ms_2_cpu_time()` (`system.c:681-684`). |  |
| `priority` | `int` | 8 / 8 | Очередь `TASK_Q..NR_SCHED_QUEUES` или −1 (`system.c:645`). |  |
| `cpu` | `int` | 12 / 12 | Целевой CPU (SMP, с миграцией) или −1; без SMP игнорируется (`system.c:651-655,686-688`). |  |
| `niced` | `int` | 16 / 16 | Булево: выставить/снять `MF_NICED` (`do_schedule.c:27`, `system.c:690-693`). |  |

#### `mess_lsys_krn_sys_abort` — ipc.h:1115

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_abort`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_ABORT`. Отправитель: `sys_abort()` (`minix/lib/libsys/sys_abort.c`; PM `minix/servers/pm/main.c:309` по завершении reboot). Получатель: `do_abort()` (`minix/kernel/system/do_abort.c:21`). Ответ: формально `OK`, далее останов.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `how` | `int` | 0 / 0 | Способ останова, флаги `RB_*` из `reboot(2)` (`RB_HALT`, `RB_POWERDOWN`, …), передаётся в `prepare_shutdown()` → `minix_shutdown()` (`minix/kernel/main.c:351,390-392`). |  |

#### `mess_lsys_krn_sys_clear` — ipc.h:1122

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_clear`.

Направление: PM → ядро (kernel call). Вызовы (`m_type`): `SYS_CLEAR`. Отправитель: `sys_clear()` (`minix/lib/libsys/sys_clear.c`; `minix/servers/pm/forkexit.c:367,450`). Получатель: `do_clear()` (`minix/kernel/system/do_clear.c:29`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Завершившийся процесс, чей слот очищается (освобождение адресного пространства, IRQ-хуков, очередей). |  |

#### `mess_lsys_krn_sys_copy` — ipc.h:1129

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lsys_krn_sys_copy`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_VIRCOPY`, `SYS_PHYSCOPY` (один обработчик, разные права). Отправитель: `sys_vircopy()` (`minix/lib/libsys/sys_vircopy.c`, также `sys_datacopy*`), `sys_physcopy()` (`minix/lib/libsys/sys_physcopy.c`). Получатель: `do_copy()` (`minix/kernel/system/do_copy.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `src_endpt` | `endpoint_t` | 0 / 0 | Процесс-источник; `SELF` → вызывающий; `NONE` → `src_addr` — **физический** адрес (`do_copy.c:51,62-70`). |  |
| `src_addr` | `vir_bytes` | 4 / 8 | Адрес источника: виртуальный в `src_endpt` или физический при `NONE` (`do_copy.c:54`). | А, Ф |
| `dst_endpt` | `endpoint_t` | 8 / 16 | Процесс-приёмник, те же правила (`do_copy.c:52`). | В |
| `dst_addr` | `vir_bytes` | 12 / 24 | Адрес приёмника: виртуальный или физический при `NONE` (`do_copy.c:55`). | А, Ф |
| `nr_bytes` | `phys_bytes` | 16 / 32 | Число байт; проверяется, что умещается в `vir_bytes` (`do_copy.c:56,77`). | Р, Ф |
| `flags` | `int` | 20 / 40 | `CP_FLAG_TRY` — не подкачивать страницы через VM, вернуть `EFAULT` (разрешено только VFS, `do_copy.c:80-85`); иначе 0. | В |

Замечания: физический адрес в поле `vir_bytes` — на 32-битной платформе с PAE/LPAE (физ. > 4 ГБ) не уместится; длина как `phys_bytes`, а проверка — на `vir_bytes`.

#### `mess_lsys_krn_sys_devio` — ipc.h:1141

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_devio`.

Направление: libsys → ядро (kernel call, порты x86). Вызовы (`m_type`): `SYS_DEVIO`. Отправитель: `sys_in()`, `sys_out()` (`minix/lib/libsys/arch/i386/sys_in.c`, `sys_out.c`; макросы `sys_inb/outb/...` в `syslib.h`). Получатель: `do_devio()` (`minix/kernel/system/do_devio.c`). Ответ: `mess_krn_lsys_sys_devio` для ввода.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | `_DIO_INPUT`/`_DIO_OUTPUT` \| `_DIO_BYTE`/`_DIO_WORD`/`_DIO_LONG` (маски `_DIO_DIRMASK`, `_DIO_TYPEMASK`, `do_devio.c:27-28`). |  |
| `port` | `int` | 4 / 4 | Номер порта ввода-вывода x86; проверяется по `s_io_tab` при `CHECK_IO_PORT` и на выравнивание (`do_devio.c:46-67`). | Ж |
| `value` | `__uint32_t` | 8 / 8 | Для вывода — записываемое значение (`outb/outw/outl`, `do_devio.c:90-99`); при вводе не используется. | Ж |

#### `mess_lsys_krn_sys_diagctl` — ipc.h:1150

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_diagctl`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_DIAGCTL`. Отправитель: `sys_diagctl()` (`minix/lib/libsys/sys_diagctl.c`; `sys_diagctl_diag`, `sys_diagctl_stacktrace`, `sys_diagctl_register` и т.п.). Получатель: `do_diagctl()` (`minix/kernel/system/do_diagctl.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `code` | `int` | 0 / 0 | `DIAGCTL_CODE_DIAG` (вывести строку в журнал ядра), `_STACKTRACE`, `_REGISTER`/`_UNREGISTER` (подписка на `SIGKMESS`), `do_diagctl.c:24`. |  |
| `buf` | `vir_bytes` | 4 / 8 | Только `DIAG`: виртуальный адрес строки в пространстве вызывающего (`do_diagctl.c:26,33`). | А |
| `len` | `int` | 8 / 16 | Только `DIAG`: длина строки, 1..`DIAG_BUFSIZE` (2000), `do_diagctl.c:27-31`. | В |
| `endpt` | `endpoint_t` | 12 / 20 | Только `STACKTRACE`: процесс, чей стек печатать (`do_diagctl.c:44`). | В |

#### `mess_lsys_krn_sys_exec` — ipc.h:1160

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lsys_krn_sys_exec`.

Направление: PM/VM → ядро (kernel call). Вызовы (`m_type`): `SYS_EXEC`. Отправитель: `sys_exec()` (`minix/lib/libsys/sys_exec.c`; `minix/servers/pm/exec.c:197`, `minix/servers/vm/main.c:408`). Получатель: `do_exec()` (`minix/kernel/system/do_exec.c`) → `arch_proc_init()` (`minix/kernel/arch/i386/memory.c:722`, `arch/earm/memory.c:627`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Процесс, выполнивший exec (`do_exec.c:27`). |  |
| `ip` | `vir_bytes` | 4 / 8 | Точка входа; загружается в `p_reg.pc` (`do_exec.c:46` с приведением к `u32_t`, `memory.c:729`). | А, Ж |
| `stack` | `vir_bytes` | 8 / 16 | Начальный указатель стека; в `p_reg.sp` (`do_exec.c:47`, `u32_t`). | А, Ж |
| `name` | `vir_bytes` | 12 / 24 | Виртуальный адрес имени программы в пространстве **вызывающего** (PM/VM); копируется `PROC_NAME_LEN-1` байт (`do_exec.c:37`). | А |
| `ps_str` | `vir_bytes` | 16 / 32 | Адрес `struct ps_strings` в стеке процесса; передаётся в регистре (`p_reg.bx` на i386, `r0` на earm; `memory.c:731`, earm `memory.c:636`). | А, Ж |

Замечания: `do_exec()` явно режет `ip/stack/ps_str` до `u32_t` и `arch_proc_init()` принимает `u32_t` — на 64 битах обрезка помимо самого сообщения. Регистры для передачи `ps_str` архитектурно-зависимы.

#### `mess_lsys_krn_sys_fork` — ipc.h:1171

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_fork`.

Направление: VM → ядро (kernel call). Вызовы (`m_type`): `SYS_FORK`. Отправитель: `sys_fork()` (`minix/lib/libsys/sys_fork.c`; `minix/servers/vm/fork.c:89`). Получатель: `do_fork()` (`minix/kernel/system/do_fork.c`). Ответ: `mess_krn_lsys_sys_fork`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Родитель (процесс, вызвавший fork), `do_fork.c:41`. |  |
| `slot` | `endpoint_t` | 4 / 4 | Номер слота таблицы процессов для потомка (не endpoint, хотя тип `endpoint_t`), `do_fork.c:45,71`. |  |
| `flags` | `__uint32_t` | 8 / 8 | `PFF_VMINHIBIT` — не планировать потомка до разрешения VM (`do_fork.c:115`). |  |

#### `mess_lsys_krn_sys_getinfo` — ipc.h:1180

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_krn_sys_getinfo`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_GETINFO` (подзапросы `GET_MACHINE`, `GET_KINFO`, `GET_LOADINFO`, `GET_CPUINFO`, `GET_HZ`, `GET_IMAGE`, `GET_IRQHOOKS`, `GET_PROCTAB`, `GET_PRIVTAB`, `GET_PROC`, `GET_PRIV`, `GET_REGS`, `GET_WHOAMI`, `GET_MONPARAMS`, `GET_RANDOMNESS`, `GET_RANDOMNESS_BIN`, `GET_IRQACTIDS`, `GET_IDLETSC`, `GET_CPUTICKS`). Отправитель: `sys_getinfo()` и макросы `sys_get*` (`minix/lib/libsys/sys_getinfo.c`), `sys_whoami()`, `env_get_prm` (`minix/lib/libsys/env_get_prm.c:50-53`). Получатель: `do_getinfo()` (`minix/kernel/system/do_getinfo.c`). Ответ: данные копируются в `val_ptr`; для `GET_WHOAMI` — `mess_krn_lsys_sys_getwhoami`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | Подзапрос `GET_*` (`do_getinfo.c:60`). |  |
| `endpt` | `endpoint_t` | 4 / 4 | Всегда `SELF`; ядро **не читает** (копирует всегда вызывающему) — мёртвое поле. |  |
| `val_ptr` | `vir_bytes` | 8 / 8 | Виртуальный адрес приёмного буфера у вызывающего (`do_getinfo.c:214-215`). | А |
| `val_len` | `int` | 12 / 16 | Максимальная длина буфера в байтах; при >0 и меньше размера данных — `E2BIG` (`do_getinfo.c:210-212`); 0 — без проверки. | В |
| `val_ptr2` | `vir_bytes` | 16 / 24 | Второй указатель; ядро **не использует** — мёртвое поле. | А |
| `val_len2_e` | `int` | 20 / 32 | Параметр подзапроса: endpoint (`GET_PROC`, `GET_PRIV`, `GET_REGS`, `SELF` допустим), номер корзины (`GET_RANDOMNESS_BIN`), номер CPU (`GET_CPUTICKS`), `do_getinfo.c:108-195`. | В |

Замечания: `GET_PROCTAB/PRIVTAB/PROC/PRIV/REGS/IRQHOOKS` копируют внутренние структуры ядра «как есть» — ABI зависит от раскладки `struct proc`/`struct priv`/`stackframe_s` (Ж по содержимому). `env_get_prm` не инициализирует `val_ptr2`/`val_len2_e`.

#### `mess_lsys_krn_sys_getmcontext` — ipc.h:1192

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_getmcontext`.

Направление: PM → ядро (kernel call). Вызовы (`m_type`): `SYS_GETMCONTEXT`. Отправитель: `sys_getmcontext()` (`minix/lib/libsys/sys_mcontext.c`; `minix/servers/pm/mcontext.c:25`). Получатель: `do_getmcontext()` (`minix/kernel/system/do_mcontext.c:23`). Ответ: `mcontext_t` записывается по `ctx_ptr`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Процесс, чей контекст запрашивается (`do_mcontext.c:31`). |  |
| `ctx_ptr` | `vir_bytes` | 4 / 8 | Виртуальный адрес `mcontext_t` в пространстве **процесса `endpt`** (не вызывающего); ядро читает структуру, дописывает FPU-состояние (`__fpregs`, `_MC_FPU_SAVED`) и записывает обратно (`do_mcontext.c:42-64`). | А |

Замечания: содержимое `mcontext_t` — регистры CPU/FPU (машинно-зависимо), копируется через память, а не в сообщении.

#### `mess_lsys_krn_sys_iopenable` — ipc.h:1200

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_iopenable`.

Направление: libsys → ядро (kernel call, только i386). Вызовы (`m_type`): `SYS_IOPENABLE`. Отправитель: `sys_enable_iop()` (`minix/lib/libsys/arch/i386/sys_eniop.c`). Получатель: `do_iopenable()` (`minix/kernel/arch/i386/do_iopenable.c:24-27`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Процесс (или `SELF`), которому выставляются биты IOPL в EFLAGS (`enable_iop()`), т.е. прямой доступ к портам x86. |  |

Замечания: сам вызов архитектурно-специфичен (IOPL x86); в дереве `sys_enable_iop` вызовов вне libsys нет.

#### `mess_lsys_krn_sys_irqctl` — ipc.h:1207

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_irqctl`.

Направление: драйвер (libsys) → ядро (kernel call). Вызовы (`m_type`): `SYS_IRQCTL` (подзапросы `IRQ_SETPOLICY`, `IRQ_RMPOLICY`, `IRQ_ENABLE`, `IRQ_DISABLE`). Отправитель: `sys_irqctl()` и макросы `sys_irqsetpolicy/rmpolicy/enable/disable` (`minix/lib/libsys/sys_irqctl.c`). Получатель: `do_irqctl()` (`minix/kernel/system/do_irqctl.c`). Ответ: `mess_krn_lsys_sys_irqctl` (для `IRQ_SETPOLICY`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | Подзапрос `IRQ_*` (`do_irqctl.c:39`). |  |
| `vector` | `int` | 4 / 4 | Только `IRQ_SETPOLICY`: номер линии IRQ 0..`NR_IRQ_VECTORS-1`, проверяется по `s_irq_tab` при `CHECK_IRQ` (`do_irqctl.c:36,60-82`). | Ж |
| `policy` | `int` | 8 / 8 | Только `IRQ_SETPOLICY`: флаги политики, `IRQ_REENABLE` — переразрешать линию после прерывания (`do_irqctl.c:113`, `generic_handler`). |  |
| `hook_id` | `int` | 12 / 12 | Двойной смысл: в `IRQ_SETPOLICY` — `notify_id`, номер бита в `m_notify.interrupts` (0..`CHAR_BIT*sizeof(irq_id_t)-1`, `do_irqctl.c:87-88`); в остальных — индекс хука+1, ранее возвращённый ядром (`do_irqctl.c:35`). Поле запроса перезаписывается ответом. |  |

Замечания: `irq_id_t` = `unsigned long`, предел `notify_id` на x86_64 станет 63, но `generic_handler` делает `1 << notify_id` в `int` (`do_irqctl.c:166`) — переполнение при id ≥ 31.

#### `mess_lsys_krn_sys_memset` — ipc.h:1217

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_krn_sys_memset`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_MEMSET`. Отправитель: `sys_memset()` (`minix/lib/libsys/sys_memset.c`; VM `minix/servers/vm/alloc.c:454,470`, `mem_file.c:76` с `NONE`; libexec `exec_general.c:61` с endpoint). Получатель: `do_memset()` (`minix/kernel/system/do_memset.c:20-23`) → `vm_memset()` (`minix/kernel/arch/i386/memory.c:526`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `base` | `phys_bytes` | 0 / 0 | Начало области: **физический** адрес при `process == NONE` (VM чистит страницы) или виртуальный адрес в `process` (`memory.c:539-541`). Комментарий в `do_memset.c` («virtual address») неточен. | Ф, А |
| `count` | `phys_bytes` | 4 / 8 | Длина области в байтах (комментарий в `do_memset.c` «returns physical address» ошибочен). | Р, Ф |
| `pattern` | `unsigned long` | 8 / 16 | Заполнитель; используется только младший байт (`c &= 0xFF`, `memory.c:543`). | С |
| `process` | `endpoint_t` | 12 / 24 | `NONE` — физическая адресация, иначе процесс, в чьём пространстве `base` (`memory.c:540`). | В |

Замечания: в `vm_memset()` `pattern` приходит как `int`; достаточно `uint8_t`.

#### `mess_lsys_krn_sys_privctl` — ipc.h:1227

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_krn_sys_privctl`.

Направление: RS/VM → ядро (kernel call). Вызовы (`m_type`): `SYS_PRIVCTL` (подзапросы `SYS_PRIV_ALLOW`, `_YIELD`, `_DISALLOW`, `_CLEAR_IPC_REFS`, `_SET_SYS`, `_SET_USER`, `_ADD_IO`, `_ADD_MEM`, `_ADD_IRQ`, `_QUERY_MEM`, `_UPDATE_SYS`). Отправитель: `sys_privctl()`, `sys_privquery_mem()` (`minix/lib/libsys/sys_privctl.c`; `_QUERY_MEM` — `minix/servers/vm/mmap.c:302`). Получатель: `do_privctl()` (`minix/kernel/system/do_privctl.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | Подзапрос `SYS_PRIV_*` (`do_privctl.c:54`). |  |
| `endpt` | `endpoint_t` | 4 / 4 | Целевой процесс или `SELF` (`do_privctl.c:48-50`). |  |
| `arg_ptr` | `vir_bytes` | 8 / 8 | Виртуальный адрес аргумента у вызывающего: `struct priv` (`SET_SYS`, `UPDATE_SYS`, `do_privctl.c:92-96,255-259`), `struct io_range` (`ADD_IO`, порты x86, `:201`), `struct minix_mem_range` (`ADD_MEM`, физ. диапазон, `:212`), `int` IRQ (`ADD_IRQ`, `:227`). | А |
| `phys_start` | `phys_bytes` | 12 / 16 | Только `QUERY_MEM`: начало физического диапазона (MMIO/память), проверяемого по `s_mem_tab` (`do_privctl.c:239-250`). | Ф |
| `phys_len` | `phys_bytes` | 16 / 24 | Только `QUERY_MEM`: длина физического диапазона в байтах (`do_privctl.c:240`). | Ф, Р |

Замечания: структуры по `arg_ptr` (`struct priv` целиком, `io_range` с `unsigned` портами, `minix_mem_range` с `phys_bytes`) — тоже часть ABI ядро↔RS.

#### `mess_lsys_krn_sys_sdevio` — ipc.h:1238

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lsys_krn_sys_sdevio`.

Направление: драйвер (libsys) → ядро (kernel call, только i386). Вызовы (`m_type`): `SYS_SDEVIO`. Отправитель: `sys_sdevio()` (`minix/lib/libsys/arch/i386/sys_sdevio.c`) через макросы `sys_insb/insw/outsb/outsw`, `sys_safe_insb/...` (`minix/include/minix/syslib.h:78-93`). Получатель: `do_sdevio()` (`minix/kernel/arch/i386/do_sdevio.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | `DIO_{INPUT,OUTPUT}_{BYTE,WORD}` \| `_DIO_SAFE` (маски `_DIO_DIRMASK/_TYPEMASK/_SAFEMASK`, `do_sdevio.c:40,64-68`); `_DIO_LONG` не поддержан. |  |
| `port` | `long` | 4 / 8 | Номер порта ввода-вывода x86, проверка по `s_io_tab` (`do_sdevio.c:31,109-131`). | С, Ж |
| `vec_endpt` | `endpoint_t` | 8 / 16 | Процесс с буфером: для небезопасного варианта — только `SELF`/сам вызывающий; для `_DIO_SAFE` — грантодатель (`do_sdevio.c:29,56-93`). | В |
| `vec_addr` | `phys_bytes` | 12 / 24 | **Не физический адрес**: виртуальный адрес буфера (небезопасный вариант, `do_sdevio.c:91`) либо идентификатор гранта (`_DIO_SAFE`, `do_sdevio.c:70-71`). Тип выбран неверно. | А |
| `vec_size` | `vir_bytes` | 16 / 32 | Длина в **байтах** (а не «number of elements», как в комментарии): `phys_insw` делит на 2 (`minix/kernel/arch/i386/klib.S:86-87`), `verify_grant` проверяет столько байт (`do_sdevio.c:71`). | Р |
| `offset` | `vir_bytes` | 20 / 40 | Только `_DIO_SAFE`: смещение внутри гранта (`do_sdevio.c:73`). | Р |

Замечания: `sys_sdevio()` принимает `int count` — длина обрезается до `int` ещё в libsys.

#### `mess_lsys_krn_sys_setalarm` — ipc.h:1250

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_setalarm`.

Направление: системный процесс (libsys) → ядро (kernel call); ответ в том же типе. Вызовы (`m_type`): `SYS_SETALARM`. Отправитель: `sys_setalarm2()` / `sys_setalarm()` (`minix/lib/libsys/sys_setalarm.c`). Получатель: `do_setalarm()` (`minix/kernel/system/do_setalarm.c`). Ответ: в том же типе (`time_left`, `uptime` заполняет ядро); по срабатыванию — `notify` от `CLOCK` (`mess_notify`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `exp_time` | `clock_t` | 0 / 0 | Запрос: время срабатывания в тиках — относительное или абсолютное (по `abs_time`); 0 при относительном — снять будильник (`do_setalarm.c:31,54-59`). Ядро читает в `long`. |  |
| `time_left` | `clock_t` | 4 / 4 | Ответ: тиков до срабатывания прежнего будильника, `TMR_NEVER` если не был установлен, 0 если уже истёк (`do_setalarm.c:41-45`). |  |
| `uptime` | `clock_t` | 8 / 8 | Ответ: текущее монотонное время в тиках `get_monotonic()` (`do_setalarm.c:39,49`). |  |
| `abs_time` | `int` | 12 / 12 | Запрос: булево, `exp_time` абсолютное (`do_setalarm.c:32`). |  |

#### `mess_lsys_krn_sys_setgrant` — ipc.h:1260

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_setgrant`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_SETGRANT`. Отправитель: `sys_setgrant()` (`minix/lib/libsys/sys_setgrant.c`; `minix/lib/libsys/safecopies.c:107,380`). Получатель: `do_setgrant()` (`minix/kernel/system/do_setgrant.c:23-25`) → `_K_SET_GRANT_TABLE` (`minix/include/minix/safecopies.h:104`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `addr` | `vir_bytes` | 0 / 0 | Виртуальный адрес таблицы грантов `cp_grant_t[]` вызывающего; ядро сохраняет в `priv->s_grant_table` и читает её при каждом `verify_grant()` (исх.: cp_grant_t *). | А |
| `size` | `int` | 4 / 8 | Число элементов таблицы (`s_grant_entries`). | В |

Замечания: раскладка `cp_grant_t` (с `vir_bytes` адресом/длиной) — ABI ядро↔все процессы.

#### `mess_lsys_krn_sys_setmcontext` — ipc.h:1268

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_setmcontext`.

Направление: PM → ядро (kernel call). Вызовы (`m_type`): `SYS_SETMCONTEXT`. Отправитель: `sys_setmcontext()` (`minix/lib/libsys/sys_mcontext.c`; `minix/servers/pm/mcontext.c:15`). Получатель: `do_setmcontext()` (`minix/kernel/system/do_mcontext.c:74`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Процесс, чей контекст устанавливается (`do_mcontext.c:82`). |  |
| `ctx_ptr` | `vir_bytes` | 4 / 8 | Виртуальный адрес `mcontext_t` в пространстве процесса `endpt`; ядро берёт из него только FPU-состояние при `_MC_FPU_SAVED` (`do_mcontext.c:86-96`). | А |

#### `mess_lsys_krn_sys_settime` — ipc.h:1276

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_settime`.

Направление: PM → ядро (kernel call). Вызовы (`m_type`): `SYS_SETTIME`. Отправитель: `sys_settime()` (`minix/lib/libsys/sys_settime.c`; `minix/servers/pm/time.c:81`). Получатель: `do_settime()` (`minix/kernel/system/do_settime.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `sec` | `time_t` | 0 / 0 | При `now≠0` — новое время, секунды с 1970 (исх.: time in seconds since 1970); при `now==0` — секундная часть дельты adjtime (`do_settime.c:31,39-46`). |  |
| `nsec` | `long` | 8 / 8 | Наносекунды (0..10⁹-1), переводятся в тики делением на `10⁹/system_hz` (`do_settime.c:32,53`). | С |
| `now` | `int` | 12 / 16 | Ненулевое — установить время немедленно, 0 — `adjtime()` (плавная подстройка, `set_adjtime_delta`) (`do_settime.c:29`). | В |
| `clock_id` | `clockid_t` | 16 / 20 | Часы; принимается только `CLOCK_REALTIME`, иначе `EINVAL` (`do_settime.c:25`). | В |

#### `mess_lsys_krn_sys_sprof` — ipc.h:1286

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_krn_sys_sprof`.

Направление: PM → ядро (kernel call). Вызовы (`m_type`): `SYS_SPROF`. Отправитель: `sys_sprof()` (`minix/lib/libsys/sys_sprof.c`; `minix/servers/pm/profile.c:31,36`). Получатель: `do_sprofile()` (`minix/kernel/system/do_sprofile.c`). Ответ: только код возврата; при `PROF_STOP` данные копируются по `ctl_ptr`/`mem_ptr`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `action` | `int` | 0 / 0 | `PROF_START` / `PROF_STOP` (`do_sprofile.c:41`). |  |
| `freq` | `int` | 4 / 4 | Только START: частота выборок (для `PROF_RTC` — параметр `init_profile_clock()`, для `PROF_NMI` — `nmi_watchdog_start_profiling()`), `do_sprofile.c:76-80`. |  |
| `intr_type` | `int` | 8 / 8 | Только START: источник прерываний `PROF_RTC` (CMOS RTC) или `PROF_NMI` (NMI-сторож LAPIC), `do_sprofile.c:74`. Значения специфичны для x86. |  |
| `endpt` | `endpoint_t` | 12 / 12 | Только START: процесс (PM от имени пользователя), куда будут скопированы результаты (`do_sprofile.c:56-60`). |  |
| `ctl_ptr` | `vir_bytes` | 16 / 16 | Виртуальный адрес `struct sprof_info` в `endpt`; заполняется при STOP (`do_sprofile.c:61,117-118`). | А |
| `mem_ptr` | `vir_bytes` | 20 / 24 | Виртуальный адрес буфера выборок в `endpt` (`do_sprofile.c:62,119-120`). | А |
| `mem_size` | `size_t` | 24 / 32 | Размер буфера выборок, ограничивается `SAMPLE_BUFFER_SIZE` (`do_sprofile.c:70-72`). | Р |

Замечания: `sys_sprof()` принимает `int size` — размер обрезается в libsys.

#### `mess_lsys_krn_sys_statectl` — ipc.h:1299

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_statectl`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_STATECTL` (подзапросы `SYS_STATE_CLEAR_IPC_REFS`, `_SET_STATE_TABLE`, `_ADD_IPC_BL_FILTER`, `_ADD_IPC_WL_FILTER`, `_CLEAR_IPC_FILTERS`). Отправитель: `sys_statectl()` (`minix/lib/libsys/sys_statectl.c`; SEF, RS, VM, драйверные библиотеки). Получатель: `do_statectl()` (`minix/kernel/system/do_statectl.c`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | Подзапрос `SYS_STATE_*` (`do_statectl.c:19`). |  |
| `address` | `void *` | 4 / 8 | `SET_STATE_TABLE`: виртуальный адрес таблицы состояния LLVM-инструментирования (`s_state_table`, `do_statectl.c:29`); `ADD_IPC_*_FILTER`: адрес массива `ipc_filter_el_t` у вызывающего (`do_statectl.c:35,40`, `add_ipc_filter()` в `minix/kernel/system.c:705`). | А |
| `length` | `int` | 8 / 16 | `SET_STATE_TABLE`: число записей (`s_state_entries`); фильтры: размер массива в **байтах**, кратный `sizeof(ipc_filter_el_t)` (`system.c:715`). | В |

#### `mess_lsys_krn_sys_stime` — ipc.h:1308

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_stime`.

Направление: PM → ядро (kernel call). Вызовы (`m_type`): `SYS_STIME`. Отправитель: `sys_stime()` (`minix/lib/libsys/sys_stime.c`; `minix/servers/pm/time.c:126`). Получатель: `do_stime()` (`minix/kernel/system/do_stime.c:17`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `boot_time` | `time_t` | 0 / 0 | Новое время загрузки, секунды с 1970 (`set_boottime()`). |  |

#### `mess_lsys_krn_sys_times` — ipc.h:1315

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_krn_sys_times`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_TIMES`. Отправитель: `sys_times()` (`minix/lib/libsys/sys_times.c`). Получатель: `do_times()` (`minix/kernel/system/do_times.c:33-34`). Ответ: `mess_krn_lsys_sys_times`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Процесс, чьи времена нужны; `SELF` — вызывающий; `NONE` — только системные времена (uptime, boottime). |  |

#### `mess_lsys_krn_sys_trace` — ipc.h:1322

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_trace`.

Направление: PM → ядро (kernel call). Вызовы (`m_type`): `SYS_TRACE` (подзапросы `T_STOP`, `T_GETINS`, `T_GETDATA`, `T_GETUSER`, `T_SETINS`, `T_SETDATA`, `T_SETUSER`, `T_RESUME`, `T_DETACH`, `T_STEP`, `T_SYSCALL`, `T_READB_INS`, `T_WRITEB_INS`). Отправитель: `sys_trace()` (`minix/lib/libsys/sys_trace.c`; PM, реализация ptrace). Получатель: `do_trace()` (`minix/kernel/system/do_trace.c`). Ответ: `mess_krn_lsys_sys_trace`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | Подзапрос `T_*` (`do_trace.c:50,88`). |  |
| `endpt` | `endpoint_t` | 4 / 4 | Трассируемый процесс (`do_trace.c:51,83`). |  |
| `address` | `vir_bytes` | 8 / 8 | `T_GET/SETINS/DATA`, `T_READB/WRITEB_INS`: виртуальный адрес в трассируемом процессе; `T_GETUSER`: байтовое смещение в `struct proc` (далее `struct priv`), `T_SETUSER`: смещение в `p_reg` (`stackframe_s`), выровненное на `long`/`reg_t` (`do_trace.c:105-158`). | А, Р |
| `data` | `long` | 12 / 16 | Записываемое слово (`T_SETINS/SETDATA`), значение регистра CPU (`T_SETUSER`; PSW/PSR — только разрешённые биты, сегментные регистры x86 запрещены), байт (`T_WRITEB_INS`) (`do_trace.c:126-198`). | С, Ж |

Замечания: `T_SETUSER` сравнивает смещения через `(int) &((struct proc *) 0)->p_reg.cs` (`do_trace.c:146-154`) — приведение указателя к `int`; ptrace-ABI жёстко привязан к раскладке `struct proc`.

#### `mess_lsys_krn_sys_umap` — ipc.h:1332

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_umap`.

Направление: драйвер (libsys) → ядро (kernel call). Вызовы (`m_type`): `SYS_UMAP`, `SYS_UMAP_REMOTE`. Отправитель: `sys_umap()` (`minix/lib/libsys/sys_umap.c`), `sys_umap_remote()` (`minix/lib/libsys/arch/i386/sys_umap_remote.c`). Получатель: `do_umap()` (`minix/kernel/system/do_umap.c`) → `do_umap_remote()` (`minix/kernel/system/do_umap_remote.c`). Ответ: `mess_krn_lsys_sys_umap`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `src_endpt` | `endpoint_t` | 0 / 0 | Процесс-владелец адреса или грантодатель; для `SYS_UMAP` без гранта — только `SELF` (`do_umap.c:28,34`, `do_umap_remote.c:33`). |  |
| `segment` | `int` | 4 / 4 | `VM_D` (`LOCAL_VM_SEG\|VIR_ADDR`) — виртуальный адрес, `VM_GRANT` (`LOCAL_VM_SEG\|MEM_GRANT`) — грант (`minix/include/minix/const.h:59-68`, `do_umap_remote.c:29-30`). |  |
| `src_addr` | `vir_bytes` | 8 / 8 | Виртуальный адрес (`VM_D`) или **идентификатор гранта** (`VM_GRANT`: `(cp_grant_id_t) offset`, `do_umap_remote.c:64`). | А |
| `dst_endpt` | `endpoint_t` | 12 / 16 | Только `SYS_UMAP_REMOTE`: грантополучатель, от имени которого проверяется грант (`SELF` = вызывающий); `do_umap()` принудительно ставит `SELF` (`do_umap.c:35`, `do_umap_remote.c:34,87-94`). | В |
| `nr_bytes` | `int` | 16 / 20 | Длина области в байтах; должна быть физически непрерывной (`vm_lookup_range`, `do_umap_remote.c:106`). Libsys получает `vir_bytes bytes` и обрезает до `int`. | У, В |

#### `mess_lsys_krn_sys_vdevio` — ipc.h:1344

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_krn_sys_vdevio`.

Направление: драйвер (libsys) → ядро (kernel call, порты x86). Вызовы (`m_type`): `SYS_VDEVIO`. Отправитель: `sys_vinb/vinw/vinl/voutb/voutw/voutl()` (`minix/lib/libsys/arch/i386/sys_v*.c`). Получатель: `do_vdevio()` (`minix/kernel/system/do_vdevio.c`). Ответ: при вводе значения записываются обратно в вектор пар.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | `_DIO_INPUT/_DIO_OUTPUT` \| `_DIO_BYTE/_WORD/_LONG` (`do_vdevio.c:44-45`). |  |
| `vec_size` | `int` | 4 / 4 | Число пар (порт, значение); суммарный размер ≤ `VDEVIO_BUF_SIZE`, иначе `E2BIG` (`do_vdevio.c:49-65`). |  |
| `vec_addr` | `vir_bytes` | 8 / 8 | Виртуальный адрес массива `pv{b,w,l}_pair_t` у вызывающего (порты x86 + значения), читается и при вводе записывается обратно (`do_vdevio.c:68,153`) (исх.: pv{b,w,l}_pair_t *). | А |

#### `mess_lsys_krn_sys_vumap` — ipc.h:1353

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lsys_krn_sys_vumap`.

Направление: драйвер (libsys) → ядро (kernel call). Вызовы (`m_type`): `SYS_VUMAP`. Отправитель: `sys_vumap()` (`minix/lib/libsys/sys_vumap.c`). Получатель: `do_vumap()` (`minix/kernel/system/do_vumap.c`). Ответ: `mess_krn_lsys_sys_vumap` + вектор `struct vumap_phys` по `paddr`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Грантодатель или `SELF` (тогда `vaddr` содержит локальные виртуальные адреса), `do_vumap.c:40,79-87`. |  |
| `vaddr` | `vir_bytes` | 4 / 8 | Виртуальный адрес входного вектора `struct vumap_vir` (грант или адрес + размер) у вызывающего (`do_vumap.c:41,65`) (исх.: struct vumap_vir *). | А |
| `vcount` | `int` | 8 / 16 | Число элементов входного вектора, >0, обрезается до `MAPVEC_NR` (`do_vumap.c:42-51`). | В |
| `paddr` | `vir_bytes` | 12 / 24 | Виртуальный адрес выходного вектора `struct vumap_phys` (физ. адрес `phys_bytes` + размер) у вызывающего (`do_vumap.c:45,125`) (исх.: struct vumap_phys *). | А |
| `pmax` | `int` | 16 / 32 | Ёмкость выходного вектора, >0, ≤ `MAPVEC_NR` (`do_vumap.c:46-52`). | В |
| `access` | `int` | 20 / 36 | `VUA_READ`, `VUA_WRITE` или оба; ядро переводит в `CPF_READ/CPF_WRITE` (`do_vumap.c:55-60`). | В |
| `offset` | `size_t` | 24 / 40 | Смещение в байтах в первом элементе входного вектора (`do_vumap.c:43,75-77`). | Р |

Замечания: физические адреса возвращаются через память (`struct vumap_phys.vp_addr`, `phys_bytes`) — её раскладка тоже часть ABI ядро↔драйверы.

#### `mess_lsys_kern_vsafecopy` — ipc.h:1366

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_kern_vsafecopy`.

Направление: libsys → ядро (kernel call). Вызовы (`m_type`): `SYS_VSAFECOPY`. Отправитель: `sys_vsafecopy()` (`minix/lib/libsys/sys_vsafecopy.c`). Получатель: `do_vsafecopy()` (`minix/kernel/system/do_safecopy.c:399`). Ответ: только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `vec_addr` | `void *` | 0 / 0 | Виртуальный адрес массива `struct vscp_vec` у вызывающего; каждый элемент — одна safecopy (одна из сторон `SELF`), `do_safecopy.c:409,417`. | А |
| `vec_size` | `int` | 4 / 8 | Число элементов вектора (не байт); ядро копирует `els * sizeof(struct vscp_vec)` в статический буфер на `SCPVEC_NR` элементов (`do_safecopy.c:414-418`). | В |

Замечания: `els` не проверяется ни на `> SCPVEC_NR`, ни на `< 0` перед копированием в `static struct vscp_vec vec[SCPVEC_NR]` (`do_safecopy.c:401-418`) — возможное переполнение буфера ядра.

#### `mess_notify` — ipc.h:1714

Размер: i386 56, x86_64 56. Член объединения: `m_notify`.

Направление: ядро → любой процесс (уведомление `NOTIFY_MESSAGE`, собирается при доставке). Вызовы (`m_type`): `NOTIFY_MESSAGE` (источник различается по `m_source`: `HARDWARE` — прерывания, `SYSTEM` — сигналы ядра, `CLOCK` — будильник `SYS_SETALARM`, иначе — `ipc_notify()` от процесса). Отправитель: `BuildNotifyMessage()` в `mini_receive()`/`mini_notify()` (`minix/kernel/proc.c:99-115,1047,1168`). Получатель: главные циклы серверов и драйверов (`minix/servers/{pm,vfs,rs}/main.c`, `minix/lib/lib{blockdriver,chardriver,netdriver,inputdriver,sockdriver}`, `tty`, `at_wini`, `floppy`, `lwip`, `libsys/tickdelay.c`, `sef_signal.c` и др.). Ответ: нет.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `timestamp` | `__uint64_t` | 0 / 0 | Монотонное время доставки в тиках `get_monotonic()` (`proc.c:102`); заполняется для любого источника; получатели используют для таймеров (`expire_timers(m.m_notify.timestamp)` в `minix/servers/pm/main.c:67`, `vfs/main.c:112`, `tickdelay.c:32`) и усекают до `clock_t` (исх.: valid for every notify msg). |  |
| `interrupts` | `__uint64_t` | 8 / 8 | Только `m_source == HARDWARE`: битовая карта сработавших IRQ-хуков (`priv->s_int_pending`, бит = `notify_id` из `IRQ_SETPOLICY`), после чтения обнуляется (`proc.c:104-107`); драйверы сравнивают с собственной маской (`minix/drivers/tty/tty/tty.c:190`, `libblockdriver/driver.c:395`). (исх.: raised interrupts; valid if from HARDWARE) | Ж |
| `sigset` | `sigset_t` | 16 / 16 | Только `m_source == SYSTEM`: ожидающие сигналы ядра для системного процесса (`priv->s_sig_pending`, `SIGKMESS`, `SIGKSTOP`, `SIGKSIG`, …), после чтения очищаются (`proc.c:108-112`); читает `minix/lib/libsys/sef_signal.c:96`. (исх.: raised signals; valid if from SYSTEM) |  |
| ↳ `__bits` | `__uint32_t[4]` | 16 / 16 | 128-битная маска сигналов NetBSD (`sigset_t`). |  |

Замечания: `s_int_pending` имеет тип `irq_id_t` = `unsigned long` (32 бита на i386, 64 на x86_64) — поле `u64` уже готово, но ядро ограничивает `notify_id` шириной `irq_id_t` и сдвигает `int` (см. `mess_lsys_krn_sys_irqctl`).

#### `mess_sigcalls` — ipc.h:1916

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_sigcalls`.

Направление: PM (менеджер сигналов) ↔ ядро (kernel calls). Вызовы (`m_type`): `SYS_GETKSIG`, `SYS_ENDKSIG`, `SYS_KILL`, `SYS_SIGSEND`, `SYS_SIGRETURN`. Отправитель: `sys_getksig()`, `sys_endksig()`, `sys_kill()`, `sys_sigsend()`, `sys_sigreturn()` (`minix/lib/libsys/sys_getsig.c`, `sys_endsig.c`, `sys_kill.c`, `sys_sigsend.c`, `sys_sigreturn.c`). Получатель: `do_getksig()`, `do_endksig()`, `do_kill()`, `do_sigsend()`, `do_sigreturn()` (`minix/kernel/system/do_*.c`). Ответ: для `SYS_GETKSIG` — в том же типе (`endpt`, `map`), для остальных только код возврата.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `map` | `sigset_t` | 0 / 0 | Только ответ `SYS_GETKSIG`: ожидающие сигналы процесса `p_pending` (`do_getksig.c:32`) (исх.: used to pass signal bit map). |  |
| ↳ `__bits` | `__uint32_t[4]` | 0 / 0 | 128-битная маска сигналов. |  |
| `endpt` | `endpoint_t` | 16 / 16 | `GETKSIG` (ответ): процесс с ожидающими сигналами или `NONE` (`do_getksig.c:31,40`); `ENDKSIG`: процесс, обработку сигналов которого PM завершил (`do_endksig.c:27`); `KILL`: адресат сигнала (`do_kill.c:28`); `SIGSEND`/`SIGRETURN`: процесс, в котором строится/снимается кадр сигнала (`do_sigsend.c:31`, `do_sigreturn.c:28`). |  |
| `sig` | `int` | 20 / 20 | Только `SYS_KILL`: номер сигнала < `_NSIG` (`do_kill.c:26-31`) (исх.: signal number to send). |  |
| `sigctx` | `void *` | 24 / 24 | `SIGSEND`: виртуальный адрес `struct sigmsg` в пространстве **вызывающего** (PM), `do_sigsend.c:36-37`; `SIGRETURN`: адрес `struct sigcontext` в пространстве **процесса `endpt`** (`do_sigreturn.c:33-34`). Ядро копирует регистры CPU в/из `sigcontext` (исх.: pointer to signal context). | А |

Замечания: `struct sigmsg`/`sigcontext`/`sigframe_sigcontext` передаются через память и содержат регистры CPU (Ж по содержимому); `x86_64` размер 64 > 56 из-за выравнивания `void *` после `int sig`.

#### Псевдонимы общих раскладок

| Псевдоним | Поле | Тип | Раздел заголовка | Файл:строка | Назначение | 64 |
|---|---|---|---|---|---|---|
| `SMS_DST` | `m2_i1` | `int` | SYS_SAFEMEMSET | minix/include/minix/com.h:363 | `SYS_SAFEMEMSET`, libsys `sys_safememset()` (`minix/lib/libsys/sys_safememset.c`; `drivers/storage/memory/memory.c:304`, `lib/libfsdriver/utility.c:77`) → `do_safememset()` (`minix/kernel/system/do_safememset.c:24`): грантодатель (владелец памяти); `NONE` → `EFAULT` (исх.: dst endpoint). |  |
| `SMS_GID` | `m2_i3` | `int` | SYS_SAFEMEMSET | minix/include/minix/com.h:364 | Идентификатор гранта, требуется `CPF_WRITE` (`do_safememset.c:26,48`) (исх.: grant id). |  |
| `SMS_OFFSET` | `m2_l1` | `long` | SYS_SAFEMEMSET | minix/include/minix/com.h:365 | Смещение в байтах внутри гранта; libsys приводит `vir_bytes`→`long`, ядро — в `vir_bytes` (`do_safememset.c:27`) (исх.: offset within grant). | С, Р |
| `SMS_BYTES` | `m2_l2` | `long` | SYS_SAFEMEMSET | minix/include/minix/com.h:366 | Длина заполнения в байтах (`size_t`→`long`→`size_t`, `do_safememset.c:29`) (исх.: bytes from offset). | С, Р |
| `SMS_PATTERN` | `m2_i2` | `int` | SYS_SAFEMEMSET | minix/include/minix/com.h:367 | Байт-заполнитель; `vm_memset()` берёт младший байт (`do_safememset.c:28`) (исх.: memset() pattern). |  |
| `SVMCTL_WHO` | `m1_i1` | `int` | Field names for SYS_VMCTL. | minix/include/minix/com.h:370 | `SYS_VMCTL`, VM → ядро: `sys_vmctl*()` (`minix/lib/libsys/sys_vmctl.c`) → `do_vmctl()` (`minix/kernel/system/do_vmctl.c:20`) и `arch_do_vmctl()` (`minix/kernel/arch/{i386,earm}/arch_do_vmctl.c`): целевой процесс (`SELF` = VM). В ответе `MEMREQ_GET`/`KERN_PHYSMAP` перекрывается `m2_i1`. |  |
| `SVMCTL_PARAM` | `m1_i2` | `int` | Field names for SYS_VMCTL. | minix/include/minix/com.h:371 | Подзапрос `VMCTL_*`: `CLEAR_PAGEFAULT`, `MEMREQ_GET`, `MEMREQ_REPLY`, `KERN_PHYSMAP`, `KERN_MAP_REPLY`, `VMINHIBIT_SET/CLEAR`, `CLEARMAPCACHE`, `BOOTINHIBIT_CLEAR` (`do_vmctl.c:32`); архитектурные `GET_PDBR`, `SETADDRSPACE`, `FLUSHTLB`, `I386_INVLPG` (только i386). `NOPAGEZERO`, `I386_KERNELLIMIT` не обрабатываются (исх.: All SYS_VMCTL requests.). |  |
| `SVMCTL_VALUE` | `m1_i3` | `int` | Field names for SYS_VMCTL. | minix/include/minix/com.h:372 | Многоцелевое: `MEMREQ_REPLY` — код результата VM (`do_vmctl.c:86`); `KERN_PHYSMAP`/`KERN_MAP_REPLY` — индекс отображения ядра (`:114,122`); ответ `GET_PDBR` — **CR3/TTBR0 процесса** (физ. адрес корня таблиц страниц, `arch/i386/arch_do_vmctl.c:46`, earm `:43`); `I386_INVLPG` — виртуальный адрес для `invlpg` (`arch/i386/arch_do_vmctl.c:58`, VM не использует). `sys_vmctl()` передаёт `u32_t`. | У, Ф, Ж |
| `SVMCTL_MRG_TARGET` | `m2_i1` | `int` | Field names for SYS_VMCTL. | minix/include/minix/com.h:373 | Ответ `MEMREQ_GET`: процесс, в чьей памяти нужно подготовить диапазон (`p_vmrequest.target`, `do_vmctl.c:60`); читает VM `minix/servers/vm/pagefaults.c:305` (исх.: MEMREQ_GET reply: target process). |  |
| `SVMCTL_MRG_ADDR` | `m2_i2` | `int` | Field names for SYS_VMCTL. | minix/include/minix/com.h:374 | Ответ `MEMREQ_GET`: начальный виртуальный адрес диапазона (`params.check.start` — `vir_bytes`, сужается до `int`, `do_vmctl.c:62`) (исх.: MEMREQ_GET reply: address). | У |
| `SVMCTL_MRG_LENGTH` | `m2_i3` | `int` | Field names for SYS_VMCTL. | minix/include/minix/com.h:375 | Ответ `MEMREQ_GET`: длина диапазона в байтах (`params.check.length` — `vir_bytes`→`int`, `do_vmctl.c:64`) (исх.: MEMREQ_GET reply: length). | У |
| `SVMCTL_MRG_FLAG` | `m2_s1` | `short` | Field names for SYS_VMCTL. | minix/include/minix/com.h:376 | Ответ `MEMREQ_GET`: ненулевое — нужен доступ на запись (`params.check.writeflag`, `do_vmctl.c:66`) (исх.: MEMREQ_GET reply: flag). |  |
| `SVMCTL_MRG_EP2` | `m2_l1` | `long` | Field names for SYS_VMCTL. | minix/include/minix/com.h:377 | Ответ `MEMREQ_GET`: ядро **не заполняет**; `sys_vmctl_get_memreq()` читает мусор в `who_s`, VM его не использует — мёртвое поле (исх.: MEMREQ_GET reply: source process). | С |
| `SVMCTL_MRG_ADDR2` | `m2_l2` | `long` | Field names for SYS_VMCTL. | minix/include/minix/com.h:378 | Ответ `MEMREQ_GET`: не заполняется ядром, VM не использует (`mem_s`) — мёртвое поле (исх.: MEMREQ_GET reply: source address). | С |
| `SVMCTL_MRG_REQUESTOR` | `m2_p1` | `char *` | Field names for SYS_VMCTL. | minix/include/minix/com.h:379 | Ответ `MEMREQ_GET`: **endpoint** процесса-запросчика, приведённый к указателю `(void *) rp->p_endpoint` (`do_vmctl.c:68`) и обратно к `endpoint_t` в libsys; VM проверяет, не VFS ли это (исх.: MEMREQ_GET reply: requestor). Использование не по назначению. | А |
| `SVMCTL_MAP_VIR_ADDR` | `m1_p1` | `char *` | Field names for SYS_VMCTL. | minix/include/minix/com.h:380 | `KERN_MAP_REPLY`: виртуальный адрес, по которому VM отобразила область ядра с индексом `SVMCTL_VALUE` (LAPIC, IOAPIC, OXPCIe, usermapped) — ядро запоминает для MMIO (`do_vmctl.c:123`, `arch_phys_map_reply()` `minix/kernel/arch/i386/memory.c:847-875`). | А |
| `SVMCTL_PTROOT` | `m1_i3` | `int` | Field names for SYS_VMCTL. | minix/include/minix/com.h:381 | `SETADDRSPACE`: **физический адрес корня таблиц страниц** (CR3 на i386, TTBR0 на earm) процесса `SVMCTL_WHO` (`pt->pt_dir_phys`, `minix/servers/vm/pagetable.c:1421`); ядро пишет в `p_seg.p_cr3/p_ttbr` и в регистр (`arch/i386/arch_do_vmctl.c:49`, earm `:46`). Совпадает с `SVMCTL_VALUE` (тот же `m1_i3`); libsys получает `phys_bytes` и обрезает до `int`. | У, Ф, Ж |
| `SVMCTL_PTROOT_V` | `m1_p1` | `char *` | Field names for SYS_VMCTL. | minix/include/minix/com.h:382 | `SETADDRSPACE`: виртуальный адрес той же таблицы страниц (`p_cr3_v/p_ttbr_v`) — ядро обращается к PDE процесса через него (`arch/i386/arch_do_vmctl.c:49`). | А |
| `SVMCTL_MAP_FLAGS` | `m2_i1` | `int` | Reply message for VMCTL_KERN_PHYSMAP | minix/include/minix/com.h:385 | Ответ `KERN_PHYSMAP`: атрибуты отображения `VMMF_UNCACHED/USER/WRITE/GLO` (`arch_phys_map()`, `do_vmctl.c:115-118`); VM переводит их в флаги PTE (исх.: VMMF_*). |  |
| `SVMCTL_MAP_PHYS_ADDR` | `m2_l1` | `long` | Reply message for VMCTL_KERN_PHYSMAP | minix/include/minix/com.h:386 | Ответ `KERN_PHYSMAP`: физический адрес области, которую ядру нужно отобразить (MMIO LAPIC/IOAPIC/OXPCIe, видеопамять, страницы usermapped); ядро пишет через `(phys_bytes *) &m2_l1` (`do_vmctl.c:116`, `arch_phys_map()` `memory.c:746`). | С, Ф, Ж |
| `SVMCTL_MAP_PHYS_LEN` | `m2_l2` | `long` | Reply message for VMCTL_KERN_PHYSMAP | minix/include/minix/com.h:387 | Ответ `KERN_PHYSMAP`: длина области в байтах (запись через `(phys_bytes *)`, `do_vmctl.c:117`). | С, Ф, Р |
| `VT_WHICH` | `m2_i1` | `int` | Field names for SYS_VTIMER. | minix/include/minix/com.h:419 | `SYS_VTIMER`, PM → ядро: `sys_vtimer()` (`minix/lib/libsys/sys_vtimer.c`; `minix/servers/pm/alarm.c:205,240`) → `do_vtimer()` (`minix/kernel/system/do_vtimer.c:33`): `VT_VIRTUAL` или `VT_PROF` (исх.: which timer to set/retrieve). |  |
| `VT_SET` | `m2_i2` | `int` | Field names for SYS_VTIMER. | minix/include/minix/com.h:422 | Булево: установить новое значение (`do_vtimer.c:60`) (исх.: 1 for setting a timer, 0 retrieval only). |  |
| `VT_VALUE` | `m2_l1` | `long` | Field names for SYS_VTIMER. | minix/include/minix/com.h:423 | Запрос: новое значение в тиках (`clock_t`), ≤0 — выключить; ответ: прежнее значение в тиках (`do_vtimer.c:63-71`) (исх.: new/previous value of the timer). | С |
| `VT_ENDPT` | `m2_l2` | `long` | Field names for SYS_VTIMER. | minix/include/minix/com.h:424 | Endpoint процесса (или `SELF`), хранится в `long` (`do_vtimer.c:37`) (исх.: process to set/retrieve the timer for). | С |
| `RC_ENDPT` | `m1_i1` | `int` | Field names for SYS_RUNCTL. | minix/include/minix/com.h:427 | `SYS_RUNCTL`, PM/VFS → ядро: `sys_runctl()`, макросы `sys_stop/sys_delay_stop/sys_resume` (`minix/lib/libsys/sys_runctl.c`, `syslib.h:45-48`) → `do_runctl()` (`minix/kernel/system/do_runctl.c:30`): останавливаемый/возобновляемый процесс (исх.: which process to stop or resume). |  |
| `RC_ACTION` | `m1_i2` | `int` | Field names for SYS_RUNCTL. | minix/include/minix/com.h:428 | `RC_STOP` (выставить `RTS_PROC_STOP`, на SMP — через другой CPU) или `RC_RESUME` (`do_runctl.c:34`) (исх.: set or clear stop flag). |  |
| `RC_FLAGS` | `m1_i3` | `int` | Field names for SYS_RUNCTL. | minix/include/minix/com.h:431 | `RC_DELAY` — если процесс отправляет/трассируется, выставить `MF_SIG_DELAY` и вернуть `EBUSY` (`do_runctl.c:35,44-50`) (исх.: request flags). |  |
| `SYS_UPD_SRC_ENDPT` | `m1_i1` | `int` | Field names for SYS_UPDATE. | minix/include/minix/com.h:435 | `SYS_UPDATE`, RS/VM → ядро (live update): `sys_update()` (`minix/lib/libsys/sys_update.c`; `minix/servers/rs/update.c:243`, `minix/servers/vm/rs.c:183`) → `do_update()` (`minix/kernel/system/do_update.c:54`): старый экземпляр сервиса (должен быть `SYS_PROC`) (исх.: source endpoint). Комментарий в `do_update.c:5` называет поле `m2_i1` — смещение совпадает с `m1_i1`. |  |
| `SYS_UPD_DST_ENDPT` | `m1_i2` | `int` | Field names for SYS_UPDATE. | minix/include/minix/com.h:436 | Новый экземпляр, со слотом которого обменивается `src` (`do_update.c:64`) (исх.: destination endpoint). |  |
| `SYS_UPD_FLAGS` | `m1_i3` | `int` | Field names for SYS_UPDATE. | minix/include/minix/com.h:437 | `SYS_UPD_ROLLBACK` — обновление-откат (`do_update.c:53`) (исх.: update flags). |  |
| `PADCONF_PADCONF` | `m2_i1` | `int` | Field names for SYS_PADCONF | minix/include/minix/com.h:455 | `SYS_PADCONF` (только earm/TI OMAP), драйверы → ядро: `sys_padconf()` (`minix/lib/libsys/sys_padconf.c`; `drivers/system/gpio/gpio.c:182`, `drivers/bus/i2c/arch/earm/omap_i2c.c`, `drivers/storage/mmc/emmc.c`) → `do_padconf()` (`minix/kernel/arch/earm/do_padconf.c:14`) → `bsp_padconf_set()` (`minix/kernel/arch/earm/bsp/ti/omap_padconf.c:50`): **смещение регистра** pinmux `CONTROL_PADCONF_*` относительно базы SCM (исх.: pad to configure). | Ж |
| `PADCONF_MASK` | `m2_i2` | `int` | Field names for SYS_PADCONF | minix/include/minix/com.h:456 | Маска изменяемых бит 32-битного регистра (`set32(..., mask, value)`, `omap_padconf.c:57`) (исх.: mask to apply). | Ж |
| `PADCONF_VALUE` | `m2_i3` | `int` | Field names for SYS_PADCONF | minix/include/minix/com.h:457 | Значение регистра (`PADCONF_MUXMODE()`, `PADCONF_PULL_MODE_*`, `PADCONF_INPUT_ENABLE()`) (исх.: value to write). | Ж |
| `VMAD_EP` | `m2_i1` | `int` | General calls. | minix/include/minix/com.h:657 | `VM_ADDDMA` — **не используется**: в VM нет обработчика, в libsys нет отправителя; упоминается только в таблице `minix/commands/minix-service/parse.c:736`. Был: процесс-владелец DMA-области. |  |
| `VMAD_START` | `m2_l1` | `long` | General calls. | minix/include/minix/com.h:658 | **Не используется** (остаток старого протокола DMA). Был: начало DMA-области. | С |
| `VMAD_SIZE` | `m2_l2` | `long` | General calls. | minix/include/minix/com.h:659 | **Не используется**. Был: размер DMA-области. | С |
| `VMDD_EP` | `m2_i1` | `int` | General calls. | minix/include/minix/com.h:665 | `VM_DELDMA` — **не используется** (только `parse.c:737`). Был: процесс-владелец. |  |
| `VMDD_START` | `m2_l1` | `long` | General calls. | minix/include/minix/com.h:666 | **Не используется**. | С |
| `VMDD_SIZE` | `m2_l2` | `long` | General calls. | minix/include/minix/com.h:667 | **Не используется**. | С |
| `VMGD_PROCP` | `m2_i1` | `int` | General calls. | minix/include/minix/com.h:673 | `VM_GETDMA` — **не используется** (только `parse.c:738`). |  |
| `VMGD_BASEP` | `m2_l1` | `long` | General calls. | minix/include/minix/com.h:674 | **Не используется**. | С |
| `VMGD_SIZEP` | `m2_l2` | `long` | General calls. | minix/include/minix/com.h:675 | **Не используется**. | С |

Замечания: в `SYS_VMCTL` запрос использует раскладку `mess_1`, ответ — `mess_2`; совместимость держится на совпадении смещений `m1_i1..i3`/`m2_i1..i3` (8/12/16) и `m1_p1`/`m2_l1` (20 на i386, 24 на x86_64). `SVMCTL_PTROOT` и `SVMCTL_VALUE` — одно поле `int`, в нём едут CR3/TTBR — первое, что нужно расширить до `phys_bytes`/`u64` (LPAE, x86_64).

## 6. Системные вызовы → PM, VM, IPC, MIB, READCLOCK; PM ↔ службы, планировщик

#### `mess_lc_ipc_semctl` — ipc.h:364

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_ipc_semctl`.

Направление: libc → IPC (системный вызов SysV IPC). Вызовы (`m_type`): `IPC_SEMCTL`. Отправитель: `semctl()` (`minix/lib/libc/sys/sem.c:49`, конечная точка IPC ищется через `minix_rs_lookup("ipc")`). Получатель: `do_semctl()` (`minix/servers/ipc/sem.c:469`). Ответ: в том же типе (поле `ret` заполняет IPC; код возврата в `m_type`, `minix/servers/ipc/main.c:268`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `int` | 0 / 0 | Запрос: идентификатор набора семафоров (результат `semget`), `sem.c:480`; для `IPC_INFO`/`SEM_INFO` не используется как id, для `SEM_STAT` — индекс в таблице. |  |
| `num` | `int` | 4 / 4 | Запрос: номер семафора в наборе для `GETVAL`/`SETVAL`/`GETPID`/`GETNCNT`/`GETZCNT` (`sem.c:481`, проверка `num < sem_nsems`). |  |
| `cmd` | `int` | 8 / 8 | Запрос: команда `IPC_STAT`, `IPC_SET`, `IPC_RMID`, `IPC_INFO`, `SEM_INFO`, `SEM_STAT`, `GETALL`, `SETALL`, `GETVAL`, `SETVAL`, `GETPID`, `GETNCNT`, `GETZCNT` (`sem.c:482`). |  |
| `opt` | `vir_bytes` | 12 / 16 | Запрос: четвёртый аргумент `semctl` (`union semun`). Для `IPC_STAT`/`IPC_SET`/`SEM_STAT`/`IPC_INFO`/`SEM_INFO`/`GETALL`/`SETALL` — адрес буфера вызывающего (`struct semid_ds`, `struct seminfo`, массив `unsigned short`), копируется `sys_datacopy` (`sem.c:544`, `551`, `573`, `586`, `612`); для `SETVAL` — **целое значение**, а не адрес (`sem.c:630`, `val = (int)opt`); иначе 0. | А |
| `ret` | `int` | 16 / 24 | Ответ: результат для `GETVAL`/`GETPID`/`GETNCNT`/`GETZCNT` (`sem.c:593`–`608`), максимальный индекс для `IPC_INFO`/`SEM_INFO` (`sem.c:577`), IPC-id для `SEM_STAT` (`sem.c:547`). libc возвращает его для этих команд (`sem.c:86`–`89`). | В |

Замечания: `opt` — объединение «адрес или int» в поле ширины указателя; при переработке разделить на `addr` и `val`.

#### `mess_lc_ipc_semget` — ipc.h:374

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_ipc_semget`.

Направление: libc → IPC. Вызовы (`m_type`): `IPC_SEMGET`. Отправитель: `semget()` (`minix/lib/libc/sys/sem.c:25`). Получатель: `do_semget()` (`minix/servers/ipc/sem.c:93`). Ответ: в том же типе (поле `retid`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `key` | `key_t` | 0 / 0 | Запрос: ключ SysV IPC или `IPC_PRIVATE` (`sem.c:100`, поиск `sem_find_key`). | С |
| `nr` | `int` | 4 / 8 | Запрос: число семафоров в наборе (`nsems`), 1..`SEMMSL` при создании, не больше существующего при открытии (`sem.c:101`, `109`, `115`). | В |
| `flag` | `int` | 8 / 12 | Запрос: `IPC_CREAT`, `IPC_EXCL` и биты прав доступа (`sem.c:102`–`113`). | В |
| `retid` | `int` | 12 / 16 | Ответ: IPC-идентификатор набора (`IXSEQ_TO_IPCID`, `sem.c:154`); libc возвращает его (`sem.c:45`). | В |

#### `mess_lc_ipc_semop` — ipc.h:383

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_ipc_semop`.

Направление: libc → IPC. Вызовы (`m_type`): `IPC_SEMOP`. Отправитель: `semop()` (`minix/lib/libc/sys/sem.c:94`). Получатель: `do_semop()` (`minix/servers/ipc/sem.c:654`). Ответ: только код в `m_type` (может быть отложен — `SUSPEND`, затем `send_reply()`, `sem.c:207`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `int` | 0 / 0 | Запрос: идентификатор набора семафоров (`sem.c:664`). |  |
| `ops` | `void *` | 4 / 8 | Запрос: адрес массива `struct sembuf` в пространстве вызывающего, копируется `sys_datacopy` (`sem.c:679`). | А |
| `size` | `unsigned int` | 8 / 16 | Запрос: число элементов `struct sembuf` (не байт), 1..`SEMOPM` (`sem.c:665`–`672`). | В |

Замечания: в libc `nsops` имеет тип `size_t` и молча усекается до `unsigned int` (безопасно из-за предела `SEMOPM`).

#### `mess_lc_ipc_shmat` — ipc.h:391

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_ipc_shmat`.

Направление: libc → IPC. Вызовы (`m_type`): `IPC_SHMAT`. Отправитель: `shmat()` (`minix/lib/libc/sys/shmat.c:36`–`43`). Получатель: `do_shmat()` (`minix/servers/ipc/shm.c:130`). Ответ: в том же типе (поле `retaddr`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `int` | 0 / 0 | Запрос: идентификатор сегмента разделяемой памяти (`shm.c:137`). |  |
| `addr` | `const void *` | 4 / 8 | Запрос: желаемый адрес присоединения в пространстве вызывающего или 0; при `SHM_RND` округляется вниз до `PAGE_SIZE`, иначе невыровненный → `EINVAL` (`shm.c:138`–`145`); передаётся в `vm_remap()` (`shm.c:158`). | А |
| `flag` | `int` | 8 / 16 | Запрос: `SHM_RDONLY`, `SHM_RND` (`shm.c:139`, `152`). | В |
| `retaddr` | `void *` | 12 / 24 | Ответ: фактический адрес отображения в пространстве вызывающего — результат `vm_remap()` (`shm.c:168`); libc возвращает его (`shmat.c:43`). | А |

#### `mess_lc_ipc_shmctl` — ipc.h:400

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_ipc_shmctl`.

Направление: libc → IPC. Вызовы (`m_type`): `IPC_SHMCTL`. Отправитель: `shmctl()` (`minix/lib/libc/sys/shmctl.c:35`–`42`). Получатель: `do_shmctl()` (`minix/servers/ipc/shm.c:261`). Ответ: в том же типе (поле `ret`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `int` | 0 / 0 | Запрос: идентификатор сегмента (для `SHM_STAT` — индекс в таблице) (`shm.c:272`). |  |
| `cmd` | `int` | 4 / 4 | Запрос: `IPC_STAT`, `IPC_SET`, `IPC_RMID`, `IPC_INFO`, `SHM_INFO`, `SHM_STAT` (`shm.c:273`). |  |
| `buf` | `void *` | 8 / 8 | Запрос: адрес `struct shmid_ds` / `struct shminfo` / `struct shm_info` у вызывающего; `sys_datacopy` в обе стороны (`shm.c:274`, `308`, `319`, `341`). | А |
| `ret` | `int` | 12 / 16 | Ответ: IPC-id для `SHM_STAT` (`shm.c:311`), максимальный индекс для `IPC_INFO`/`SHM_INFO` (`shm.c:344`, `363`); libc возвращает его при успехе (`shmctl.c:42`). | В |

#### `mess_lc_ipc_shmdt` — ipc.h:409

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_ipc_shmdt`.

Направление: libc → IPC. Вызовы (`m_type`): `IPC_SHMDT`. Отправитель: `shmdt()` (`minix/lib/libc/sys/shmat.c:58`). Получатель: `do_shmdt()` (`minix/servers/ipc/shm.c:209`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `addr` | `const void *` | 0 / 0 | Запрос: адрес присоединённого сегмента у вызывающего; IPC определяет сегмент через `vm_getphys()` (физический адрес как id, `shm.c:218`) и снимает отображение `vm_unmap()` (`shm.c:232`). | А |

#### `mess_lc_ipc_shmget` — ipc.h:415

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_ipc_shmget`.

Направление: libc → IPC. Вызовы (`m_type`): `IPC_SHMGET`. Отправитель: `shmget()` (`minix/lib/libc/sys/shmget.c:33`–`40`). Получатель: `do_shmget()` (`minix/servers/ipc/shm.c:51`). Ответ: в том же типе (поле `retid`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `key` | `key_t` | 0 / 0 | Запрос: ключ SysV IPC или `IPC_PRIVATE` (`shm.c:60`). | С |
| `size` | `size_t` | 4 / 8 | Запрос: размер сегмента в байтах; при создании округляется до `PAGE_SIZE` и выделяется `mmap` в IPC (`shm.c:61`, `76`–`93`); при открытии существующего — не больше `shm_segsz`. | Р |
| `flag` | `int` | 8 / 16 | Запрос: `IPC_CREAT`, `IPC_EXCL`, биты прав (`shm.c:62`; целиком передаётся и в `check_perm`, `shm.c:65`). | В |
| `retid` | `int` | 12 / 20 | Ответ: IPC-идентификатор сегмента (`shm.c:125`); libc возвращает его (`shmget.c:40`). | В |

#### `mess_lc_mib_sysctl` — ipc.h:424

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lc_mib_sysctl`.

Направление: libc → MIB (системный вызов). Вызовы (`m_type`): `MIB_SYSCTL`. Отправитель: `__sysctl()` (`minix/lib/libc/sys/__sysctl.c`, `_syscall(MIB_PROC_NR, MIB_SYSCTL)`). Получатель: `mib_sysctl()` (`minix/servers/mib/main.c:277`). Ответ: тип `mess_mib_lc_sysctl` (код в `m_type`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `oldp` | `vir_bytes` | 0 / 0 | Запрос: адрес буфера для старого значения у вызывающего или 0 (`main.c:296`, `321`); для удалённых поддеревьев MIB делает из него magic-грант (`main.c:216`). | А |
| `oldlen` | `size_t` | 4 / 8 | Запрос: размер буфера `oldp` в байтах (`*oldlenp` или 0) (`main.c:297`). | Р |
| `newp` | `vir_bytes` | 8 / 16 | Запрос: адрес нового значения или 0 (`main.c:298`, `333`). | А |
| `newlen` | `size_t` | 12 / 24 | Запрос: длина нового значения в байтах (`main.c:299`). | Р |
| `namelen` | `unsigned int` | 16 / 32 | Запрос: число компонентов MIB-имени, 1..`CTL_MAXNAME` (`main.c:300`–`303`). | В |
| `namep` | `vir_bytes` | 20 / 40 | Запрос: адрес массива `int name[]` у вызывающего; читается только при `namelen > CTL_SHORTNAME` (`main.c:310`). | А |
| `name` | `int[8]` | 24 / 48 | Запрос: копия первых `namelen` компонентов имени, если `namelen <= CTL_SHORTNAME` (8) — экономия `sys_datacopy` (`main.c:314`). | В |

Замечания: на 64 битах из-за трёх адресов и двух `size_t` встроенное короткое имя не помещается; при переработке сократить `CTL_SHORTNAME` или вынести имя в грант.

#### `mess_lc_pm_exec` — ipc.h:435

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lc_pm_exec`.

Направление: libc → PM. Вызовы (`m_type`): `PM_EXEC`. Отправитель: `execve()` (`minix/lib/libc/sys/execve.c:47`–`53`). Получатель: `do_exec()` (`minix/servers/pm/exec.c:38`), который пересылает поля в VFS сообщением `VFS_PM_EXEC` (псевдонимы `VFS_PM_*`, `exec.c:44`–`50`). Ответ: нет (при успехе процесс заменён; при ошибке — `reply()` из `exec_restart()`, только код).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name` | `vir_bytes` | 0 / 0 | Запрос: адрес строки пути к исполняемому файлу у вызывающего (`execve.c:47`) → `VFS_PM_PATH` → `fetch_name()` в VFS (`minix/servers/vfs/exec.c:245`). | А |
| `namelen` | `size_t` | 4 / 8 | Запрос: длина пути с завершающим нулём (`strlen+1`) → `VFS_PM_PATH_LEN` (`int`!). | Р |
| `frame` | `vir_bytes` | 8 / 16 | Запрос: адрес подготовленного libc начального стека (argv/envp/ps_strings), выделенного `sbrk` (`execve.c:35`) → `VFS_PM_FRAME`; VFS копирует его (`vfs/exec.c:229`). | А |
| `framelen` | `size_t` | 12 / 24 | Запрос: размер кадра в байтах (≤ `ARG_MAX`, `vfs/exec.c:226`) → `VFS_PM_FRAME_LEN` (`int`!). | Р |
| `ps_str` | `vir_bytes` | 16 / 32 | Запрос: адрес `struct ps_strings` **в стеке будущего образа** (`vsp + смещение`, `execve.c:51`) → `VFS_PM_PS_STR` (`int`!) → обратно `VFS_PM_NEWPS_STR` → `sys_exec()` (`pm/exec.c:197`). | А |

Замечания: PM перекладывает адреса/размеры в `mess_7` через `int`-поля (`m7_i2`, `m7_i3`, `m7_i5`) — на 64 битах `ps_str`, `namelen`, `framelen` усекаются (см. псевдонимы `VFS_PM_*`).

#### `mess_lc_pm_exit` — ipc.h:446

Размер: i386 56, x86_64 56. Член объединения: `m_lc_pm_exit`.

Направление: libc → PM. Вызовы (`m_type`): `PM_EXIT`. Отправитель: `_exit()` (`minix/lib/libc/sys/_exit.c:45`–`46`). Получатель: `do_exit()` (`minix/servers/pm/forkexit.c:246`). Ответ: нет (`SUSPEND`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | Запрос: код завершения процесса, передаётся в `exit_proc()` (`forkexit.c:259`), затем в `W_EXITCODE` для `wait4`. |  |

#### `mess_lc_pm_getsid` — ipc.h:453

Размер: i386 56, x86_64 56. Член объединения: `m_lc_pm_getsid`.

Направление: libc → PM. Вызовы (`m_type`): `PM_GETSID`. Отправитель: `getsid()` (`minix/lib/libc/sys/getsid.c:13`). Получатель: `do_get()` (`minix/servers/pm/getset.c:19`, ветка `PM_GETSID`). Ответ: результат (id сессии = группа процессов) в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pid` | `__pid_t` | 0 / 0 | Запрос: pid процесса или 0 — вызывающий (`getset.c:73`–`74`). |  |

#### `mess_lc_pm_groups` — ipc.h:460

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_groups`.

Направление: libc → PM. Вызовы (`m_type`): `PM_GETGROUPS`, `PM_SETGROUPS`. Отправитель: `getgroups()` (`minix/lib/libc/sys/getgroups.c:17`–`20`), `setgroups()` (`minix/lib/libc/sys/setgroups.c:13`–`16`). Получатель: `do_get()` (`minix/servers/pm/getset.c:28`), `do_set()` (`getset.c:172`). Ответ: число групп в `m_type` (для GET); SET дополнительно уведомляет VFS (`VFS_PM_SETGROUPS`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `num` | `int` | 0 / 0 | Запрос: GET — ёмкость массива (0 = только узнать число), SET — число групп; 0..`NGROUPS_MAX` (`getset.c:30`, `176`). |  |
| `ptr` | `vir_bytes` | 4 / 8 | Запрос: адрес массива `gid_t` у вызывающего; GET — копия наружу (`getset.c:43`–`44`), SET — внутрь (`getset.c:184`). (исх.: gid_t *) | А |

#### `mess_lc_pm_itimer` — ipc.h:468

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_pm_itimer`.

Направление: libc → PM. Вызовы (`m_type`): `PM_ITIMER`. Отправитель: `getitimer()` (`minix/lib/libc/sys/getitimer.c:17`–`21`, `value = 0`), `setitimer()` (`minix/lib/libc/sys/setitimer.c:26`–`30`). Получатель: `do_itimer()` (`minix/servers/pm/alarm.c:93`). Ответ: только код в `m_type` (старое значение копируется по `ovalue`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `which` | `int` | 0 / 0 | Запрос: `ITIMER_REAL`, `ITIMER_VIRTUAL`, `ITIMER_PROF` (0..`NR_ITIMERS-1`, `alarm.c:100`–`101`). |  |
| `value` | `vir_bytes` | 4 / 8 | Запрос: адрес новой `struct itimerval` или 0 — не устанавливать (`alarm.c:107`, `116`). (исх.: const struct itimerval *) | А |
| `ovalue` | `vir_bytes` | 8 / 16 | Запрос: адрес для старого значения или 0 — не возвращать (`alarm.c:108`, `149`). (исх.: struct itimerval *) | А |

#### `mess_lc_pm_mcontext` — ipc.h:477

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_mcontext`.

Направление: libc → PM → ядро. Вызовы (`m_type`): `PM_GETMCONTEXT`, `PM_SETMCONTEXT`. Отправитель: `getmcontext()`/`setmcontext()` (`minix/lib/libc/sys/_mcontext.c:17`–`30`). Получатель: `do_getmcontext()`/`do_setmcontext()` (`minix/servers/pm/mcontext.c:13`, `23`), передают адрес ядру через `sys_getmcontext`/`sys_setmcontext`. Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `ctx` | `vir_bytes` | 0 / 0 | Запрос: адрес `mcontext_t` (машинные регистры, FPU) у вызывающего; ядро копирует туда/оттуда (`mcontext.c:15`, `25`). Содержимое структуры аппаратно-зависимо, само поле — адрес. (исх.: mcontext_t *) | А |

#### `mess_lc_pm_priority` — ipc.h:484

Размер: i386 56, x86_64 56. Член объединения: `m_lc_pm_priority`.

Направление: libc → PM (→ SCHED для SET). Вызовы (`m_type`): `PM_GETPRIORITY`, `PM_SETPRIORITY`. Отправитель: `getpriority()`/`setpriority()` (`minix/lib/libc/sys/priority.c:22`–`48`). Получатель: `do_getsetpriority()` (`minix/servers/pm/misc.c:239`). Ответ: GET — `nice - PRIO_MIN` в `m_type` (libc вычитает обратно); SET — код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `which` | `int` | 0 / 0 | Запрос: `PRIO_PROCESS` (остальные → `EINVAL`, `misc.c:251`). |  |
| `who` | `int` | 4 / 4 | Запрос: pid или 0 — вызывающий (`misc.c:254`–`258`). |  |
| `prio` | `int` | 8 / 8 | Запрос (только SET): nice в диапазоне `PRIO_MIN..PRIO_MAX`, передаётся в `sched_nice()` (`misc.c:280`). |  |

#### `mess_lc_pm_ptrace` — ipc.h:493

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_ptrace`.

Направление: libc → PM (→ ядро `SYS_TRACE`). Вызовы (`m_type`): `PM_PTRACE`. Отправитель: `ptrace()` (`minix/lib/libc/sys/ptrace.c:13`–`17`). Получатель: `do_trace()` (`minix/servers/pm/trace.c:42`). Ответ: тип `mess_pm_lc_ptrace`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pid` | `__pid_t` | 0 / 0 | Запрос: pid трассируемого процесса (`trace.c:63`, `103`, `140`); для `T_OK` не используется. |  |
| `req` | `int` | 4 / 4 | Запрос: `T_OK`, `T_ATTACH`, `T_STOP`, `T_GETINS`/`T_GETDATA`/`T_SETINS`/`T_SETDATA`, `T_GETUSER`/`T_SETUSER`, `T_READB_INS`/`T_WRITEB_INS`, `T_EXIT`, `T_SETOPT`, `T_GETRANGE`/`T_SETRANGE`, `T_DETACH`, `T_RESUME`, `T_STEP`, `T_SYSCALL` (`trace.c:48`; `sys/sys/ptrace.h:226`–`250`). |  |
| `addr` | `vir_bytes` | 8 / 8 | Запрос: адрес в пространстве трассируемого (`T_GET/SETINS/DATA`, `*B_INS`), **смещение в структуре `proc` ядра** (`T_GETUSER`/`T_SETUSER`) или адрес `struct ptrace_range` у трассировщика (`T_GET/SETRANGE`, `trace.c:169`); передаётся в `sys_trace(..., long addr, ...)` (`trace.c:106`, `244`). | А, Ж |
| `data` | `long` | 12 / 16 | Запрос: записываемое слово (`T_SET*`), код выхода (`T_EXIT`, `trace.c:151`), флаги `TO_*` (`T_SETOPT`, `trace.c:162`), номер сигнала (`T_RESUME`/`T_STEP`/`T_DETACH`, `trace.c:191`, `220`). Для `T_SETUSER` — значение регистра процессора. | С, Ж |

Замечания: `addr` и `data` — машинное слово трассируемого; смешение «адрес/смещение в proc ядра/адрес структуры» в одном поле.

#### `mess_lc_pm_reboot` — ipc.h:503

Размер: i386 56, x86_64 56. Член объединения: `m_lc_pm_reboot`.

Направление: libc → PM. Вызовы (`m_type`): `PM_REBOOT`. Отправитель: `reboot()` (`minix/lib/libc/sys/reboot.c:19`–`20`). Получатель: `do_reboot()` (`minix/servers/pm/misc.c:199`). Ответ: нет (`SUSPEND`; система останавливается).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `how` | `int` | 0 / 0 | Запрос: флаги `RB_*` (`RB_HALT`, `RB_POWERDOWN`, …), сохраняются в `abort_flag` (`misc.c:207`) и после ответа VFS передаются ядру `sys_abort()` (`minix/servers/pm/main.c:309`); при `RB_POWERDOWN` PM шлёт `RTCDEV_PWR_OFF` драйверу readclock (`misc.c:210`–`215`). |  |

#### `mess_lc_pm_rusage` — ipc.h:510

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_rusage`.

Направление: libc → PM. Вызовы (`m_type`): `PM_GETRUSAGE`. Отправитель: `getrusage()` (`minix/lib/libc/sys/getrusage.c:15`–`18`). Получатель: `do_getrusage()` (`minix/servers/pm/misc.c:401`). Ответ: только код (структура копируется по `addr`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `who` | `endpoint_t` | 0 / 0 | Запрос: **не endpoint**, а `RUSAGE_SELF` или `RUSAGE_CHILDREN` (`misc.c:407`, `421`); тип поля выбран неверно. |  |
| `addr` | `vir_bytes` | 4 / 8 | Запрос: адрес `struct rusage` у вызывающего, `sys_datacopy` наружу (`misc.c:445`–`446`). | А |

#### `mess_lc_pm_setgid` — ipc.h:518

Размер: i386 56, x86_64 56. Член объединения: `m_lc_pm_setgid`.

Направление: libc → PM (→ VFS `VFS_PM_SETGID`). Вызовы (`m_type`): `PM_SETGID`, `PM_SETEGID`. Отправитель: `setgid()`, `setegid()` (`minix/lib/libc/sys/setgid.c:14`, `23`). Получатель: `do_set()` (`minix/servers/pm/getset.c:144`, `159`). Ответ: код; ответ откладывается до `VFS_PM_SETGID_REPLY`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `gid` | `__gid_t` | 0 / 0 | Запрос: новый (эффективный для `PM_SETEGID`) gid (`getset.c:144`, `159`). |  |

#### `mess_lc_pm_setuid` — ipc.h:525

Размер: i386 56, x86_64 56. Член объединения: `m_lc_pm_setuid`.

Направление: libc → PM (→ VFS `VFS_PM_SETUID`). Вызовы (`m_type`): `PM_SETUID`, `PM_SETEUID`. Отправитель: `setuid()`, `seteuid()` (`minix/lib/libc/sys/setuid.c:13`, `22`). Получатель: `do_set()` (`minix/servers/pm/getset.c:112`, `129`). Ответ: код, после `VFS_PM_SETUID_REPLY`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `uid` | `__uid_t` | 0 / 0 | Запрос: новый (эффективный для `PM_SETEUID`) uid (`getset.c:112`, `129`). |  |

#### `mess_lc_pm_sig` — ipc.h:532

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_pm_sig`.

Направление: libc → PM. Вызовы (`m_type`): `PM_KILL`, `PM_SIGACTION`. Отправитель: `kill()` (`minix/lib/libc/sys/kill.c:19`–`21`), `sigaction()` (`minix/lib/libc/sys/sigaction.c:15`–`20`). Получатель: `do_kill()` (`minix/servers/pm/signal.c:197`), `do_sigaction()` (`signal.c:40`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pid` | `__pid_t` | 0 / 0 | Запрос (`PM_KILL`): pid получателя; 0 — своя группа, -1 — все, < -1 — группа `-pid` (`check_sig`, `signal.c:201`). В `PM_SIGACTION` не используется. |  |
| `nr` | `int` | 4 / 4 | Запрос: номер сигнала 1..`_NSIG-1` (`signal.c:48`, `201`). |  |
| `act` | `vir_bytes` | 8 / 8 | Запрос (`PM_SIGACTION`): адрес новой `struct sigaction` или 0 (`signal.c:59`, `63`). (исх.: const struct sigaction *) | А |
| `oact` | `vir_bytes` | 12 / 16 | Запрос (`PM_SIGACTION`): адрес для старой `struct sigaction` или 0 (`signal.c:53`–`55`). (исх.: struct sigaction *) | А |
| `ret` | `vir_bytes` | 16 / 24 | Запрос (`PM_SIGACTION`): адрес функции `__sigreturn` в libc вызывающего; сохраняется в `mp_sigreturn` (`signal.c:84`) и кладётся в `sigmsg.sm_sigreturn` при доставке сигнала (`signal.c:799`). (исх.: int (*)(void)) | А |

#### `mess_lc_pm_sigset` — ipc.h:543

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_sigset`.

Направление: libc → PM. Вызовы (`m_type`): `PM_SIGPROCMASK`, `PM_SIGSUSPEND`, `PM_SIGRETURN`. Отправитель: `sigprocmask()` (`minix/lib/libc/sys/sigprocmask.c:21`–`27`), `sigsuspend()` (`minix/lib/libc/sys/sigsuspend.c:14`–`15`), `sigreturn()` (`minix/lib/libc/sys/sigreturn.c:29`–`31`). Получатель: `do_sigprocmask()` (`minix/servers/pm/signal.c:102`), `do_sigsuspend()` (`signal.c:160`), `do_sigreturn()` (`signal.c:176`). Ответ: `PM_SIGPROCMASK` — тип `mess_pm_lc_sigset`; остальные — только код (sigsuspend — `EINTR` после сигнала, sigreturn обычно не возвращается).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `how` | `int` | 0 / 0 | Запрос (`PM_SIGPROCMASK`): `SIG_BLOCK`, `SIG_UNBLOCK`, `SIG_SETMASK`, `SIG_INQUIRE` (`signal.c:122`); в остальных вызовах не используется. |  |
| `ctx` | `vir_bytes` | 4 / 8 | Запрос (`PM_SIGRETURN`): адрес `struct sigcontext` на стеке процесса; PM передаёт его ядру `sys_sigreturn()` (приводит к `struct sigmsg *`, `signal.c:189`), ядро копирует регистры (`minix/kernel/system/do_sigreturn.c:33`). | А |
| `set` | `sigset_t` | 8 / 16 | Запрос: маска сигналов — аргумент `sigprocmask` (`signal.c:119`), временная маска `sigsuspend` (`signal.c:165`), восстанавливаемая маска `sigreturn` (`signal.c:185`). | В |
| ↳ `__bits` | `__uint32_t[4]` | 8 / 16 | 128 бит маски, по биту на сигнал. | В |

#### `mess_lc_pm_sprof` — ipc.h:552

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_pm_sprof`.

Направление: libc → PM → ядро (`SYS_SPROF`). Вызовы (`m_type`): `PM_SPROF`. Отправитель: `sprofile()` (`minix/lib/libc/sys/sprofile.c:22`–`29`). Получатель: `do_sprofile()` (`minix/servers/pm/profile.c:22`, только при `SPROFILE`, иначе `ENOSYS`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `action` | `int` | 0 / 0 | Запрос: `PROF_START` или `PROF_STOP` (`profile.c:28`). |  |
| `freq` | `int` | 4 / 4 | Запрос: частота выборок (параметр для таймера RTC/NMI) → `sys_sprof` (`profile.c:32`). |  |
| `intr_type` | `int` | 8 / 8 | Запрос: источник прерываний профилирования `PROF_RTC` (CMOS RTC) / `PROF_NMI` (`minix/include/minix/profile.h:14`–`15`); обрабатывает ядро (`minix/kernel/system/do_sprofile.c:74`). | Ж |
| `ctl_ptr` | `vir_bytes` | 12 / 16 | Запрос: адрес управляющей структуры `struct sprof_info_s` у вызывающего (ядро пишет туда статистику). | А |
| `mem_ptr` | `vir_bytes` | 16 / 24 | Запрос: адрес буфера выборок у вызывающего. | А |
| `mem_size` | `size_t` | 20 / 32 | Запрос: размер буфера выборок в байтах; в `sys_sprof(int size, ...)` усекается до `int` (`minix/include/minix/syslib.h:254`). | Р |

#### `mess_lc_pm_sysuname` — ipc.h:564

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_sysuname`.

**Не используется**: вызов `PM_SYSUNAME` помечен как obsolete (`minix/include/minix/callnr.h:38`), в libc и прочем дереве отправителя нет; обработчик остался как «compatibility block». Направление: (было) libc → PM. Вызовы (`m_type`): `PM_SYSUNAME`. Отправитель: нет. Получатель: `do_sysuname()` (`minix/servers/pm/misc.c:72`). Ответ: число скопированных байт в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req` | `int` | 0 / 0 | Запрос: 0 — получить строку (`misc.c:85`); прочие → `EINVAL`. |  |
| `field` | `int` | 4 / 4 | Запрос: индекс в `uts_tbl` (sysname, nodename, release, …) (`misc.c:79`–`81`). |  |
| `len` | `size_t` | 8 / 8 | Запрос: размер буфера вызывающего в байтах (`misc.c:89`). | Р |
| `value` | `vir_bytes` | 12 / 16 | Запрос: адрес буфера для строки (`misc.c:91`). | А |

#### `mess_lc_pm_time` — ipc.h:574

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_time`.

Направление: libc → PM (→ ядро `SYS_SETTIME`/`SYS_STIME`). Вызовы (`m_type`): `PM_CLOCK_GETTIME`, `PM_CLOCK_GETRES`, `PM_CLOCK_SETTIME`, `PM_STIME`. Отправитель: `clock_gettime()`, `clock_getres()`, `clock_settime()`, `adjtime()` (тоже `PM_CLOCK_SETTIME`, `now = 0`), `stime()` (`minix/lib/libc/sys/clock_*.c`, `adjtime.c:18`–`23`, `stime.c:15`–`16`). Получатель: `do_gettime()`, `do_getres()`, `do_settime()`, `do_stime()` (`minix/servers/pm/time.c:22`, `53`, `71`, `110`). Ответ: GET/GETRES — тип `mess_pm_lc_time`; SET/STIME — код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `sec` | `time_t` | 0 / 0 | Запрос: секунды устанавливаемого времени (`clock_settime`), смещения (`adjtime`, `delta->tv_sec`) или новое время для `stime` (`time.c:82`, `124`). `time_t` уже 64-битный. |  |
| `clk_id` | `clockid_t` | 8 / 8 | Запрос: `CLOCK_REALTIME`/`CLOCK_MONOTONIC` (`time.c:31`, `55`, `79`). |  |
| `now` | `int` | 12 / 12 | Запрос (`PM_CLOCK_SETTIME`): 1 — установить сразу, 0 — плавная подстройка (adjtime) → `sys_settime(now, ...)` (`time.c:81`). |  |
| `nsec` | `long` | 16 / 16 | Запрос: наносекунды (`clock_settime`) или `tv_usec*1000` (`adjtime`) (`time.c:82`). | С |

#### `mess_lc_pm_wait4` — ipc.h:585

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_pm_wait4`.

Направление: libc → PM. Вызовы (`m_type`): `PM_WAIT4`. Отправитель: `wait4()` (`minix/lib/libc/sys/wait4.c:18`–`25`). Получатель: `do_wait4()` (`minix/servers/pm/forkexit.c:475`). Ответ: тип `mess_pm_lc_wait4` (pid потомка в `m_type`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pid` | `__pid_t` | 0 / 0 | Запрос: >0 — конкретный потомок, -1 — любой, 0 — своя группа, < -1 — группа `-pid` (`forkexit.c:490`–`493`). |  |
| `options` | `int` | 4 / 4 | Запрос: `WNOHANG`, `WUNTRACED` и т.п. (`forkexit.c:491`, `553`). |  |
| `addr` | `vir_bytes` | 8 / 8 | Запрос: адрес `struct rusage` или 0; сохраняется в `mp_waddr` при ожидании (`forkexit.c:558`), заполняется в `tell_parent()` (`forkexit.c:704`). (исх.: struct rusage *) | А |

#### `mess_lc_readclock_rtcdev` — ipc.h:594

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_readclock_rtcdev`.

Направление: команда readclock → драйвер readclock; драйвер readclock (forward) → внешний драйвер RTC (tps65950). Вызовы (`m_type`): `RTCDEV_GET_TIME`, `RTCDEV_SET_TIME` (поле `tm`), `RTCDEV_GET_TIME_G`, `RTCDEV_SET_TIME_G` (поле `grant`), `RTCDEV_PWR_OFF` (без полей). Отправитель: `readclock()` (`minix/commands/readclock/readclock.c:138`), `fwd_msg()` (`minix/drivers/clock/readclock/forward.c:62`), PM `do_reboot()` (`RTCDEV_PWR_OFF`, `minix/servers/pm/misc.c:214`). Получатель: `main()` драйвера readclock (`minix/drivers/clock/readclock/readclock.c:70`), `main()` tps65950 (`minix/drivers/power/tps65950/tps65950.c:294`). Ответ: `RTCDEV_REPLY`, тип `mess_readclock_lc_rtcdev`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `grant` | `cp_grant_id_t` | 0 / 0 | Запрос (`*_G`): грант на `struct tm` вызывающего (`forward.c:79`, `85`); tps65950 копирует через `sys_safecopy*` (`tps65950.c:304`, `313`). |  |
| `tm` | `vir_bytes` | 4 / 8 | Запрос (`RTCDEV_GET_TIME`/`SET_TIME`): адрес `struct tm` у вызывающего (`readclock.c:151`), драйвер копирует `sys_datacopy` (`readclock/readclock.c:80`, `88`, `180`–`190`). (исх.: struct tm *) | А |
| `flags` | `int` | 8 / 16 | Запрос: `RTCDEV_NOFLAGS`, `RTCDEV_Y2KBUG`, `RTCDEV_CMOSREG` (x86 CMOS-специфичный) (`minix/include/minix/com.h:1014`–`1016`); передаётся в `rtc.get_time/set_time` (`readclock/readclock.c:73`, `95`). | В |

Замечания: в `fwd_msg()` и PM `do_reboot()` сообщение не обнуляется (`forward.c:65`, `misc.c:213`) — мусор в неиспользуемых полях. Два варианта адресации (адрес vs грант) в одном типе.

#### `mess_lc_svrctl` — ipc.h:603

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_svrctl`.

Направление: libc → PM или VFS. Вызовы (`m_type`): `PM_SVRCTL` (группы ioctl `'P'`, `'M'`), `VFS_SVRCTL` (группа `'F'`). Отправитель: `svrctl()` (`minix/lib/libc/sys/svrctl.c:14`–`22`). Получатель: `do_svrctl()` PM (`minix/servers/pm/misc.c:291`), `do_svrctl()` VFS (`minix/servers/vfs/misc.c:797`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `unsigned long` | 0 / 0 | Запрос: код в формате ioctl (`_IOW`/`_IOWR`, группа в `IOCGROUP`): `PMGETPARAM`, `PMSETPARAM`, `OPMGETPARAM`, `OPMSETPARAM`, `VFSSETPARAM`, … (`minix/include/sys/svrctl.h:16`–`24`; `misc.c:303`). Значения укладываются в 32 бита. | С |
| `arg` | `vir_bytes` | 4 / 8 | Запрос: адрес аргумента у вызывающего (обычно `struct sysgetenv`, содержащая в свою очередь указатели `key`/`val`) (`misc.c:304`, `319`). | А |

#### `mess_lc_vm_brk` — ipc.h:922

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vm_brk`.

Направление: libc → VM. Вызовы (`m_type`): `VM_BRK`. Отправитель: `brk()` (`minix/lib/libc/sys/brk.c:29`–`30`). Получатель: `do_brk()` (`minix/servers/vm/break.c:44`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `addr` | `void *` | 0 / 0 | Запрос: новый адрес конца сегмента данных (break) вызывающего → `real_brk()` (`break.c:56`). | А |

#### `mess_lc_vm_getphys` — ipc.h:928

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vm_getphys`.

Направление: libc (используется сервером IPC) → VM. Вызовы (`m_type`): `VM_GETPHYS`. Отправитель: `vm_getphys()` (`minix/lib/libc/sys/mmap.c:143`); вызывается из IPC (`minix/servers/ipc/shm.c:118`, `218`). Получатель: `do_get_phys()` (`minix/servers/vm/mmap.c:438`). Ответ: в том же типе (`ret_addr`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Запрос: процесс, в чьём пространстве ищется адрес (`mmap.c:446`). |  |
| `addr` | `void *` | 4 / 8 | Запрос: виртуальный адрес в пространстве `endpt` (`mmap.c:447`). | А |
| `ret_addr` | `void *` | 8 / 16 | Ответ: **физический** адрес страницы (`phys_bytes` из `map_get_phys()`, приведённый к `void *`, `mmap.c:456`); libc возвращает его как `unsigned long`, IPC использует как идентификатор сегмента (`shm.c:118`). | Ф |

Замечания: физический адрес в поле-указателе: при PAE/LPAE (Ф > ширины указателя) обрежется. При переработке — `phys_bytes`/`uint64_t`.

#### `mess_lc_vm_shm_unmap` — ipc.h:936

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vm_shm_unmap`.

Направление: libc (сервер IPC) → VM. Вызовы (`m_type`): `VM_SHM_UNMAP`. Отправитель: `vm_unmap()` (`minix/lib/libc/sys/mmap.c:132`); вызывается из IPC `do_shmdt()` (`minix/servers/ipc/shm.c:232`). Получатель: `do_munmap()` (`minix/servers/vm/mmap.c:512`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `forwhom` | `endpoint_t` | 0 / 0 | Запрос: процесс, у которого снимается отображение (`mmap.c:523`; `SELF` → отправитель). |  |
| `addr` | `void *` | 4 / 8 | Запрос: начальный адрес региона (выровнен на страницу); длина берётся из найденного региона (`mmap.c:554`–`565`). | А |

#### `mess_lexec_pm_exec_new` — ipc.h:967

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lexec_pm_exec_new`.

Направление: libexec (в VFS или RS) → PM. Вызовы (`m_type`): `PM_EXEC_NEW`. Отправитель: `libexec_pm_newexec()` (`minix/lib/libexec/exec_general.c:78`–`91`), вызывается из VFS `pm_exec()` (`minix/servers/vfs/exec.c:359`) и RS (`minix/servers/rs/exec.c:102`). Получатель: `do_newexec()` (`minix/servers/pm/exec.c:62`). Ответ: тип `mess_pm_lexec_exec_new`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Запрос: endpoint процесса, выполняющего exec (`exec.c:73`). |  |
| `ptr` | `vir_bytes` | 4 / 8 | Запрос: адрес `struct exec_info` в пространстве **отправителя** (VFS/RS); PM копирует её (`exec.c:78`–`79`) и берёт `allow_setuid`, `new_uid/gid`, `progname`, `stack_high`, `frame_len`. (исх.: struct exec_info *) | А |

Замечания: PM читает структуру `exec_info` из памяти VFS/RS — её раскладка (с `vir_bytes`/`size_t`) тоже часть ABI.

#### `mess_lsys_getsysinfo` — ipc.h:1065

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_getsysinfo`.

Направление: libsys → PM / VFS / RS / DS. Вызовы (`m_type`): `PM_GETSYSINFO`, `VFS_GETSYSINFO`, `RS_GETSYSINFO`, `DS_GETSYSINFO`. Отправитель: `getsysinfo()` (`minix/lib/libsys/getsysinfo.c`). Получатель: `do_getsysinfo()` в PM (`minix/servers/pm/misc.c:108`), VFS (`minix/servers/vfs/misc.c:52`), RS (`minix/servers/rs/request.c:1095`), DS (`minix/servers/ds/store.c:653`). Ответ: только код (данные копируются по `where`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `what` | `int` | 0 / 0 | Запрос: `SI_PROC_TAB`, `SI_DMAP_TAB`, `SI_DATA_STORE`, `SI_CALL_STATS`, `SI_PROCPUB_TAB`, `SI_PROCALL_TAB`, `SI_PROCLIGHT_TAB` (`minix/include/minix/sysinfo.h:11`–`17`; PM `misc.c:124`). |  |
| `where` | `vir_bytes` | 4 / 8 | Запрос: адрес буфера у вызывающего, `sys_datacopy` наружу (PM `misc.c:142`–`143`, DS `store.c:672`). | А |
| `size` | `size_t` | 8 / 16 | Запрос: размер буфера в байтах; PM/VFS/DS требуют точного совпадения с размером таблицы (`misc.c:139`), RS — не меньше (`request.c:1109`). | Р |

Замечания: копируются целиком внутренние таблицы серверов (`struct mproc`, `struct fproc`, `struct rproc`, …) — их раскладка становится ABI между сервером и утилитами (ps, top, procfs).

#### `mess_lsys_mib_register` — ipc.h:1373

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_mib_register`.

Направление: libsys (rmib, в сервисах) → MIB. Вызовы (`m_type`): `MIB_REGISTER`, `MIB_DEREGISTER` (только `root_id`). Отправитель: `rmib_send_reg()` (`minix/lib/libsys/rmib.c:863`), `rmib_deregister()` (`rmib.c:959`–`962`), асинхронно (`asynsend3`). Получатель: `mib_register()` (`minix/servers/mib/remote.c:198`), `mib_deregister()` (`remote.c:292`). Ответ: нет (EDONTREPLY для асинхронных).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `root_id` | `__uint32_t` | 0 / 0 | Запрос: локальный в сервисе номер корня монтируемого поддерева (`rmib.c:871`, `remote.c:227`, `303`). |  |
| `flags` | `__uint32_t` | 4 / 4 | Запрос: `SYSCTL_VERSION` \| флаги узла `CTLFLAG_*` без `CTLFLAG_SPARSE` (`rmib.c:872`–`873`). |  |
| `csize` | `unsigned int` | 8 / 8 | Запрос: размер массива дочерних узлов (`rnode_size`) — число элементов, не байт (`rmib.c:874`). |  |
| `clen` | `unsigned int` | 12 / 12 | Запрос: число реально заданных дочерних узлов (`rnode_clen`) (`rmib.c:875`). |  |
| `miblen` | `unsigned int` | 16 / 16 | Запрос: длина MIB-имени точки монтирования, ≤ 8 (`remote.c:221`). |  |
| `mib` | `int[8]` | 20 / 20 | Запрос: MIB-имя точки монтирования (`rmib.c:877`, `remote.c:229`). |  |

#### `mess_lsys_mib_reply` — ipc.h:1384

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_mib_reply`.

Направление: libsys (rmib) → MIB, ответ на `COMMON_MIB_INFO`/`COMMON_MIB_CALL`. Вызовы (`m_type`): `COMMON_MIB_REPLY`. Отправитель: `rmib_process()` (`minix/lib/libsys/rmib.c:1037`). Получатель: `mib_remote_info()` (`minix/servers/mib/remote.c:359`–`364`), `mib_remote_call()` (`remote.c:461`–`476`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__uint32_t` | 0 / 0 | Ответ: копия `req_id` запроса (сейчас всегда 0, резерв для асинхронности, `remote.c:361`, `463`). |  |
| `status` | `ssize_t` | 4 / 8 | Ответ: результат `rmib_call()` — длина данных `oldp` в байтах (≥ 0) или код ошибки; `ERESTART` — поддерево не зарегистрировано (`rmib.c:1079`, `remote.c:473`). | Р |

#### `mess_lsys_pm_getepinfo` — ipc.h:1399

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_pm_getepinfo`.

Направление: libsys → PM. Вызовы (`m_type`): `PM_GETEPINFO`. Отправитель: `getepinfo()` (`minix/lib/libsys/getepinfo.c:8`, используется `getnpid/getnuid/getngid`) и `getsockcred()` (`getepinfo.c:59`, используется `minix/net/uds/io.c:599`). Получатель: `do_getepinfo()` (`minix/servers/pm/misc.c:169`). Ответ: тип `mess_pm_lsys_getepinfo`, pid процесса в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Запрос: endpoint опрашиваемого процесса (`misc.c:175`). |  |
| `groups` | `vir_bytes` | 4 / 8 | Запрос: адрес массива `gid_t` у вызывающего для дополнительных групп или 0 (`misc.c:188`–`189`). | А |
| `ngroups` | `int` | 8 / 16 | Запрос: ёмкость массива `groups` (число элементов) (`misc.c:185`). | В |

#### `mess_lsys_pm_getprocnr` — ipc.h:1408

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_pm_getprocnr`.

Направление: libsys (RS) → PM. Вызовы (`m_type`): `PM_GETPROCNR`. Отправитель: `getprocnr()` (`minix/lib/libsys/getprocnr.c:6`), вызывается RS (`minix/servers/rs/main.c:451`). Получатель: `do_getprocnr()` (`minix/servers/pm/misc.c:149`, только от RS). Ответ: тип `mess_pm_lsys_getprocnr`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pid` | `__pid_t` | 0 / 0 | Запрос: pid процесса (`misc.c:159`). |  |

#### `mess_lsys_pm_proceventmask` — ipc.h:1415

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_pm_proceventmask`.

Направление: libsys (системный сервис) → PM. Вызовы (`m_type`): `PM_PROCEVENTMASK`. Отправитель: `proceventmask()` (`minix/lib/libsys/proceventmask.c:12`), вызывается IPC (`minix/servers/ipc/main.c:164`–`166`). Получатель: `do_proceventmask()` (`minix/servers/pm/event.c:171`, только `PRIV_PROC`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `mask` | `unsigned int` | 0 / 0 | Запрос: маска событий подписки `PROC_EVENT_EXIT` \| `PROC_EVENT_SIGNAL`; 0 — отписка (`event.c:179`–`192`). |  |

#### `mess_lsys_pm_srv_fork` — ipc.h:1422

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_pm_srv_fork`.

Направление: libsys (RS) → PM. Вызовы (`m_type`): `PM_SRV_FORK`. Отправитель: `srv_fork()` (`minix/lib/libsys/srv_fork.c:6`), вызывается RS (`minix/servers/rs/main.c:446`). Получатель: `do_srv_fork()` (`minix/servers/pm/forkexit.c:146`, только от RS). Ответ: pid потомка в `m_type` (после `VFS_PM_SRV_FORK_REPLY`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `uid` | `__uid_t` | 0 / 0 | Запрос: реальный и эффективный uid потомка (`forkexit.c:206`–`208`), передаётся в VFS как `VFS_PM_REUID` (`forkexit.c:227`). |  |
| `gid` | `__gid_t` | 4 / 4 | Запрос: реальный и эффективный gid потомка (`forkexit.c:209`–`211`) → `VFS_PM_REGID` (`forkexit.c:228`). |  |

#### `mess_lsys_sched_scheduling_start` — ipc.h:1430

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_sched_scheduling_start`.

Направление: libsys (PM, RS) → SCHED. Вызовы (`m_type`): `SCHEDULING_START`, `SCHEDULING_INHERIT`. Отправитель: `sched_start()` (`minix/lib/libsys/sched_start.c:46`), `sched_inherit()` (`sched_start.c:11`); вызываются из PM (`minix/servers/pm/schedule.c:37`, `79`) и RS (`minix/servers/rs/utility.c:375`). Получатель: `do_start_scheduling()` (`minix/servers/sched/schedule.c:140`). Ответ: тип `mess_sched_lsys_scheduling_start`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpoint` | `endpoint_t` | 0 / 0 | Запрос: планируемый процесс (`schedule.c:154`, `161`). |  |
| `parent` | `endpoint_t` | 4 / 4 | Запрос: родитель; для `SCHEDULING_INHERIT` из него наследуются приоритет и квант (`schedule.c:162`, `203`). |  |
| `maxprio` | `int` | 8 / 8 | Запрос: максимальная очередь приоритета 0..`NR_SCHED_QUEUES-1` (`schedule.c:163`). |  |
| `quantum` | `int` | 12 / 12 | Запрос (`SCHEDULING_START`): квант времени в мс (`schedule.c:196`); для `INHERIT` не задаётся. |  |

#### `mess_lsys_sched_scheduling_stop` — ipc.h:1440

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_sched_scheduling_stop`.

Направление: libsys (PM, RS) → SCHED. Вызовы (`m_type`): `SCHEDULING_STOP`. Отправитель: `sched_stop()` (`minix/lib/libsys/sched_stop.c:9`), вызывается из PM (`minix/servers/pm/forkexit.c:425`), RS (`minix/servers/rs/manager.c:461`, `request.c:342`). Получатель: `do_stop_scheduling()` (`minix/servers/sched/schedule.c:112`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpoint` | `endpoint_t` | 0 / 0 | Запрос: процесс, планирование которого прекращается (`schedule.c:121`). |  |

#### `mess_mib_lc_sysctl` — ipc.h:1548

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_mib_lc_sysctl`.

Направление: MIB → libc, ответ на `MIB_SYSCTL` (`mess_lc_mib_sysctl`). Вызовы (`m_type`): код результата. Отправитель: `mib_sysctl()` (`minix/servers/mib/main.c:369`, `376`). Получатель: `__sysctl()` (`minix/lib/libc/sys/__sysctl.c`, `*oldlenp = …oldlen`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `oldlen` | `size_t` | 0 / 0 | Ответ: полная длина старых данных в байтах (может быть больше буфера, тогда `ENOMEM`); возвращается и при ошибке (поведение NetBSD) (`main.c:369`–`376`). | Р |

#### `mess_mib_lsys_call` — ipc.h:1554

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_mib_lsys_call`.

Направление: MIB → libsys (rmib в сервисе, владеющем поддеревом). Вызовы (`m_type`): `COMMON_MIB_CALL`. Отправитель: `mib_remote_call()` (`minix/servers/mib/remote.c:379`, `ipc_sendrec`). Получатель: `rmib_process()` → `rmib_call()` (`minix/lib/libsys/rmib.c:1056`, `679`). Ответ: тип `mess_lsys_mib_reply`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__uint32_t` | 0 / 0 | Запрос: id запроса, сейчас всегда 0 (`remote.c:425`), возвращается в ответе (`rmib.c:1057`). |  |
| `root_id` | `__uint32_t` | 4 / 4 | Запрос: номер корня поддерева в сервисе (`node_rid`) (`rmib.c:695`); неизвестный → `ERESTART`. |  |
| `name_grant` | `cp_grant_id_t` | 8 / 8 | Запрос: грант (`CPF_READ`) на остаток MIB-имени в MIB (`remote.c:396`, `rmib.c:719`). |  |
| `name_len` | `unsigned int` | 12 / 12 | Запрос: число компонентов остатка имени (`rmib.c:713`). |  |
| `oldp_grant` | `cp_grant_id_t` | 16 / 16 | Запрос: magic-грант (`CPF_WRITE`) прямо на буфер `oldp` пользователя или `GRANT_INVALID` (`minix/servers/mib/main.c:216`, `rmib.c:725`). |  |
| `oldp_len` | `size_t` | 20 / 24 | Запрос: размер буфера `oldp` в байтах (`rmib.c:726`). | Р |
| `newp_grant` | `cp_grant_id_t` | 24 / 32 | Запрос: magic-грант (`CPF_READ`) на новое значение или `GRANT_INVALID` (`rmib.c:729`). | В |
| `newp_len` | `size_t` | 28 / 40 | Запрос: длина нового значения в байтах (`rmib.c:730`). | Р |
| `user_endpt` | `endpoint_t` | 32 / 48 | Запрос: endpoint исходного вызывающего sysctl (`rmib.c:733`). | В |
| `flags` | `__uint32_t` | 36 / 52 | Запрос: 1 — вызывающий авторизован (`mib_authed`), прочие биты не определены (`remote.c:434`, `rmib.c:737`). | В |
| `root_ver` | `__uint32_t` | 40 / 56 | Запрос: версия узла-корня в MIB (`node_ver`) (`rmib.c:738`). | В |
| `tree_ver` | `__uint32_t` | 44 / 60 | Запрос: версия всего дерева MIB (`rmib.c:739`). | В |

#### `mess_mib_lsys_info` — ipc.h:1571

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_mib_lsys_info`.

Направление: MIB → libsys (rmib). Вызовы (`m_type`): `COMMON_MIB_INFO`. Отправитель: `mib_remote_info()` (`minix/servers/mib/remote.c:317`, `343`–`349`). Получатель: `rmib_process()` → `rmib_info()` (`minix/lib/libsys/rmib.c:1049`, `998`). Ответ: тип `mess_lsys_mib_reply`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__uint32_t` | 0 / 0 | Запрос: id запроса, всегда 0; rmib читает его и для неизвестных типов («HACK», `rmib.c:1069`). |  |
| `root_id` | `__uint32_t` | 4 / 4 | Запрос: номер корня поддерева (`rmib.c:1006`). |  |
| `name_grant` | `cp_grant_id_t` | 8 / 8 | Запрос: грант на буфер MIB для имени узла (`rmib.c:1016`). |  |
| `name_size` | `size_t` | 12 / 16 | Запрос: размер буфера имени в байтах (`rmib.c:1013`). | Р |
| `desc_grant` | `cp_grant_id_t` | 16 / 24 | Запрос: грант на буфер для описания узла (`rmib.c:1028`). | В |
| `desc_size` | `size_t` | 20 / 32 | Запрос: размер буфера описания в байтах (`rmib.c:1025`–`1026`). | Р |

Замечания: `rmib_process()` полагается на то, что `req_id` на одном месте во всех MIB-запросах (смещение 0) — неявная зависимость раскладок.

#### `mess_mmap` — ipc.h:1582

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_mmap`.

Направление: libc → VM. Вызовы (`m_type`): `VM_MMAP`; также `VM_MUNMAP` через псевдонимы `VMUM_ADDR = m_mmap.addr`, `VMUM_LEN = m_mmap.len` (`minix/include/minix/com.h:650`–`651`). Отправитель: `minix_mmap_for()`/`mmap()` (`minix/lib/libc/sys/mmap.c:21`–`46`, сторонний процесс — из libexec `minix/lib/libexec/exec_general.c:23`–`45` в VFS/RS), `munmap()` (`mmap.c:76`). Получатель: `do_mmap()` (`minix/servers/vm/mmap.c:200`), для файлов — продолжение `mmap_file_cont()` (`mmap.c:160`) после запроса к VFS; `do_munmap()` (`mmap.c:512`). Ответ: в том же типе (`retaddr`); для файловых отображений — отдельное сообщение из `mmap_file_cont()` (`mmap.c:189`–`194`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `offset` | `__off_t` | 0 / 0 | Запрос: смещение в файле в байтах (64 бита) для файловых отображений (`mmap.c:180`). |  |
| `addr` | `void *` | 8 / 8 | Запрос: желаемый адрес (подсказка или `MAP_FIXED`) в пространстве получателя отображения (`mmap.c:204`); для `VM_MUNMAP` — адрес снимаемого региона (`VMUM_ADDR`). | А |
| `len` | `size_t` | 12 / 16 | Запрос: длина в байтах (`mmap.c:207`); для `VM_MUNMAP` — `VMUM_LEN`, округляется до страницы. | Р |
| `prot` | `int` | 16 / 24 | Запрос: `PROT_READ`/`PROT_WRITE`/`PROT_EXEC` (используется `PROT_WRITE`, `mmap.c:169`, `261`). | В |
| `flags` | `int` | 20 / 28 | Запрос: `MAP_ANON`, `MAP_SHARED`, `MAP_PRIVATE`, `MAP_FIXED`, `MAP_CONTIG`, `MAP_PREALLOC`, `MAP_THIRDPARTY` (ставит libc при `forwhom != SELF`) и др. (`mmap.c:213`, `242`–`250`). | В |
| `fd` | `int` | 24 / 32 | Запрос: дескриптор файла или -1 для анонимной памяти; для файла VM спрашивает VFS (`VMVFSREQ_FDLOOKUP`, `mmap.c:265`). | В |
| `forwhom` | `endpoint_t` | 28 / 36 | Запрос: процесс, в чьё пространство отображать (при `MAP_THIRDPARTY`, только VFS/RS; `mmap.c:213`–`216`). | В |
| `retaddr` | `void *` | 32 / 40 | Ответ: адрес созданного отображения (`mmap.c:276`, `191`) или `MAP_FAILED`. | А |

Замечания: VM сохраняет всё исходное сообщение до ответа VFS (`vfs_request(..., m, sizeof(*m))`, `mmap.c:265`) — раскладка `message` используется и как внутреннее состояние.

#### `mess_pm_lc_getgid` — ipc.h:1731

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lc_getgid`.

Направление: PM → libc, ответ на `PM_GETGID`. Вызовы (`m_type`): реальный gid. Отправитель: `do_get()` (`minix/servers/pm/getset.c:56`–`58`). Получатель: `getegid()` (`minix/lib/libc/sys/getegid.c:18`–`19`); `getgid()` берёт только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `egid` | `__uid_t` | 0 / 0 | Ответ: эффективный gid (`getset.c:58`). Тип объявлен `uid_t` вместо `gid_t` (оба 32 бита). |  |

#### `mess_pm_lc_getpid` — ipc.h:1738

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lc_getpid`.

Направление: PM → libc, ответ на `PM_GETPID`. Вызовы (`m_type`): pid вызывающего. Отправитель: `do_get()` (`minix/servers/pm/getset.c:61`–`63`). Получатель: `getppid()` (`minix/lib/libc/sys/getppid.c:18`–`19`); `getpid()` берёт `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `parent_pid` | `__pid_t` | 0 / 0 | Ответ: pid родителя (`getset.c:63`). |  |

#### `mess_pm_lc_getuid` — ipc.h:1745

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lc_getuid`.

Направление: PM → libc, ответ на `PM_GETUID`. Вызовы (`m_type`): реальный uid. Отправитель: `do_get()` (`minix/servers/pm/getset.c:51`–`53`). Получатель: `geteuid()` (`minix/lib/libc/sys/geteuid.c:18`–`19`); `getuid()` берёт `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `euid` | `__uid_t` | 0 / 0 | Ответ: эффективный uid (`getset.c:53`). |  |

#### `mess_pm_lc_ptrace` — ipc.h:1752

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_pm_lc_ptrace`.

Направление: PM → libc, ответ на `PM_PTRACE` (`mess_lc_pm_ptrace`). Отправитель: `do_trace()` (`minix/servers/pm/trace.c:59`, `110`, `133`, `248`), `exit_restart()` для `T_EXIT` (`minix/servers/pm/forkexit.c:462`). Получатель: `ptrace()` (`minix/lib/libc/sys/ptrace.c:23`–`24`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `data` | `long` | 0 / 0 | Ответ: прочитанное слово памяти (`T_GETINS`/`T_GETDATA`), байт (`T_READB_INS`), значение из `proc` ядра/регистр (`T_GETUSER`) — результат `sys_trace(..., &data)`; иначе 0. Значение -1 libc отличает обнулением `errno`. | С, Ж |

#### `mess_pm_lc_sigset` — ipc.h:1759

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lc_sigset`.

Направление: PM → libc, ответ на `PM_SIGPROCMASK` и `PM_SIGPENDING`. Отправитель: `do_sigprocmask()` (`minix/servers/pm/signal.c:120`), `do_sigpending()` (`signal.c:95`). Получатель: `sigprocmask()` (`minix/lib/libc/sys/sigprocmask.c:28`), `sigpending()` (`minix/lib/libc/sys/sigpending.c:15`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `set` | `sigset_t` | 0 / 0 | Ответ: старая маска блокировки (`PM_SIGPROCMASK`) или множество ожидающих сигналов (`PM_SIGPENDING`). |  |
| ↳ `__bits` | `__uint32_t[4]` | 0 / 0 | 128 бит маски. |  |

#### `mess_pm_lc_time` — ipc.h:1766

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_pm_lc_time`.

Направление: PM → libc, ответ на `PM_CLOCK_GETTIME`, `PM_CLOCK_GETRES`, `PM_GETTIMEOFDAY`. Отправитель: `do_gettime()`, `do_getres()`, `do_time()` (`minix/servers/pm/time.c:42`–`44`, `59`–`60`, `101`–`102`). Получатель: `clock_gettime()`, `clock_getres()`, `gettimeofday()` (`minix/lib/libc/sys/clock_gettime.c:22`–`23`, `clock_getres.c:22`–`23`, `gettimeofday.c:21`–`22`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `sec` | `time_t` | 0 / 0 | Ответ: секунды (`boottime + clock/system_hz`; для GETRES — 0). |  |
| `nsec` | `long` | 8 / 8 | Ответ: наносекунды 0..999999999 (вычисляется как `uint32_t`, `time.c:43`–`44`); `gettimeofday` делит на 1000. | С |

#### `mess_pm_lc_wait4` — ipc.h:1775

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lc_wait4`.

Направление: PM → libc, ответ на `PM_WAIT4` (`mess_lc_pm_wait4`); pid потомка в `m_type`. Отправитель: `do_wait4()` (`minix/servers/pm/forkexit.c:529`), `tell_parent()` (`forkexit.c:712`), `tell_tracer()` (`forkexit.c:748`), `trace.c:273` (остановка трассируемого). Получатель: `wait4()` (`minix/lib/libc/sys/wait4.c:24`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | Ответ: статус завершения в формате `W_EXITCODE(код, сигнал)` или `W_STOPCODE(сигнал)`. |  |

#### `mess_pm_lexec_exec_new` — ipc.h:1782

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lexec_exec_new`.

Направление: PM → libexec (VFS/RS), ответ на `PM_EXEC_NEW` (`mess_lexec_pm_exec_new`). Отправитель: `do_newexec()` (`minix/servers/pm/exec.c:122`). Получатель: `libexec_pm_newexec()` (`minix/lib/libexec/exec_general.c:89`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `suid` | `int` | 0 / 0 | Ответ: булево — разрешено ли выполнение setuid/setgid (процесс не трассируется и файл имеет биты); записывается в `exec_info.allow_setuid`. |  |

#### `mess_pm_lsys_getepinfo` — ipc.h:1789

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lsys_getepinfo`.

Направление: PM → libsys, ответ на `PM_GETEPINFO`; pid в `m_type`. Отправитель: `do_getepinfo()` (`minix/servers/pm/misc.c:180`–`184`). Получатель: `getepinfo()` (`minix/lib/libsys/getepinfo.c:22`–`24`), `getsockcred()` (`getepinfo.c:73`–`77`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `uid` | `__uid_t` | 0 / 0 | Ответ: реальный uid. |  |
| `euid` | `__uid_t` | 4 / 4 | Ответ: эффективный uid. |  |
| `gid` | `__gid_t` | 8 / 8 | Ответ: реальный gid. |  |
| `egid` | `__gid_t` | 12 / 12 | Ответ: эффективный gid. |  |
| `ngroups` | `int` | 16 / 16 | Ответ: **полное** число дополнительных групп процесса (скопировано может быть меньше — по ёмкости запроса, `misc.c:184`–`186`). |  |

#### `mess_pm_lsys_getprocnr` — ipc.h:1800

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lsys_getprocnr`.

Направление: PM → libsys (RS), ответ на `PM_GETPROCNR`. Отправитель: `do_getprocnr()` (`minix/servers/pm/misc.c:162`). Получатель: `getprocnr()` (`minix/lib/libsys/getprocnr.c:17`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Ответ: endpoint процесса с запрошенным pid. |  |

#### `mess_pm_lsys_proc_event` — ipc.h:1807

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lsys_proc_event`.

Направление: PM → подписанный сервис (сейчас только IPC) и обратно (эхо-ответ). Вызовы (`m_type`): `PROC_EVENT` (PM → сервис), `PROC_EVENT_REPLY` (сервис → PM, то же содержимое). Отправитель: `resume_event()` (`minix/servers/pm/event.c:100`–`105`, `asynsend3`); ответ — `got_proc_event()` (`minix/servers/ipc/main.c:207`–`208`). Получатель: `got_proc_event()` (`ipc/main.c:191`); ответ — `do_proc_event_reply()` (`event.c:219`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Запрос/ответ: процесс, с которым произошло событие (`ipc/main.c:196`, `event.c:240`). |  |
| `event` | `unsigned int` | 4 / 4 | Запрос/ответ: `PROC_EVENT_EXIT` или `PROC_EVENT_SIGNAL`; PM сверяет эхо (`event.c:274`). |  |

#### `mess_pm_lsys_sigs_signal` — ipc.h:1815

Размер: i386 56, x86_64 56. Член объединения: `m_pm_lsys_sigs_signal`.

Направление: менеджер сигналов (PM для RS/системных процессов, RS для сервисов) → libsys SEF. Вызовы (`m_type`): `SIGS_SIGNAL_RECEIVED`. Отправитель: PM `sig_proc()` (`minix/servers/pm/signal.c:471`–`473`), RS (`minix/servers/rs/main.c:699`–`701`), асинхронно. Получатель: `do_sef_signal_request()` (`minix/lib/libsys/sef_signal.c:117`). Ответ: нет.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `num` | `int` | 0 / 0 | Запрос: номер сигнала (не завершающего), передаётся в обработчик SEF. |  |

Замечания: в PM `signal.c:470` сообщение не обнуляется (мусор в остальных байтах).

#### `mess_pm_sched_scheduling_set_nice` — ipc.h:1822

Размер: i386 56, x86_64 56. Член объединения: `m_pm_sched_scheduling_set_nice`.

Направление: PM → SCHED. Вызовы (`m_type`): `SCHEDULING_SET_NICE`. Отправитель: `sched_nice()` (`minix/servers/pm/schedule.c:89`, из `do_getsetpriority()`). Получатель: `do_nice()` (`minix/servers/sched/schedule.c:254`). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpoint` | `endpoint_t` | 0 / 0 | Запрос: процесс (`schedule.c:265`). |  |
| `maxprio` | `__uint32_t` | 4 / 4 | Запрос: новая максимальная очередь, пересчитанная из nice (`nice_to_priority`), < `NR_SCHED_QUEUES` (`schedule.c:272`). |  |

Замечания: PM не обнуляет сообщение (`pm/schedule.c:92`).

#### `mess_readclock_lc_rtcdev` — ipc.h:1848

Размер: i386 56, x86_64 56. Член объединения: `m_readclock_lc_rtcdev`.

Направление: драйвер RTC (readclock, tps65950) → вызывающий, ответ на `RTCDEV_*` (`mess_lc_readclock_rtcdev`). Вызовы (`m_type`): `RTCDEV_REPLY`. Отправитель: `main()` readclock (`minix/drivers/clock/readclock/readclock.c:117`–`118`), tps65950 (`minix/drivers/power/tps65950/tps65950.c:337`–`338`). Получатель: команда readclock (`minix/commands/readclock/readclock.c:155`), `fwd_msg()` (`minix/drivers/clock/readclock/forward.c:90`), PM (ответ игнорируется).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | Ответ: 0 или код ошибки (`EPERM` для `PWR_OFF` не от PM, `EINVAL` для неизвестного вызова). |  |

#### `mess_rs_pm_exec_restart` — ipc.h:1869

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_rs_pm_exec_restart`.

Направление: RS → PM. Вызовы (`m_type`): `PM_EXEC_RESTART`. Отправитель: `exec_restart()` (`minix/servers/rs/exec.c:122`–`137`). Получатель: `do_execrestart()` (`minix/servers/pm/exec.c:130`, только от RS). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Запрос: процесс, загруженный RS (`exec.c:139`). |  |
| `result` | `int` | 4 / 4 | Запрос: OK или код ошибки загрузки; при ошибке PM убивает процесс (`exec.c:144`, `163`–`172`). |  |
| `pc` | `vir_bytes` | 8 / 8 | Запрос: точка входа нового образа → `sys_exec()` (`exec.c:145`, `197`). | А |
| `ps_str` | `vir_bytes` | 12 / 16 | Запрос: адрес `struct ps_strings` в новом стеке → `sys_exec()` (`exec.c:146`). Указатель стека PM берёт из `mp_frame_addr`. | А |

#### `mess_rs_pm_srv_kill` — ipc.h:1879

Размер: i386 56, x86_64 56. Член объединения: `m_rs_pm_srv_kill`.

Направление: libsys (RS) → PM. Вызовы (`m_type`): `PM_SRV_KILL`. Отправитель: `srv_kill()` (`minix/lib/libsys/srv_kill.c:6`), вызывается RS (`minix/servers/rs/manager.c:470`). Получатель: `do_srv_kill()` (`minix/servers/pm/signal.c:207`, только от RS). Ответ: только код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pid` | `__pid_t` | 0 / 0 | Запрос: pid получателя сигнала (`signal.c:219`). |  |
| `nr` | `int` | 4 / 4 | Запрос: номер сигнала; доставляется как сигнал ядра (`ksig = TRUE`) (`signal.c:219`–`220`). |  |

Замечания: тип назван `rs_pm`, а заполняется в libsys (`srv_kill.c`), в отличие от соседнего `lsys_pm_srv_fork` — непоследовательное имя.

#### `mess_sched_lsys_scheduling_start` — ipc.h:1908

Размер: i386 56, x86_64 56. Член объединения: `m_sched_lsys_scheduling_start`.

Направление: SCHED → libsys, ответ на `SCHEDULING_START`/`SCHEDULING_INHERIT` (`mess_lsys_sched_scheduling_start`; ответ пишется в тот же буфер). Отправитель: `do_start_scheduling()` (`minix/servers/sched/schedule.c:246`), отправка — `reply()` в `minix/servers/sched/main.c:91`–`92`. Получатель: `sched_start()`/`sched_inherit()` (`minix/lib/libsys/sched_start.c:39`, `96`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `scheduler` | `endpoint_t` | 0 / 0 | Ответ: endpoint планировщика, взявшего процесс (сейчас всегда `SCHED_PROC_NR`; задуман для делегирования). |  |


#### Псевдонимы общих раскладок

| Псевдоним | Поле | Тип | Раздел заголовка | Файл:строка | Назначение | 64 |
|---|---|---|---|---|---|---|
| `VFS_PM_ENDPT` | `m7_i1` | `int` | Standard parameters for all requests and replies, except PM_REBOOT | minix/include/minix/com.h:547 | PM ↔ VFS, все `VFS_PM_*` запросы (`tell_vfs()`, `minix/servers/pm/utility.c:123`) и ответы `*_REPLY` (`service_pm()`/`service_pm_postponed()` в `minix/servers/vfs/main.c`, `handle_vfs_reply()` в `minix/servers/pm/main.c:315`): endpoint процесса; `NONE` в последнем `VFS_PM_INIT` (`pm/main.c:234`, `vfs/main.c:422`). (исх.: process endpoint) |  |
| `VFS_PM_SLOT` | `m7_i2` | `int` | Additional parameters for PM_INIT | minix/include/minix/com.h:550 | `VFS_PM_INIT` (PM → VFS, `pm/main.c:223`): номер слота процесса в таблице ядра → индекс `fproc` (`vfs/main.c:424`). (исх.: process slot number) |  |
| `VFS_PM_PID` | `m7_i3` | `int` | Additional parameters for PM_INIT | minix/include/minix/com.h:551 | `VFS_PM_INIT`: pid системного процесса (`pm/main.c:224`, `vfs/main.c:426`). (исх.: process pid) |  |
| `VFS_PM_EID` | `m7_i2` | `int` | Additional parameters for PM_SETUID and PM_SETGID | minix/include/minix/com.h:554 | `VFS_PM_SETUID`/`VFS_PM_SETGID` (PM → VFS, `minix/servers/pm/getset.c:123`, `153`): эффективный uid/gid (`vfs/main.c:790`, `806`). (исх.: effective user/group id) |  |
| `VFS_PM_RID` | `m7_i3` | `int` | Additional parameters for PM_SETUID and PM_SETGID | minix/include/minix/com.h:555 | То же: реальный uid/gid (`getset.c:124`, `vfs/main.c:791`). (исх.: real user/group id) |  |
| `VFS_PM_GROUP_NO` | `m7_i2` | `int` | Additional parameter for PM_SETGROUPS | minix/include/minix/com.h:558 | `VFS_PM_SETGROUPS` (PM → VFS, `getset.c:201`): число дополнительных групп (`vfs/main.c:883`). (исх.: number of groups) |  |
| `VFS_PM_GROUP_ADDR` | `m7_p1` | `char *` | Additional parameter for PM_SETGROUPS | minix/include/minix/com.h:559 | `VFS_PM_SETGROUPS`: адрес массива `mp_sgroups` **в памяти PM** (`getset.c:202`); VFS копирует из PM (`minix/servers/vfs/misc.c:752`). (исх.: struct holding group data) | А |
| `VFS_PM_PATH` | `m7_p1` | `char *` | Additional parameters for PM_EXEC | minix/include/minix/com.h:562 | `VFS_PM_EXEC` (PM → VFS, `minix/servers/pm/exec.c:46`): адрес пути в памяти пользователя (из `m_lc_pm_exec.name`), `vfs/main.c:682`. Также `VFS_PM_DUMPCORE` (`pm/forkexit.c:356`): адрес `mp_name` в памяти PM, VFS копирует (`vfs/misc.c:927`). (исх.: executable) | А |
| `VFS_PM_PATH_LEN` | `m7_i2` | `int` | Additional parameters for PM_EXEC | minix/include/minix/com.h:563 | `VFS_PM_EXEC`: длина пути с нулём (из `size_t namelen`, `exec.c:47`; `vfs/main.c:683`). | У |
| `VFS_PM_FRAME` | `m7_p2` | `char *` | Additional parameters for PM_EXEC | minix/include/minix/com.h:566 | `VFS_PM_EXEC`: адрес начального стека, подготовленного libc у пользователя (`exec.c:48`, `vfs/main.c:684`). (исх.: arguments and environment) | А |
| `VFS_PM_FRAME_LEN` | `m7_i3` | `int` | Additional parameters for PM_EXEC | minix/include/minix/com.h:567 | `VFS_PM_EXEC`: размер кадра в байтах (из `size_t framelen`, `exec.c:49`; `vfs/main.c:685`). (исх.: size of frame) | У |
| `VFS_PM_PS_STR` | `m7_i5` | `int` | Additional parameters for PM_EXEC | minix/include/minix/com.h:568 | `VFS_PM_EXEC`: **адрес** `ps_strings` в новом стеке в `int`-поле (`exec.c:50`, `vfs/main.c:686`). (исх.: ps_strings pointer) | У |
| `VFS_PM_STATUS` | `m7_i2` | `int` | Additional parameters for PM_EXEC_REPLY and PM_CORE_REPLY | minix/include/minix/com.h:571 | `VFS_PM_EXEC_REPLY`/`VFS_PM_CORE_REPLY` (VFS → PM, `vfs/main.c:697`, `736`): OK или код ошибки (`pm/main.c:350`, `357`). (исх.: OK or failure) |  |
| `VFS_PM_PC` | `m7_p1` | `char *` | Additional parameters for PM_EXEC_REPLY and PM_CORE_REPLY | minix/include/minix/com.h:572 | `VFS_PM_EXEC_REPLY`: точка входа нового образа (`vfs/main.c:696`) → `exec_restart()` → `sys_exec()` (`pm/main.c:350`). (исх.: program counter) | А |
| `VFS_PM_NEWSP` | `m7_p2` | `char *` | Additional parameters for PM_EXEC_REPLY and PM_CORE_REPLY | minix/include/minix/com.h:573 | `VFS_PM_EXEC_REPLY`: начальный указатель стека нового образа (`vfs/main.c:698`, `pm/main.c:351`). (исх.: possibly-changed stack ptr) | А |
| `VFS_PM_NEWPS_STR` | `m7_i5` | `int` | Additional parameters for PM_EXEC_REPLY and PM_CORE_REPLY | minix/include/minix/com.h:574 | `VFS_PM_EXEC_REPLY`: **адрес** `ps_strings` в `int`-поле (`vfs/main.c:699`, `pm/main.c:352`). (исх.: possibly-changed ps_strings ptr) | У |
| `VFS_PM_PENDPT` | `m7_i2` | `int` | Additional parameters for PM_FORK and PM_SRV_FORK | minix/include/minix/com.h:577 | `VFS_PM_FORK`/`VFS_PM_SRV_FORK` (PM → VFS, `pm/forkexit.c:125`, `225`): endpoint родителя (`vfs/main.c:858`). (исх.: parent process endpoint) |  |
| `VFS_PM_CPID` | `m7_i3` | `int` | Additional parameters for PM_FORK and PM_SRV_FORK | minix/include/minix/com.h:578 | То же: pid потомка (`forkexit.c:126`, `vfs/main.c:860`). (исх.: child pid) |  |
| `VFS_PM_REUID` | `m7_i4` | `int` | Additional parameters for PM_FORK and PM_SRV_FORK | minix/include/minix/com.h:579 | Только `VFS_PM_SRV_FORK`: реальный=эффективный uid потомка (`forkexit.c:227`, `vfs/main.c:861`, `869`); в `VFS_PM_FORK` = -1, не используется. (исх.: real and effective uid) |  |
| `VFS_PM_REGID` | `m7_i5` | `int` | Additional parameters for PM_FORK and PM_SRV_FORK | minix/include/minix/com.h:580 | Только `VFS_PM_SRV_FORK`: gid потомка (`forkexit.c:228`, `vfs/main.c:862`, `870`). (исх.: real and effective gid) |  |
| `VFS_PM_TERM_SIG` | `m7_i2` | `int` | Additional parameters for PM_DUMPCORE | minix/include/minix/com.h:583 | `VFS_PM_DUMPCORE` (PM → VFS, `forkexit.c:355`): сигнал завершения (`mp_sigstatus`), 0 → panic (`vfs/main.c:718`–`728`). (исх.: process's termination signal) |  |
| `VME_ENDPOINT` | `m1_i1` | `int` | Calls from PM | minix/include/minix/com.h:631 | `VM_EXIT` (PM через `vm_exit()`, `minix/lib/libsys/vm_exit.c:15`; `pm/forkexit.c:455`): завершаемый процесс → `do_exit()` VM (`minix/servers/vm/exit.c:67`). |  |
| `VMF_ENDPOINT` | `m1_i1` | `int` | Calls from PM | minix/include/minix/com.h:633 | `VM_FORK` (PM через `vm_fork()`, `minix/lib/libsys/vm_fork.c:16`; `pm/forkexit.c:78`, `183`): endpoint родителя → `do_fork()` VM (`minix/servers/vm/fork.c:41`). |  |
| `VMF_SLOTNO` | `m1_i2` | `int` | Calls from PM | minix/include/minix/com.h:634 | `VM_FORK`: номер слота процесса-потомка (`vm_fork.c:17`, `fork.c:47`). |  |
| `VMF_CHILD_ENDPOINT` | `m1_i3` | `int` | Calls from PM | minix/include/minix/com.h:635 | `VM_FORK`, ответ: endpoint созданного потомка (`fork.c:111`, `vm_fork.c:21`). (исх.: result) |  |
| `VMEN_ENDPOINT` | `m1_i1` | `int` | Calls from PM | minix/include/minix/com.h:638 | **Не используется**: `VM_EXEC_NEWMEM` не имеет ни отправителя, ни обработчика в VM (`minix/servers/vm/main.c` его не регистрирует); остался только в таблице `minix-service` (`minix/commands/minix-service/parse.c:733`). |  |
| `VMEN_ARGSPTR` | `m1_p1` | `char *` | Calls from PM | minix/include/minix/com.h:639 | **Не используется** (см. `VMEN_ENDPOINT`); по смыслу — адрес аргументов exec. | А |
| `VMEN_ARGSSIZE` | `m1_i2` | `int` | Calls from PM | minix/include/minix/com.h:640 | **Не используется**; по смыслу — размер аргументов в `int`. | У |
| `VMEN_FLAGS` | `m1_i3` | `int` | Calls from PM | minix/include/minix/com.h:641 | **Не используется**. (исх.: result) |  |
| `VMEN_STACK_TOP` | `m1_p2` | `char *` | Calls from PM | minix/include/minix/com.h:642 | **Не используется**; по смыслу — вершина стека. (исх.: result) | А |
| `VMWE_ENDPOINT` | `m1_i1` | `int` | Calls from PM | minix/include/minix/com.h:644 | `VM_WILLEXIT` (PM через `vm_willexit()`, `minix/lib/libsys/vm_exit.c:31`; `pm/forkexit.c:332`): процесс, который скоро завершится → `do_willexit()` VM (`minix/servers/vm/exit.c:105`). |  |

Замечания: протокол PM ↔ VFS (`mess_7`) переносит адреса пользователя/PM через `char *`, а адрес `ps_strings` и длины — через `int` (`VFS_PM_PS_STR`, `VFS_PM_NEWPS_STR`, `VFS_PM_PATH_LEN`, `VFS_PM_FRAME_LEN`) — при переходе на 64 бита нужен типизированный `mess_pm_vfs_*`. `VMEN_*` и `VM_EXEC_NEWMEM` — мёртвый код.

## 7. Системные вызовы → VFS; VM ↔ VFS, службы ↔ VFS

#### `mess_lc_vfs_chown` — ipc.h:611

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_chown`.

Направление: libc → VFS (системный вызов, `_syscall`). Вызовы (`m_type`): `VFS_CHOWN`, `VFS_FCHOWN`. Отправитель: `chown()` (`minix/lib/libc/sys/chown.c`), `fchown()` (`minix/lib/libc/sys/fchown.c`). Получатель: `do_chown()` (`minix/servers/vfs/protect.c:98`, оба вызова в `table.c`). Ответ: только код в `m_type`, поля не заполняются.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name` | `vir_bytes` | 0 / 0 | Только `VFS_CHOWN`: адрес пути в пространстве вызывающего; VFS копирует его `fetch_name()` (`protect.c:119,127`). Для `VFS_FCHOWN` = 0 | А |
| `len` | `size_t` | 4 / 8 | Только `VFS_CHOWN`: длина пути вместе с NUL (`strlen+1`), `protect.c:120` | Р |
| `fd` | `int` | 8 / 16 | Только `VFS_FCHOWN`: дескриптор файла, `get_filp()` (`protect.c:131`) | В |
| `owner` | `__uid_t` | 12 / 20 | Новый uid (`(uid_t)-1` — не менять), `protect.c:115` | В |
| `group` | `__gid_t` | 16 / 24 | Новый gid (`(gid_t)-1` — не менять), `protect.c:116` | В |

Замечания: одна раскладка на два вызова, `name/len` и `fd` взаимоисключающи — кандидат на объединение (union) при переработке.

#### `mess_lc_vfs_close` — ipc.h:622

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_close`.

Направление: libc/libsys → VFS. Вызовы (`m_type`): `VFS_CLOSE`. Отправитель: `close()` (`minix/lib/libc/sys/close.c`, `nblock=0`, `_syscall`), `closenb()` (`minix/lib/libsys/closenb.c`, `nblock=1`, `_taskcall`; используется UDS `minix/net/uds/io.c`). Получатель: `do_close()` (`minix/servers/vfs/open.c:674`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Закрываемый дескриптор, `open.c:679` → `close_fd()` |  |
| `nblock` | `int` | 4 / 4 | 0 — обычный `close` (может приостановиться, напр. `SO_LINGER`), 1 — неблокирующий `closenb`; передаётся как `!nblock` в `may_suspend` (`open.c:680,682`) |  |

#### `mess_lc_vfs_creat` — ipc.h:630

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_creat`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_CREAT` (выбирается в `open()`, если в флагах `O_CREAT`). Отправитель: `open()` (`minix/lib/libc/sys/open.c:20-25`). Получатель: `do_creat()` (`minix/servers/vfs/open.c:58`). Ответ: в `m_type` номер нового дескриптора или код ошибки.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name` | `vir_bytes` | 0 / 0 | Адрес пути у вызывающего; `fetch_name()` (`open.c:66,74`) | А |
| `len` | `size_t` | 4 / 8 | Длина пути с NUL, `open.c:67` | Р |
| `flags` | `int` | 8 / 16 | Флаги `open(2)` (`O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_RDWR`, `O_CLOEXEC`, …), `open.c:68` | В |
| `mode` | `__mode_t` | 12 / 20 | Права создаваемого файла (до применения umask), `open.c:69` | В |

#### `mess_lc_vfs_fchdir` — ipc.h:640

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_fchdir`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_FCHDIR`. Отправитель: `fchdir()` (`minix/lib/libc/sys/chdir.c:24`). Получатель: `do_fchdir()` (`minix/servers/vfs/stadir.c:32`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор открытого каталога, `stadir.c:38` |  |

#### `mess_lc_vfs_fchmod` — ipc.h:647

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_fchmod`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_FCHMOD`. Отправитель: `fchmod()` (`minix/lib/libc/sys/fchmod.c`). Получатель: `do_chmod()` (`minix/servers/vfs/protect.c:25`, ветка `else`; `VFS_CHMOD` использует `mess_lc_vfs_path`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор файла, `protect.c:53` |  |
| `mode` | `__mode_t` | 4 / 4 | Новые биты прав (`S_I*`, setuid/setgid), `protect.c:54` |  |

#### `mess_lc_vfs_fcntl` — ipc.h:655

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_fcntl`.

Направление: libc → VFS; также VFS сам себе (восстановление `F_SETLKW` в `unblock()`, `minix/servers/vfs/main.c:948-951`). Вызовы (`m_type`): `VFS_FCNTL`. Отправитель: `fcntl()` (`minix/lib/libc/sys/fcntl.c`; также `ioctl_to_fcntl()` в `ioctl.c`). Получатель: `do_fcntl()` (`minix/servers/vfs/misc.c:117`). Ответ: в `m_type` результат (новый fd, флаги и т.п.) или код ошибки.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор, `misc.c:125` |  |
| `cmd` | `int` | 4 / 4 | Команда `F_*` (`F_DUPFD`, `F_GETFD/SETFD`, `F_GETFL/SETFL`, `F_GETLK/SETLK/SETLKW`, `F_FREESP`, `F_GETNOSIGPIPE/SETNOSIGPIPE`), `misc.c:126` |  |
| `arg_int` | `int` | 8 / 8 | Целый аргумент для `F_DUPFD*`, `F_SETFD`, `F_SETFL`, `F_SETNOSIGPIPE` (`misc.c:127`) |  |
| `arg_ptr` | `vir_bytes` | 12 / 16 | Адрес `struct flock` у вызывающего для `F_GETLK/SETLK/SETLKW` (`lock_op()`, `lock.c:37`) и `F_FREESP` (`misc.c:193`) (исх.: struct flock *) | А |

Замечания: `struct flock` копируется как есть — VFS и пользователь должны иметь одну ABI (`off_t` в ней 64 бита, но выравнивание/размер структуры зависят от ABI).

#### `mess_lc_vfs_fstat` — ipc.h:665

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_fstat`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_FSTAT`. Отправитель: `fstat()` (`minix/lib/libc/sys/stat.c:35`). Получатель: `do_fstat()` (`minix/servers/vfs/stadir.c:173`). Ответ: код в `m_type`; данные — копированием в `buf`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор, `stadir.c:181` |  |
| `buf` | `vir_bytes` | 4 / 8 | Адрес `struct stat` у вызывающего; заполняет ФС-сервер (`req_stat`), `stadir.c:180` (исх.: struct stat *) | А |

#### `mess_lc_vfs_fsync` — ipc.h:673

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_fsync`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_FSYNC`. Отправитель: `fsync()` (`minix/lib/libc/sys/fsync.c`). Получатель: `do_fsync()` (`minix/servers/vfs/misc.c:297`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор; по нему находится устройство и синхронизируется вся ФС (`req_sync`), `misc.c:305` |  |

#### `mess_lc_vfs_gcov` — ipc.h:680

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_gcov`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_GCOV_FLUSH`. Отправитель: `gcov_flush_svr()` (`minix/lib/libc/sys/gcov_flush_sys.c`, утилита `gcov-pull`). Получатель: `do_gcov_flush()` (`minix/servers/vfs/gcov.c:10`), которая шлёт целевому сервису `COMMON_REQ_GCOV_DATA` (`mess_vfs_lsys_gcov`). Ответ: код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `labellen` | `size_t` | 0 / 0 | Длина метки сервиса с NUL, `< LABEL_MAX` (`gcov.c:34,39`) | Р |
| `buflen` | `size_t` | 4 / 8 | Размер буфера для данных gcov, байт; размер magic-гранта (`gcov.c:36,54`) | Р |
| `label` | `vir_bytes` | 8 / 16 | Адрес метки целевого сервиса у вызывающего, копируется `sys_datacopy` (`gcov.c:33,41`) | А |
| `buf` | `vir_bytes` | 12 / 24 | Адрес буфера у вызывающего; VFS делает на него `cpf_grant_magic(... CPF_WRITE)` для целевого сервиса (`gcov.c:35,54`) | А |

#### `mess_lc_vfs_getvfsstat` — ipc.h:690

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_getvfsstat`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_GETVFSSTAT`. Отправитель: `getvfsstat()` (`minix/lib/libc/sys/getvfsstat.c`). Получатель: `do_getvfsstat()` (`minix/servers/vfs/stadir.c:351`). Ответ: в `m_type` число смонтированных ФС (записанных или всего) или код ошибки.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `flags` | `__int32_t` | 0 / 0 | `ST_WAIT`/`ST_NOWAIT`; при `ST_NOWAIT` ФС не опрашиваются и не блокируются (`stadir.c:361,371`) |  |
| `len` | `size_t` | 4 / 8 | Размер буфера, байт (кратен `sizeof(struct statvfs)`), `stadir.c:360` | Р |
| `buf` | `vir_bytes` | 8 / 16 | Адрес массива `struct statvfs`; 0 — только посчитать ФС (`stadir.c:359,365`) (исх.: struct statvfs) | А |

#### `mess_lc_vfs_ioctl` — ipc.h:699

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_ioctl`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_IOCTL`. Отправитель: `ioctl()` (`minix/lib/libc/sys/ioctl.c:361`; часть запросов предварительно переписывается в плоские структуры MINIX). Получатель: `do_ioctl()` (`minix/servers/vfs/device.c`), далее `bdev_ioctl()` / `cdev_io(CDEV_IOCTL)` / `sdev_ioctl()`. Ответ: код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор устройства/сокета, `device.c:26` |  |
| `req` | `unsigned long` | 4 / 8 | Код запроса `_IO*` (направление и размер аргумента закодированы в 32 младших битах); VFS по нему строит грант (`make_ioctl_grant`), `device.c:27` | С |
| `arg` | `vir_bytes` | 8 / 16 | Адрес аргумента у вызывающего (или адрес временной переписанной структуры libc), `device.c:28` | А |

Замечания: `req` по сути 32-битный код, `unsigned long` лишь повторяет прототип NetBSD — при переработке сделать `u32_t`.

#### `mess_lc_vfs_link` — ipc.h:708

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_link`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_LINK`, `VFS_RENAME`, `VFS_SYMLINK`. Отправитель: `link()`, `rename()`, `symlink()` (`minix/lib/libc/sys/{link,rename,symlink}.c`). Получатель: `do_link()` (`minix/servers/vfs/link.c:29`), `do_rename()` (`link.c:169`), `do_slink()` (`link.c:387`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name1` | `vir_bytes` | 0 / 0 | Адрес первого пути: существующий файл (link), старое имя (rename), содержимое ссылки (symlink); `link.c:40,181,402` | А |
| `name2` | `vir_bytes` | 4 / 8 | Адрес второго пути: новое имя / путь создаваемой ссылки; `link.c:42,183,404` | А |
| `len1` | `size_t` | 8 / 16 | Длина `name1` с NUL | Р |
| `len2` | `size_t` | 12 / 24 | Длина `name2` с NUL | Р |

#### `mess_lc_vfs_listen` — ipc.h:718

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_listen`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_LISTEN`. Отправитель: `listen()` (`minix/lib/libc/sys/listen.c`). Получатель: `do_listen()` (`minix/servers/vfs/socket.c:344`) → `sdev_listen()`. Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор сокета, `socket.c:349` |  |
| `backlog` | `int` | 4 / 4 | Длина очереди входящих соединений, передаётся драйверу сокетов, `socket.c:350` |  |

#### `mess_lc_vfs_lseek` — ipc.h:726

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_lseek`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_LSEEK`. Отправитель: `lseek()` (`minix/lib/libc/sys/lseek.c`). Получатель: `do_lseek()` (`minix/servers/vfs/open.c:655`). Ответ: тип `mess_vfs_lc_lseek` (новая позиция).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `offset` | `__off_t` | 0 / 0 | Смещение в файле, байт, 64 бита со знаком; `actual_lseek()` (`open.c:662`) |  |
| `fd` | `int` | 8 / 8 | Дескриптор |  |
| `whence` | `int` | 12 / 12 | `SEEK_SET`/`SEEK_CUR`/`SEEK_END` |  |

#### `mess_lc_vfs_mknod` — ipc.h:736

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_mknod`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_MKNOD`. Отправитель: `mknod()` (`minix/lib/libc/sys/mknod.c`). Получатель: `do_mknod()` (`minix/servers/vfs/open.c:514`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Номер устройства (`makedev`) для `S_IFCHR/S_IFBLK`, 64 бита; `open.c:530` → `req_mknod` |  |
| `name` | `vir_bytes` | 8 / 8 | Адрес пути, `fetch_name()` (`open.c:527,544`) | А |
| `len` | `size_t` | 12 / 16 | Длина пути с NUL | Р |
| `mode` | `__mode_t` | 16 / 24 | Тип (`S_IFIFO`, `S_IFCHR`, …) и права; не-root может только FIFO (`open.c:529,537`) | В |

#### `mess_lc_vfs_mount` — ipc.h:747

Размер: i386 56, x86_64 96 — **не помещается**. Член объединения: `m_lc_vfs_mount`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_MOUNT`. Отправитель: `minix_mount()` (`minix/lib/libc/sys/mount.c:148-157`; перед этим запускает ФС-сервер через `service up`). Получатель: `do_mount()` (`minix/servers/vfs/mount.c:85`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `flags` | `int` | 0 / 0 | Флаги монтирования (`MNT_RDONLY` и т.п.), передаются в `mount_fs()` (`mount.c:97,148`) |  |
| `devlen` | `size_t` | 4 / 8 | Длина пути устройства с NUL; 0 — ФС без устройства (`nodev`, `mount.c:126`) | Р |
| `pathlen` | `size_t` | 8 / 16 | Длина пути точки монтирования с NUL | Р |
| `typelen` | `size_t` | 12 / 24 | Длина имени типа ФС с NUL (`FSTYPE_MAX`) | Р |
| `labellen` | `size_t` | 16 / 32 | Длина метки ФС-сервера; проверяется только `<= LABEL_MAX` (`mount.c:111`) | Р |
| `dev` | `vir_bytes` | 20 / 40 | Адрес пути блочного устройства (`fetch_name`, `mount.c:129`) | А |
| `path` | `vir_bytes` | 24 / 48 | Адрес пути точки монтирования (`mount.c:141`) | А |
| `type` | `vir_bytes` | 28 / 56 | Адрес строки типа ФС (`mount.c:145`) | А |
| `label` | `vir_bytes` | 32 / 64 | Адрес метки ФС-сервера; по ней VFS ищет endpoint в DS (`mount.c:113-119`) | А |

Замечания: метка копируется длиной `sizeof(mount_label)`, а не `labellen` (`mount.c:113`) — читается за концом строки вызывающего. Самый «раздутый» тип группы (96 байт на x86_64).

#### `mess_lc_vfs_path` — ipc.h:762

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_path`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_OPEN` (без `O_CREAT`), `VFS_ACCESS`, `VFS_CHDIR`, `VFS_CHROOT`, `VFS_CHMOD`, `VFS_MKDIR`, `VFS_UNLINK`, `VFS_RMDIR`. Отправитель: `_loadname()` (`minix/lib/libc/sys/loadname.c`) + `open()`, `access()`, `chdir()`, `chroot()`, `chmod()`, `mkdir()`, `unlink()`, `rmdir()` (`minix/lib/libc/sys/*.c`). Получатель: `copy_path()` (`minix/servers/vfs/utility.c:24`) из `do_open` (`open.c:38`), `do_access` (`protect.c:198`), `do_chdir`/`do_chroot` (`stadir.c:50,83`), `do_chmod` (`protect.c:25`), `do_mkdir` (`open.c:564`), `do_unlink` (`link.c:91`). Ответ: код/fd в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name` | `vir_bytes` | 0 / 0 | Адрес пути у вызывающего; используется, только если `len > M_PATH_STRING_MAX` (`utility.c:34,44`) | А |
| `len` | `size_t` | 4 / 8 | Длина пути с NUL; `> PATH_MAX` → `ENAMETOOLONG` (`utility.c:35,37`) | Р |
| `flags` | `int` | 8 / 16 | Только `VFS_OPEN`: флаги `O_*` (`open.c:44`) | В |
| `mode` | `__mode_t` | 12 / 20 | `VFS_CHMOD`: новые права (`protect.c:47`); `VFS_MKDIR`: права каталога (`open.c:577`); `VFS_ACCESS`: маска `R_OK/W_OK/X_OK` (`protect.c:210`, не по названию) | В |
| `buf` | `char[40]` | 16 / 24 | Встроенная копия пути, если `len <= 40` (`loadname.c:18`, `utility.c:47`) — экономия копирования | В |

Замечания: на x86_64 `buf` заканчивается на 64-м байте полезной нагрузки (56 доступно) — встроенный путь не помещается; при переработке либо сократить `M_PATH_STRING_MAX` до 32, либо сделать `name/len` фиксированной ширины.

#### `mess_lc_vfs_pipe2` — ipc.h:771

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_pipe2`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_PIPE2`. Отправитель: `pipe2()` (`minix/lib/libc/sys/pipe.c`). Получатель: `do_pipe2()` (`minix/servers/vfs/pipe.c:39`). Ответ: тип `mess_vfs_lc_fdpair`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `flags` | `int` | 0 / 0 | Флаги `O_CLOEXEC`, `O_NONBLOCK`, `O_NOSIGPIPE`, `pipe.c:45` |  |
| `_unused` | `int` | 4 / 4 | Не используется (остаток старой раскладки m1) |  |
| `oflags` | `int` | 8 / 8 | Те же флаги для совместимости со старой раскладкой; OR-ится с `flags` (`pipe.c:46`) |  |

Замечания: `_unused`/`oflags` — переходный мусор, по комментарию в ipc.h подлежат удалению.

#### `mess_lc_vfs_readlink` — ipc.h:785

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_readlink`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_READLINK`. Отправитель: `readlink()` (`minix/lib/libc/sys/readlink.c`). Получатель: `do_rdlink()` (`minix/servers/vfs/link.c:471`). Ответ: в `m_type` число записанных байт или код ошибки.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name` | `vir_bytes` | 0 / 0 | Адрес пути к символической ссылке (`fetch_name`, `link.c:483`) (исх.: const char *) | А |
| `namelen` | `size_t` | 4 / 8 | Длина пути с NUL, `link.c:484` | Р |
| `buf` | `vir_bytes` | 8 / 16 | Адрес буфера для содержимого ссылки, заполняет ФС-сервер (`req_rdlink`), `link.c:485` | А |
| `bufsize` | `size_t` | 12 / 24 | Размер буфера, `<= SSIZE_MAX` (`link.c:486-487`) | Р |

#### `mess_lc_vfs_readwrite` — ipc.h:795

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_readwrite`.

Направление: libc → VFS; также VFS сам себе (восстановление чтения/записи в канал в `unblock()`, `minix/servers/vfs/main.c:940-944` → `do_pending_pipe()` `main.c:231-234`). Вызовы (`m_type`): `VFS_READ`, `VFS_WRITE`, `VFS_GETDENTS`. Отправитель: `read()`, `write()`, `getdents()` (`minix/lib/libc/sys/{read,write,getdents}.c`). Получатель: `do_read()` (`minix/servers/vfs/read.c:30`), `do_write()` (`write.c:15`), `do_getdents()` (`read.c:282`). Ответ: в `m_type` число байт или код ошибки.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор |  |
| `buf` | `vir_bytes` | 4 / 8 | Адрес буфера у вызывающего; на него создаются гранты для ФС/драйвера (`read.c:41`) | А |
| `len` | `size_t` | 8 / 16 | Число байт для чтения/записи (`getdents` — размер буфера) | Р |
| `cum_io` | `size_t` | 12 / 24 | Снаружи обязан быть 0, иначе `EINVAL` (`read.c:38,292`, `write.c:20`); внутри VFS — уже переданное число байт при возобновлении операции с каналом (`main.c:234`) (исх.: reserved/internal, set to 0) | Р |

Замечания: внутреннее использование поля сообщения для состояния возобновления — при переработке вынести состояние из сообщения.

#### `mess_lc_vfs_select` — ipc.h:805

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lc_vfs_select`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_SELECT`. Отправитель: `select()` (`minix/lib/libc/sys/select.c`). Получатель: `do_select()` (`minix/servers/vfs/select.c:96`). Ответ: в `m_type` число готовых дескрипторов или код ошибки; наборы копируются обратно по адресам.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `nfds` | `__uint32_t` | 0 / 0 | Наибольший fd + 1, `<= OPEN_MAX` (`select.c:114,118`) |  |
| `readfds` | `fd_set *` | 4 / 8 | Адрес `fd_set` чтения у вызывающего (может быть NULL), `select.c:130` | А |
| `writefds` | `fd_set *` | 8 / 16 | Адрес `fd_set` записи, `select.c:131` | А |
| `errorfds` | `fd_set *` | 12 / 24 | Адрес `fd_set` исключений, `select.c:132` | А |
| `timeout` | `vir_bytes` | 16 / 32 | Адрес `struct timeval` (0 — ждать бесконечно), копируется `select.c:141` (исх.: user-provided 'struct timeval *') | А |

Замечания: три поля — указатели с типом пространства вызывающего (`fd_set *`) прямо в ipc.h; `struct timeval` (`time_t` 64 + `suseconds_t`) зависит от ABI.

#### `mess_lc_vfs_sendrecv` — ipc.h:816

Размер: i386 56, x86_64 80 — **не помещается**. Член объединения: `m_lc_vfs_sendrecv`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_SENDTO`, `VFS_RECVFROM`. Отправитель: `sendto()`, `recvfrom()` (`minix/lib/libc/sys/{sendto,recvfrom}.c`). Получатель: `do_sendto()` (`minix/servers/vfs/socket.c:483`), `do_recvfrom()` (`socket.c:504`) → `sdev_readwrite()`. Ответ: для `sendto` — код/число байт в `m_type`; для `recvfrom` — число байт в `m_type` + `mess_vfs_lc_socklen` (`resume_recvfrom`, `socket.c:532`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор сокета |  |
| `buf` | `vir_bytes` | 4 / 8 | Адрес данных (исх.: void *), `socket.c:493,514` | А |
| `len` | `size_t` | 8 / 16 | Длина данных, байт | Р |
| `flags` | `int` | 12 / 24 | Флаги `MSG_*` (`MSG_PEEK`, `MSG_DONTWAIT`, …), передаются драйверу | В |
| `addr` | `vir_bytes` | 16 / 32 | Адрес `struct sockaddr` (получатель для sendto / буфер адреса для recvfrom), может быть 0 (исх.: struct sockaddr *) | А |
| `addr_len` | `unsigned int` | 20 / 40 | Длина адреса / размер буфера адреса (исх.: socklen_t) | В |

#### `mess_lc_vfs_shutdown` — ipc.h:828

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_shutdown`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_SHUTDOWN`. Отправитель: `shutdown()` (`minix/lib/libc/sys/shutdown.c`). Получатель: `do_shutdown()` (`minix/servers/vfs/socket.c:747`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор сокета, `socket.c:752` |  |
| `how` | `int` | 4 / 4 | `SHUT_RD`/`SHUT_WR`/`SHUT_RDWR`, `socket.c:753` |  |

#### `mess_lc_vfs_sockaddr` — ipc.h:836

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_sockaddr`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_BIND`, `VFS_CONNECT`, `VFS_ACCEPT`, `VFS_GETSOCKNAME`, `VFS_GETPEERNAME`. Отправитель: `bind()`, `connect()`, `accept()`, `getsockname()`, `getpeername()` (`minix/lib/libc/sys/*.c`). Получатель: `do_bind`/`do_connect`/`do_accept`/`do_getsockname`/`do_getpeername` (`minix/servers/vfs/socket.c:313,331,370,707,730`). Ответ: для bind/connect — код; для accept (новый fd в `m_type`), getsockname, getpeername — `mess_vfs_lc_socklen`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор сокета |  |
| `addr` | `vir_bytes` | 4 / 8 | Адрес `struct sockaddr` у вызывающего: вход для bind/connect, выход для accept/getsockname/getpeername (accept — может быть 0) (исх.: struct sockaddr *) | А |
| `addr_len` | `unsigned int` | 8 / 16 | Длина адреса (bind/connect) или размер буфера адреса (остальные) (исх.: socklen_t) | В |

#### `mess_lc_vfs_socket` — ipc.h:845

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_socket`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_SOCKET`, `VFS_SOCKETPAIR`. Отправитель: `socket()`, `socketpair()` (`minix/lib/libc/sys/{socket,socketpair}.c`). Получатель: `do_socket()` (`minix/servers/vfs/socket.c:182`), `do_socketpair()` (`socket.c:230`). Ответ: socket — fd в `m_type`; socketpair — `mess_vfs_lc_fdpair`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `domain` | `int` | 0 / 0 | Семейство `PF_*`; по нему VFS выбирает драйвер сокетов (`smap`) |  |
| `type` | `int` | 4 / 4 | `SOCK_*` с флагами `SOCK_CLOEXEC/SOCK_NONBLOCK/SOCK_NOSIGPIPE` |  |
| `protocol` | `int` | 8 / 8 | Номер протокола, передаётся драйверу |  |

#### `mess_lc_vfs_sockmsg` — ipc.h:854

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_sockmsg`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_SENDMSG`, `VFS_RECVMSG`. Отправитель: `sendmsg()`, `recvmsg()` (`minix/lib/libc/sys/{sendmsg,recvmsg}.c`; libc сводит iovec к одному элементу). Получатель: `do_sockmsg()` (`minix/servers/vfs/socket.c:543`). Ответ: число байт в `m_type`; для recvmsg VFS обновляет `msghdr` у пользователя (`resume_recvmsg`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор сокета, `socket.c:554` |  |
| `msgbuf` | `vir_bytes` | 4 / 8 | Адрес `struct msghdr` у вызывающего; VFS копирует её и первый `iovec` (`socket.c:555,561,577`) (исх.: struct msghdr *) | А |
| `flags` | `int` | 8 / 16 | Флаги `MSG_*`, `socket.c:592` | В |

Замечания: VFS разбирает пользовательские `msghdr`/`iovec` (указатели и `size_t`) своей ABI — смешанные 32/64-битные процессы потребуют compat-слоя.

#### `mess_lc_vfs_sockopt` — ipc.h:863

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lc_vfs_sockopt`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_SETSOCKOPT`, `VFS_GETSOCKOPT`. Отправитель: `setsockopt()`, `getsockopt()` (`minix/lib/libc/sys/{setsockopt,getsockopt}.c`). Получатель: `do_setsockopt()` (`minix/servers/vfs/socket.c:657`), `do_getsockopt()` (`socket.c:676`). Ответ: setsockopt — код; getsockopt — `mess_vfs_lc_socklen` (`socket.c:693`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Дескриптор сокета |  |
| `level` | `int` | 4 / 4 | Уровень `SOL_SOCKET`/`IPPROTO_*` |  |
| `name` | `int` | 8 / 8 | Имя опции `SO_*`/`TCP_*`… |  |
| `buf` | `vir_bytes` | 12 / 16 | Адрес значения опции у вызывающего (исх.: void *) | А |
| `len` | `unsigned int` | 16 / 24 | Длина значения (set) / размер буфера (get) (исх.: socklen_t) | В |

#### `mess_lc_vfs_stat` — ipc.h:874

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_stat`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_STAT`, `VFS_LSTAT`. Отправитель: `stat()`, `lstat()` (`minix/lib/libc/sys/stat.c:23,46`). Получатель: `do_stat()` (`minix/servers/vfs/stadir.c:140`), `do_lstat()` (`stadir.c:418`). Ответ: код в `m_type`; данные — в `buf`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `len` | `size_t` | 0 / 0 | Длина пути с NUL, `stadir.c:152` | Р |
| `name` | `vir_bytes` | 4 / 8 | Адрес пути (`fetch_name`, `stadir.c:151,159`) (исх.: const char *) | А |
| `buf` | `vir_bytes` | 8 / 16 | Адрес `struct stat`, заполняет ФС-сервер (`req_stat`) (исх.: struct stat *) | А |

#### `mess_lc_vfs_statvfs1` — ipc.h:883

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_statvfs1`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_STATVFS1`, `VFS_FSTATVFS1`. Отправитель: `statvfs1()` (`minix/lib/libc/sys/statvfs.c`), `fstatvfs1()` (`fstatvfs.c`). Получатель: `do_statvfs()` (`minix/servers/vfs/stadir.c:294`), `do_fstatvfs()` (`stadir.c:328`) → `fill_statvfs()`. Ответ: код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd` | `int` | 0 / 0 | Только `VFS_FSTATVFS1`: дескриптор, `stadir.c:335` |  |
| `flags` | `int` | 4 / 4 | `ST_WAIT`/`ST_NOWAIT` для `fill_statvfs`, `stadir.c:308,337` |  |
| `len` | `size_t` | 8 / 8 | Только `VFS_STATVFS1`: длина пути с NUL | Р |
| `name` | `vir_bytes` | 12 / 16 | Только `VFS_STATVFS1`: адрес пути (`stadir.c:305`) | А |
| `buf` | `vir_bytes` | 16 / 24 | Адрес `struct statvfs` у вызывающего (`stadir.c:307,336`) | А |

#### `mess_lc_vfs_truncate` — ipc.h:894

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_truncate`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_TRUNCATE`, `VFS_FTRUNCATE`. Отправитель: `truncate()`, `ftruncate()` (`minix/lib/libc/sys/{truncate,ftruncate}.c`). Получатель: `do_truncate()` (`minix/servers/vfs/link.c:276`), `do_ftruncate()` (`link.c:327`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `offset` | `__off_t` | 0 / 0 | Новая длина файла, байт (64 бита, `< 0` → `EINVAL`), `link.c:299,337` |  |
| `fd` | `int` | 8 / 8 | Только `VFS_FTRUNCATE`: дескриптор, `link.c:335` |  |
| `name` | `vir_bytes` | 12 / 16 | Только `VFS_TRUNCATE`: адрес пути, `link.c:292` | А |
| `len` | `size_t` | 16 / 24 | Только `VFS_TRUNCATE`: длина пути с NUL, `link.c:293` | Р |

#### `mess_lc_vfs_umask` — ipc.h:905

Размер: i386 56, x86_64 56. Член объединения: `m_lc_vfs_umask`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_UMASK`. Отправитель: `umask()` (`minix/lib/libc/sys/umask.c`). Получатель: `do_umask()` (`minix/servers/vfs/protect.c:182`). Ответ: в `m_type` старая маска (VFS хранит дополнение и возвращает `~fp_umask`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `mask` | `__mode_t` | 0 / 0 | Новая маска создания файлов (`& RWX_MODES`), `protect.c:187-190` |  |

#### `mess_lc_vfs_umount` — ipc.h:912

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lc_vfs_umount`.

Направление: libc → VFS. Вызовы (`m_type`): `VFS_UMOUNT`. Отправитель: `minix_umount()` (`minix/lib/libc/sys/mount.c:176-180`). Получатель: `do_umount()` (`minix/servers/vfs/mount.c:430`). Ответ: код в `m_type`; метка ФС-сервера записывается в `label` (libc потом останавливает сервер через `service down`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name` | `vir_bytes` | 0 / 0 | Адрес пути устройства или точки монтирования (`mount.c:441,450`) | А |
| `namelen` | `size_t` | 4 / 8 | Длина пути с NUL | Р |
| `label` | `vir_bytes` | 8 / 16 | Выход: адрес буфера для метки ФС-сервера, VFS пишет `sys_datacopy` (`mount.c:443,462`) | А |
| `labellen` | `size_t` | 12 / 24 | Размер буфера метки (`MNT_LABEL_LEN`), метка обрезается до него (`mount.c:461`) | Р |

#### `mess_lsys_vfs_copyfd` — ipc.h:1456

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_vfs_copyfd`.

Направление: libsys (сервис/драйвер) → VFS, обратный вызов во время обработки запроса пользователя. Вызовы (`m_type`): `VFS_COPYFD`. Отправитель: `copyfd()` (`minix/lib/libsys/copyfd.c`, `_taskcall`); вызывают UDS (`minix/net/uds/io.c:871,1372,1397`) и VND (`minix/drivers/storage/vnd/vnd.c:387`). Получатель: `do_copyfd()` (`minix/servers/vfs/filedes.c:524`, только `super_user`). Ответ: в `m_type` номер нового fd (`COPYFD_FROM/TO`) или код ошибки.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Endpoint пользовательского процесса, с fd-таблицей которого идёт работа (`filedes.c:541,548`) |  |
| `fd` | `int` | 4 / 4 | Дескриптор: в процессе `endpt` (FROM/CLOSE) или в вызывающем (TO) |  |
| `what` | `int` | 8 / 8 | `COPYFD_FROM`/`COPYFD_TO`/`COPYFD_CLOSE` плюс флаг `COPYFD_CLOEXEC` (маска `COPYFD_FLAGS` 0xF000), `filedes.c:543-546` |  |

#### `mess_lsys_vfs_mapdriver` — ipc.h:1465

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_vfs_mapdriver`.

Направление: libsys (RS) → VFS. Вызовы (`m_type`): `VFS_MAPDRIVER`. Отправитель: `mapdriver()` (`minix/lib/libsys/mapdriver.c`), вызывает RS (`minix/servers/rs/manager.c:820`). Получатель: `do_mapdriver()` (`minix/servers/vfs/dmap.c:106`, только от `RS_PROC_NR`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `major` | `__devmajor_t` | 0 / 0 | Старший номер устройства драйвера или `NO_DEV` (только сокетные домены), `dmap.c:127,160` |  |
| `labellen` | `size_t` | 4 / 8 | Длина метки с NUL, `<= LABEL_MAX` (`dmap.c:126,132`) | Р |
| `label` | `vir_bytes` | 8 / 16 | Адрес метки драйвера в пространстве RS, копируется `sys_vircopy` (`dmap.c:125,136`) | А |
| `ndomains` | `int` | 12 / 24 | Число сокетных доменов (`PF_*`), обслуживаемых драйвером, `<= NR_DOMAIN` (8) | В |
| `domains` | `int[8]` | 16 / 28 | Номера доменов для `smap_map()` (`dmap.c:129,165`) | В |

Замечания: `ndomains` в libsys усекается до 8 только при заполнении массива, но в сообщение пишется исходное значение (`mapdriver.c:17-19`); проверка в `smap_map`.

#### `mess_lsys_vfs_socketpath` — ipc.h:1476

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_vfs_socketpath`.

Направление: libsys (UDS) → VFS. Вызовы (`m_type`): `VFS_SOCKETPATH`. Отправитель: `socketpath()` (`minix/lib/libsys/socketpath.c`), вызывает UDS (`minix/net/uds/uds.c:542,611`). Получатель: `do_socketpath()` (`minix/servers/vfs/path.c:803`, только `super_user`). Ответ: тип `mess_vfs_lsys_socketpath`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Пользовательский процесс, от имени которого выполняется действие (права, umask, cwd), `path.c:826` |  |
| `grant` | `cp_grant_id_t` | 4 / 4 | Прямой грант `CPF_READ` на строку пути в UDS, `sys_safecopyfrom` (`path.c:827,838`) |  |
| `count` | `size_t` | 8 / 8 | Длина пути (без обязательного NUL), `1 ≤ count < PATH_MAX` (`path.c:828,837`) | Р |
| `what` | `int` | 12 / 16 | `SPATH_CHECK` (проверить доступ к существующему сокет-файлу) / `SPATH_CREATE` (создать сокет-файл), `path.c:829` | В |

#### `mess_vfs_lc_fdpair` — ipc.h:2198

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lc_fdpair`.

Направление: VFS → libc (ответ). Вызовы (`m_type`): ответ на `VFS_PIPE2` и `VFS_SOCKETPAIR`. Отправитель: `do_pipe2()` (`minix/servers/vfs/pipe.c:50`), `do_socketpair()` (`socket.c:264`). Получатель: `pipe2()` (`minix/lib/libc/sys/pipe.c:22`), `socketpair()` (`socketpair.c:37`). Ответ: это ответ, `m_type` = OK/код.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fd0` | `int` | 0 / 0 | Первый дескриптор (конец чтения канала / первый сокет) |  |
| `fd1` | `int` | 4 / 4 | Второй дескриптор (конец записи / второй сокет) |  |

#### `mess_vfs_lc_lseek` — ipc.h:2206

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lc_lseek`.

Направление: VFS → libc (ответ). Вызовы (`m_type`): ответ на `VFS_LSEEK`. Отправитель: `do_lseek()` (`minix/servers/vfs/open.c:667`). Получатель: `lseek()` (`minix/lib/libc/sys/lseek.c:23`). Ответ: это ответ.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `offset` | `__off_t` | 0 / 0 | Новая позиция в файле, байт (64 бита) |  |

#### `mess_vfs_lc_socklen` — ipc.h:2213

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lc_socklen`.

Направление: VFS → libc (ответ). Вызовы (`m_type`): ответ на `VFS_ACCEPT`, `VFS_RECVFROM`, `VFS_GETSOCKOPT`, `VFS_GETSOCKNAME`, `VFS_GETPEERNAME`. Отправитель: `resume_accept()` (`minix/servers/vfs/socket.c:474`), `resume_recvfrom()` (`socket.c:532`), `do_getsockopt/getsockname/getpeername` (`socket.c:693,716,739`). Получатель: `accept()`, `recvfrom()`, `getsockopt()`, `getsockname()`, `getpeername()` (`minix/lib/libc/sys/*.c`). Ответ: это ответ; `m_type` — fd/число байт/OK.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `len` | `unsigned int` | 0 / 0 | Фактическая длина адреса / значения опции, libc пишет в `*address_len`/`*option_len` (исх.: socklen_t) |  |

#### `mess_vfs_lchardriver_cancel` — ipc.h:2220

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lchardriver_cancel`.

Направление: VFS → символьный драйвер (libchardriver). Вызовы (`m_type`): `CDEV_CANCEL`. Отправитель: `cdev_cancel()` (`minix/servers/vfs/cdev.c:381`, `asynsend3`). Получатель: `do_cancel()` (`minix/lib/libchardriver/chardriver.c:392`) → `cdr_cancel`. Ответ: `CDEV_REPLY` (`mess_lchardriver_vfs_reply`) с `id` отменяемого запроса.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `endpoint_t` | 0 / 0 | Идентификатор отменяемого запроса = endpoint пользовательского процесса (`cdev.c:396`; драйвер трактует как `cdev_id_t`) |  |
| `minor` | `__devminor_t` | 4 / 4 | Младший номер устройства, `cdev.c:395` |  |

#### `mess_vfs_lchardriver_openclose` — ipc.h:2228

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lchardriver_openclose`.

Направление: VFS → символьный драйвер. Вызовы (`m_type`): `CDEV_OPEN`, `CDEV_CLOSE`. Отправитель: `cdev_opcl()` (`minix/servers/vfs/cdev.c:149`). Получатель: `do_open()`/`do_close()` (`minix/lib/libchardriver/chardriver.c:291,320`); блочные драйверы отвечают ошибкой на `CDEV_OPEN` (`minix/lib/libblockdriver/driver.c:373,415`). Ответ: `CDEV_REPLY` (`mess_lchardriver_vfs_reply`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `endpoint_t` | 0 / 0 | Идентификатор запроса (endpoint вызывающего процесса), возвращается в ответе для сопоставления (`cdev.c:197`) |  |
| `user` | `endpoint_t` | 4 / 4 | Только `CDEV_OPEN`: endpoint пользователя (для TTY/PTY и клонирования), `cdev.c:203` |  |
| `minor` | `__devminor_t` | 8 / 8 | Младший номер устройства |  |
| `access` | `int` | 12 / 12 | Только `CDEV_OPEN`: `CDEV_R_BIT`, `CDEV_W_BIT`, `CDEV_NOCTTY` (`cdev.c:199-204`) |  |

#### `mess_vfs_lchardriver_readwrite` — ipc.h:2238

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_vfs_lchardriver_readwrite`.

Направление: VFS → символьный драйвер. Вызовы (`m_type`): `CDEV_READ`, `CDEV_WRITE`, `CDEV_IOCTL`. Отправитель: `cdev_io()` (`minix/servers/vfs/cdev.c:280`). Получатель: `do_transfer()` / `do_ioctl()` (`minix/lib/libchardriver/chardriver.c:341-347,378-384`). Ответ: `CDEV_REPLY` (`mess_lchardriver_vfs_reply`) со статусом/числом байт.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pos` | `__off_t` | 0 / 0 | READ/WRITE: позиция в устройстве, байт (`cdev.c:322`); для IOCTL не заполняется |  |
| `grant` | `cp_grant_id_t` | 8 / 8 | Magic-грант на буфер пользователя (READ — `CPF_WRITE`, WRITE — `CPF_READ`) или ioctl-грант (`make_ioctl_grant`), `cdev.c:306-311,326` |  |
| `count` | `size_t` | 12 / 16 | READ/WRITE: число байт (`cdev.c:323`) | Р |
| `request` | `unsigned long` | 16 / 24 | IOCTL: код запроса `_IO*` (`cdev.c:319`); в VFS он приходит через параметр `bytes` — общая переменная для длины и кода | С |
| `flags` | `int` | 20 / 32 | `CDEV_NONBLOCK`, если у filp `O_NONBLOCK` (`cdev.c:327-329`) | В |
| `id` | `endpoint_t` | 24 / 36 | Идентификатор запроса (endpoint пользователя), для ответа и `CDEV_CANCEL` | В |
| `user` | `endpoint_t` | 28 / 40 | IOCTL: endpoint пользователя (`cdev.c:320`) | В |
| `minor` | `__devminor_t` | 32 / 44 | Младший номер устройства | В |

Замечания: `count`/`request` взаимоисключающи и `pos` не нужен для IOCTL — union сократил бы тип до 56 байт и на x86_64; `request` достаточно `u32_t`.

#### `mess_vfs_lchardriver_select` — ipc.h:2252

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lchardriver_select`.

Направление: VFS → символьный драйвер. Вызовы (`m_type`): `CDEV_SELECT`. Отправитель: `cdev_select()` (`minix/servers/vfs/cdev.c:350`). Получатель: `do_select()` (`minix/lib/libchardriver/chardriver.c:428`) → `cdr_select`. Ответ: `CDEV_SEL1_REPLY` сразу, позднее `CDEV_SEL2_REPLY` (`mess_lchardriver_vfs_sel1/sel2`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `minor` | `__devminor_t` | 0 / 0 | Младший номер устройства (без CTTY-отображения), `cdev.c:367` |  |
| `ops` | `int` | 4 / 4 | Маска `CDEV_OP_RD/WR/ERR` и `CDEV_NOTIFY` (`cdev.c:368`) |  |

#### `mess_vfs_lsys_gcov` — ipc.h:2340

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_lsys_gcov`.

Направление: VFS → любой сервис (libsys). Вызовы (`m_type`): `COMMON_REQ_GCOV_DATA`. Отправитель: `do_gcov_flush()` (`minix/servers/vfs/gcov.c:65-67`, `_taskcall`). Получатель: `do_gcov()` (`minix/lib/libsys/gcov.c:162-165`) → `gcov_flush()`. Ответ: код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `grant` | `cp_grant_id_t` | 0 / 0 | Magic-грант (`CPF_WRITE`) на буфер утилиты, куда сервис пишет данные gcov |  |
| `size` | `size_t` | 4 / 8 | Размер буфера, байт | Р |

#### `mess_vfs_lsys_socketpath` — ipc.h:2348

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lsys_socketpath`.

Направление: VFS → libsys (UDS), ответ. Вызовы (`m_type`): ответ на `VFS_SOCKETPATH`. Отправитель: `do_socketpath()` (`minix/servers/vfs/path.c:866,904`). Получатель: `socketpath()` (`minix/lib/libsys/socketpath.c:30-31`). Ответ: это ответ.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Устройство ФС, на которой лежит сокет-файл (`v_dev`) |  |
| `inode` | `ino_t` | 8 / 8 | Номер inode сокет-файла (`v_inode_nr`); пара dev/ino — ключ UDS для поиска сокета |  |

#### `mess_vfs_utimens` — ipc.h:2356

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_vfs_utimens`.

Направление: libc → VFS (несмотря на имя без `lc_`). Вызовы (`m_type`): `VFS_UTIMENS`. Отправитель: `futimens()`, `futimes()`, `lutimes()`, `utimensat()`, `utimes()` (`minix/lib/libc/sys/*.c`). Получатель: `do_utimens()` (`minix/servers/vfs/time.c:26`). Ответ: только код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `atime` | `time_t` | 0 / 0 | Секунды времени доступа (`time_t` 64 бита), `time.c:54` |  |
| `mtime` | `time_t` | 8 / 8 | Секунды времени модификации, `time.c:56` |  |
| `ansec` | `long` | 16 / 16 | Наносекунды atime или `UTIME_NOW`/`UTIME_OMIT`, `time.c:55` | С |
| `mnsec` | `long` | 20 / 24 | Наносекунды mtime или `UTIME_NOW`/`UTIME_OMIT`, `time.c:57` | С |
| `len` | `size_t` | 24 / 32 | Длина пути с NUL (если `name != NULL`), `time.c:65` | Р |
| `name` | `char *` | 28 / 40 | Адрес пути у вызывающего; NULL — режим futimens по `fd` (`time.c:59,64`) | А |
| `fd` | `int` | 32 / 48 | Дескриптор для futimens/futimes (при `name != NULL` игнорируется, `AT_FDCWD`-относительность не поддержана) | В |
| `flags` | `int` | 36 / 52 | `AT_SYMLINK_NOFOLLOW` (lutimes/utimensat), для fd-режима обязан быть 0 (`time.c:61,66,80`) | В |

Замечания: `ansec/mnsec` фактически `< 1e9` или спец-значения — хватит `i32_t`; `name` объявлен как `char *` (тип пространства пользователя) вместо `vir_bytes`.

#### `mess_vm_vfs_mmap` — ipc.h:2369

Размер: i386 56, x86_64 56. Член объединения: `m_vm_vfs_mmap`.

Направление: **VFS → VM** (имя вводит в заблуждение). Вызовы (`m_type`): `VM_VFS_MMAP`. Отправитель: `minix_vfs_mmap()` (`minix/lib/libc/sys/mmap.c:49-67`), вызывается из `vfs_memmap()` при exec (`minix/servers/vfs/exec.c:173`). Получатель: `do_vfs_mmap()` (`minix/servers/vm/mmap.c:135`) → `mmap_file()`. Ответ: код в `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `offset` | `__off_t` | 0 / 0 | Смещение сегмента в файле, байт (`mmap.c:152`) |  |
| `dev` | `dev_t` | 8 / 8 | Устройство файла (`v_dev`), ключ кэша страниц VM |  |
| `ino` | `ino_t` | 16 / 16 | Inode файла (`v_inode_nr`) |  |
| `who` | `endpoint_t` | 24 / 24 | Процесс, в чьё пространство отображается сегмент (`mmap.c:148`) |  |
| `vaddr` | `__uint32_t` | 28 / 28 | Виртуальный адрес сегмента ELF (из `vir_bytes`), отображение `MAP_FIXED` | У |
| `len` | `__uint32_t` | 32 / 32 | Длина отображения, байт (из `vir_bytes`/`size_t`) | У |
| `flags` | `__uint32_t` | 36 / 36 | `MVM_WRITABLE` (0x8000) для записываемых сегментов; VM читает в `u16_t` (`mmap.c:146`) |  |
| `fd` | `__uint32_t` | 40 / 40 | Номер fd в таблице процесса VM (получен `dupvm`, `vi->vmfd`) |  |
| `clearend` | `__uint16_t` | 44 / 44 | Число байт в конце последней страницы, которые нужно обнулить (хвост .data / начало .bss) |  |

Замечания: `vaddr`/`len` — 32-битные адреса/размеры, на 64 битах обрежутся молча (прототип `minix_vfs_mmap` тоже `u32_t vaddr`).


#### Псевдонимы общих раскладок

| Псевдоним | Поле | Тип | Раздел заголовка | Файл:строка | Назначение | 64 |
|---|---|---|---|---|---|---|
| `VFS_VMCALL_REQ` | `m10_i1` | `int` | To VFS: fields for request from VM. | minix/include/minix/com.h:694 | `VFS_VMCALL` (VM → VFS, `vfs_request()` `minix/servers/vm/vfs.c:84`, `asynsend`; получатель `do_vm_call()` `minix/servers/vfs/misc.c:387`): код запроса `VMVFSREQ_FDLOOKUP`/`FDCLOSE`/`FDIO` |  |
| `VFS_VMCALL_FD` | `m10_i2` | `int` | To VFS: fields for request from VM. | minix/include/minix/com.h:695 | Там же: fd в процессе `ENDPOINT` (FDLOOKUP) или в таблице самого VM (FDCLOSE, FDIO), `misc.c:388` |  |
| `VFS_VMCALL_REQID` | `m10_i3` | `int` | To VFS: fields for request from VM. | minix/include/minix/com.h:696 | Там же: порядковый id запроса VM, возвращается в `VMV_REQID` |  |
| `VFS_VMCALL_ENDPOINT` | `m10_i4` | `int` | To VFS: fields for request from VM. | minix/include/minix/com.h:697 | Там же: endpoint процесса, ради которого VM делает запрос (`misc.c:390`) |  |
| `VFS_VMCALL_OFFSET` | `m10_ull1` | `__uint64_t` | To VFS: fields for request from VM. | minix/include/minix/com.h:698 | Там же: смещение в файле для `FDIO` (`actual_lseek`, `misc.c:464`) |  |
| `VFS_VMCALL_LENGTH` | `m10_l3` | `long` | To VFS: fields for request from VM. | minix/include/minix/com.h:699 | Там же: длина `FDIO` (PEEK) в байтах; VM передаёт `u32_t`, VFS читает в `u32_t` (`misc.c:392`) | С, У |
| `VMV_ENDPOINT` | `m10_i1` | `int` | Calls from VFS. | minix/include/minix/com.h:708 | `VM_VFS_REPLY` (VFS → VM, ответ на `VFS_VMCALL`, `misc.c:485`, `asynsend3`; получатель `do_vfs_reply()` `minix/servers/vm/vfs.c:124`): endpoint из запроса |  |
| `VMV_RESULT` | `m10_i2` | `int` | Calls from VFS. | minix/include/minix/com.h:709 | Там же: результат OK/код ошибки (`vm/mmap.c:172`, `vm/pagefaults.c:179`) |  |
| `VMV_REQID` | `m10_i3` | `int` | Calls from VFS. | minix/include/minix/com.h:710 | Там же: id запроса, сверяется `assert` (`vm/vfs.c:121`) |  |
| `VMV_DEV` | `m10_i4` | `int` | Calls from VFS. | minix/include/minix/com.h:711 | Там же (FDLOOKUP): устройство файла (`v_dev`) или `v_sdev` для блочного устройства (`misc.c:436,440`); `dev_t` (64 бита) сужается до `int` |  |
| `VMV_INO` | `m10_l1` | `long` | Calls from VFS. | minix/include/minix/com.h:712 | Там же: inode (`ino_t`, 64 бита → `long`) или `VMC_NO_INODE` для блочного устройства | С |
| `VMV_FD` | `m10_l2` | `long` | Calls from VFS. | minix/include/minix/com.h:713 | Там же: номер fd, созданного `dupvm()` в таблице VM (`misc.c:447`, `vm/mmap.c:180`) | С |
| `VMV_SIZE_PAGES` | `m10_l3` | `long` | Calls from VFS. | minix/include/minix/com.h:714 | Там же: размер файла в страницах (`roundup(v_size, PAGE_SIZE)/PAGE_SIZE`) или `LONG_MAX` для блочного устройства; VM умножает на `PAGE_SIZE` в `u64_t` (`vm/mmap.c:183`) | С |
| `VM_RS_NR` | `m2_i1` | `int` | Calls from VFS. | minix/include/minix/com.h:725 | `VM_RS_SET_PRIV` — **не от VFS**: RS → VM (`vm_set_priv()` `minix/lib/libsys/vm_set_priv.c:12`; получатель `do_rs_set_priv()` `minix/servers/vm/rs.c:40`): endpoint процесса |  |
| `VM_RS_BUF` | `m2_l1` | `long` | Calls from VFS. | minix/include/minix/com.h:726 | Там же: адрес битовой маски разрешённых VM-вызовов в пространстве RS (0 — без маски), копируется `sys_datacopy` (`rs.c:49-50`); указатель в `long` | А, С |
| `VM_RS_SYS` | `m2_i2` | `int` | Calls from VFS. | minix/include/minix/com.h:727 | Там же: признак системного процесса (`acl_set`, `rs.c:63`) |  |

Замечания: `VM_RS_*` попали под комментарий «Calls from VFS» только из-за положения в `com.h` — это протокол RS → VM. `mess_10` (`u64 + 4×int + 3×long`) на x86_64 сам не помещается в 56 байт. `VMV_DEV`/`VMV_INO` сужают 64-битные `dev_t`/`ino_t` уже на i386.

## 8. VFS ↔ файловые системы и драйверы (блочные, символьные, сокеты), PTY

#### `mess_fs_vfs_breadwrite` — ipc.h:118

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_fs_vfs_breadwrite`.

Направление: ФС → VFS (ответ на запрос `mess_vfs_fs_breadwrite`). Вызовы (`m_type`): ответ на `REQ_BREAD`, `REQ_BWRITE`, `REQ_BPEEK`; `m_type` ответа = код результата (`TRNS_ADD_ID(r, transid)`, `minix/lib/libfsdriver/fsdriver.c:51`). Отправитель: `bread_bwrite()` (`minix/lib/libfsdriver/call.c:901`), `fsdriver_bpeek()` (`call.c:969`). Получатель: `req_breadwrite_actual()` (`minix/servers/vfs/request.c:30`); `req_bpeek()` (`request.c:89`) поля ответа не читает.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `seek_pos` | `__off_t` | 0 / 0 | Новая позиция на блочном устройстве в байтах = `pos + r` (`call.c:929`); VFS → `*new_pos` (`request.c:58`). Для `REQ_BPEEK` не заполняется.  |  |
| `nbytes` | `size_t` | 8 / 8 | Число переданных байт (`ssize_t r` от `fdr_bread/fdr_bwrite/fdr_bpeek`, `call.c:930`, `call.c:991`); VFS → `*cum_iop` (`request.c:59`).  | Р |
| `data` | `__uint8_t[44]` | 12 / 16 | Заполнитель до 56 байт; не используется.  | В |

Замечания: на x86_64 заполнитель не пересчитан — тип переполняет 56 байт. Запрос несёт число байт как `unsigned int` в VFS (`req_breadwrite(..., unsigned int num_of_bytes, ...)`), т.е. реальный предел — 32 бита, хотя поле `size_t`.

#### `mess_fs_vfs_chmod` — ipc.h:127

Размер: i386 56, x86_64 56. Член объединения: `m_fs_vfs_chmod`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_chmod`). Вызовы (`m_type`): ответ на `REQ_CHMOD`, `m_type` = код результата. Отправитель: `fsdriver_chmod()` (`minix/lib/libfsdriver/call.c:773`). Получатель: `req_chmod()` (`minix/servers/vfs/request.c:108`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `mode` | `__mode_t` | 0 / 0 | Фактический итоговый режим файла (тип + права `S_IF*`/`07777`) после `fdr_chmod` (`call.c:787`); VFS → `*new_modep` (`request.c:127`, читается даже при ошибке).  |  |
| `data` | `__uint8_t[52]` | 4 / 4 | Заполнитель; не используется.  |  |

#### `mess_fs_vfs_chown` — ipc.h:134

Размер: i386 56, x86_64 56. Член объединения: `m_fs_vfs_chown`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_chown`). Вызовы (`m_type`): ответ на `REQ_CHOWN`, `m_type` = код результата. Отправитель: `fsdriver_chown()` (`minix/lib/libfsdriver/call.c:747`). Получатель: `req_chown()` (`minix/servers/vfs/request.c:136`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `mode` | `__mode_t` | 0 / 0 | Новый режим файла после chown (ФС может сбросить `S_ISUID`/`S_ISGID`) (`call.c:764`); VFS → `*new_modep` (`request.c:157`).  |  |
| `data` | `__uint8_t[52]` | 4 / 4 | Заполнитель; не используется.  |  |

#### `mess_fs_vfs_create` — ipc.h:141

Размер: i386 56, x86_64 56. Член объединения: `m_fs_vfs_create`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_create`). Вызовы (`m_type`): ответ на `REQ_CREATE`, `m_type` = код результата. Отправитель: `fsdriver_create()` (`minix/lib/libfsdriver/call.c:399`). Получатель: `req_create()` (`minix/servers/vfs/request.c:166`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `file_size` | `__off_t` | 0 / 0 | Размер созданного файла в байтах (`fn_size`, `call.c:432`); VFS → `res->fsize` (`request.c:207`).  |  |
| `inode` | `ino_t` | 8 / 8 | Номер inode созданного (и открытого, с ссылкой) файла (`call.c:430`) → `res->inode_nr`.  |  |
| `mode` | `__mode_t` | 16 / 16 | Режим созданного файла (`call.c:431`) → `res->fmode`.  |  |
| `uid` | `__uid_t` | 20 / 20 | Владелец (`call.c:433`) → `res->uid`.  |  |
| `gid` | `__gid_t` | 24 / 24 | Группа (`call.c:434`) → `res->gid`.  |  |
| `data` | `__uint8_t[28]` | 28 / 28 | Заполнитель; не используется.  |  |

#### `mess_fs_vfs_getdents` — ipc.h:153

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_fs_vfs_getdents`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_getdents`). Вызовы (`m_type`): ответ на `REQ_GETDENTS`, `m_type` = код результата. Отправитель: `fsdriver_getdents()` (`minix/lib/libfsdriver/call.c:321`). Получатель: `req_getdents_actual()` (`minix/servers/vfs/request.c:288`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `seek_pos` | `__off_t` | 0 / 0 | Новая позиция в каталоге (непрозрачная для VFS «cookie» ФС, обновляется `fdr_getdents`, `call.c:347`) → `*new_pos` (`request.c:333`).  |  |
| `nbytes` | `size_t` | 8 / 8 | Число байт `struct dirent`, записанных в буфер по гранту (`call.c:348`); VFS возвращает как результат вызова (`request.c:334`, в `int r`).  | Р |
| `data` | `__uint8_t[44]` | 12 / 16 | Заполнитель; не используется.  | В |

Замечания: VFS кладёт `nbytes` в `int r` — фактический предел 2^31.

#### `mess_fs_vfs_lookup` — ipc.h:162

Размер: i386 56, x86_64 56. Член объединения: `m_fs_vfs_lookup`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_lookup`). Вызовы (`m_type`): ответ на `REQ_LOOKUP`; `m_type` = `OK`, `EENTERMOUNT`, `ELEAVEMOUNT`, `ESYMLINK` (`minix/include/minix/vfsif.h`) или ошибка — от этого зависит, какие поля заполнены. Отправитель: `fsdriver_lookup()` (`minix/lib/libfsdriver/lookup.c:118`). Получатель: `req_lookup()` (`minix/servers/vfs/request.c:424`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `offset` | `__off_t` | 0 / 0 | Только при `EENTERMOUNT`/`ELEAVEMOUNT`/`ESYMLINK`: число уже обработанных символов пути (индекс в буфере пути, `(int)(ptr - path)`, `lookup.c:308`) → `res->char_processed` (`request.c:506`). Смещение в строке, а не в файле; `off_t` избыточен.  |  |
| `file_size` | `__off_t` | 8 / 8 | При `OK`: размер найденного файла (`lookup.c:325`) → `res->fsize`.  |  |
| `device` | `dev_t` | 16 / 16 | При `OK`: `fn_dev` — номер устройства для спецфайлов (char/block), иначе `NO_DEV` (`lookup.c:328`) → `res->dev`.  |  |
| `inode` | `ino_t` | 24 / 24 | При `OK`: inode результата (открыт, со ссылкой); при `EENTERMOUNT`: inode точки монтирования (`lookup.c:312`, `lookup.c:323`).  |  |
| `mode` | `__mode_t` | 32 / 32 | При `OK`: режим файла (`lookup.c:324`).  |  |
| `uid` | `__uid_t` | 36 / 36 | При `OK`: владелец (`lookup.c:326`).  |  |
| `gid` | `__gid_t` | 40 / 40 | При `OK`: группа (`lookup.c:327`).  |  |
| `symloop` | `__uint16_t` | 44 / 44 | При редиректах: накопленный счётчик разыменованных симлинков (предел `_POSIX_SYMLOOP_MAX`) (`lookup.c:309`) → `res->symloop` (`request.c:507`).  |  |
| `data` | `__uint8_t[10]` | 46 / 46 | Заполнитель; не используется.  |  |

Замечания: при `symloop > 0` ФС переписывает путь обратно в буфер VFS по `grant_path` (`lookup.c:299`), а `offset` указывает в изменённый путь.

#### `mess_fs_vfs_newnode` — ipc.h:178

Размер: i386 56, x86_64 56. Член объединения: `m_fs_vfs_newnode`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_newnode`). Вызовы (`m_type`): ответ на `REQ_NEWNODE`, `m_type` = код результата. Отправитель: `fsdriver_newnode()` (`minix/lib/libfsdriver/call.c:120`). Получатель: `req_newnode()` (`minix/servers/vfs/request.c:624`); запрос шлётся в PFS (pipe, сокет-узлы, `cdev.c:109`, `pipe.c:101`, `socket.c:138`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `file_size` | `__off_t` | 0 / 0 | Размер нового безымянного узла (`call.c:141`) → `res->fsize` (`request.c:652`).  |  |
| `device` | `dev_t` | 8 / 8 | Устройство узла (`fn_dev`, `call.c:144`) → `res->dev`.  |  |
| `inode` | `ino_t` | 16 / 16 | Номер inode нового узла (`call.c:139`) → `res->inode_nr`.  |  |
| `mode` | `__mode_t` | 24 / 24 | Режим узла (`call.c:140`).  |  |
| `uid` | `__uid_t` | 28 / 28 | Владелец (`call.c:142`).  |  |
| `gid` | `__gid_t` | 32 / 32 | Группа (`call.c:143`).  |  |
| `data` | `__uint8_t[20]` | 36 / 36 | Заполнитель; не используется.  |  |

#### `mess_fs_vfs_rdlink` — ipc.h:191

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_fs_vfs_rdlink`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_rdlink`). Вызовы (`m_type`): ответ на `REQ_RDLINK`, `m_type` = код результата. Отправитель: `fsdriver_rdlink()` (`minix/lib/libfsdriver/call.c:691`). Получатель: `req_rdlink_actual()` (`minix/servers/vfs/request.c:717`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `nbytes` | `size_t` | 0 / 0 | Длина содержимого симлинка, скопированного в буфер по гранту (без `\0`) (`call.c:707`); VFS возвращает как результат (`request.c:745`, в `int r`).  | Р |
| `data` | `__uint8_t[52]` | 4 / 8 | Заполнитель; не используется.  | В |

#### `mess_fs_vfs_readsuper` — ipc.h:198

Размер: i386 56, x86_64 56. Член объединения: `m_fs_vfs_readsuper`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_readsuper`). Вызовы (`m_type`): ответ на `REQ_READSUPER`, `m_type` = код результата. Отправитель: `fsdriver_readsuper()` (`minix/lib/libfsdriver/call.c:12`). Получатель: `req_readsuper()` (`minix/servers/vfs/request.c:780`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `file_size` | `__off_t` | 0 / 0 | Размер корневого каталога ФС (`call.c:55`) → `res->fsize` (`request.c:821`).  |  |
| `device` | `dev_t` | 8 / 8 | Не заполняется ФС и не читается VFS (`res->dev` не присваивается).  |  |
| `inode` | `ino_t` | 16 / 16 | Номер inode корня ФС (`call.c:53`) → `res->inode_nr`.  |  |
| `flags` | `__uint32_t` | 24 / 24 | Возможности ФС `RES_*`: `RES_THREADED`, `RES_HASPEEK` (ставит libfsdriver, `call.c:49`), `RES_64BIT` (`call.c:58`) → `*fs_flags` → `vmp->m_fs_flags` (проверка `RES_64BIT` в `request.c:323`, `request.c:856`).  |  |
| `mode` | `__mode_t` | 28 / 28 | Режим корня (`call.c:54`).  |  |
| `uid` | `__uid_t` | 32 / 32 | Владелец корня (`call.c:56`).  |  |
| `gid` | `__gid_t` | 36 / 36 | Группа корня (`call.c:57`).  |  |
| `con_reqs` | `__uint16_t` | 40 / 40 | **Не используется** (нигде в дереве не заполняется и не читается; остаток протокола «числа одновременных запросов»).  |  |
| `data` | `__uint8_t[14]` | 42 / 42 | Заполнитель; не используется.  |  |

#### `mess_fs_vfs_readwrite` — ipc.h:214

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_fs_vfs_readwrite`.

Направление: ФС → VFS (ответ на `mess_vfs_fs_readwrite`). Вызовы (`m_type`): ответ на `REQ_READ`, `REQ_WRITE`, `REQ_PEEK`; `m_type` = код результата. Отправитель: `read_write()` (`minix/lib/libfsdriver/call.c:154`), `fsdriver_peek()` (`call.c:279`). Получатель: `req_readwrite_actual()` (`minix/servers/vfs/request.c:834`); `req_peek()` (`request.c:903`) поля ответа не читает.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `seek_pos` | `__off_t` | 0 / 0 | Новая позиция в файле = `pos + r` (`call.c:182`) → `*new_posp` (`request.c:868`). Для `REQ_PEEK` не заполняется.  |  |
| `nbytes` | `size_t` | 8 / 8 | Число прочитанных/записанных байт (`call.c:183`, `call.c:310`) → `*cum_iop` (`request.c:869`).  | Р |
| `data` | `__uint8_t[44]` | 12 / 16 | Заполнитель; не используется.  | В |

#### `mess_lbdev_lblockdriver_msg` — ipc.h:338

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lbdev_lblockdriver_msg`.

Направление: клиент блочного устройства (libbdev в ФС, VFS, драйверы-фильтры) → блочный драйвер (libblockdriver). Вызовы (`m_type`): `BDEV_OPEN`, `BDEV_CLOSE`, `BDEV_READ`, `BDEV_WRITE`, `BDEV_GATHER`, `BDEV_SCATTER`, `BDEV_IOCTL` (`minix/include/minix/com.h:970`). Отправитель: libbdev `bdev_opcl()`/`bdev_rdwt_setup()`/`bdev_vrdwt_setup()`/`bdev_ioctl_setup()` (`minix/lib/libbdev/bdev.c:71`, `:137`, `:228`, `:338`), `bdev_minor_reopen()` (`minix/lib/libbdev/minor.c:45`), отправка `bdev_sendrec()`/`bdev_senda()` (`minix/lib/libbdev/ipc.c:134`, `:159`); VFS `bdev_open()`/`bdev_close()`/`bdev_ioctl()` (`minix/servers/vfs/bdev.c:98`, `:129`, `:169`); `fbd` (`minix/drivers/storage/fbd/fbd.c:147…348`), `filter` (`minix/drivers/storage/filter/driver.c:33…930`), `fb_edid.c:95`, `blocktest`. Получатель: `blockdriver_process_on_thread()` → `do_open/do_close/do_rdwt/do_vrdwt/do_ioctl` (`minix/lib/libblockdriver/driver.c:177…328`), диспетчер потоков `driver_mt.c:317`, трассировка `trace.c:177`. Ответ: `mess_lblockdriver_lbdev_reply` (`BDEV_REPLY`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `pos` | `__off_t` | 0 / 0 | READ/WRITE/GATHER/SCATTER: байтовое смещение на устройстве (`driver.c:210`, `:251`; libbdev передаёт `u64_t pos`). Остальные вызовы — 0.  |  |
| `minor` | `int` | 8 / 8 | Младший номер устройства (раздел), все вызовы (`driver.c:177`, `:425` — проверка «открыт ли»).  |  |
| `id` | `int` | 12 / 12 | Идентификатор запроса, эхо в ответе (`driver.c:165`): libbdev `NO_ID` (-1) для синхронных, номер вызова `bdev_id_t` для асинхронных (`ipc.c:134`, `ipc.c:282`); VFS/fbd/filter/fb шлют 0 и не проверяют.  |  |
| `access` | `int` | 16 / 16 | Только `BDEV_OPEN`: биты `BDEV_R_BIT`, `BDEV_W_BIT` (`driver.c:177`, VFS `bdev.c:96`).  |  |
| `count` | `int` | 20 / 20 | READ/WRITE: длина буфера в байтах (`driver.c:206` → `iov_size`; отрицательное — `EINVAL`, `driver.c:202`); GATHER/SCATTER: число элементов вектора `iovec_s_t` (≤ `NR_IOREQS`, `driver.c:232`).  | У |
| `grant` | `cp_grant_id_t` | 24 / 24 | READ/WRITE: грант на буфер данных (кладётся в `iov_addr`, `driver.c:205`); GATHER/SCATTER: грант на массив `iovec_s_t` (`driver.c:235`); IOCTL: грант на аргумент ioctl или `GRANT_INVALID`.  |  |
| `flags` | `int` | 28 / 28 | READ/WRITE/GATHER/SCATTER: `BDEV_NOFLAGS`, `BDEV_FORCEWRITE`, `BDEV_NOPAGE` (`com.h:986`) → `bdr_transfer` (`driver.c:213`).  |  |
| `user` | `endpoint_t` | 32 / 32 | Только IOCTL: endpoint процесса-инициатора ioctl (или `NONE`) (`driver.c:328`).  |  |
| `request` | `unsigned long` | 36 / 40 | Только IOCTL: код ioctl (`_IOR/_IOW`-кодировка NetBSD, размер аргумента в битах кода; `driver.c:326`, `bdev.c:320`).  | С |

Замечания: размер передачи в `int count` (и ответ в `int status`) — предел 2 ГБ на запрос; при переработке — фиксированная ширина. Тип `unsigned long` для кода ioctl — единственное слово-зависимое поле; код по факту 32-битный. Поле `grant` READ/WRITE переносится в `iovec_t.iov_addr` (`vir_bytes`) — грант в поле адреса.

#### `mess_lblockdriver_lbdev_reply` — ipc.h:356

Размер: i386 56, x86_64 56. Член объединения: `m_lblockdriver_lbdev_reply`.

Направление: блочный драйвер → клиент (libbdev, VFS, fbd, filter, blocktest). Вызовы (`m_type`): `BDEV_REPLY` — ответ на все `BDEV_*` из `mess_lbdev_lblockdriver_msg`. Отправитель: `blockdriver_reply()` (`minix/lib/libblockdriver/driver.c:154`); `libchardriver` `do_block_open()` отвечает `ENXIO` на `BDEV_*`, пришедший символьному драйверу (`minix/lib/libchardriver/chardriver.c:445`). Получатель: libbdev `bdev_sendrec()` (`minix/lib/libbdev/ipc.c:188`), `bdev_reply_asyn()` (`ipc.c:282`), `minor.c:61`; VFS `bdev_sendrec()` (`minix/servers/vfs/bdev.c:48`); fbd, filter, blocktest.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | Результат: для READ/WRITE/GATHER/SCATTER — число переданных байт (`ssize_t r` из `bdr_transfer`, `driver.c:216`) или отрицательная ошибка; для OPEN/CLOSE/IOCTL — `OK`/ошибка; `ERESTART` — драйвер перезапущен, устройство не открыто (`driver.c:429`; libbdev/VFS повторяют, `ipc.c:202`, `bdev.c:49`).  | У |
| `id` | `int` | 4 / 4 | Эхо `id` запроса (`driver.c:165`); libbdev сверяет с `NO_ID`/номером асинхронного вызова (`ipc.c:188`, `ipc.c:282`).  |  |

#### `mess_lchardriver_vfs_reply` — ipc.h:943

Размер: i386 56, x86_64 56. Член объединения: `m_lchardriver_vfs_reply`.

Направление: символьный драйвер (libchardriver) → VFS. Вызовы (`m_type`): `CDEV_REPLY` — ответ на `CDEV_OPEN`, `CDEV_CLOSE`, `CDEV_READ`, `CDEV_WRITE`, `CDEV_IOCTL`, а также на `CDEV_CANCEL` (ответ относится к исходному запросу). Отправитель: `send_reply()`-ветка `chardriver_reply()` (`minix/lib/libchardriver/chardriver.c:240…258`), `chardriver_reply_task()` (`chardriver.c:142`); libblockdriver `do_char_open()` отвечает `ENXIO` на `CDEV_OPEN` блочному драйверу (`minix/lib/libblockdriver/driver.c:371`). Получатель: `cdev_reply()` → `cdev_generic_reply()` (`minix/servers/vfs/cdev.c:491`, `:436`), ожидающие `cdev_opcl()` (`cdev.c:221`) и `cdev_cancel()` (`cdev.c:416`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | OPEN: `OK` или новый minor с флагами `CDEV_CLONED`/`CDEV_CTTY` в старших битах (`cdev.c:231`); READ/WRITE: число переданных байт или ошибка; IOCTL/CLOSE: результат; после CANCEL — `EINTR`/`EAGAIN` (VFS меняет местами, `cdev.c:472`). `SUSPEND` запрещён (`cdev.c:438`).  | У |
| `id` | `__uint32_t` | 4 / 4 | `cdev_id_t` — эхо `id` запроса; VFS кладёт туда endpoint процесса-пользователя (`cdev.c:197`, `:325`, `:396`) и по нему находит ожидающий поток/процесс (`cdev.c:436`). (исх.: should be cdev_id_t)  |  |

Замечания: статус-счётчик байт в `int` ограничивает одну передачу 2 ГБ; `status` открытия совмещает minor и флаги в одном `int`.

#### `mess_lchardriver_vfs_sel1` — ipc.h:951

Размер: i386 56, x86_64 56. Член объединения: `m_lchardriver_vfs_sel1`.

Направление: символьный драйвер → VFS. Вызовы (`m_type`): `CDEV_SEL1_REPLY` — немедленный ответ на `CDEV_SELECT`. Отправитель: `chardriver_reply()` (`minix/lib/libchardriver/chardriver.c:263`). Получатель: `cdev_reply()` → `select_cdev_reply1()` (`minix/servers/vfs/cdev.c:494`, `minix/servers/vfs/select.c:1004`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | Готовые операции — битовая маска `CDEV_OP_RD`/`CDEV_OP_WR`/`CDEV_OP_ERR` (`com.h:949`) или отрицательная ошибка.  |  |
| `minor` | `__int32_t` | 4 / 4 | Minor устройства из запроса `m_vfs_lchardriver_select.minor` (`chardriver.c:265`); VFS сопоставляет с filp (`select.c:1004`).  |  |

#### `mess_lchardriver_vfs_sel2` — ipc.h:959

Размер: i386 56, x86_64 56. Член объединения: `m_lchardriver_vfs_sel2`.

Направление: символьный драйвер → VFS (асинхронно, `asynsend3(AMF_NOREPLY)`). Вызовы (`m_type`): `CDEV_SEL2_REPLY` — отложенное уведомление о готовности после `CDEV_SELECT` с `CDEV_NOTIFY`. Отправитель: `chardriver_reply_select()` (`minix/lib/libchardriver/chardriver.c:166`). Получатель: `select_cdev_reply2()` (`minix/servers/vfs/cdev.c:499`, `minix/servers/vfs/select.c:1167`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | Ставшие готовыми операции `CDEV_OP_*` или ошибка (`chardriver.c:168`).  |  |
| `minor` | `__int32_t` | 4 / 4 | Minor устройства (`chardriver.c:167`).  |  |

Замечания: раскладка идентична `mess_lchardriver_vfs_sel1` — кандидат на объединение.

#### `mess_lsockdriver_vfs_accept_reply` — ipc.h:1003

Размер: i386 56, x86_64 56. Член объединения: `m_lsockdriver_vfs_accept_reply`.

Направление: драйвер сокетов (libsockdriver) → VFS. Вызовы (`m_type`): `SDEV_ACCEPT_REPLY` — ответ на `SDEV_ACCEPT`. Отправитель: `sockdriver_reply_accept()` (`minix/lib/libsockdriver/sockdriver.c:327`). Получатель: `sdev_reply()` (`minix/servers/vfs/sdev.c:1011`, `:1058`), `sdev_finish()` (`sdev.c:759`, ветка `:863`), `resume_accept` через `sdev.c:697`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | `sockreq_t` — эхо `req_id` запроса; VFS кладёт туда endpoint процесса (`sdev.c:316`) и ищет по нему процесс (`sdev.c:1012`, `isokendpt`).  |  |
| `sock_id` | `__int32_t` | 4 / 4 | `sockid_t` нового сокета (≥ 0) или -1 при неудаче (`sockdriver.c:386`); VFS строит из него `dev_t` (`sdev.c:710`).  |  |
| `status` | `int` | 8 / 8 | `OK` или ошибка; комбинация `sock_id ≥ 0` и ошибки означает «сокет создан, но адрес не скопирован — закрыть» (`sockdriver.c:340`, `sdev.c:876`).  |  |
| `len` | `unsigned int` | 12 / 12 | Длина адреса пира, скопированного по гранту (`socklen_t`, ≤ `_sc_len`) (`sockdriver.c:388`) → `resume_accept` (`sdev.c:699`).  |  |

#### `mess_lsockdriver_vfs_recv_reply` — ipc.h:1013

Размер: i386 56, x86_64 56. Член объединения: `m_lsockdriver_vfs_recv_reply`.

Направление: драйвер сокетов → VFS. Вызовы (`m_type`): `SDEV_RECV_REPLY` — ответ на `SDEV_RECV`. Отправитель: `sockdriver_reply_recv()` (`minix/lib/libsockdriver/sockdriver.c:401`). Получатель: `sdev_reply()` (`minix/servers/vfs/sdev.c:1014`) → `sdev_finish()` (`sdev.c:826`) → `resume_recvfrom/resume_recvmsg`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Эхо `req_id` (endpoint процесса) (`sockdriver.c:437`).  |  |
| `status` | `int` | 4 / 4 | Число принятых байт данных (≥ 0, `data_len` урезан до `INT_MAX`, `sockdriver.c:740`) или ошибка (`sdev.c:827`).  | У |
| `ctl_len` | `unsigned int` | 8 / 8 | Длина записанных управляющих данных (`socklen_t`) (`sockdriver.c:439`) → `msg_controllen` в `resume_recvmsg`.  |  |
| `addr_len` | `unsigned int` | 12 / 12 | Длина записанного адреса источника (`socklen_t`) (`sockdriver.c:440`).  |  |
| `flags` | `int` | 16 / 16 | Выходные флаги `MSG_*` (`MSG_TRUNC`, `MSG_CTRUNC`, …) → `msg_flags` (`sdev.c:831`).  |  |

#### `mess_lsockdriver_vfs_reply` — ipc.h:1024

Размер: i386 56, x86_64 56. Член объединения: `m_lsockdriver_vfs_reply`.

Направление: драйвер сокетов → VFS. Вызовы (`m_type`): `SDEV_REPLY` — общий ответ на `SDEV_BIND`, `SDEV_CONNECT`, `SDEV_LISTEN`, `SDEV_SEND`, `SDEV_IOCTL`, `SDEV_SETSOCKOPT`, `SDEV_GETSOCKOPT`, `SDEV_GETSOCKNAME`, `SDEV_GETPEERNAME`, `SDEV_SHUTDOWN`, `SDEV_CLOSE` (и на исходный запрос после `SDEV_CANCEL`). Отправитель: `send_generic_reply()`/`sockdriver_reply_generic()` (`minix/lib/libsockdriver/sockdriver.c:276`, `:294`). Получатель: `sdev_reply()` (`minix/servers/vfs/sdev.c:1005`), `sdev_simple()` (`sdev.c:273`), `sdev_setsockopt()` (`sdev.c:497`), `sdev_get()` (`sdev.c:550`), `sdev_finish()` (`sdev.c:798`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Эхо `req_id` (endpoint процесса) (`sockdriver.c:283`; `sdev.c:1006`).  |  |
| `status` | `int` | 4 / 4 | `OK`/ошибка; для `SDEV_SEND` — число отправленных байт (`data_len` урезан до `INT_MAX`, `sockdriver.c:669`); для GETSOCKOPT/GETSOCKNAME/GETPEERNAME — длина результата (`sockdriver.c:867`, `:922`; VFS `*len = r`, `sdev.c:553`).  | У |

#### `mess_lsockdriver_vfs_select_reply` — ipc.h:1032

Размер: i386 56, x86_64 56. Член объединения: `m_lsockdriver_vfs_select_reply`.

Направление: драйвер сокетов → VFS. Вызовы (`m_type`): `SDEV_SELECT1_REPLY` (немедленный ответ на `SDEV_SELECT`), `SDEV_SELECT2_REPLY` (отложенное уведомление при `SDEV_NOTIFY`). Отправитель: `send_select_reply()` (`minix/lib/libsockdriver/sockdriver.c:450`), из `do_select()` и `sockdriver_reply_select()`. Получатель: `sdev_reply()` → `select_sdev_reply1/2()` (`minix/servers/vfs/sdev.c:1017`, `:1023`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `sock_id` | `__int32_t` | 0 / 0 | `sockid_t` сокета; VFS → `dev_t` через `make_smap_dev()` (`sdev.c:1018`).  |  |
| `status` | `int` | 4 / 4 | Маска готовых операций `SDEV_OP_RD/WR/ERR` (`com.h:1075`) или ошибка (`sockdriver.c:459`).  |  |

Замечания: нет `req_id` — select не привязан к процессу.

#### `mess_lsockdriver_vfs_socket_reply` — ipc.h:1040

Размер: i386 56, x86_64 56. Член объединения: `m_lsockdriver_vfs_socket_reply`.

Направление: драйвер сокетов → VFS. Вызовы (`m_type`): `SDEV_SOCKET_REPLY` — ответ на `SDEV_SOCKET`, `SDEV_SOCKETPAIR`. Отправитель: `send_socket_reply()` (`minix/lib/libsockdriver/sockdriver.c:305`). Получатель: `sdev_socket()` (`minix/servers/vfs/sdev.c:150`), `sdev_reply()` (`sdev.c:1009`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Эхо `req_id` (endpoint процесса) (`sockdriver.c:313`).  |  |
| `sock_id` | `__int32_t` | 4 / 4 | `sockid_t` созданного сокета (≥ 0) или отрицательная ошибка (`sdev.c:154`).  |  |
| `sock_id2` | `__int32_t` | 8 / 8 | Только SOCKETPAIR: второй сокет пары; для SOCKET — -1 (`sockdriver.c:493`, `:521`).  |  |

#### `mess_pty_ptyfs_req` — ipc.h:1830

Размер: i386 56, x86_64 56. Член объединения: `m_pty_ptyfs_req`.

Направление: драйвер PTY → PTYFS (`ipc_sendrec`, endpoint по метке DS `"ptyfs"`). Вызовы (`m_type`): `PTYFS_SET`, `PTYFS_CLEAR`, `PTYFS_NAME` (`minix/include/minix/com.h:901`). Отправитель: `ptyfs_set()`, `ptyfs_clear()`, `ptyfs_name()` (`minix/drivers/tty/pty/ptyfs.c:39`, `:64`, `:89`; вызываются из `pty.c:162…387`). Получатель: `ptyfs_other()` (`minix/fs/ptyfs/ptyfs.c:300`, через `fdr_other` libfsdriver; проверяет метку отправителя `"pty"`). Ответ: `m_type` = код результата; для `PTYFS_NAME` при `OK` — `mess_ptyfs_pty_name`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `dev` | `dev_t` | 0 / 0 | Только SET: номер устройства slave-узла (`ptyfs.c:329`).  |  |
| `mode` | `__mode_t` | 8 / 8 | Только SET: режим узла (`UNIX98_MODE`, `pty.c:371`).  |  |
| `uid` | `__uid_t` | 12 / 12 | Только SET: владелец узла.  |  |
| `gid` | `__gid_t` | 16 / 16 | Только SET: группа (`tty_gid`).  |  |
| `index` | `__uint32_t` | 20 / 20 | Все вызовы: индекс PTY (`tty_index`) = номер узла PTYFS (`ptyfs.c:335`, `:340`, `:348`).  |  |

#### `mess_ptyfs_pty_name` — ipc.h:1841

Размер: i386 56, x86_64 56. Член объединения: `m_ptyfs_pty_name`.

Направление: PTYFS → PTY (ответ). Вызовы (`m_type`): ответ на `PTYFS_NAME`, `m_type` = код результата. Отправитель: `ptyfs_other()` → `make_name()` (`minix/fs/ptyfs/ptyfs.c:346`, `:55`). Получатель: `ptyfs_name()` (`minix/drivers/tty/pty/ptyfs.c:106`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `name` | `char[20]` | 0 / 0 | Имя slave-узла без пути монтирования (десятичный индекс), с `\0`; получатель принудительно терминирует (`ptyfs.c:106`) и копирует в `pm.sn`.  |  |

Замечания: ошибка в `make_name()` (`minix/fs/ptyfs/ptyfs.c:59`): `snprintf(name, sizeof(name), …)` по указателю — пишет ≤ 3 цифр на i386 и ≤ 7 на 64 битах; поведение зависит от ширины указателя (для индексов ≥ 1000 на i386 имя молча обрезается).

#### `mess_vfs_fs_breadwrite` — ipc.h:1933

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_breadwrite`.

Направление: VFS → ФС (`fs_sendrec`, `minix/servers/vfs/comm.c:134`). Вызовы (`m_type`): `REQ_BREAD`, `REQ_BWRITE` (ввод-вывод блочного спецфайла через буферный кэш ФС, на `v_bfs_e`), `REQ_BPEEK` (подкачка в кэш VM для mmap блочного устройства). Отправитель: `req_breadwrite_actual()` (`minix/servers/vfs/request.c:30`), `req_bpeek()` (`request.c:89`, из `read.c:220`). Получатель: `bread_bwrite()` (`minix/lib/libfsdriver/call.c:901`), `fsdriver_bpeek()` (`call.c:969`). Ответ: `mess_fs_vfs_breadwrite`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Номер блочного устройства (`call.c:910`) → `fdr_bread/fdr_bwrite/fdr_bpeek`.  |  |
| `seek_pos` | `__off_t` | 8 / 8 | Байтовая позиция на устройстве; < 0 → `EINVAL` (`call.c:914`).  |  |
| `grant` | `cp_grant_id_t` | 16 / 16 | BREAD/BWRITE: magic-грант на буфер пользователя (`cpf_grant_magic`, `CPF_WRITE` для чтения / `CPF_READ` для записи, `request.c:39`); BPEEK: не используется (0 после `memset`).  |  |
| `nbytes` | `size_t` | 20 / 24 | Длина передачи в байтах; > `SSIZE_MAX` → `EINVAL` (`call.c:914`). VFS заполняет из `unsigned int`.  | Р |
| `data` | `__uint8_t[32]` | 24 / 32 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_chmod` — ipc.h:1944

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_chmod`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_CHMOD`. Отправитель: `req_chmod()` (`minix/servers/vfs/request.c:108`). Получатель: `fsdriver_chmod()` (`minix/lib/libfsdriver/call.c:773`). Ответ: `mess_fs_vfs_chmod`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode файла (`call.c:780`).  |  |
| `mode` | `__mode_t` | 8 / 8 | Новые права (`07777`-биты; VFS уже проверил привилегии) (`call.c:781`) → `fdr_chmod`.  |  |
| `data` | `__uint8_t[44]` | 12 / 12 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_chown` — ipc.h:1953

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_chown`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_CHOWN`. Отправитель: `req_chown()` (`minix/servers/vfs/request.c:136`). Получатель: `fsdriver_chown()` (`minix/lib/libfsdriver/call.c:747`). Ответ: `mess_fs_vfs_chown`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode файла (`call.c:756`).  |  |
| `uid` | `__uid_t` | 8 / 8 | Новый владелец (`call.c:757`).  |  |
| `gid` | `__gid_t` | 12 / 12 | Новая группа (`call.c:758`).  |  |
| `data` | `__uint8_t[40]` | 16 / 16 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_create` — ipc.h:1963

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_create`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_CREATE`. Отправитель: `req_create()` (`minix/servers/vfs/request.c:166`). Получатель: `fsdriver_create()` (`minix/lib/libfsdriver/call.c:399`). Ответ: `mess_fs_vfs_create`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode родительского каталога (`call.c:414`).  |  |
| `mode` | `__mode_t` | 8 / 8 | Режим нового файла (тип `I_REGULAR` + права с учётом umask) (`call.c:415`).  |  |
| `uid` | `__uid_t` | 12 / 12 | Владелец нового файла (эффективный uid процесса).  |  |
| `gid` | `__gid_t` | 16 / 16 | Группа нового файла.  |  |
| `grant` | `cp_grant_id_t` | 20 / 20 | Прямой грант (`CPF_READ`) на имя последнего компонента в памяти VFS (`request.c:185`) → `fsdriver_getname()` (`call.c:422`).  |  |
| `path_len` | `size_t` | 24 / 24 | Длина имени включая `\0` (≤ `NAME_MAX+1`, иначе `ENAMETOOLONG`; `minix/lib/libfsdriver/utility.c:90`).  | Р |
| `data` | `__uint8_t[28]` | 28 / 32 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_flush` — ipc.h:1976

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_flush`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_FLUSH` (сброс и инвалидация кэша блоков устройства). Отправитель: `req_flush()` (`minix/servers/vfs/request.c:219`; из `misc.c:255`, `filedes.c:447`). Получатель: `fsdriver_flush()` (`minix/lib/libfsdriver/call.c:1002`). Ответ: только `m_type` (всегда `OK`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Устройство, чьи блоки сбросить (`call.c:1007`) → `fdr_bflush`.  |  |
| `data` | `__uint8_t[48]` | 8 / 8 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_ftrunc` — ipc.h:1983

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_ftrunc`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_FTRUNC` (truncate/ftruncate и освобождение диапазона). Отправитель: `req_ftrunc()` (`minix/servers/vfs/request.c:261`). Получатель: `fsdriver_trunc()` (`minix/lib/libfsdriver/call.c:359`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode файла (`call.c:365`).  |  |
| `trc_start` | `__off_t` | 8 / 8 | Начало освобождаемого диапазона в байтах (новый размер при truncate); < 0 → `EINVAL` (`call.c:369`).  |  |
| `trc_end` | `__off_t` | 16 / 16 | Конец диапазона; 0 = «до конца файла» (обычный truncate) (`call.c:367`).  |  |
| `data` | `__uint8_t[32]` | 24 / 24 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_getdents` — ipc.h:1992

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_getdents`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_GETDENTS`. Отправитель: `req_getdents_actual()` (`minix/servers/vfs/request.c:288`). Получатель: `fsdriver_getdents()` (`minix/lib/libfsdriver/call.c:321`). Ответ: `mess_fs_vfs_getdents`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode каталога (`call.c:330`).  |  |
| `seek_pos` | `__off_t` | 8 / 8 | Текущая позиция в каталоге; без `RES_64BIT` у ФС > `INT_MAX` → `EINVAL` в VFS (`request.c:323`).  |  |
| `grant` | `cp_grant_id_t` | 16 / 16 | Грант `CPF_WRITE` на буфер `dirent`: прямой (буфер VFS) или magic (буфер процесса) (`request.c:308`) → `data.grant` (`call.c:341`).  |  |
| `mem_size` | `size_t` | 20 / 24 | Размер буфера в байтах; > `SSIZE_MAX` → `EINVAL` (`call.c:337`).  | Р |
| `data` | `__uint8_t[32]` | 24 / 32 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_inhibread` — ipc.h:2003

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_inhibread`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_INHIBREAD` (lseek: подавить упреждающее чтение). Отправитель: `req_inhibread()` (`minix/servers/vfs/request.c:374`; из `open.c:643`). Получатель: `fsdriver_inhibread()` (`minix/lib/libfsdriver/call.c:382`) → `fdr_seek`. Ответ: только `m_type` (`OK`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode файла (`call.c:387`).  |  |
| `data` | `__uint8_t[48]` | 8 / 8 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_link` — ipc.h:2010

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_link`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_LINK`. Отправитель: `req_link()` (`minix/servers/vfs/request.c:390`). Получатель: `fsdriver_link()` (`minix/lib/libfsdriver/call.c:518`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode существующего файла, на который ставится ссылка (`call.c:530`).  |  |
| `dir_ino` | `ino_t` | 8 / 8 | Inode каталога, где создаётся новое имя (`call.c:529`).  |  |
| `grant` | `cp_grant_id_t` | 16 / 16 | Прямой грант `CPF_READ` на новое имя (`call.c:527`).  |  |
| `path_len` | `size_t` | 20 / 24 | Длина имени с `\0` (≤ `NAME_MAX+1`) (`call.c:528`).  | Р |
| `data` | `__uint8_t[32]` | 24 / 32 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_lookup` — ipc.h:2021

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_vfs_fs_lookup`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_LOOKUP`. Отправитель: `req_lookup()` (`minix/servers/vfs/request.c:424`). Получатель: `fsdriver_lookup()` (`minix/lib/libfsdriver/lookup.c:118`). Ответ: `mess_fs_vfs_lookup`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `dir_ino` | `ino_t` | 0 / 0 | Inode каталога, с которого начинается разбор (`lookup.c:134`).  |  |
| `root_ino` | `ino_t` | 8 / 8 | Inode корня процесса (chroot) на этой ФС или 0, если корень не на ней; «..» из него не выходит (`lookup.c:135`).  |  |
| `flags` | `__uint32_t` | 16 / 16 | `PATH_RET_SYMLINK` (не разыменовывать последний симлинк), `PATH_GET_UCRED` (креды по гранту) (`vfsif.h`; `lookup.c:139`, `:147`).  |  |
| `path_len` | `size_t` | 20 / 24 | Длина пути с `\0` (≤ `PATH_MAX`) (`lookup.c:138`, `fsdriver_getname`).  | Р |
| `path_size` | `size_t` | 24 / 32 | Ёмкость буфера пути VFS для обратной записи пути после раскрытия симлинков; VFS ставит `PATH_MAX + 1` (`request.c:455`), проверка `lookup.c:299`.  | Р |
| `ucred_size` | `size_t` | 28 / 40 | Только при `PATH_GET_UCRED`: `sizeof(vfs_ucred_t)`, сверяется получателем (`lookup.c:148`).  | Р |
| `grant_path` | `cp_grant_id_t` | 32 / 48 | Прямой грант `CPF_READ` + `CPF_WRITE` на буфер пути VFS (`request.c:444`); ФС читает путь и при симлинках пишет обратно.  | В |
| `grant_ucred` | `cp_grant_id_t` | 36 / 52 | Только при `PATH_GET_UCRED`: грант `CPF_READ` на `vfs_ucred_t` (uid/gid/доп. группы) (`request.c:470`, `lookup.c:155`).  | В |
| `uid` | `__uid_t` | 40 / 56 | Без `PATH_GET_UCRED`: эффективный uid для проверки прав (`lookup.c:159`).  | В |
| `gid` | `__gid_t` | 44 / 60 | Без `PATH_GET_UCRED`: эффективный gid (`lookup.c:160`).  | В |
| `data` | `__uint8_t[8]` | 48 / 64 | Заполнитель; не используется.  | В |

Замечания: VFS даёт грант на `PATH_MAX` байт, а объявляет `path_size = PATH_MAX + 1` — ФС может попытаться записать на байт больше гранта (safecopy вернёт ошибку). `req_lookup` не обнуляет сообщение: неиспользуемая ветка (`uid/gid` или `grant_ucred/ucred_size`) уходит мусором. Самый крупный тип группы (72 байта на x86_64) — три `size_t` при пути ≤ 4 КБ.

#### `mess_vfs_fs_mkdir` — ipc.h:2038

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_mkdir`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_MKDIR`. Отправитель: `req_mkdir()` (`minix/servers/vfs/request.c:528`). Получатель: `fsdriver_mkdir()` (`minix/lib/libfsdriver/call.c:444`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode родительского каталога (`call.c:458`).  |  |
| `mode` | `__mode_t` | 8 / 8 | Режим каталога (`I_DIRECTORY` + права после umask) (`call.c:459`).  |  |
| `uid` | `__uid_t` | 12 / 12 | Владелец.  |  |
| `gid` | `__gid_t` | 16 / 16 | Группа.  |  |
| `grant` | `cp_grant_id_t` | 20 / 20 | Прямой грант `CPF_READ` на имя (`call.c:456`).  |  |
| `path_len` | `size_t` | 24 / 24 | Длина имени с `\0` (≤ `NAME_MAX+1`, непустое).  | Р |
| `data` | `__uint8_t[28]` | 28 / 32 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_mknod` — ipc.h:2051

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_mknod`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_MKNOD`. Отправитель: `req_mknod()` (`minix/servers/vfs/request.c:567`). Получатель: `fsdriver_mknod()` (`minix/lib/libfsdriver/call.c:480`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Номер устройства для `S_IFCHR`/`S_IFBLK` (`call.c:499`) → `fdr_mknod`.  |  |
| `inode` | `ino_t` | 8 / 8 | Inode родительского каталога (`call.c:495`).  |  |
| `mode` | `__mode_t` | 16 / 16 | Тип узла (`S_IFCHR`, `S_IFBLK`, `S_IFIFO`, `S_IFSOCK`) + права (`call.c:496`).  |  |
| `uid` | `__uid_t` | 20 / 20 | Владелец.  |  |
| `gid` | `__gid_t` | 24 / 24 | Группа.  |  |
| `grant` | `cp_grant_id_t` | 28 / 28 | Прямой грант `CPF_READ` на имя (`call.c:493`).  |  |
| `path_len` | `size_t` | 32 / 32 | Длина имени с `\0` (непустое).  | Р |
| `data` | `__uint8_t[20]` | 36 / 40 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_mountpoint` — ipc.h:2065

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_mountpoint`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_MOUNTPOINT` (пометить inode как точку монтирования). Отправитель: `req_mountpoint()` (`minix/servers/vfs/request.c:608`; из `mount.c:220`). Получатель: `fsdriver_mountpoint()` (`minix/lib/libfsdriver/call.c:818`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode каталога — будущей точки монтирования (`call.c:823`) → `fdr_mountpt`.  |  |
| `data` | `__uint8_t[48]` | 8 / 8 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_new_driver` — ipc.h:2072

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_new_driver`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_NEW_DRIVER` (блочный драйвер устройства ФС перезапущен / сменил endpoint). Отправитель: `req_newdriver()` (`minix/servers/vfs/request.c:664`; из `bdev.c:266`, `:278`, `mount.c:72`, `open.c:213`). Получатель: `fsdriver_newdriver()` (`minix/lib/libfsdriver/call.c:872`) → `fdr_driver(dev, label)` (обычно `bdev_driver()`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Устройство, чей драйвер сменился (`call.c:881`).  |  |
| `grant` | `cp_grant_id_t` | 8 / 8 | Прямой грант `CPF_READ` на метку драйвера в DS (`call.c:882`).  |  |
| `path_len` | `size_t` | 12 / 16 | Длина метки с `\0` (≤ `DS_MAX_KEYLEN`) (`call.c:883`).  | Р |
| `data` | `__uint8_t[40]` | 16 / 24 | Заполнитель; не используется.  | В |

Замечания: поле называется `path_len`, но несёт длину метки DS.

#### `mess_vfs_fs_newnode` — ipc.h:2082

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_newnode`.

Направление: VFS → ФС (на практике PFS). Вызовы (`m_type`): `REQ_NEWNODE` (безымянный узел: pipe, cloned cdev, сокет). Отправитель: `req_newnode()` (`minix/servers/vfs/request.c:624`; из `pipe.c:101`, `:172`, `cdev.c:109`, `socket.c:138`). Получатель: `fsdriver_newnode()` (`minix/lib/libfsdriver/call.c:120`). Ответ: `mess_fs_vfs_newnode`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Устройство, связанное с узлом (cloned cdev / сокет) или `NO_DEV` (`call.c:133`).  |  |
| `mode` | `__mode_t` | 8 / 8 | Тип и права узла (`I_NAMED_PIPE`, `S_IFCHR`, `S_IFSOCK`…) (`call.c:130`).  |  |
| `uid` | `__uid_t` | 12 / 12 | Владелец (`call.c:131`).  |  |
| `gid` | `__gid_t` | 16 / 16 | Группа (`call.c:132`).  |  |
| `data` | `__uint8_t[36]` | 20 / 20 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_putnode` — ipc.h:2093

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_putnode`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_PUTNODE` (сбросить ссылки VFS на inode). Отправитель: `req_putnode()` (`minix/servers/vfs/request.c:700`). Получатель: `fsdriver_putnode()` (`minix/lib/libfsdriver/call.c:96`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode (`call.c:102`).  |  |
| `count` | `unsigned int` | 8 / 8 | Сколько ссылок освободить; 0 или > `INT_MAX` → `EINVAL` (`call.c:105`). VFS передаёт `int count`.  |  |
| `data` | `__uint8_t[44]` | 12 / 12 | Заполнитель; не используется.  |  |

#### `mess_vfs_fs_rdlink` — ipc.h:2101

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_rdlink`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_RDLINK`. Отправитель: `req_rdlink_actual()` (`minix/servers/vfs/request.c:717`). Получатель: `fsdriver_rdlink()` (`minix/lib/libfsdriver/call.c:691`). Ответ: `mess_fs_vfs_rdlink`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode симлинка (`call.c:704`).  |  |
| `grant` | `cp_grant_id_t` | 8 / 8 | Грант `CPF_WRITE` на буфер: прямой (буфер VFS при разборе пути) или magic (буфер процесса для readlink) (`request.c:727`).  |  |
| `mem_size` | `size_t` | 12 / 16 | Размер буфера в байтах (`call.c:702`).  | Р |
| `data` | `__uint8_t[40]` | 16 / 24 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_readsuper` — ipc.h:2111

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_readsuper`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_READSUPER` (монтирование; единственный запрос, принимаемый до монтирования, `fsdriver.c:39`). Отправитель: `req_readsuper()` (`minix/servers/vfs/request.c:780`). Получатель: `fsdriver_readsuper()` (`minix/lib/libfsdriver/call.c:12`). Ответ: `mess_fs_vfs_readsuper`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `device` | `dev_t` | 0 / 0 | Монтируемое устройство (`call.c:23`) → `fdr_driver`, `fdr_mount`.  |  |
| `flags` | `__uint32_t` | 8 / 8 | `REQ_RDONLY`, `REQ_ISROOT` (`request.c:805`) → `fdr_mount(dev, flags, …)`.  |  |
| `path_len` | `size_t` | 12 / 16 | Длина метки блочного драйвера с `\0` (≤ `DS_MAX_KEYLEN`) (`call.c:25`).  | Р |
| `grant` | `cp_grant_id_t` | 16 / 24 | Прямой грант `CPF_READ` на метку драйвера (`request.c:799`).  | В |
| `data` | `__uint8_t[36]` | 20 / 28 | Заполнитель; не используется.  | В |

Замечания: как и в `new_driver`, `path_len` — длина метки, не пути.

#### `mess_vfs_fs_readwrite` — ipc.h:2122

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_readwrite`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_READ`, `REQ_WRITE`, `REQ_PEEK` (заполнение кэша VM для mmap файла). Отправитель: `req_readwrite_actual()` (`minix/servers/vfs/request.c:834`), `req_peek()` (`request.c:903`, из `read.c:239`). Получатель: `read_write()` (`minix/lib/libfsdriver/call.c:154`), `fsdriver_peek()` (`call.c:279`). Ответ: `mess_fs_vfs_readwrite`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode файла (`call.c:163`).  |  |
| `seek_pos` | `__off_t` | 8 / 8 | Позиция в файле; без `RES_64BIT` у ФС > `INT_MAX` → `EINVAL` (`request.c:856`); для PEEK VFS паникует, если старшие 32 бита ≠ 0 (`request.c:909`).  |  |
| `grant` | `cp_grant_id_t` | 16 / 16 | READ/WRITE: magic-грант на буфер процесса (`request.c:846`); PEEK: -1 (не используется).  |  |
| `nbytes` | `size_t` | 20 / 24 | Длина передачи в байтах (> `SSIZE_MAX` → `EINVAL`, `call.c:167`); VFS заполняет из `unsigned int`.  | Р |
| `data` | `__uint8_t[32]` | 24 / 32 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_rename` — ipc.h:2133

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_rename`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_RENAME`. Отправитель: `req_rename()` (`minix/servers/vfs/request.c:927`). Получатель: `fsdriver_rename()` (`minix/lib/libfsdriver/call.c:612`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `dir_old` | `ino_t` | 0 / 0 | Inode исходного каталога (`call.c:623`).  |  |
| `dir_new` | `ino_t` | 8 / 8 | Inode целевого каталога (`call.c:626`).  |  |
| `len_old` | `size_t` | 16 / 16 | Длина старого имени с `\0` (`call.c:622`).  | Р |
| `len_new` | `size_t` | 20 / 24 | Длина нового имени с `\0` (`call.c:625`).  | Р |
| `grant_old` | `cp_grant_id_t` | 24 / 32 | Прямой грант `CPF_READ` на старое имя (`request.c:935`).  | В |
| `grant_new` | `cp_grant_id_t` | 28 / 36 | Прямой грант `CPF_READ` на новое имя (`request.c:940`).  | В |
| `data` | `__uint8_t[24]` | 32 / 40 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_slink` — ipc.h:2146

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_slink`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_SLINK`. Отправитель: `req_slink_actual()` (`minix/servers/vfs/request.c:996`). Получатель: `fsdriver_slink()` (`minix/lib/libfsdriver/call.c:652`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode каталога, где создаётся симлинк (`call.c:666`).  |  |
| `path_len` | `size_t` | 8 / 8 | Длина имени симлинка с `\0` (≤ `NAME_MAX+1`) (`call.c:665`).  | Р |
| `mem_size` | `size_t` | 12 / 16 | Длина содержимого симлинка (целевого пути) в буфере процесса (`call.c:682`).  | Р |
| `grant_path` | `cp_grant_id_t` | 16 / 24 | Прямой грант `CPF_READ` на имя симлинка (`request.c:1014`).  | В |
| `grant_target` | `cp_grant_id_t` | 20 / 28 | Magic-грант `CPF_READ` на целевой путь в памяти процесса (`request.c:1018`) → `data.grant` (`call.c:681`).  | В |
| `uid` | `__uid_t` | 24 / 32 | Владелец симлинка (`call.c:667`).  | В |
| `gid` | `__gid_t` | 28 / 36 | Группа (`call.c:668`).  | В |
| `data` | `__uint8_t[24]` | 32 / 40 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_stat` — ipc.h:2160

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_stat`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_STAT`. Отправитель: `req_stat_actual()` (`minix/servers/vfs/request.c:1080`). Получатель: `fsdriver_stat()` (`minix/lib/libfsdriver/call.c:718`). Ответ: только `m_type`; данные — `struct stat` по гранту.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode файла (`call.c:726`).  |  |
| `grant` | `cp_grant_id_t` | 8 / 8 | Грант `CPF_WRITE` на `struct stat` в памяти процесса/VFS; ФС делает `sys_safecopyto(… sizeof(struct stat))` (`call.c:739`).  |  |
| `data` | `__uint8_t[44]` | 12 / 12 | Заполнитель; не используется.  |  |

Замечания: по гранту передаётся `struct stat` целиком — её раскладка (а не только сообщение) становится частью ABI VFS↔ФС и должна совпадать у обеих сторон.

#### `mess_vfs_fs_statvfs` — ipc.h:2169

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_statvfs`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_STATVFS`. Отправитель: `req_statvfs()` (`minix/servers/vfs/request.c:235`). Получатель: `fsdriver_statvfs()` (`minix/lib/libfsdriver/call.c:835`). Ответ: только `m_type`; данные — `struct statvfs` по гранту.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `grant` | `cp_grant_id_t` | 0 / 0 | Прямой грант `CPF_WRITE` на `struct statvfs` в VFS (`call.c:849`).  |  |
| `data` | `__uint8_t[52]` | 4 / 4 | Заполнитель; не используется.  |  |

Замечания: `struct statvfs` содержит `unsigned long` поля — её размер зависит от ABI, хотя само сообщение — нет.

#### `mess_vfs_fs_unlink` — ipc.h:2176

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_fs_unlink`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_UNLINK`, `REQ_RMDIR`. Отправитель: `req_unlink()` (`minix/servers/vfs/request.c:1150`), `req_rmdir()` (`request.c:967`). Получатель: `fsdriver_unlink()` (`minix/lib/libfsdriver/call.c:549`), `fsdriver_rmdir()` (`call.c:579`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode родительского каталога (`call.c:560`, `:590`).  |  |
| `grant` | `cp_grant_id_t` | 8 / 8 | Прямой грант `CPF_READ` на удаляемое имя (`call.c:558`).  |  |
| `path_len` | `size_t` | 12 / 16 | Длина имени с `\0` (`call.c:559`).  | Р |
| `data` | `__uint8_t[40]` | 16 / 24 | Заполнитель; не используется.  | В |

#### `mess_vfs_fs_utime` — ipc.h:2186

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_fs_utime`.

Направление: VFS → ФС. Вызовы (`m_type`): `REQ_UTIME`. Отправитель: `req_utime()` (`minix/servers/vfs/request.c:1195`). Получатель: `fsdriver_utime()` (`minix/lib/libfsdriver/call.c:796`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `inode` | `ino_t` | 0 / 0 | Inode файла (`call.c:802`).  |  |
| `actime` | `time_t` | 8 / 8 | Время доступа, секунды (`tv_sec`) (`call.c:803`). `time_t` в NetBSD-8 — 64 бита на всех платформах.  |  |
| `modtime` | `time_t` | 16 / 16 | Время модификации, секунды (`call.c:805`).  |  |
| `acnsec` | `__uint32_t` | 24 / 24 | Наносекунды доступа или `UTIME_NOW`/`UTIME_OMIT` (`call.c:804`).  |  |
| `modnsec` | `__uint32_t` | 28 / 28 | Наносекунды модификации или `UTIME_NOW`/`UTIME_OMIT` (`call.c:806`).  |  |
| `data` | `__uint8_t[24]` | 32 / 32 | Заполнитель; не используется.  |  |

Замечания: `tv_nsec` (`long`) сужается до `uint32_t` — безопасно (значения < 2^30).

#### `mess_vfs_lsockdriver_addr` — ipc.h:2260

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lsockdriver_addr`.

Направление: VFS → драйвер сокетов (`asynsend3(AMF_NOREPLY)`). Вызовы (`m_type`): `SDEV_BIND`, `SDEV_CONNECT`, `SDEV_ACCEPT`. Отправитель: `sdev_bindconn()` (`minix/servers/vfs/sdev.c:179`), `sdev_accept()` (`sdev.c:292`). Получатель: `do_bind_connect()` (`minix/lib/libsockdriver/sockdriver.c:535`), `do_accept()` (`sockdriver.c:609`). Ответ: `mess_lsockdriver_vfs_reply` (BIND/CONNECT), `mess_lsockdriver_vfs_accept_reply` (ACCEPT).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | `sockreq_t`: VFS кладёт endpoint процесса (`(sockid_t)who_e`, `sdev.c:200`); эхо в ответе.  |  |
| `sock_id` | `__int32_t` | 4 / 4 | `sockid_t` сокета у драйвера (`sockdriver.c:551`).  |  |
| `grant` | `cp_grant_id_t` | 8 / 8 | BIND/CONNECT: magic-грант `CPF_READ` на `sockaddr` процесса; ACCEPT: грант `CPF_WRITE` на буфер адреса пира или `GRANT_INVALID` (`sockdriver.c:552`, `:622`).  |  |
| `len` | `unsigned int` | 12 / 12 | Длина `sockaddr` / ёмкость буфера адреса (`socklen_t`); BIND/CONNECT: 0 или > `SOCKADDR_MAX` → `EINVAL` (`sockdriver.c:566`).  |  |
| `user_endpt` | `endpoint_t` | 16 / 16 | Endpoint процесса-пользователя (для учёта прав/кред драйвером) (`sockdriver.c:554`).  |  |
| `sflags` | `int` | 20 / 20 | `SDEV_NONBLOCK` (из `O_NONBLOCK` filp) — запрет приостановки (`sockdriver.c:574`).  |  |

#### `mess_vfs_lsockdriver_getset` — ipc.h:2272

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lsockdriver_getset`.

Направление: VFS → драйвер сокетов (через `sdev_sendrec`, поток VFS ждёт ответ). Вызовы (`m_type`): `SDEV_SETSOCKOPT`, `SDEV_GETSOCKOPT`, `SDEV_GETSOCKNAME`, `SDEV_GETPEERNAME`. Отправитель: `sdev_setsockopt()` (`minix/servers/vfs/sdev.c:454`), `sdev_get()` (`sdev.c:504`). Получатель: `do_setsockopt()`, `do_getsockopt()`, `do_getname()` (`minix/lib/libsockdriver/sockdriver.c:814`, `:840`, `:881`). Ответ: `mess_lsockdriver_vfs_reply` (статус = длина результата для GET*).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Endpoint процесса как `sockreq_t` (`sdev.c:474`).  |  |
| `sock_id` | `__int32_t` | 4 / 4 | `sockid_t` сокета.  |  |
| `level` | `int` | 8 / 8 | SET/GETSOCKOPT: уровень опции (`SOL_SOCKET`, `IPPROTO_*`); GETSOCKNAME/GETPEERNAME — 0, не используется (`sockdriver.c:894`).  |  |
| `name` | `int` | 12 / 12 | SET/GETSOCKOPT: имя опции (`SO_*`, …); для GET*NAME не используется.  |  |
| `grant` | `cp_grant_id_t` | 16 / 16 | Magic-грант на значение опции (`CPF_READ` для SET, `CPF_WRITE` для GET) или буфер адреса.  |  |
| `len` | `unsigned int` | 20 / 20 | Длина значения опции / ёмкость буфера (`socklen_t`) (`sockdriver.c:822`, `:898`).  |  |

#### `mess_vfs_lsockdriver_ioctl` — ipc.h:2284

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_lsockdriver_ioctl`.

Направление: VFS → драйвер сокетов (`asynsend3`, процесс приостанавливается). Вызовы (`m_type`): `SDEV_IOCTL`. Отправитель: `sdev_ioctl()` (`minix/servers/vfs/sdev.c:417`). Получатель: `do_ioctl()` (`minix/lib/libsockdriver/sockdriver.c:773`). Ответ: `mess_lsockdriver_vfs_reply`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Endpoint процесса как `sockreq_t` (`sdev.c:434`).  |  |
| `sock_id` | `__int32_t` | 4 / 4 | `sockid_t` сокета.  |  |
| `request` | `unsigned long` | 8 / 8 | Код ioctl; размер аргумента извлекается из кода (`_MINIX_IOCTL_SIZE[_BIG]`, `sockdriver.c:793`).  | С |
| `grant` | `cp_grant_id_t` | 12 / 16 | Грант на аргумент ioctl (`make_ioctl_grant`, права по `_IOR/_IOW`) или `GRANT_INVALID` (`sockdriver.c:792`).  | В |
| `user_endpt` | `endpoint_t` | 16 / 20 | Endpoint процесса-пользователя (`sockdriver.c:788`).  | В |
| `sflags` | `int` | 20 / 24 | `SDEV_NONBLOCK` (`sockdriver.c:801`).  | В |

Замечания: перенос `request` в конец (или фиксация 32 бит) убрал бы смещения.

#### `mess_vfs_lsockdriver_select` — ipc.h:2296

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lsockdriver_select`.

Направление: VFS → драйвер сокетов (`asynsend3`). Вызовы (`m_type`): `SDEV_SELECT`. Отправитель: `sdev_select()` (`minix/servers/vfs/sdev.c:647`). Получатель: `do_select()` (`minix/lib/libsockdriver/sockdriver.c:1019`). Ответ: `mess_lsockdriver_vfs_select_reply` (`SDEV_SELECT1_REPLY`, позже `SDEV_SELECT2_REPLY`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `sock_id` | `__int32_t` | 0 / 0 | `sockid_t` сокета (`sockdriver.c:1027`).  |  |
| `ops` | `int` | 4 / 4 | Запрошенные операции `SDEV_OP_RD/WR/ERR` + `SDEV_NOTIFY` (ждать отложенного уведомления) (`sockdriver.c:1032`).  |  |

#### `mess_vfs_lsockdriver_sendrecv` — ipc.h:2304

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vfs_lsockdriver_sendrecv`.

Направление: VFS → драйвер сокетов (`asynsend3`, процесс приостанавливается). Вызовы (`m_type`): `SDEV_SEND`, `SDEV_RECV`. Отправитель: `sdev_readwrite()` (`minix/servers/vfs/sdev.c:340`). Получатель: `do_send()` (`minix/lib/libsockdriver/sockdriver.c:649`), `do_recv()` (`sockdriver.c:717`). Ответ: `mess_lsockdriver_vfs_reply` (SEND), `mess_lsockdriver_vfs_recv_reply` (RECV).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Endpoint процесса как `sockreq_t` (`sdev.c:389`).  |  |
| `sock_id` | `__int32_t` | 4 / 4 | `sockid_t` сокета.  |  |
| `data_grant` | `cp_grant_id_t` | 8 / 8 | Magic-грант на буфер данных процесса (`CPF_READ` для SEND, `CPF_WRITE` для RECV) или `GRANT_INVALID` (`sdev.c:366`).  |  |
| `data_len` | `size_t` | 12 / 16 | Длина данных в байтах; драйвер урезает до `INT_MAX` (`sockdriver.c:669`, `:740`).  | Р |
| `ctl_grant` | `cp_grant_id_t` | 16 / 24 | Грант на управляющие данные (`msg_control`) или `GRANT_INVALID`.  | В |
| `ctl_len` | `unsigned int` | 20 / 28 | Длина управляющих данных (`socklen_t`).  | В |
| `addr_grant` | `cp_grant_id_t` | 24 / 32 | Грант на адрес назначения (SEND, `CPF_READ`) / источника (RECV, `CPF_WRITE`) или `GRANT_INVALID`.  | В |
| `addr_len` | `unsigned int` | 28 / 36 | Длина адреса / ёмкость буфера адреса (`socklen_t`; SEND: ≤ `SOCKADDR_MAX`, `sockdriver.c:687`).  | В |
| `user_endpt` | `endpoint_t` | 32 / 40 | Endpoint процесса-пользователя.  | В |
| `flags` | `int` | 36 / 44 | Флаги `MSG_*` пользователя; VFS добавляет `MSG_DONTWAIT` при `O_NONBLOCK` и `MSG_NOSIGNAL` при `O_NOSIGPIPE` (`sdev.c:400`, `:402`); `MSG_DONTWAIT` запрещает приостановку.  | В |

Замечания: единственное `size_t` поле (`data_len`) драйвер всё равно режет до `INT_MAX`, т.к. ответ — `int status`; его можно сделать `uint32_t` без потери функциональности.

#### `mess_vfs_lsockdriver_simple` — ipc.h:2320

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lsockdriver_simple`.

Направление: VFS → драйвер сокетов. Вызовы (`m_type`): `SDEV_LISTEN`, `SDEV_SHUTDOWN`, `SDEV_CLOSE`, `SDEV_CANCEL`. Отправитель: `sdev_simple()` (`minix/servers/vfs/sdev.c:242`), `sdev_close()` (`sdev.c:604`), `sdev_cancel()` (`sdev.c:940`). Получатель: `do_listen()`, `do_shutdown()`, `do_close()`, `do_cancel()` (`minix/lib/libsockdriver/sockdriver.c:588`, `:939`, `:962`, `:993`). Ответ: `mess_lsockdriver_vfs_reply` (CANCEL собственного ответа не имеет — ответ идёт на отменённый запрос).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Endpoint процесса как `sockreq_t`; для CANCEL — идентификатор отменяемого запроса (`sockdriver.c:999`).  |  |
| `sock_id` | `__int32_t` | 4 / 4 | `sockid_t` сокета.  |  |
| `param` | `int` | 8 / 8 | Зависит от вызова: LISTEN — backlog (`sockdriver.c:595`); SHUTDOWN — `how` (`SHUT_RD/WR/RDWR`); CLOSE — `SDEV_NONBLOCK` (`sockdriver.c:971`); CANCEL — не используется (0).  |  |

#### `mess_vfs_lsockdriver_socket` — ipc.h:2329

Размер: i386 56, x86_64 56. Член объединения: `m_vfs_lsockdriver_socket`.

Направление: VFS → драйвер сокетов (через `sdev_sendrec`). Вызовы (`m_type`): `SDEV_SOCKET`, `SDEV_SOCKETPAIR`. Отправитель: `sdev_socket()` (`minix/servers/vfs/sdev.c:132`). Получатель: `do_socket()`, `do_socketpair()` (`minix/lib/libsockdriver/sockdriver.c:479`, `:501`). Ответ: `mess_lsockdriver_vfs_socket_reply`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `req_id` | `__int32_t` | 0 / 0 | Endpoint процесса как `sockreq_t` (`sdev.c:133`).  |  |
| `domain` | `int` | 4 / 4 | Домен (`PF_INET`, `PF_UNIX`, …) (`sockdriver.c:485`).  |  |
| `type` | `int` | 8 / 8 | Тип сокета (`SOCK_STREAM`, …, без `SOCK_NONBLOCK`/`SOCK_CLOEXEC` — VFS снимает их, `minix/servers/vfs/socket.c:207`).  |  |
| `protocol` | `int` | 12 / 12 | Протокол.  |  |
| `user_endpt` | `endpoint_t` | 16 / 16 | Endpoint процесса-создателя (`sockdriver.c:488`).  |  |

## 9. Службы и драйверы: VM, RS, DS, PCI, TTY, ввод, I2C, сеть, USB, VirtualBox

#### `mess_ds_reply` — ipc.h:100

Размер: i386 56, x86_64 56. Член объединения: `m_ds_reply`.

Направление: DS → libsys (ответ). Вызовы (`m_type`): ответ на `DS_RETRIEVE` (в `m_type` — код результата). Отправитель: `do_retrieve()` (`minix/servers/ds/store.c:383`), ответ уходит тем же буфером (`minix/servers/ds/main.c:83`). Получатель: `ds_retrieve_label_endpt()`, `ds_retrieve_u32()`, `ds_retrieve_raw()` (`minix/lib/libsys/ds.c:103-148`). Ответ на `DS_CHECK` идёт в раскладке **`mess_ds_req`**, а не этой (см. ниже).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `val_out` | `union ds_val` | 0 / 0 | Значение записи для типов `DSF_TYPE_U32`/`DSF_TYPE_LABEL` (заполняет DS). Перекрывает поле `key_grant` запроса — DS отвечает тем же сообщением. |  |
| ↳ `grant` | `cp_grant_id_t` | 0 / 0 | В ответе не используется (для STR/MEM данные копируются по гранту запроса `val_in.grant`). |  |
| ↳ `u32` | `__uint32_t` | 0 / 0 | Значение записи `DSF_TYPE_U32` (`store.c:405`) → `*value` в `ds.c:123`. |  |
| ↳ `ep` | `endpoint_t` | 0 / 0 | Endpoint, опубликованный под меткой `DSF_TYPE_LABEL` (`store.c:408`) → `ds.c:111`. |  |
| `val_len` | `int` | 4 / 4 | Для STR/MEM — сколько байт фактически скопировано в буфер клиента = MIN(запрошено, длина записи) (`store.c:412,420`) → `*length` (`ds.c:144`). Перекрывает `key_len` запроса. Длина буфера памяти в `int`. | У |

Замечания: ответ строится «на месте» в буфере запроса, поэтому раскладки `mess_ds_reply` и `mess_ds_req` обязаны совпадать по смещениям 0/4 по смыслу; при переработке лучше сделать один тип запроса/ответа DS.

#### `mess_ds_req` — ipc.h:107

Размер: i386 56, x86_64 56. Член объединения: `m_ds_req`.

Направление: libsys → DS (запрос; для `DS_CHECK` — также ответ DS → libsys). Вызовы (`m_type`): `DS_PUBLISH`, `DS_RETRIEVE`, `DS_RETRIEVE_LABEL`, `DS_DELETE`, `DS_SUBSCRIBE`, `DS_CHECK` (`DS_SNAPSHOT` объявлен в `com.h`, не реализован; `DS_GETSYSINFO` идёт в другой раскладке). Отправитель: `do_invoke_ds()` и обёртки `ds_publish_*`, `ds_retrieve_*`, `ds_delete_*`, `ds_subscribe`, `ds_check` (`minix/lib/libsys/ds.c`). Получатель: `do_publish/do_retrieve/do_retrieve_label/do_delete/do_subscribe/do_check` (`minix/servers/ds/store.c`), диспетчер `minix/servers/ds/main.c:52-76`. Ответ: тот же буфер, `m_type` = результат; для `DS_RETRIEVE` — поля `mess_ds_reply`; для `DS_CHECK` — поля `flags`/`owner` этого же типа.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `key_grant` | `cp_grant_id_t` | 0 / 0 | Грант на ключ (имя записи / регулярное выражение для SUBSCRIBE), создаёт `do_invoke_ds()` (`ds.c:22-27`): `CPF_READ` для обычных вызовов; `CPF_WRITE` на буфер `DS_MAX_KEYLEN` для `DS_CHECK`/`DS_RETRIEVE_LABEL` — тогда DS **пишет** туда имя ключа (`store.c:442,561`). Читается `get_key_name()` (`store.c:170`). |  |
| `key_len` | `int` | 4 / 4 | Длина ключа с `'\0'` (или `DS_MAX_KEYLEN` для CHECK/RETRIEVE_LABEL); DS проверяет 2..`DS_MAX_KEYLEN` (`store.c:163`). |  |
| `flags` | `int` | 8 / 8 | Запрос: `DSF_TYPE_U32/STR/MEM/LABEL` (тип записи, маска `DSF_MASK_TYPE`), для PUBLISH дополнительно `DSF_OVERWRITE`, `DSF_PRIV_*`; для SUBSCRIBE — маска типов (0 = все) + `DSF_OVERWRITE`, `DSF_INITIAL` (`store.c:292,473,501,511`). Ответ `DS_CHECK`: тип изменившейся записи (`store.c:571` → `ds.c:216`). |  |
| `val_in` | `union ds_val` | 12 / 12 | Значение для PUBLISH / параметр поиска. |  |
| ↳ `grant` | `cp_grant_id_t` | 12 / 12 | STR/MEM: грант на буфер значения — `CPF_READ` для PUBLISH (`ds.c:64`, `store.c:352`), `CPF_WRITE` для RETRIEVE (`ds.c:135`, `store.c:413`). |  |
| ↳ `u32` | `__uint32_t` | 12 / 12 | Значение для `ds_publish_u32` (`ds.c:51`, `store.c:331`). |  |
| ↳ `ep` | `endpoint_t` | 12 / 12 | PUBLISH LABEL: публикуемый endpoint (только от RS, `store.c:302,334`); RETRIEVE_LABEL: endpoint, чьё имя ищется (`store.c:438`). |  |
| `val_len` | `int` | 16 / 16 | STR/MEM: длина буфера значения в байтах (PUBLISH — размер данных, `store.c:338`; RETRIEVE — ёмкость буфера клиента, `store.c:412`). Приходит из `size_t` с обрезкой. | У |
| `owner` | `endpoint_t` | 20 / 20 | Только ответ `DS_CHECK`: endpoint владельца изменившейся записи (`store.c:572` → `ds.c:217`). В запросе не используется. |  |

Замечания: `ds_check()` читает ответ через `m_ds_req`, а `ds_retrieve_*` — через `m_ds_reply`; одна операция DS — два «взгляда» на один буфер.

#### `mess_i2c_li2cdriver_busc_i2c_exec` — ipc.h:223

Размер: i386 56, x86_64 56. Член объединения: `m_i2c_li2cdriver_busc_i2c_exec`.

Направление: шина I2C → libi2cdriver (ответ). Вызовы (`m_type`): ответ на `BUSC_I2C_EXEC`. Отправитель: `i2c_other()` (`minix/drivers/bus/i2c/i2c.c:356-366`) — шлёт обнулённое сообщение только с `m_type` = результат. Получатель: `i2cdriver_exec()` (`minix/lib/libi2cdriver/i2cdriver.c:187-194`) читает лишь `m_type`. **Не используется**: тип-заглушка (только `padding[56]`), к члену объединения никто не обращается; результат операции возвращается через грант запроса.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|

#### `mess_i2c_li2cdriver_busc_i2c_reserve` — ipc.h:228

Размер: i386 56, x86_64 56. Член объединения: `m_i2c_li2cdriver_busc_i2c_reserve`.

Направление: шина I2C → libi2cdriver (ответ). Вызовы (`m_type`): ответ на `BUSC_I2C_RESERVE` (`OK`, `EBUSY`, `EINVAL`…). Отправитель: `i2c_other()` (`minix/drivers/bus/i2c/i2c.c:356-366`). Получатель: `i2cdriver_reserve_device()` (`minix/lib/libi2cdriver/i2cdriver.c:164-169`), читает только `m_type`. **Не используется**: тип-заглушка без полей, к члену объединения не обращаются.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|

#### `mess_input_linputdriver_input_conf` — ipc.h:233

Размер: i386 56, x86_64 56. Член объединения: `m_input_linputdriver_input_conf`.

Направление: сервер INPUT → драйвер ввода (libinputdriver), одностороннее (asynsend, `AMF_NOREPLY`). Вызовы (`m_type`): `INPUT_CONF`. Отправитель: `minix/servers/input/input.c:516-522` (при регистрации драйвера по событию DS). Получатель: `do_conf()` (`minix/lib/libinputdriver/inputdriver.c:83-112`), отвергает отправителя, не совпадающего с меткой `"input"` в DS. Ответа нет.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `kbd_id` | `int` | 0 / 0 | ID устройства «клавиатура» (индекс в `devs[]` сервера INPUT) или `INVALID_INPUT_ID` (-1); драйвер ставит его в `INPUT_EVENT.id` (`inputdriver.c:105`, `:52`). |  |
| `mouse_id` | `int` | 4 / 4 | То же для мыши (`inputdriver.c:106`). Оба -1 → драйвер отключён (`:109`). |  |
| `rsvd1_id` | `int` | 8 / 8 | Резерв (джойстик?), всегда `INVALID_INPUT_ID` (`input.c:519`); драйвер не читает. |  |
| `rsvd2_id` | `int` | 12 / 12 | Резерв, всегда `INVALID_INPUT_ID` (`input.c:520`); драйвер не читает. |  |

#### `mess_input_linputdriver_setleds` — ipc.h:243

Размер: i386 56, x86_64 56. Член объединения: `m_input_linputdriver_setleds`.

Направление: (1) INPUT → драйвер ввода; (2) TTY → INPUT. Одностороннее. Вызовы (`m_type`): `INPUT_SETLEDS`. Отправители: `input_set_leds()` (`minix/servers/input/input.c:214-215`, рассылает всем клавиатурам); `set_leds()` TTY (`minix/drivers/tty/tty/arch/i386/keyboard.c:380-381`, asynsend в INPUT). Получатели: `do_setleds()` (`minix/lib/libinputdriver/inputdriver.c:131`) → `idr_leds()`; сервер INPUT (`input.c:630-632`, принимает только от `TTY_PROC_NR` и вызывает `input_set_leds(KBDMUX_MINOR, …)`). Ответа нет.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `led_mask` | `__uint32_t` | 0 / 0 | Битовая маска светодиодов: бит `1 << INPUT_LED_NUMLOCK/CAPSLOCK/SCROLLLOCK` (`minix/include/minix/input.h:293`); TTY отправляет `locks[ccurrent] & ~ALT_LOCK`. |  |

Замечания: имя типа описывает только направление INPUT→драйвер; путь TTY→INPUT использует тот же тип.

#### `mess_input_tty_event` — ipc.h:250

Размер: i386 56, x86_64 56. Член объединения: `m_input_tty_event`.

Направление: INPUT → TTY, одностороннее (`ipc_send`). Вызовы (`m_type`): `TTY_INPUT_EVENT`. Отправитель: `input_event()` (`minix/servers/input/input.c:410-416`) — пересылает событие драйвера, если ни устройство, ни мультиплексор не открыты. Получатель: `minix/drivers/tty/tty/arch/i386/keyboard.c:148-170` (только i386; проверяет `m_source == input_endpt`). Ответа нет.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `int` | 0 / 0 | ID исходного устройства (копия `INPUT_EVENT.id`); TTY не читает. |  |
| `page` | `int` | 4 / 4 | HID usage page (`INPUT_PAGE_*`); TTY обрабатывает только `INPUT_PAGE_KEY` (`keyboard.c:155`). |  |
| `code` | `int` | 8 / 8 | HID usage code (код клавиши 00h–E7h); TTY использует как скан-код (`keyboard.c:159`). |  |
| `value` | `int` | 12 / 12 | Значение: `INPUT_PRESS`(1)/`INPUT_RELEASE`(0) для клавиш (`keyboard.c:164`), для осей — смещение/координата. |  |
| `flags` | `int` | 16 / 16 | `INPUT_FLAG_ABS`/`INPUT_FLAG_REL`; TTY не читает. |  |

Замечания: раскладка совпадает с `mess_linputdriver_input_event` (поле в поле) — кандидат на объединение в один тип.

#### `mess_li2cdriver_i2c_busc_i2c_exec` — ipc.h:975

Размер: i386 56, x86_64 56. Член объединения: `m_li2cdriver_i2c_busc_i2c_exec`.

Направление: libi2cdriver (драйвер устройства I2C) → драйвер шины I2C. Вызовы (`m_type`): `BUSC_I2C_EXEC`. Отправитель: `i2cdriver_exec()` (`minix/lib/libi2cdriver/i2cdriver.c:178-185`). Получатель: `i2c_other()` → `do_i2c_ioctl_exec()` (`minix/drivers/bus/i2c/i2c.c:343`, `:257-300`). Ответ: только `m_type` (тип `mess_i2c_li2cdriver_busc_i2c_exec`, пустой).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `grant` | `cp_grant_id_t` | 0 / 0 | Грант `CPF_READ\|CPF_WRITE` на структуру `minix_i2c_ioctl_exec_t` клиента (`minix/include/minix/i2c.h:16`): шина читает её (`i2c.c:263`), проверяет резервирование `iie_addr`, выполняет и пишет результат обратно (`i2c.c:290`). |  |

Замечания: `minix_i2c_ioctl_exec_t` содержит `size_t` — раскладка буфера зависит от разрядности, но обе стороны на одной платформе.

#### `mess_li2cdriver_i2c_busc_i2c_reserve` — ipc.h:982

Размер: i386 56, x86_64 56. Член объединения: `m_li2cdriver_i2c_busc_i2c_reserve`.

Направление: libi2cdriver → драйвер шины I2C. Вызовы (`m_type`): `BUSC_I2C_RESERVE`. Отправитель: `i2cdriver_reserve_device()` (`minix/lib/libi2cdriver/i2cdriver.c:161-162`; повторно при перезапуске шины, `:148`). Получатель: `do_reserve()` (`minix/drivers/bus/i2c/i2c.c:339`, `:106-140`). Ответ: только `m_type` (`OK`/`EBUSY`/`EINVAL`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `addr` | `__uint16_t` | 0 / 0 | Адрес ведомого на шине I2C (`i2c_addr_t`, до 10 бит); шина проверяет `0 ≤ addr < NR_I2CDEV (0x3ff)` и закрепляет за меткой DS вызывающего (`i2c.c:123-131`). (исх.: FIXME: strictly speaking this is an i2c_addr_t, but…) |  |

#### `mess_linputdriver_input_event` — ipc.h:992

Размер: i386 56, x86_64 56. Член объединения: `m_linputdriver_input_event`.

Направление: драйвер ввода (libinputdriver) → INPUT, одностороннее (блокирующий `ipc_send`). Вызовы (`m_type`): `INPUT_EVENT`. Отправитель: `inputdriver_send_event()` (`minix/lib/libinputdriver/inputdriver.c:58-63`). Получатель: `input_event()`/`input_process()` (`minix/servers/input/input.c:383`, `:348-352`). Ответа нет.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `int` | 0 / 0 | ID устройства, выданный в `INPUT_CONF` (`kbd_id`/`mouse_id`); сервер использует как индекс `devs[]` и проверяет владельца (`input.c:383-389`). |  |
| `page` | `int` | 4 / 4 | HID usage page (`INPUT_PAGE_*`), из `unsigned short`; в буфер событий как `uint16_t` (`input.c:348`). |  |
| `code` | `int` | 8 / 8 | HID usage code, из `unsigned short` (`input.c:349`). |  |
| `value` | `int` | 12 / 12 | Значение события (нажатие/отпускание, смещение оси) (`input.c:350`). |  |
| `flags` | `int` | 16 / 16 | `INPUT_FLAG_ABS/REL` (`input.c:351`). |  |

#### `mess_lsys_fi_ctl` — ipc.h:1049

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_fi_ctl`.

Направление: RS → любой сервис (SEF), одностороннее (`rs_asynsend`). Вызовы (`m_type`): `COMMON_REQ_FI_CTL` (по команде `RS_FI` от `minix-service fi <label>`). Отправитель: `fi_service()` (`minix/servers/rs/utility.c:74-76`). Получатель: SEF `do_sef_fi_request()` (`minix/lib/libsys/sef_fi.c:15-30`), перехват в `sef.c:253` (только при `INTERCEPT_SEF_FI_REQUESTS`). Ответа нет (при `RS_FI_CRASH` сервис паникует).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `gid` | `cp_grant_id_t` | 0 / 0 | Грант на управляющий запрос внешнего инжектора EDFI (`edfi_ctl_process_request`); в дереве не заполняется и не читается (RS шлёт мусор из стека). |  |
| `size` | `size_t` | 4 / 8 | Размер этого запроса EDFI; в дереве не используется. | Р |
| `subtype` | `int` | 8 / 16 | Подфункция: `RS_FI_CRASH` (1) → `panic("Crash!")` с `sef_controlled_crash` (`sef_fi.c:18`); иначе запрос уходит в EDFI, если тот слинкован, либо `ENOSYS`. | В |

Замечания: `gid`/`size` — интерфейс внешнего EDFI (llvm-apps), в дереве его нет; сообщение RS не обнуляет (`utility.c:72`). Поле `size` можно сделать `uint32_t` или убрать.

#### `mess_lsys_fi_reply` — ipc.h:1058

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_fi_reply`.

Направление: предположительно сервис → RS (ответ на `COMMON_REQ_FI_CTL`). Вызовы (`m_type`): нет. Отправитель: нет. Получатель: нет. **Не используется**: в дереве нет обращений (FI-запросы односторонние); вероятно, предназначался для внешнего EDFI.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `status` | `int` | 0 / 0 | Не используется (код результата FI-запроса, по замыслу). |  |

#### `mess_lsys_pci_busc_get_bar` — ipc.h:1391

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_pci_busc_get_bar`.

Направление: libsys → драйвер шины PCI. Вызовы (`m_type`): `BUSC_PCI_GET_BAR`. Отправитель: `pci_get_bar()` (`minix/lib/libsys/pci_get_bar.c:22-24`). Получатель: `do_get_bar()` (`minix/drivers/bus/pci/main.c:484-485`) → `_pci_get_bar()` (`minix/drivers/bus/pci/pci.c:2444`). Ответ: тип `mess_pci_lsys_busc_get_bar`. Остальной протокол PCI — на общих раскладках, см. таблицу «Протокол PCI» в конце.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `devind` | `int` | 0 / 0 | Индекс устройства в таблице `pcidev[]` драйвера PCI (из `pci_first_dev/next_dev/find_dev`); проверка `0..nr_pcidev-1` (`pci.c:2449`). |  |
| `port` | `int` | 4 / 4 | Смещение регистра BAR в конфигурационном пространстве: `PCI_BAR + 4*n` (0x10…0x24); сравнивается с `PCI_BAR+4*pb_nr` (`pci.c:2454-2456`). | Ж |

#### `mess_lsys_tty_fkey_ctl` — ipc.h:1447

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_tty_fkey_ctl`.

Направление: libsys → TTY. Вызовы (`m_type`): `TTY_FKEY_CONTROL`. Отправитель: `fkey_ctl()` (`minix/lib/libsys/fkey_ctl.c:20-23`; макросы `fkey_map/fkey_unmap/fkey_events`; используют IS, `rtl8139`, `lance`, `dpeth`). Получатель: `do_fkey_ctl()` (`minix/drivers/tty/tty/arch/i386/keyboard.c:429-524`; на earm — заглушка `arch/earm/keyboard.c:7`), диспетчер `tty.c:206`. Ответ: тот же буфер, тип `mess_tty_lsys_fkey_ctl`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `request` | `int` | 0 / 0 | Подфункция: `FKEY_MAP` (10), `FKEY_UNMAP` (11), `FKEY_EVENTS` (12) (`com.h:875-877`; `keyboard.c:438`). |  |
| `fkeys` | `int` | 4 / 4 | Битовая маска F1–F12 (бит i+1 = Fi+1) для MAP/UNMAP; TTY снимает биты успешно обработанных клавиш (`keyboard.c:442-452,481-485`). |  |
| `sfkeys` | `int` | 8 / 8 | То же для Shift+F1–F12 (`keyboard.c:462-468,492-496`). |  |

Замечания: **ошибка раскладки** — для MAP/UNMAP TTY правит поля запроса (смещения 4/8), а libsys читает ответ по смещениям 0/4 (`fkey_ctl.c:24-25`), т.е. `*fkeys` получает код `request`, `*sfkeys` — остаток `fkeys`. Вызывающие эти значения пока игнорируют.

#### `mess_lsys_vm_getref` — ipc.h:1486

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_vm_getref`.

Направление: libc → VM (несмотря на `lsys` в имени, код в `minix/lib/libc/sys/mmap.c`). Вызовы (`m_type`): `VM_GETREF`. Отправитель: `vm_getrefcount()` (`minix/lib/libc/sys/mmap.c:164-170`), используется сервером IPC (`minix/servers/ipc/shm.c:182`). Получатель: `do_get_refcount()` (`minix/servers/vm/mmap.c:471-481`). Ответ: в том же типе (`retc`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Процесс, в адресном пространстве которого ищется страница (`mmap.c:471`, `vm_isokendpt`). |  |
| `addr` | `void *` | 4 / 8 | Виртуальный адрес страницы в АП `endpt` → `map_get_ref()`. | А |
| `retc` | `int` | 8 / 16 | Ответ: счётчик ссылок на физическую страницу (`u8_t`, `mmap.c:481`); клиент приводит к `u8_t`. | В |

#### `mess_lsys_vm_info` — ipc.h:1494

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_vm_info`.

Направление: libsys → VM. Вызовы (`m_type`): `VM_INFO`. Отправитель: `vm_info_stats()`, `vm_info_usage()`, `vm_info_region()` (`minix/lib/libsys/vm_info.c`). Получатель: `do_info()` (`minix/servers/vm/utility.c:100-183`). Ответ: в том же типе (`count`, `next` для `VMIW_REGION`) + данные копируются в буфер `ptr` (`sys_datacopy`, `utility.c:181`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `what` | `int` | 0 / 0 | Подфункция: `VMIW_STATS` (1, `struct vm_stats_info`), `VMIW_USAGE` (2, `struct vm_usage_info`), `VMIW_REGION` (3, массив `struct vm_region_info`) (`utility.c:115`). |  |
| `ep` | `endpoint_t` | 4 / 4 | USAGE/REGION: процесс, о котором спрашивают; USAGE с `ep < 0` — статистика ядра (`utility.c:131`); REGION: `SELF` заменяется на `m_source` (`:143`). |  |
| `count` | `int` | 8 / 8 | REGION: запрос — ёмкость массива (обрезается до `MAX_VRI_COUNT`), ответ — число записанных записей (`utility.c:149,154`; `vm_info.c:56`). |  |
| `ptr` | `void *` | 12 / 16 | Адрес буфера-приёмника в АП вызывающего; VM предварительно обрабатывает страницы (`handle_memory_once`) и копирует (`utility.c:113,175-182`). | А |
| `next` | `vir_bytes` | 16 / 24 | REGION: курсор итерации — виртуальный адрес, с которого продолжить перебор регионов (вход/выход; `region.c:1460,1473`). | А |

#### `mess_lsys_vm_map_phys` — ipc.h:1504

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_vm_map_phys`.

Направление: libsys → VM. Вызовы (`m_type`): `VM_MAP_PHYS`. Отправитель: `vm_map_phys()` (`minix/lib/libsys/vm_map_phys.c:15-29`), драйверы (MMIO, фреймбуфер и т.п.). Получатель: `do_map_phys()` (`minix/servers/vm/mmap.c:320-358`). Ответ: в том же типе (`reply`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `ep` | `endpoint_t` | 0 / 0 | В чьё АП отображать (`SELF` = отправитель) (`mmap.c:320,325`). |  |
| `phaddr` | `phys_bytes` | 4 / 8 | Физический адрес начала (не обязательно выровнен); право проверяется `map_perm_check()` → `sys_privquery_mem` (`mmap.c:337`). VM приводит его к `vir_bytes` (`mmap.c:331`). | Ф |
| `len` | `size_t` | 8 / 16 | Длина в байтах; VM расширяет до границ страниц (`mmap.c:344-348`). | Р |
| `reply` | `void *` | 12 / 24 | Ответ: виртуальный адрес в АП `ep`, соответствующий `phaddr` (с учётом смещения в странице) (`mmap.c:358`). | А |

Замечания: в API libsys физический адрес передаётся как `void *phaddr`, а VM сводит его к `vir_bytes` — при PAE/LPAE (Ф шире указателя) адреса >4 ГБ теряются; `phys_bytes` нужно держать 64-битным на всех платформах.

#### `mess_lsys_vm_rusage` — ipc.h:1513

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_vm_rusage`.

Направление: PM (через libsys) → VM. Вызовы (`m_type`): `VM_GETRUSAGE`. Отправитель: `vm_getrusage()` (`minix/lib/libsys/vm_getrusage.c:12-15`), вызывается PM (`minix/servers/pm/misc.c:441`). Получатель: `do_getrusage()` (`minix/servers/vm/utility.c:430-472`); не от PM — сразу `OK` без действий. Ответ: только `m_type`; данные — в `struct rusage` в памяти PM.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `endpt` | `endpoint_t` | 0 / 0 | Процесс, чья статистика нужна (`utility.c:441`). |  |
| `addr` | `vir_bytes` | 4 / 8 | Адрес `struct rusage` в АП PM; VM читает её, дописывает `ru_maxrss`, `ru_minflt`, `ru_majflt` и пишет обратно (`utility.c:450,470`). | А |
| `children` | `int` | 8 / 16 | 0 — данные самого процесса; ≠0 — по детям (не реализовано, поля не трогаются) (`utility.c:454`). | В |

#### `mess_lsys_vm_unmap_phys` — ipc.h:1522

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_lsys_vm_unmap_phys`.

Направление: libsys → VM. Вызовы (`m_type`): `VM_UNMAP_PHYS`. Отправитель: `vm_unmap_phys()` (`minix/lib/libsys/vm_map_phys.c:39-41`). Получатель: `do_munmap()` (`minix/servers/vm/mmap.c:521,552`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `ep` | `endpoint_t` | 0 / 0 | В чьём АП снимать отображение (`SELF` = отправитель) (`mmap.c:521`). |  |
| `vaddr` | `void *` | 4 / 8 | Виртуальный адрес начала региона (из `vm_map_phys`), должен быть выровнен на страницу; длина берётся из региона (`mmap.c:552-566`). | А |

Замечания: параметр `len` у `vm_unmap_phys()` в сообщение не передаётся.

#### `mess_lsys_vm_update` — ipc.h:1529

Размер: i386 56, x86_64 56. Член объединения: `m_lsys_vm_update`.

Направление: RS (libsys) → VM; также локально внутри VM. Вызовы (`m_type`): `VM_RS_UPDATE`, `VM_RS_PREPARE`. Отправитель: `vm_update()` (`minix/lib/libsys/vm_update.c`), `vm_prepare()` (`minix/lib/libsys/vm_prepare.c`); вызывающие — `minix/servers/rs/update.c:249,345,505`; плюс VM сам строит сообщение и зовёт `do_rs_update()` (`minix/servers/vm/main.c:655-658`). Получатель: `do_rs_update()` (`minix/servers/vm/rs.c:157-159`), `do_rs_prepare()` (`rs.c:87-89`). Ответ: только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `src` | `endpoint_t` | 0 / 0 | Endpoint новой (src) копии процесса при живом обновлении. |  |
| `dst` | `endpoint_t` | 4 / 4 | Endpoint старой (dst) копии; VM меняет местами их состояние (`swap_proc_dyn_data`, `rs.c:194`). |  |
| `flags` | `int` | 8 / 8 | `SF_VM_UPDATE`, `SF_VM_ROLLBACK`, `SF_VM_NOMMAP` (`minix/include/minix/rs.h:197-199`); влияют на копирование mmap-регионов и `SYS_UPD_ROLLBACK` (`rs.c:141,175,184`). |  |

#### `mess_lsys_vm_vmremap` — ipc.h:1537

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_lsys_vm_vmremap`.

Направление: libc → VM. Вызовы (`m_type`): `VM_REMAP`, `VM_REMAP_RO`. Отправитель: `vm_remap()`, `vm_remap_ro()` (`minix/lib/libc/sys/mmap.c:88-130`), используется сервером IPC (`minix/servers/ipc/shm.c:159`). Получатель: `do_remap()` (`minix/servers/vm/mmap.c:383-432`). Ответ: в том же типе (`ret_addr`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `destination` | `endpoint_t` | 0 / 0 | Процесс, в АП которого создаётся разделяемое отображение (`mmap.c:389`). |  |
| `source` | `endpoint_t` | 4 / 4 | Процесс-владелец исходного региона (`mmap.c:391`). |  |
| `dest_addr` | `void *` | 8 / 8 | Желаемый адрес в АП `destination`; 0 — любой в `VM_MMAPBASE..VM_MMAPTOP` (`mmap.c:416-420`). | А |
| `src_addr` | `void *` | 12 / 16 | Адрес **начала** региона в АП `source` (иначе `EFAULT`, `mmap.c:399`). | А |
| `size` | `size_t` | 16 / 24 | Размер в байтах; после округления до страницы должен совпасть с длиной региона (`mmap.c:405-410`). | Р |
| `ret_addr` | `void *` | 20 / 32 | Ответ: адрес нового отображения в АП `destination` (`mmap.c:431`). | А |

#### `mess_ndev_netdriver_init` — ipc.h:1595

Размер: i386 56, x86_64 56. Член объединения: `m_ndev_netdriver_init`.

Направление: стек TCP/IP lwip (ndev) → сетевой драйвер (libnetdriver), asynsend. Вызовы (`m_type`): `NDEV_INIT`. Отправитель: `minix/net/lwip/ndev.c:348-349`. Получатель: `do_init()` libnetdriver (`minix/lib/libnetdriver/netdriver.c:707-750`) — сбрасывает очереди, запоминает `status_endpt = m_source`. Ответ: `NDEV_INIT_REPLY` (`mess_netdriver_ndev_init_reply`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `__uint32_t` | 0 / 0 | Порядковый номер запроса (текущая голова очереди отправки `ndev_sendq.nq_head`); драйвер возвращает его в ответе (`netdriver.c:733`), ndev сверяет (`ndev.c:529`). |  |

#### `mess_ndev_netdriver_conf` — ipc.h:1602

Размер: i386 56, x86_64 56. Член объединения: `m_ndev_netdriver_conf`.

Направление: lwip ndev → сетевой драйвер. Вызовы (`m_type`): `NDEV_CONF`. Отправитель: `ndev_conf()` (`minix/net/lwip/ndev.c:661-701`). Получатель: `do_conf()` (`minix/lib/libnetdriver/netdriver.c:434-506`). Ответ: `NDEV_CONF_REPLY` (`mess_netdriver_ndev_reply`, всегда `OK`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `__uint32_t` | 0 / 0 | Порядковый номер запроса (seq), возвращается в ответе (`netdriver.c:494`). |  |
| `set` | `__uint32_t` | 4 / 4 | Какие параметры менять: `NDEV_SET_MODE/CAPS/FLAGS/MEDIA/HWADDR` (`com.h:1111-1115`). |  |
| `mode` | `__uint32_t` | 8 / 8 | При `SET_MODE`: `NDEV_MODE_DOWN/UP/BCAST/MCAST_LIST/MCAST_ALL/PROMISC` (`com.h:1118-1123`). |  |
| `mcast_grant` | `cp_grant_id_t` | 12 / 12 | `CPF_READ`-грант на массив `netdriver_addr_t` мультикаст-адресов (при `NDEV_MODE_MCAST_LIST`), иначе `GRANT_INVALID` (`ndev.c:674-687`, `netdriver.c:476-479`). |  |
| `mcast_count` | `unsigned int` | 16 / 16 | Число адресов в списке; если больше `NETDRIVER_MCAST_MAX`, драйвер переходит в `MCAST_ALL` (`netdriver.c:466-471`). |  |
| `caps` | `__uint32_t` | 20 / 20 | При `SET_CAPS`: `NDEV_CAP_CS_*` (аппаратные контрольные суммы) (`netdriver.c:456`). |  |
| `flags` | `__uint32_t` | 24 / 24 | При `SET_FLAGS`: `NDEV_FLAG_DEBUG/LINK0/1/2` (`netdriver.c:459`). |  |
| `media` | `__uint32_t` | 28 / 28 | При `SET_MEDIA`: слово медиа в формате `IFM_*` (`netdriver.c:462`). |  |
| `hwaddr` | `__uint8_t[6]` | 32 / 32 | При `SET_HWADDR`: новый MAC-адрес (`netdriver.c:466`). |  |

#### `mess_ndev_netdriver_transfer` — ipc.h:1617

Размер: i386 56, x86_64 56. Член объединения: `m_ndev_netdriver_transfer`.

Направление: lwip ndev → сетевой драйвер, asynsend. Вызовы (`m_type`): `NDEV_SEND`, `NDEV_RECV`. Отправитель: `ndev_transfer()` (`minix/net/lwip/ndev.c:751-781`). Получатель: `do_transfer()` (`minix/lib/libnetdriver/netdriver.c:356-420`). Ответ (асинхронно, по завершении): `NDEV_SEND_REPLY`/`NDEV_RECV_REPLY` (`mess_netdriver_ndev_reply`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `__uint32_t` | 0 / 0 | Порядковый номер запроса (seq), возвращается в ответе. |  |
| `count` | `__uint32_t` | 4 / 4 | Число сегментов вектора, 1..`NDEV_IOV_MAX` (8) (`netdriver.c:383-386`). |  |
| `grant` | `cp_grant_id_t[8]` | 8 / 8 | Гранты на сегменты pbuf-цепочки: `CPF_READ` для SEND, `CPF_WRITE` для RECV (`ndev.c:762-771`). |  |
| `len` | `__uint16_t[8]` | 40 / 40 | Длины сегментов в байтах (≤ 65535); сумма проверяется на `NDEV_ETH_PACKET_MIN`/`_MAX_TAGGED` (`netdriver.c:390-399`). |  |

#### `mess_ndev_netdriver_status_reply` — ipc.h:1625

Размер: i386 56, x86_64 56. Член объединения: `m_ndev_netdriver_status_reply`.

Направление: lwip ndev → сетевой драйвер (подтверждение). Вызовы (`m_type`): `NDEV_STATUS_REPLY` (ответ на `NDEV_STATUS`). Отправитель: `minix/net/lwip/ndev.c:958-959`. Получатель: `do_status_reply()` (`minix/lib/libnetdriver/netdriver.c:598-614`) — снимает `pending_status`, при необходимости шлёт следующий статус.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `__uint32_t` | 0 / 0 | Копия `NDEV_STATUS.id`; драйвер требует 0, иначе panic (`netdriver.c:605`). |  |

#### `mess_netdriver_ndev_init_reply` — ipc.h:1632

Размер: i386 56, x86_64 56. Член объединения: `m_netdriver_ndev_init_reply`.

Направление: сетевой драйвер → lwip ndev. Вызовы (`m_type`): `NDEV_INIT_REPLY` (ответ на `NDEV_INIT`). Отправитель: `do_init()` (`minix/lib/libnetdriver/netdriver.c:732-748`). Получатель: `ndev_init_reply()` (`minix/net/lwip/ndev.c:517-620`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `__uint32_t` | 0 / 0 | Копия `NDEV_INIT.id`; ndev сверяет с `nq_head` (`ndev.c:529`). |  |
| `link` | `__uint32_t` | 4 / 4 | Состояние линка: `NDEV_LINK_UNKNOWN/UP/DOWN` (`com.h:1143-1145`). |  |
| `media` | `__uint32_t` | 8 / 8 | Активная среда, слово `IFM_*`. |  |
| `caps` | `__uint32_t` | 12 / 12 | Возможности: `NDEV_CAP_CS_*` + только при init `NDEV_CAP_MCAST/BCAST/HWADDR` (`com.h:1126-1134`). |  |
| `name` | `char[16]` | 16 / 16 | Имя устройства (напр. `"e1000#0"`), обязано быть непустым и с `'\0'` (`ndev.c:536-538`). |  |
| `hwaddr` | `__uint8_t[6]` | 32 / 32 | MAC-адрес устройства (`ndev.c:603`). |  |
| `hwaddr_len` | `__uint8_t` | 38 / 38 | Длина адреса в байтах (`sizeof(netdriver_addr_t)` = 6), проверяется ndev (`ndev.c:547`). |  |
| `max_send` | `__uint8_t` | 39 / 39 | Ёмкость очереди отправки драйвера (≥ 1) (`ndev.c:557`). |  |
| `max_recv` | `__uint8_t` | 40 / 40 | Ёмкость очереди приёма драйвера (≥ 1) (`ndev.c:558`). |  |

#### `mess_netdriver_ndev_reply` — ipc.h:1647

Размер: i386 56, x86_64 56. Член объединения: `m_netdriver_ndev_reply`.

Направление: сетевой драйвер → lwip ndev. Вызовы (`m_type`): `NDEV_CONF_REPLY`, `NDEV_SEND_REPLY`, `NDEV_RECV_REPLY`. Отправитель: `do_conf()` (`netdriver.c:492-496`), `finish_send()` (`:312-313`), `finish_recv()` (`:233-234`) (`minix/lib/libnetdriver/netdriver.c`). Получатель: `ndev_conf_reply/ndev_send_reply/ndev_recv_reply` (`minix/net/lwip/ndev.c:726-733`, `:843-850`, `:919-926`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `__uint32_t` | 0 / 0 | Номер запроса, на который отвечают; ndev сверяет с головой очереди (`ndev_pop`). |  |
| `result` | `__int32_t` | 4 / 4 | CONF: всегда `OK`; SEND: `OK`/ошибка (`EINTR` при down); RECV: длина принятого пакета в байтах (>0) или ошибка. |  |

#### `mess_netdriver_ndev_status` — ipc.h:1655

Размер: i386 56, x86_64 56. Член объединения: `m_netdriver_ndev_status`.

Направление: сетевой драйвер → lwip ndev (asynsend, `AMF_NOREPLY`), инициирует драйвер. Вызовы (`m_type`): `NDEV_STATUS`. Отправитель: `send_status()` (`minix/lib/libnetdriver/netdriver.c:562-568`). Получатель: `ndev_status()` (`minix/net/lwip/ndev.c:945-950`) → `ethif_status()`. Ответ: `NDEV_STATUS_REPLY` (`mess_ndev_netdriver_status_reply`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `id` | `__uint32_t` | 0 / 0 | Всегда 0 («for now»), возвращается в `STATUS_REPLY`. |  |
| `link` | `__uint32_t` | 4 / 4 | Состояние линка `NDEV_LINK_*`. |  |
| `media` | `__uint32_t` | 8 / 8 | Активная среда `IFM_*`. |  |
| `oerror` | `__uint32_t` | 12 / 12 | Приращение числа ошибок отправки с прошлого статуса (счётчики после отправки обнуляются). |  |
| `coll` | `__uint32_t` | 16 / 16 | Приращение числа коллизий. |  |
| `ierror` | `__uint32_t` | 20 / 20 | Приращение числа ошибок приёма. |  |
| `iqdrop` | `__uint32_t` | 24 / 24 | Приращение числа отброшенных входящих пакетов. |  |

#### `mess_net_netdrv_dl_conf` — ipc.h:1668

Размер: i386 56, x86_64 56. Член объединения: `m_net_netdrv_dl_conf`.

Направление: (было) INET → сетевой драйвер. Вызовы (`m_type`): `DL_CONF` (`com.h:176`). Отправитель: нет. Получатель: нет. **Не используется**: остаток старого протокола DL_* (сервер inet), заменённого протоколом NDEV_* (lwip + libnetdriver); ни `DL_*`, ни член объединения в дереве не упоминаются, кроме `com.h`/`ipc.h`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `mode` | `int` | 0 / 0 | Не используется (было: `DL_PROMISC_REQ/MULTI_REQ/BROAD_REQ`). |  |

#### `mess_net_netdrv_dl_getstat_s` — ipc.h:1675

Размер: i386 56, x86_64 56. Член объединения: `m_net_netdrv_dl_getstat_s`.

Направление: (было) INET → сетевой драйвер. Вызовы (`m_type`): `DL_GETSTAT_S`. Отправитель: нет. Получатель: нет. **Не используется**: старый протокол DL_*.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `grant` | `cp_grant_id_t` | 0 / 0 | Не используется (было: грант на буфер статистики). |  |

#### `mess_net_netdrv_dl_readv_s` — ipc.h:1682

Размер: i386 56, x86_64 56. Член объединения: `m_net_netdrv_dl_readv_s`.

Направление: (было) INET → сетевой драйвер. Вызовы (`m_type`): `DL_READV_S`. Отправитель: нет. Получатель: нет. **Не используется**: старый протокол DL_*.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `grant` | `cp_grant_id_t` | 0 / 0 | Не используется (было: грант на вектор iovec_s_t). |  |
| `count` | `int` | 4 / 4 | Не используется (было: число элементов вектора). |  |

#### `mess_net_netdrv_dl_writev_s` — ipc.h:1690

Размер: i386 56, x86_64 56. Член объединения: `m_net_netdrv_dl_writev_s`.

Направление: (было) INET → сетевой драйвер. Вызовы (`m_type`): `DL_WRITEV_S`. Отправитель: нет. Получатель: нет. **Не используется**: старый протокол DL_*.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `grant` | `cp_grant_id_t` | 0 / 0 | Не используется (было: грант на вектор iovec_s_t). |  |
| `count` | `int` | 4 / 4 | Не используется (было: число элементов вектора). |  |

#### `mess_netdrv_net_dl_conf` — ipc.h:1698

Размер: i386 56, x86_64 56. Член объединения: `m_netdrv_net_dl_conf`.

Направление: (было) сетевой драйвер → INET. Вызовы (`m_type`): `DL_CONF_REPLY`. Отправитель: нет. Получатель: нет. **Не используется**: старый протокол DL_*.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `stat` | `int` | 0 / 0 | Не используется (было: результат конфигурации). |  |
| `hw_addr` | `__uint8_t[6]` | 4 / 4 | Не используется (было: MAC-адрес). |  |

#### `mess_netdrv_net_dl_task` — ipc.h:1706

Размер: i386 56, x86_64 56. Член объединения: `m_netdrv_net_dl_task`.

Направление: (было) сетевой драйвер → INET. Вызовы (`m_type`): `DL_TASK_REPLY`. Отправитель: нет. Получатель: нет. **Не используется**: старый протокол DL_*.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `count` | `int` | 0 / 0 | Не используется (было: длина принятого пакета). |  |
| `flags` | `__uint32_t` | 4 / 4 | Не используется (было: `DL_PACK_SEND`/`DL_PACK_RECV`). |  |

#### `mess_pci_lsys_busc_get_bar` — ipc.h:1722

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_pci_lsys_busc_get_bar`.

Направление: драйвер шины PCI → libsys (ответ). Вызовы (`m_type`): ответ на `BUSC_PCI_GET_BAR` (`m_type` = `OK`/`EINVAL`; поля заполняются только при `OK`). Отправитель: `do_get_bar()` (`minix/drivers/bus/pci/main.c:487-496`). Получатель: `pci_get_bar()` (`minix/lib/libsys/pci_get_bar.c:30-35`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `base` | `int` | 0 / 0 | Базовый адрес BAR: физический MMIO-адрес или номер порта ввода-вывода (`pb_base`, `u32_t`) (`pci.c:2461`) → `u32_t *base`. 64-битные BAR не поддерживаются. | Ф, У, Ж |
| `size` | `size_t` | 4 / 8 | Размер окна BAR в байтах (`pb_size`, `u32_t`) → `u32_t *size` клиента. | Р |
| `flags` | `__uint32_t` | 8 / 16 | 1 — BAR пространства ввода-вывода (`PBF_IO`), 0 — память (`pci.c:2463`). | В |

Замечания: типы сквозь цепочку не согласованы (`u32_t` → `int`/`size_t` → `u32_t`); при переработке — `uint64_t base`, `uint64_t size` (64-битные BAR, LPAE).

#### `mess_rs_init` — ipc.h:1855

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_rs_init`.

Направление: RS → сервис (SEF), и обратно сервис → RS (ответ «init ready»). Вызовы (`m_type`): `RS_INIT` в обе стороны. Отправитель запроса: `init_service()` (`minix/servers/rs/utility.c:50-61`, `rs_asynsend`). Получатель: SEF `do_sef_init_request()` (`minix/lib/libsys/sef_init.c:202-211`). Ответ: тот же тип, `m_type = RS_INIT`, только `result` (`sef_init.c:114-117`) → RS `do_init_ready()` (`minix/servers/rs/request.c:472`), также `rs/main.c:596,801`, `rs/update.c:591-595`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `result` | `int` | 0 / 0 | Только ответ: результат инициализации сервиса (`OK`/ошибка). |  |
| `type` | `int` | 4 / 4 | Тип инициализации: `SEF_INIT_FRESH` (0), `SEF_INIT_LU` (1), `SEF_INIT_RESTART` (2) (`sef.h:93-95`). RS пишет через `(short)`. |  |
| `rproctab_gid` | `cp_grant_id_t` | 8 / 8 | Грант (`ANY`, чтение) на таблицу `rprocpub[]` RS (`rs/main.c:185`); сервисы читают её для поиска меток. |  |
| `old_endpoint` | `endpoint_t` | 12 / 12 | Endpoint прежнего экземпляра (живое обновление — `state_endpoint`, перезапуск — предыдущая копия) или `NONE` (`utility.c:34-41`). |  |
| `restarts` | `int` | 16 / 16 | Номер перезапуска (`r_restarts+1`, через `(short)`). |  |
| `flags` | `int` | 20 / 20 | `SEF_INIT_CRASH/FAIL/TIMEOUT/DEFCB/SCRIPT_RESTART/ST` (`sef.h:98-103`). |  |
| `buff_addr` | `vir_bytes` | 24 / 24 | Адрес в АП сервиса заранее выделенного VM mmap-буфера (`VM_RS_MEM_MAP_PREALLOC`, `rs/request.c:801-809`) или 0; SEF берёт как `init_buff_start` (`sef_init.c:208`). | А |
| `buff_len` | `size_t` | 28 / 32 | Длина этого буфера в байтах (`sef_init.c:210`). | Р |
| `prepare_state` | `int` | 32 / 40 | Состояние подготовки живого обновления `SEF_LU_STATE_*`, при котором был снят старый экземпляр (`utility.c:37`). | В |

#### `mess_rs_req` — ipc.h:1887

Размер: i386 56, x86_64 72 — **не помещается**. Член объединения: `m_rs_req`.

Направление: пользователь/сервис → RS. Вызовы (`m_type`): `RS_UP`, `RS_EDIT`, `RS_UPDATE` (поле `addr` → `struct rs_start`); `RS_DOWN`, `RS_REFRESH`, `RS_RESTART`, `RS_CLONE`, `RS_UNCLONE`, `RS_FI` (`addr`+`len` → метка); `RS_SYSCTL` (`subtype`); `RS_LOOKUP` (`name`, `name_len` → ответ `endpoint`). Отправители: `minix-service` (`minix/commands/minix-service/minix-service.c:816-828`), `filter` (`minix/drivers/storage/filter/driver.c:433-434`), `minix_rs_lookup()` (`minix/lib/libc/sys/minix_rs.c:31-35`). Получатель: `minix/servers/rs/request.c` (`do_up:39`, `do_down/refresh/restart/clone:118,168,216,261`, `do_edit:307`, `do_unclone:397`, `do_update:552`, `do_lookup:1152-1173`, `do_sysctl:1183`, `do_fi:1237`). Ответ: тот же тип, `m_type` = результат; `endpoint` для `RS_LOOKUP`. Разбор — `minix/usr.bin/trace/service/rs.c`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `len` | `int` | 0 / 0 | Длина метки в байтах (без `'\0'`) для DOWN/REFRESH/RESTART/CLONE/UNCLONE/FI; `copy_label()` берёт MIN с размером буфера (`rs/manager.c:151-168`). |  |
| `name_len` | `int` | 4 / 4 | LOOKUP: длина имени с `'\0'`, 2..99 (`request.c:1152-1157`). |  |
| `endpoint` | `endpoint_t` | 8 / 8 | Ответ LOOKUP: endpoint найденного сервиса (`request.c:1173`). |  |
| `addr` | `void *` | 12 / 16 | Адрес в АП вызывающего: `struct rs_start` (UP/EDIT/UPDATE, копируется `sys_datacopy` целиком, `manager.c:135-146`) или строка метки (остальные). | А |
| `name` | `const char *` | 16 / 24 | LOOKUP: адрес имени сервиса в АП вызывающего (`request.c:1159`). | А |
| `subtype` | `int` | 20 / 32 | SYSCTL: `RS_SYSCTL_SRV_STATUS/UPD_START/UPD_RUN/UPD_STOP/UPD_STATUS` (`com.h:485-489`). | В |

Замечания: для `RS_UPDATE` `minix-service` заполняет в одном буфере и `m_rs_update.state/prepare_maxtime`, и `m_rs_req.addr` — на i386 `addr` перекрывает `m_rs_update.flags`, на x86_64 — `state_data_gid`; работает лишь потому, что RS эти поля в запросе не читает. `struct rs_start` содержит указатели — его раскладка тоже зависит от разрядности.

#### `mess_rs_update` — ipc.h:1898

Размер: i386 56, x86_64 56. Член объединения: `m_rs_update`.

Направление: (1) `minix-service` → RS (`RS_UPDATE`, вместе с `m_rs_req`); (2) RS → сервис (`RS_LU_PREPARE`, запрос подготовки/отмены); (3) сервис → RS (`RS_LU_PREPARE`, ответ). Отправители: `minix-service.c:736-737`; `request_prepare_update_service()` (`minix/servers/rs/update.c:207-223`); `sef_lu_ready()` (`minix/lib/libsys/sef_liveupdate.c:181-184`). Получатели: `do_update()` (`minix/servers/rs/request.c:647,653`); `do_sef_lu_request()` (`sef_liveupdate.c:102-104`); `do_upd_ready()` (`request.c:901`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `result` | `int` | 0 / 0 | Только ответ сервиса: результат подготовки (`OK` — готов к обновлению) (`request.c:901`). |  |
| `state` | `int` | 4 / 4 | Состояние подготовки `SEF_LU_STATE_*` (`sef.h:213-224`): в `RS_UPDATE` — требуемое (`NULL` → `EINVAL`), в `RS_LU_PREPARE` от RS — целевое (`NULL` = отмена), в ответе — текущее состояние сервиса. |  |
| `prepare_maxtime` | `int` | 8 / 8 | Только `RS_UPDATE`: лимит времени подготовки в тиках (0 → `RS_DEFAULT_PREPARE_MAXTIME`) (`request.c:653-656`). |  |
| `flags` | `int` | 12 / 12 | Только `RS_LU_PREPARE` от RS: флаги обновления `SEF_LU_SELF/ASR/MULTI/NOMMAP/DETACHED…` (`sef.h:235-242`; `update.c:207`). |  |
| `state_data_gid` | `__gid_t` | 16 / 16 | Только `RS_LU_PREPARE` от RS: **грант** (`cp_grant_id_t`) на данные состояния подготовки (`rpupd->prepare_state_data_gid`, напр. выражение для `SEF_LU_STATE_EVAL`), объявлен как `gid_t` по ошибке (`update.c:208`, `sef_liveupdate.c:104`). |  |

Замечания: тип поля `state_data_gid` надо исправить на `cp_grant_id_t`.

#### `mess_tty_lsys_fkey_ctl` — ipc.h:1925

Размер: i386 56, x86_64 56. Член объединения: `m_tty_lsys_fkey_ctl`.

Направление: TTY → libsys (ответ). Вызовы (`m_type`): ответ на `TTY_FKEY_CONTROL` (`m_type` = `OK`/`EPERM`/`EINVAL`). Отправитель: `do_fkey_ctl()` (`minix/drivers/tty/tty/arch/i386/keyboard.c:503-523`, `ipc_sendnb` тем же буфером). Получатель: `fkey_ctl()` (`minix/lib/libsys/fkey_ctl.c:24-25`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `fkeys` | `int` | 0 / 0 | `FKEY_EVENTS`: маска F1–F12, нажатых с прошлого опроса этим наблюдателем (`keyboard.c:505-510`). Для MAP/UNMAP TTY это поле не пишет — клиент получает код `request` (см. `mess_lsys_tty_fkey_ctl`). |  |
| `sfkeys` | `int` | 4 / 4 | `FKEY_EVENTS`: то же для Shift+F1–F12 (`keyboard.c:512-516`). Для MAP/UNMAP — остаток `fkeys` запроса. |  |

#### `mess_vmmcp` — ipc.h:2383

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vmmcp`.

Направление: ФС-сервер (libminixfs через libsys) → VM. Вызовы (`m_type`): `VM_MAPCACHEPAGE`, `VM_SETCACHEPAGE`, `VM_FORGETCACHEPAGE`, `VM_CLEARCACHE`. Отправитель: `vm_cachecall()` / `vm_map_cacheblock()` / `vm_set_cacheblock()` / `vm_forget_cacheblock()` / `vm_clear_cache()` (`minix/lib/libsys/vm_cache.c`), вызывающие — `minix/lib/libminixfs/cache.c:444,567`. Получатель: `do_mapcache()` (`minix/servers/vm/mem_cache.c:95-172`), `do_setcache()` (`:196-277`), `do_forgetcache()` (`:283-312`), `do_clearcache()` (`:315-324`). Ответ: `VM_MAPCACHEPAGE` — `mess_vmmcp_reply`; остальные — только `m_type`.

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `dev` | `dev_t` | 0 / 0 | Номер блочного устройства (ключ кэша); единственное поле для `VM_CLEARCACHE` (`mem_cache.c:319`). (исх.: 64bits long.) |  |
| `dev_offset` | `__off_t` | 8 / 8 | Байтовое смещение блока на устройстве, кратно `PAGE_SIZE` (`mem_cache.c:98,108`). |  |
| `ino_offset` | `__off_t` | 16 / 16 | Байтовое смещение блока внутри файла, кратно `PAGE_SIZE`; для FORGET — 0. |  |
| `ino` | `ino_t` | 24 / 24 | Номер inode или `VMC_NO_INODE` (метаданные/FORGET); участвует в поиске/обновлении кэша (`mem_cache.c:148,235,265`). |  |
| `block` | `void *` | 32 / 32 | SETCACHE: адрес блока в АП ФС, страницы которого VM берёт в кэш (`mem_cache.c:221`); MAPCACHE/FORGET — NULL. | А |
| `flags_ptr` | `__uint32_t *` | 36 / 40 | Адрес слова флагов `VMMC_*` в АП ФС (`vm.h:84-87`). VM не читает — мёртвое поле (`vm_cache.c:39`). | А |
| `pages` | `__uint8_t` | 40 / 48 | Размер блока в страницах (`blocksize / PAGE_SIZE`, ≤255) (`mem_cache.c:101,206,292`). | В |
| `flags` | `__uint8_t` | 41 / 49 | SETCACHE: `VMSF_ONCE` (отбросить после однократного использования) (`mem_cache.c:202`, `vm.h:93`). | В |

Замечания: `flags_ptr` можно удалить; `pages` (`u8`) ограничивает блок 255 страницами — для 64 КБ+ страниц/больших блоков взять `uint32_t`.

#### `mess_vmmcp_reply` — ipc.h:2396

Размер: i386 56, x86_64 64 — **не помещается**. Член объединения: `m_vmmcp_reply`.

Направление: VM → ФС (libsys), ответ. Вызовы (`m_type`): ответ на `VM_MAPCACHEPAGE` (`OK`/`ENOENT`/`ENOMEM`/…). Отправитель: `do_mapcache()` (`minix/servers/vm/mem_cache.c:168-170`, сообщение предварительно обнуляется). Получатель: `vm_map_cacheblock()` (`minix/lib/libsys/vm_cache.c:56`).

| Поле | Тип | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|---|
| `addr` | `void *` | 0 / 0 | Виртуальный адрес в АП ФС, куда VM отобразил кэшированные страницы блока. | А |
| `flags` | `__uint8_t` | 4 / 8 | Не используется (VM всегда 0, клиент не читает). | В |


#### Псевдонимы общих раскладок

| Псевдоним | Поле | Тип | Раздел заголовка | Файл:строка | Назначение | 64 |
|---|---|---|---|---|---|---|
| `VM_RS_CTL_ENDPT` | `m1_i1` | `int` | VM_INFO 'what' values. | minix/include/minix/com.h:739 | `VM_RS_MEMCTL` (RS → VM): endpoint процесса. Отправитель `vm_memctl()` (`minix/lib/libsys/vm_memctl.c:12`, вызывают `rs/main.c:470`, `rs/manager.c`, `rs/request.c:802`); получатель `do_rs_memctl()` (`minix/servers/vm/rs.c:355`). |  |
| `VM_RS_CTL_REQ` | `m1_i2` | `int` | VM_INFO 'what' values. | minix/include/minix/com.h:740 | `VM_RS_MEMCTL`: подфункция `VM_RS_MEM_PIN`/`MAKE_VM`/`HEAP_PREALLOC`/`MAP_PREALLOC`/`GET_PREALLOC_MAP` (`com.h:741-745`; `rs.c:356-389`). |  |
| `VM_RS_CTL_ADDR` | `m2_p1` | `char *` | VM_INFO 'what' values. | minix/include/minix/com.h:746 | `VM_RS_MEMCTL` *_PREALLOC: вход/выход — адрес предвыделенного региона в АП процесса (VM пишет через `(vir_bytes*)`, `rs.c:378-384`; `vm_memctl.c:14,22`). Смещение i386 28 / x86_64 40. | А |
| `VM_RS_CTL_LEN` | `m2_i3` | `int` | VM_INFO 'what' values. | minix/include/minix/com.h:747 | `VM_RS_MEMCTL` *_PREALLOC: вход/выход — длина региона в байтах. libsys кладёт `size_t` в `int`, VM пишет по `(size_t*)&m2_i3` (`rs.c:378-384`) — на LP64 8 байт поверх `int`+выравнивания. | У |
| `VMPCTL_PARAM` | `m9_l1` | `long` | VM_INFO 'what' values. | minix/include/minix/com.h:753 | `VM_PROCCTL` (RS/VFS → VM): подфункция `VMPPARAM_CLEAR` / `VMPPARAM_HANDLEMEM` (`minix/servers/vm/exit.c:129`). Отправители `vm_procctl()` (`minix/lib/libsys/vm_procctl.c:18-22`), `vm_vfs_procctl_handlemem()` (`minix/servers/vfs/comm.c:211-215`). | С |
| `VMPCTL_WHO` | `m9_l2` | `long` | VM_INFO 'what' values. | minix/include/minix/com.h:754 | `VM_PROCCTL`: endpoint целевого процесса (`exit.c:122`). Endpoint в `long`. | С |
| `VMPCTL_M1` | `m9_l3` | `long` | VM_INFO 'what' values. | minix/include/minix/com.h:755 | `VM_PROCCTL`/HANDLEMEM: виртуальный адрес начала диапазона в АП целевого процесса, который VM должен «подкачать» (`handle_memory_start`, `exit.c:144`). Адрес в `long`. | А |
| `VMPCTL_LEN` | `m9_l4` | `long` | VM_INFO 'what' values. | minix/include/minix/com.h:756 | `VM_PROCCTL`/HANDLEMEM: длина диапазона в байтах (`exit.c:145`). | Р |
| `VMPCTL_FLAGS` | `m9_l5` | `long` | VM_INFO 'what' values. | minix/include/minix/com.h:757 | `VM_PROCCTL`/HANDLEMEM: признак записи (writeflag: 1 — нужна запись) (`exit.c:145`). | С |
| `VPF_ADDR` | `m1_i1` | `int` | Total. | minix/include/minix/com.h:774 | `VM_PAGEFAULT` (ядро → VM, `mini_send` от имени процесса): адрес сбоя — CR2 (`minix/kernel/arch/i386/exception.c:121`) / DFAR/IFAR (`kernel/arch/earm/exception.c:101`); VM → `handle_pagefault(vir_bytes)` (`minix/servers/vm/pagefaults.c:242`). Виртуальный адрес в `int`. | У, Ж |
| `VPF_FLAGS` | `m1_i2` | `int` | Total. | minix/include/minix/com.h:775 | `VM_PAGEFAULT`: аппаратный код ошибки — x86 `#PF errcode` (`exception.c:122`) / ARM DFSR/IFSR (`earm/exception.c:102`); VM разбирает через `PFERR_*` арх.-зависимо. | Ж |
| `USB_GRANT_ID` | `m4_l1` | `long` | Messages for USB | minix/include/minix/com.h:829 | `USB_RQ_SEND_URB` (USB-драйвер libusb → usbd/libddekit): грант на URB (`minix/lib/libusb/usb.c:47`; `minix/lib/libddekit/src/usb_server.c:322,362`). Грант в `long`. | С |
| `USB_GRANT_SIZE` | `m4_l2` | `long` | Messages for USB | minix/include/minix/com.h:830 | `USB_RQ_SEND_URB`: размер копируемой части URB в байтах = `urb_size - sizeof(void*)` (`usb.c:48`; `usb_server.c:313,323`) — зависит от ширины указателя. | Р |
| `USB_URB_ID` | `m4_l1` | `long` | Messages for USB | minix/include/minix/com.h:832 | ID URB: ответ на `SEND_URB` (`usb_server.c:383` → `usb.c:66`), запрос `USB_RQ_CANCEL_URB` (`usb.c:95` → `usb_server.c:431`), уведомление `USB_COMPLETE_URB` (`usb_server.c:522` → `usb.c:214`). | С |
| `USB_RESULT` | `m4_l2` | `long` | Messages for USB | minix/include/minix/com.h:833 | Код результата в `USB_REPLY` (`usb_server.c:201,384,435,471` → `usb.c:61,108,147,251`). | С |
| `USB_DEV_ID` | `m4_l1` | `long` | Messages for USB | minix/include/minix/com.h:834 | `USB_ANNOUCE_DEV`/`USB_WITHDRAW_DEV` (usbd → драйвер): ID USB-устройства (`usb_server.c:218` → `usb.c:217,220`). | С |
| `USB_DRIVER_EP` | `m4_l2` | `long` | Messages for USB | minix/include/minix/com.h:835 | Не используется (обращений нет). | С |
| `USB_INTERFACES` | `m4_l3` | `long` | Messages for USB | minix/include/minix/com.h:836 | `USB_ANNOUCE_DEV`: битовая маска интерфейсов устройства, отданных драйверу (`usb_server.c:219` → `usb.c:217`). | С |
| `USB_RB_INIT_NAME` | `m3_ca1` | `char[44]` | Messages for USB | minix/include/minix/com.h:837 | `USB_RQ_INIT` (драйвер → usbd): имя/метка драйвера, до `M_PATH_STRING_MAX` (40) (`usb.c:135`); сервер его не читает (`register_driver`, `usb_server.c:192`). Смещение i386 12 / x86_64 16 — на 64 битах 16+44 > 56. | В |
| `USB_INFO_TYPE` | `m4_l1` | `long` | Messages for USB | minix/include/minix/com.h:839 | `USB_RQ_SEND_INFO`: тип информации для `ddekit_usb_info()` (`usb.c:239` → `usb_server.c:459`); `usb_send_info()` в дереве не вызывается. | С |
| `USB_INFO_VALUE` | `m4_l2` | `long` | Messages for USB | minix/include/minix/com.h:840 | `USB_RQ_SEND_INFO`: значение (`usb.c:240` → `usb_server.c:460`). | С |
| `DEVMAN_GRANT_ID` | `m4_l1` | `long` | Messages for DeviceManager (s/t like SysFS) | minix/include/minix/com.h:861 | `DEVMAN_ADD_DEV` (libdevman → devman): грант на описание устройства (`minix/lib/libdevman/generic.c:118` → `minix/servers/devman/device.c:239`). | С |
| `DEVMAN_GRANT_SIZE` | `m4_l2` | `long` | Messages for DeviceManager (s/t like SysFS) | minix/include/minix/com.h:862 | `DEVMAN_ADD_DEV`: размер описания в байтах (`generic.c:119` → `device.c:231,240`). | Р |
| `DEVMAN_ENDPOINT` | `m4_l3` | `long` | Messages for DeviceManager (s/t like SysFS) | minix/include/minix/com.h:864 | `DEVMAN_BIND`/`UNBIND` (RS → devman → владелец устройства): endpoint драйвера, привязываемого к устройству (`minix/servers/rs/manager.c:847,904` → `devman/bind.c` → `generic.c:216,242`). | С |
| `DEVMAN_DEVICE_ID` | `m4_l2` | `long` | Messages for DeviceManager (s/t like SysFS) | minix/include/minix/com.h:865 | ID устройства: ответ `ADD_DEV` (`device.c:270` → `generic.c:139`), запрос `DEL_DEV` (`generic.c:160` → `device.c:426`), `BIND`/`UNBIND` (`manager.c:848`, `bind.c:21,70`). Совпадает по полю с `DEVMAN_GRANT_SIZE`. | С |
| `DEVMAN_RESULT` | `m4_l1` | `long` | Messages for DeviceManager (s/t like SysFS) | minix/include/minix/com.h:866 | Код результата в `DEVMAN_REPLY` (`device.c:216`, `bind.c:36-47`, `generic.c:218,244`). | С |
| `VBOX_CONN` | `m2_i1` | `int` | Field names for VBOX messages. | minix/include/minix/vboxif.h:25 | `VBOX_CLOSE`/`VBOX_CALL`/`VBOX_CANCEL` (libsys `minix/lib/libsys/arch/i386/vbox.c` → драйвер `minix/drivers/vmm_guest/vbox/hgcm.c:406,559,646`): индекс HGCM-соединения (0..`MAX_CONNS`-1). (исх.: connection identifier) |  |
| `VBOX_GRANT` | `m2_i2` | `int` | Field names for VBOX messages. | minix/include/minix/vboxif.h:26 | `VBOX_OPEN`: грант на имя HGCM-сервиса; `VBOX_CALL`: грант на массив `vbox_param_t` (`hgcm.c:376,575-581`). (исх.: grant ID of buffer or name) |  |
| `VBOX_COUNT` | `m2_i3` | `int` | Field names for VBOX messages. | minix/include/minix/vboxif.h:27 | `VBOX_OPEN`: длина имени в байтах (≤ `VMMDEV_HGCM_NAME_SIZE`, `hgcm.c:337`); `VBOX_CALL`: число параметров (`hgcm.c:560`). (исх.: number of bytes or elements) |  |
| `VBOX_RESULT` | `m2_i1` | `int` | Field names for VBOX messages. | minix/include/minix/vboxif.h:28 | `VBOX_REPLY`: результат MINIX (для OPEN — номер соединения или ошибка) (`hgcm.c:82` → `vbox.c:60,86,153`). (исх.: result or error code) |  |
| `VBOX_CODE` | `m2_i2` | `int` | Field names for VBOX messages. | minix/include/minix/vboxif.h:29 | `VBOX_REPLY`: код результата VirtualBox (`hgcm.c:83` → `vbox.c:151`). (исх.: VirtualBox result code) |  |
| `VBOX_FUNCTION` | `m2_l1` | `long` | Field names for VBOX messages. | minix/include/minix/vboxif.h:30 | `VBOX_CALL`: номер функции HGCM-сервиса (`vbox.c:135` → `hgcm.c:588`). (исх.: function call number) | С |
| `VBOX_ID` | `m2_l2` | `long` | Field names for VBOX messages. | minix/include/minix/vboxif.h:31 | Непрозрачный ID запроса для асинхронных клиентов, драйвер возвращает его в `VBOX_REPLY` и ищет по нему при `VBOX_CANCEL` (`hgcm.c:84,660`); libsys всегда шлёт 0 и проверяет 0 в ответе (`vbox.c:48,57`). (исх.: opaque request ID) | С |

#### Прочие псевдонимы

| Псевдоним | Поле | Тип | Раздел заголовка | Файл:строка | Назначение | 64 |
|---|---|---|---|---|---|---|
| `VTR_VGRANT` | `m10_l1` | `long` |  | minix/tests/kernel/sys_vumap/com.h:6 | Тест `VTR_RELAY` (`vumaptest.c:250` → `vumaprelay.c:21`): грант на вектор `struct vumap_vir`, который релей передаёт в `sys_vumap`. Грант в `long`. (исх.: grant for virtual vector) | С |
| `VTR_VCOUNT` | `m10_i1` | `int` |  | minix/tests/kernel/sys_vumap/com.h:7 | Число элементов виртуального вектора (`vumaprelay.c:16`). (исх.: nr of elements in virtual vector) |  |
| `VTR_OFFSET` | `m10_l2` | `long` |  | minix/tests/kernel/sys_vumap/com.h:8 | Байтовое смещение в первом элементе (параметр `offset` `sys_vumap`, `vumaprelay.c:18`). (исх.: offset into first element) | Р |
| `VTR_ACCESS` | `m10_i2` | `int` |  | minix/tests/kernel/sys_vumap/com.h:9 | Флаги доступа `VUA_READ/VUA_WRITE` (`vumaprelay.c:19`). (исх.: access flags (VUA_)) |  |
| `VTR_PGRANT` | `m10_l3` | `long` |  | minix/tests/kernel/sys_vumap/com.h:10 | Грант на выходной вектор `struct vumap_phys` (физ. адреса), релей пишет туда результат (`vumaprelay.c:29`). (исх.: grant for physical vector) | С |
| `VTR_PCOUNT` | `m10_i3` | `int` |  | minix/tests/kernel/sys_vumap/com.h:11 | Вход — ёмкость физического вектора, выход — число заполненных элементов (`vumaprelay.c:17,34`; `vumaptest.c:255,265`). (исх.: nr of physical elements (in/out)) |  |

#### Протокол PCI (BUSC_PCI_*) на общих раскладках

Клиент — функции libsys `minix/lib/libsys/pci_*.c` (драйверы устройств; ACL — RS), сервер — драйвер шины `minix/drivers/bus/pci/main.c` (диспетчер `:672-695`), все вызовы `ipc_sendrec`, ответ тем же буфером, `m_type` — результат. Смещения полей: `m1_i1/i2/i3` = 8/12/16 (i386 и x86_64); `m2_i1/i2` = 8/12, `m2_l1` = 20 (i386) / 24 (x86_64); `m7_i1..i4` = 0/4/8/12.

| Вызов (`m_type`) | Клиент (libsys) | Запрос: поля | Ответ: `m_type` и поля | Сервер (`main.c`) | 64 |
|---|---|---|---|---|---|
| `BUSC_PCI_INIT` | `pci_init.c:23` | — | `0` | `do_init:46` |  |
| `BUSC_PCI_FIRST_DEV` | `pci_first_dev.c:20` | — (видимость по ACL отправителя) | `1` найдено / `0` нет; `m1_i1` = devind, `m1_i2` = vendor ID, `m1_i3` = device ID | `do_first_dev:62` |  |
| `BUSC_PCI_NEXT_DEV` | `pci_next_dev.c:20-21` | `m1_i1` = текущий devind | как FIRST_DEV | `do_next_dev:91` |  |
| `BUSC_PCI_FIND_DEV` | `pci_find_dev.c:17-20` | `m1_i1` = шина, `m1_i2` = устройство, `m1_i3` = функция (BDF) | `1`/`0`; `m1_i1` = devind | `do_find_dev:117` |  |
| `BUSC_PCI_IDS` | `pci_ids.c:20-21` | `m1_i1` = devind | `OK`; `m1_i1` = vendor ID, `m1_i2` = device ID | `do_ids:139` |  |
| `BUSC_PCI_RESERVE` | `pci_reserve.c:18-19`, `pci_reserve_ok:36` | `m1_i1` = devind | `OK`/`EBUSY`/`EPERM` (`_pci_reserve` с ACL) | `do_reserve:322` |  |
| `BUSC_PCI_ATTR_R8/R16/R32` | `pci_attr_r8/16/32.c:19-21` | `m2_i1` = devind, `m2_i2` = смещение регистра конфиг. пространства | `m2_l1` = прочитанное значение (8/16/32 бит); R16/R32 возвращают `OK` даже при ошибке (`main.c:383,409`) | `do_attr_r8/16/32:341/367/393` | `m2_i2`: Ж; `m2_l1`: С, Ж |
| `BUSC_PCI_ATTR_W8/W16/W32` | `pci_attr_w8/16/32.c:17-20` | `m2_i1` = devind, `m2_i2` = смещение регистра, `m2_l1` = записываемое значение | `OK` | `do_attr_w8/16/32:419/439/459` | `m2_i2`: Ж; `m2_l1`: С, Ж |
| `BUSC_PCI_RESCAN` | `pci_rescan_bus.c:17-18` | клиент: `m1_i1` = номер шины; **сервер читает `m2_i1`** (`main.c:509`) | `OK` | `do_rescan_bus:505` |  |
| `BUSC_PCI_DEV_NAME_S` | `pci_dev_name.c:29-33` | `m7_i1` = vendor ID, `m7_i2` = device ID, `m7_i3` = размер буфера, `m7_i4` = грант (`CPF_WRITE`) на буфер имени | `OK`/`ENOENT`; имя — в буфер по гранту | `do_dev_name:165` |  |
| `BUSC_PCI_SLOT_NAME_S` | `pci_slot_name.c:30-33` | `m1_i1` = devind, `m1_i2` = размер буфера, `m1_i3` = грант на буфер | `OK`; строка `"bus.dev.func"` — по гранту | `do_slot_name:202` |  |
| `BUSC_PCI_SET_ACL` | `pci_set_acl.c:43-44` (только RS) | `m1_i1` = грант на `struct rs_pci` (ACL сервиса) | `OK`/`EPERM`/`ENOMEM` (через `reply()`) | `do_set_acl:238` |  |
| `BUSC_PCI_DEL_ACL` | `pci_del_acl.c:33-34` (только RS) | `m1_i1` = endpoint сервиса | `OK`/`EPERM`/`EINVAL`; освобождает зарезервированные устройства | `do_del_acl:281` |  |
| `BUSC_PCI_GET_BAR` | `pci_get_bar.c:22-24` | `mess_lsys_pci_busc_get_bar` | `mess_pci_lsys_busc_get_bar` | `do_get_bar:479` | см. типы выше |

Замечания: `BUSC_PCI_RESCAN` работает только потому, что `m1_i1` и `m2_i1` лежат на одном смещении (8) — нестыковка отправителя и получателя. Значения регистров (`m2_l1`) — `long`, хотя по смыслу `uint32_t`. Протокол стоит перевести на явные `mess_*` с полями фиксированной ширины.

#### Протокол IOMMU_MAP (amddev) на общих раскладках

Вызов `IOMMU_MAP` (`com.h:157`) — просьба к драйверу IOMMU AMD (`minix/drivers/iommu/amddev/amddev.c`, собирается только для i386, `amddev.conf`) открыть DMA-доступ устройству к буферу. Отправители (одинаковые функции «TODO»): `tell_iommu()` в `minix/drivers/net/fxp/fxp.c:1746-1782` и `minix/drivers/net/rtl8139/rtl8139.c:1489-1520`, `tell_dev()` в `minix/lib/libaudiodriver/audio_fw.c:832-866` (только `__i386__`); вызываются с BDF = 0.0.0, если в DS есть метка `amddev`. Получатель: главный цикл `amddev.c:85-89` → `do_add4pci()` (`amddev.c:374-420`): `sys_umap_remote` → `add_range(busaddr,size)`. Ответ: тот же буфер, `m_type` = результат.

| Поле (отправитель / получатель) | Смещ. i386 / x86_64 | Назначение | 64 |
|---|---|---|---|
| `m2_i1` / `m1_i1` | 8 / 8 | Номер шины PCI устройства (сейчас всегда 0). |  |
| `m2_i2` / `m1_i2` | 12 / 12 | Номер устройства PCI (0). |  |
| `m2_i3` / `m1_i3` | 16 / 16 | Номер функции PCI (0). |  |
| `m2_l1` | 20 / 24 | Виртуальный адрес DMA-буфера в АП отправителя (`vir_bytes` в `long`), должен быть выровнен на страницу. | А |
| `m2_l2` | 24 / 32 | Длина буфера в байтах, кратна странице. | Р |

Замечания: отправители пишут BDF в `m2_i*`, получатель читает `m1_i*` — совпадение смещений, а не контракт. Протокол фактически отладочный (все BDF = 0, проверка с PCI не реализована, `amddev.c:406`).

## 10. Сводная таблица типов

Все 256 типов в порядке разделов. «Размер» — полезная нагрузка i386 → x86_64 (жирным — не помещается в 56 байт). «Пометки» — число полей с каждой пометкой.

| Тип | § | Направление | Вызовы (`m_type`) | Размер | Пометки |
|---|---|---|---|---|---|
| `mess_u8` | 4 | общая раскладка | через псевдонимы | 56 → 56 | см. §4 |
| `mess_u16` | 4 | общая раскладка | через псевдонимы | 56 → 56 | см. §4 |
| `mess_u32` | 4 | общая раскладка | через псевдонимы | 56 → 56 | см. §4 |
| `mess_u64` | 4 | общая раскладка | через псевдонимы | 56 → 56 | см. §4 |
| `mess_1` | 4 | общая раскладка | через псевдонимы | 56 → **80** | см. §4 |
| `mess_2` | 4 | общая раскладка | через псевдонимы | 56 → **72** | см. §4 |
| `mess_3` | 4 | общая раскладка | через псевдонимы | 56 → **64** | см. §4 |
| `mess_4` | 4 | общая раскладка | через псевдонимы | 56 → **80** | см. §4 |
| `mess_7` | 4 | общая раскладка | через псевдонимы | 56 → **72** | см. §4 |
| `mess_9` | 4 | общая раскладка | через псевдонимы | 56 → **80** | см. §4 |
| `mess_10` | 4 | общая раскладка | через псевдонимы | 56 → **72** | см. §4 |
| `mess_krn_lsys_schedule` | 5 | ядро → планировщик (сервер `sched`), асинхронное сообщение «от имени» процесса (`m_sour… | `SCHEDULING_NO_QUANTUM` | 56 → **72** | С4 В2 |
| `mess_krn_lsys_sys_devio` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_DEVIO` с `_DIO_INPUT` | 56 → 56 | Ж1 |
| `mess_krn_lsys_sys_fork` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_FORK` | 56 → **64** | А1 |
| `mess_krn_lsys_sys_getwhoami` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_GETINFO` с `request = GET_WHOAMI` | 56 → 56 |  |
| `mess_krn_lsys_sys_irqctl` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_IRQCTL` с `IRQ_SETPOLICY` | 56 → 56 |  |
| `mess_krn_lsys_sys_times` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_TIMES` | 56 → 56 |  |
| `mess_krn_lsys_sys_trace` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_TRACE` | 56 → **64** | С1 Ж1 |
| `mess_krn_lsys_sys_umap` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_UMAP` и `SYS_UMAP_REMOTE` | 56 → **64** | Ф1 |
| `mess_krn_lsys_sys_vumap` | 5 | ядро → libsys (ответ на kernel call) | ответ на `SYS_VUMAP` | 56 → 56 |  |
| `mess_lsys_krn_readbios` | 5 | libsys → ядро (kernel call, только i386) | `SYS_READBIOS` | 56 → **72** | А1 Ф1 Р1 Ж1 |
| `mess_lsys_kern_safecopy` | 5 | libsys → ядро (kernel call) | `SYS_SAFECOPYFROM`, `SYS_SAFECOPYTO` | 56 → **72** | А1 Р2 |
| `mess_lsys_krn_schedctl` | 5 | сервер планирования → ядро (kernel call) | `SYS_SCHEDCTL` | 56 → 56 |  |
| `mess_lsys_krn_schedule` | 5 | сервер планирования → ядро (kernel call) | `SYS_SCHEDULE` | 56 → 56 |  |
| `mess_lsys_krn_sys_abort` | 5 | libsys → ядро (kernel call) | `SYS_ABORT` | 56 → 56 |  |
| `mess_lsys_krn_sys_clear` | 5 | PM → ядро (kernel call) | `SYS_CLEAR` | 56 → 56 |  |
| `mess_lsys_krn_sys_copy` | 5 | libsys → ядро (kernel call) | `SYS_VIRCOPY`, `SYS_PHYSCOPY` (один обработчик, разные права) | 56 → **80** | А2 Ф3 Р1 В2 |
| `mess_lsys_krn_sys_devio` | 5 | libsys → ядро (kernel call, порты x86) | `SYS_DEVIO` | 56 → 56 | Ж2 |
| `mess_lsys_krn_sys_diagctl` | 5 | libsys → ядро (kernel call) | `SYS_DIAGCTL` | 56 → **64** | А1 В2 |
| `mess_lsys_krn_sys_exec` | 5 | PM/VM → ядро (kernel call) | `SYS_EXEC` | 56 → **80** | А4 Ж3 |
| `mess_lsys_krn_sys_fork` | 5 | VM → ядро (kernel call) | `SYS_FORK` | 56 → 56 |  |
| `mess_lsys_krn_sys_getinfo` | 5 | libsys → ядро (kernel call) | `SYS_GETINFO` (подзапросы `GET_MACHINE`, `GET_KINFO`, `GET_LOADINFO`, `GET_CPUINFO`, `GET_HZ`, `GET_IMAGE`, `GET_IRQH… | 56 → **72** | А2 В2 |
| `mess_lsys_krn_sys_getmcontext` | 5 | PM → ядро (kernel call) | `SYS_GETMCONTEXT` | 56 → **64** | А1 |
| `mess_lsys_krn_sys_iopenable` | 5 | libsys → ядро (kernel call, только i386) | `SYS_IOPENABLE` | 56 → 56 |  |
| `mess_lsys_krn_sys_irqctl` | 5 | драйвер (libsys) → ядро (kernel call) | `SYS_IRQCTL` (подзапросы `IRQ_SETPOLICY`, `IRQ_RMPOLICY`, `IRQ_ENABLE`, `IRQ_DISABLE`) | 56 → 56 | Ж1 |
| `mess_lsys_krn_sys_memset` | 5 | libsys → ядро (kernel call) | `SYS_MEMSET` | 56 → **72** | А1 Ф2 Р1 С1 В1 |
| `mess_lsys_krn_sys_privctl` | 5 | RS/VM → ядро (kernel call) | `SYS_PRIVCTL` (подзапросы `SYS_PRIV_ALLOW`, `_YIELD`, `_DISALLOW`, `_CLEAR_IPC_REFS`, `_SET_SYS`, `_SET_USER`, `_ADD_… | 56 → **72** | А1 Ф2 Р1 |
| `mess_lsys_krn_sys_sdevio` | 5 | драйвер (libsys) → ядро (kernel call, только i386) | `SYS_SDEVIO` | 56 → **80** | А1 Р2 С1 Ж1 В1 |
| `mess_lsys_krn_sys_setalarm` | 5 | системный процесс (libsys) → ядро (kernel call); ответ в том же типе | `SYS_SETALARM` | 56 → 56 |  |
| `mess_lsys_krn_sys_setgrant` | 5 | libsys → ядро (kernel call) | `SYS_SETGRANT` | 56 → **64** | А1 В1 |
| `mess_lsys_krn_sys_setmcontext` | 5 | PM → ядро (kernel call) | `SYS_SETMCONTEXT` | 56 → **64** | А1 |
| `mess_lsys_krn_sys_settime` | 5 | PM → ядро (kernel call) | `SYS_SETTIME` | 56 → **64** | С1 В2 |
| `mess_lsys_krn_sys_sprof` | 5 | PM → ядро (kernel call) | `SYS_SPROF` | 56 → **72** | А2 Р1 |
| `mess_lsys_krn_sys_statectl` | 5 | libsys → ядро (kernel call) | `SYS_STATECTL` (подзапросы `SYS_STATE_CLEAR_IPC_REFS`, `_SET_STATE_TABLE`, `_ADD_IPC_BL_FILTER`, `_ADD_IPC_WL_FILTER`… | 56 → **64** | А1 В1 |
| `mess_lsys_krn_sys_stime` | 5 | PM → ядро (kernel call) | `SYS_STIME` | 56 → 56 |  |
| `mess_lsys_krn_sys_times` | 5 | libsys → ядро (kernel call) | `SYS_TIMES` | 56 → 56 |  |
| `mess_lsys_krn_sys_trace` | 5 | PM → ядро (kernel call) | `SYS_TRACE` (подзапросы `T_STOP`, `T_GETINS`, `T_GETDATA`, `T_GETUSER`, `T_SETINS`, `T_SETDATA`, `T_SETUSER`, `T_RESU… | 56 → **64** | А1 Р1 С1 Ж1 |
| `mess_lsys_krn_sys_umap` | 5 | драйвер (libsys) → ядро (kernel call) | `SYS_UMAP`, `SYS_UMAP_REMOTE` | 56 → **64** | А1 У1 В2 |
| `mess_lsys_krn_sys_vdevio` | 5 | драйвер (libsys) → ядро (kernel call, порты x86) | `SYS_VDEVIO` | 56 → **64** | А1 |
| `mess_lsys_krn_sys_vumap` | 5 | драйвер (libsys) → ядро (kernel call) | `SYS_VUMAP` | 56 → **80** | А2 Р1 В3 |
| `mess_lsys_kern_vsafecopy` | 5 | libsys → ядро (kernel call) | `SYS_VSAFECOPY` | 56 → **64** | А1 В1 |
| `mess_notify` | 5 | ядро → любой процесс (уведомление `NOTIFY_MESSAGE`, собирается при доставке) | `NOTIFY_MESSAGE` (источник различается по `m_source`: `HARDWARE` — прерывания, `SYSTEM` — сигналы ядра, `CLOCK` — буд… | 56 → 56 | Ж1 |
| `mess_sigcalls` | 5 | PM (менеджер сигналов) ↔ ядро (kernel calls) | `SYS_GETKSIG`, `SYS_ENDKSIG`, `SYS_KILL`, `SYS_SIGSEND`, `SYS_SIGRETURN` | 56 → **64** | А1 |
| `mess_lc_ipc_semctl` | 6 | libc → IPC (системный вызов SysV IPC) | `IPC_SEMCTL` | 56 → **64** | А1 В1 |
| `mess_lc_ipc_semget` | 6 | libc → IPC | `IPC_SEMGET` | 56 → **64** | С1 В3 |
| `mess_lc_ipc_semop` | 6 | libc → IPC | `IPC_SEMOP` | 56 → **64** | А1 В1 |
| `mess_lc_ipc_shmat` | 6 | libc → IPC | `IPC_SHMAT` | 56 → **72** | А2 В1 |
| `mess_lc_ipc_shmctl` | 6 | libc → IPC | `IPC_SHMCTL` | 56 → **64** | А1 В1 |
| `mess_lc_ipc_shmdt` | 6 | libc → IPC | `IPC_SHMDT` | 56 → **64** | А1 |
| `mess_lc_ipc_shmget` | 6 | libc → IPC | `IPC_SHMGET` | 56 → **64** | Р1 С1 В2 |
| `mess_lc_mib_sysctl` | 6 | libc → MIB (системный вызов) | `MIB_SYSCTL` | 56 → **80** | А3 Р2 В2 |
| `mess_lc_pm_exec` | 6 | libc → PM | `PM_EXEC` | 56 → **80** | А3 Р2 |
| `mess_lc_pm_exit` | 6 | libc → PM | `PM_EXIT` | 56 → 56 |  |
| `mess_lc_pm_getsid` | 6 | libc → PM | `PM_GETSID` | 56 → 56 |  |
| `mess_lc_pm_groups` | 6 | libc → PM | `PM_GETGROUPS`, `PM_SETGROUPS` | 56 → **64** | А1 |
| `mess_lc_pm_itimer` | 6 | libc → PM | `PM_ITIMER` | 56 → **72** | А2 |
| `mess_lc_pm_mcontext` | 6 | libc → PM → ядро | `PM_GETMCONTEXT`, `PM_SETMCONTEXT` | 56 → **64** | А1 |
| `mess_lc_pm_priority` | 6 | libc → PM (→ SCHED для SET) | `PM_GETPRIORITY`, `PM_SETPRIORITY` | 56 → 56 |  |
| `mess_lc_pm_ptrace` | 6 | libc → PM (→ ядро `SYS_TRACE`) | `PM_PTRACE` | 56 → **64** | А1 С1 Ж2 |
| `mess_lc_pm_reboot` | 6 | libc → PM | `PM_REBOOT` | 56 → 56 |  |
| `mess_lc_pm_rusage` | 6 | libc → PM | `PM_GETRUSAGE` | 56 → **64** | А1 |
| `mess_lc_pm_setgid` | 6 | libc → PM (→ VFS `VFS_PM_SETGID`) | `PM_SETGID`, `PM_SETEGID` | 56 → 56 |  |
| `mess_lc_pm_setuid` | 6 | libc → PM (→ VFS `VFS_PM_SETUID`) | `PM_SETUID`, `PM_SETEUID` | 56 → 56 |  |
| `mess_lc_pm_sig` | 6 | libc → PM | `PM_KILL`, `PM_SIGACTION` | 56 → **72** | А3 |
| `mess_lc_pm_sigset` | 6 | libc → PM | `PM_SIGPROCMASK`, `PM_SIGSUSPEND`, `PM_SIGRETURN` | 56 → **64** | А1 В2 |
| `mess_lc_pm_sprof` | 6 | libc → PM → ядро (`SYS_SPROF`) | `PM_SPROF` | 56 → **72** | А2 Р1 Ж1 |
| `mess_lc_pm_sysuname` | 6 |  |  | 56 → **64** | А1 Р1 |
| `mess_lc_pm_time` | 6 | libc → PM (→ ядро `SYS_SETTIME`/`SYS_STIME`) | `PM_CLOCK_GETTIME`, `PM_CLOCK_GETRES`, `PM_CLOCK_SETTIME`, `PM_STIME` | 56 → **64** | С1 |
| `mess_lc_pm_wait4` | 6 | libc → PM | `PM_WAIT4` | 56 → **64** | А1 |
| `mess_lc_readclock_rtcdev` | 6 | команда readclock → драйвер readclock; драйвер readclock (forward) → внешний драйвер RT… | `RTCDEV_GET_TIME`, `RTCDEV_SET_TIME` (поле `tm`), `RTCDEV_GET_TIME_G`, `RTCDEV_SET_TIME_G` (поле `grant`), `RTCDEV_PW… | 56 → **64** | А1 В1 |
| `mess_lc_svrctl` | 6 | libc → PM или VFS | `PM_SVRCTL` (группы ioctl `'P'`, `'M'`), `VFS_SVRCTL` (группа `'F'`) | 56 → **64** | А1 С1 |
| `mess_lc_vm_brk` | 6 | libc → VM | `VM_BRK` | 56 → **64** | А1 |
| `mess_lc_vm_getphys` | 6 | libc (используется сервером IPC) → VM | `VM_GETPHYS` | 56 → **72** | А1 Ф1 |
| `mess_lc_vm_shm_unmap` | 6 | libc (сервер IPC) → VM | `VM_SHM_UNMAP` | 56 → **64** | А1 |
| `mess_lexec_pm_exec_new` | 6 | libexec (в VFS или RS) → PM | `PM_EXEC_NEW` | 56 → **64** | А1 |
| `mess_lsys_getsysinfo` | 6 | libsys → PM / VFS / RS / DS | `PM_GETSYSINFO`, `VFS_GETSYSINFO`, `RS_GETSYSINFO`, `DS_GETSYSINFO` | 56 → **72** | А1 Р1 |
| `mess_lsys_mib_register` | 6 | libsys (rmib, в сервисах) → MIB | `MIB_REGISTER`, `MIB_DEREGISTER` (только `root_id`) | 56 → 56 |  |
| `mess_lsys_mib_reply` | 6 | libsys (rmib) → MIB, ответ на `COMMON_MIB_INFO`/`COMMON_MIB_CALL` | `COMMON_MIB_REPLY` | 56 → **64** | Р1 |
| `mess_lsys_pm_getepinfo` | 6 | libsys → PM | `PM_GETEPINFO` | 56 → **64** | А1 В1 |
| `mess_lsys_pm_getprocnr` | 6 | libsys (RS) → PM | `PM_GETPROCNR` | 56 → 56 |  |
| `mess_lsys_pm_proceventmask` | 6 | libsys (системный сервис) → PM | `PM_PROCEVENTMASK` | 56 → 56 |  |
| `mess_lsys_pm_srv_fork` | 6 | libsys (RS) → PM | `PM_SRV_FORK` | 56 → 56 |  |
| `mess_lsys_sched_scheduling_start` | 6 | libsys (PM, RS) → SCHED | `SCHEDULING_START`, `SCHEDULING_INHERIT` | 56 → 56 |  |
| `mess_lsys_sched_scheduling_stop` | 6 | libsys (PM, RS) → SCHED | `SCHEDULING_STOP` | 56 → 56 |  |
| `mess_mib_lc_sysctl` | 6 | MIB → libc, ответ на `MIB_SYSCTL` (`mess_lc_mib_sysctl`) | код результата | 56 → **64** | Р1 |
| `mess_mib_lsys_call` | 6 | MIB → libsys (rmib в сервисе, владеющем поддеревом) | `COMMON_MIB_CALL` | 56 → **72** | Р2 В5 |
| `mess_mib_lsys_info` | 6 | MIB → libsys (rmib) | `COMMON_MIB_INFO` | 56 → **72** | Р2 В1 |
| `mess_mmap` | 6 | libc → VM | `VM_MMAP`; также `VM_MUNMAP` через псевдонимы `VMUM_ADDR = m_mmap.addr`, `VMUM_LEN = m_mmap.len` (`minix/include/mini… | 56 → **72** | А2 Р1 В4 |
| `mess_pm_lc_getgid` | 6 | PM → libc, ответ на `PM_GETGID` | реальный gid | 56 → 56 |  |
| `mess_pm_lc_getpid` | 6 | PM → libc, ответ на `PM_GETPID` | pid вызывающего | 56 → 56 |  |
| `mess_pm_lc_getuid` | 6 | PM → libc, ответ на `PM_GETUID` | реальный uid | 56 → 56 |  |
| `mess_pm_lc_ptrace` | 6 | PM → libc, ответ на `PM_PTRACE` (`mess_lc_pm_ptrace`) |  | 56 → **64** | С1 Ж1 |
| `mess_pm_lc_sigset` | 6 | PM → libc, ответ на `PM_SIGPROCMASK` и `PM_SIGPENDING` |  | 56 → 56 |  |
| `mess_pm_lc_time` | 6 | PM → libc, ответ на `PM_CLOCK_GETTIME`, `PM_CLOCK_GETRES`, `PM_GETTIMEOFDAY` |  | 56 → **64** | С1 |
| `mess_pm_lc_wait4` | 6 | PM → libc, ответ на `PM_WAIT4` (`mess_lc_pm_wait4`); pid потомка в `m_type` |  | 56 → 56 |  |
| `mess_pm_lexec_exec_new` | 6 | PM → libexec (VFS/RS), ответ на `PM_EXEC_NEW` (`mess_lexec_pm_exec_new`) |  | 56 → 56 |  |
| `mess_pm_lsys_getepinfo` | 6 | PM → libsys, ответ на `PM_GETEPINFO`; pid в `m_type` |  | 56 → 56 |  |
| `mess_pm_lsys_getprocnr` | 6 | PM → libsys (RS), ответ на `PM_GETPROCNR` |  | 56 → 56 |  |
| `mess_pm_lsys_proc_event` | 6 | PM → подписанный сервис (сейчас только IPC) и обратно (эхо-ответ) | `PROC_EVENT` (PM → сервис), `PROC_EVENT_REPLY` (сервис → PM, то же содержимое) | 56 → 56 |  |
| `mess_pm_lsys_sigs_signal` | 6 | менеджер сигналов (PM для RS/системных процессов, RS для сервисов) → libsys SEF | `SIGS_SIGNAL_RECEIVED` | 56 → 56 |  |
| `mess_pm_sched_scheduling_set_nice` | 6 | PM → SCHED | `SCHEDULING_SET_NICE` | 56 → 56 |  |
| `mess_readclock_lc_rtcdev` | 6 | драйвер RTC (readclock, tps65950) → вызывающий, ответ на `RTCDEV_*` (`mess_lc_readclock… | `RTCDEV_REPLY` | 56 → 56 |  |
| `mess_rs_pm_exec_restart` | 6 | RS → PM | `PM_EXEC_RESTART` | 56 → **64** | А2 |
| `mess_rs_pm_srv_kill` | 6 | libsys (RS) → PM | `PM_SRV_KILL` | 56 → 56 |  |
| `mess_sched_lsys_scheduling_start` | 6 | SCHED → libsys, ответ на `SCHEDULING_START`/`SCHEDULING_INHERIT` (`mess_lsys_sched_sche… |  | 56 → 56 |  |
| `mess_lc_vfs_chown` | 7 | libc → VFS (системный вызов, `_syscall`) | `VFS_CHOWN`, `VFS_FCHOWN` | 56 → **64** | А1 Р1 В3 |
| `mess_lc_vfs_close` | 7 | libc/libsys → VFS | `VFS_CLOSE` | 56 → 56 |  |
| `mess_lc_vfs_creat` | 7 | libc → VFS | `VFS_CREAT` (выбирается в `open()`, если в флагах `O_CREAT`) | 56 → **64** | А1 Р1 В2 |
| `mess_lc_vfs_fchdir` | 7 | libc → VFS | `VFS_FCHDIR` | 56 → 56 |  |
| `mess_lc_vfs_fchmod` | 7 | libc → VFS | `VFS_FCHMOD` | 56 → 56 |  |
| `mess_lc_vfs_fcntl` | 7 | libc → VFS; также VFS сам себе (восстановление `F_SETLKW` в `unblock()`, `minix/servers… | `VFS_FCNTL` | 56 → **64** | А1 |
| `mess_lc_vfs_fstat` | 7 | libc → VFS | `VFS_FSTAT` | 56 → **64** | А1 |
| `mess_lc_vfs_fsync` | 7 | libc → VFS | `VFS_FSYNC` | 56 → 56 |  |
| `mess_lc_vfs_gcov` | 7 | libc → VFS | `VFS_GCOV_FLUSH` | 56 → **72** | А2 Р2 |
| `mess_lc_vfs_getvfsstat` | 7 | libc → VFS | `VFS_GETVFSSTAT` | 56 → **72** | А1 Р1 |
| `mess_lc_vfs_ioctl` | 7 | libc → VFS | `VFS_IOCTL` | 56 → **72** | А1 С1 |
| `mess_lc_vfs_link` | 7 | libc → VFS | `VFS_LINK`, `VFS_RENAME`, `VFS_SYMLINK` | 56 → **72** | А2 Р2 |
| `mess_lc_vfs_listen` | 7 | libc → VFS | `VFS_LISTEN` | 56 → 56 |  |
| `mess_lc_vfs_lseek` | 7 | libc → VFS | `VFS_LSEEK` | 56 → 56 |  |
| `mess_lc_vfs_mknod` | 7 | libc → VFS | `VFS_MKNOD` | 56 → **64** | А1 Р1 В1 |
| `mess_lc_vfs_mount` | 7 | libc → VFS | `VFS_MOUNT` | 56 → **96** | А4 Р4 |
| `mess_lc_vfs_path` | 7 | libc → VFS | `VFS_OPEN` (без `O_CREAT`), `VFS_ACCESS`, `VFS_CHDIR`, `VFS_CHROOT`, `VFS_CHMOD`, `VFS_MKDIR`, `VFS_UNLINK`, `VFS_RMDIR` | 56 → **64** | А1 Р1 В3 |
| `mess_lc_vfs_pipe2` | 7 | libc → VFS | `VFS_PIPE2` | 56 → 56 |  |
| `mess_lc_vfs_readlink` | 7 | libc → VFS | `VFS_READLINK` | 56 → **72** | А2 Р2 |
| `mess_lc_vfs_readwrite` | 7 | libc → VFS; также VFS сам себе (восстановление чтения/записи в канал в `unblock()`, `mi… | `VFS_READ`, `VFS_WRITE`, `VFS_GETDENTS` | 56 → **72** | А1 Р2 |
| `mess_lc_vfs_select` | 7 | libc → VFS | `VFS_SELECT` | 56 → **80** | А4 |
| `mess_lc_vfs_sendrecv` | 7 | libc → VFS | `VFS_SENDTO`, `VFS_RECVFROM` | 56 → **80** | А2 Р1 В2 |
| `mess_lc_vfs_shutdown` | 7 | libc → VFS | `VFS_SHUTDOWN` | 56 → 56 |  |
| `mess_lc_vfs_sockaddr` | 7 | libc → VFS | `VFS_BIND`, `VFS_CONNECT`, `VFS_ACCEPT`, `VFS_GETSOCKNAME`, `VFS_GETPEERNAME` | 56 → **64** | А1 В1 |
| `mess_lc_vfs_socket` | 7 | libc → VFS | `VFS_SOCKET`, `VFS_SOCKETPAIR` | 56 → 56 |  |
| `mess_lc_vfs_sockmsg` | 7 | libc → VFS | `VFS_SENDMSG`, `VFS_RECVMSG` | 56 → **64** | А1 В1 |
| `mess_lc_vfs_sockopt` | 7 | libc → VFS | `VFS_SETSOCKOPT`, `VFS_GETSOCKOPT` | 56 → **64** | А1 В1 |
| `mess_lc_vfs_stat` | 7 | libc → VFS | `VFS_STAT`, `VFS_LSTAT` | 56 → **72** | А2 Р1 |
| `mess_lc_vfs_statvfs1` | 7 | libc → VFS | `VFS_STATVFS1`, `VFS_FSTATVFS1` | 56 → **72** | А2 Р1 |
| `mess_lc_vfs_truncate` | 7 | libc → VFS | `VFS_TRUNCATE`, `VFS_FTRUNCATE` | 56 → **72** | А1 Р1 |
| `mess_lc_vfs_umask` | 7 | libc → VFS | `VFS_UMASK` | 56 → 56 |  |
| `mess_lc_vfs_umount` | 7 | libc → VFS | `VFS_UMOUNT` | 56 → **72** | А2 Р2 |
| `mess_lsys_vfs_copyfd` | 7 | libsys (сервис/драйвер) → VFS, обратный вызов во время обработки запроса пользователя | `VFS_COPYFD` | 56 → 56 |  |
| `mess_lsys_vfs_mapdriver` | 7 | libsys (RS) → VFS | `VFS_MAPDRIVER` | 56 → **72** | А1 Р1 В2 |
| `mess_lsys_vfs_socketpath` | 7 | libsys (UDS) → VFS | `VFS_SOCKETPATH` | 56 → **64** | Р1 В1 |
| `mess_vfs_lc_fdpair` | 7 | VFS → libc (ответ) | ответ на `VFS_PIPE2` и `VFS_SOCKETPAIR` | 56 → 56 |  |
| `mess_vfs_lc_lseek` | 7 | VFS → libc (ответ) | ответ на `VFS_LSEEK` | 56 → 56 |  |
| `mess_vfs_lc_socklen` | 7 | VFS → libc (ответ) | ответ на `VFS_ACCEPT`, `VFS_RECVFROM`, `VFS_GETSOCKOPT`, `VFS_GETSOCKNAME`, `VFS_GETPEERNAME` | 56 → 56 |  |
| `mess_vfs_lchardriver_cancel` | 7 | VFS → символьный драйвер (libchardriver) | `CDEV_CANCEL` | 56 → 56 |  |
| `mess_vfs_lchardriver_openclose` | 7 | VFS → символьный драйвер | `CDEV_OPEN`, `CDEV_CLOSE` | 56 → 56 |  |
| `mess_vfs_lchardriver_readwrite` | 7 | VFS → символьный драйвер | `CDEV_READ`, `CDEV_WRITE`, `CDEV_IOCTL` | 56 → **72** | Р1 С1 В4 |
| `mess_vfs_lchardriver_select` | 7 | VFS → символьный драйвер | `CDEV_SELECT` | 56 → 56 |  |
| `mess_vfs_lsys_gcov` | 7 | VFS → любой сервис (libsys) | `COMMON_REQ_GCOV_DATA` | 56 → **64** | Р1 |
| `mess_vfs_lsys_socketpath` | 7 | VFS → libsys (UDS), ответ | ответ на `VFS_SOCKETPATH` | 56 → 56 |  |
| `mess_vfs_utimens` | 7 | libc → VFS (несмотря на имя без `lc_`) | `VFS_UTIMENS` | 56 → **72** | А1 Р1 С2 В2 |
| `mess_vm_vfs_mmap` | 7 | **VFS → VM** (имя вводит в заблуждение) | `VM_VFS_MMAP` | 56 → 56 | У2 |
| `mess_fs_vfs_breadwrite` | 8 | ФС → VFS (ответ на запрос `mess_vfs_fs_breadwrite`) | ответ на `REQ_BREAD`, `REQ_BWRITE`, `REQ_BPEEK`; `m_type` ответа = код результата (`TRNS_ADD_ID(r, transid)`, `minix/… | 56 → **64** | Р1 В1 |
| `mess_fs_vfs_chmod` | 8 | ФС → VFS (ответ на `mess_vfs_fs_chmod`) | ответ на `REQ_CHMOD`, `m_type` = код результата | 56 → 56 |  |
| `mess_fs_vfs_chown` | 8 | ФС → VFS (ответ на `mess_vfs_fs_chown`) | ответ на `REQ_CHOWN`, `m_type` = код результата | 56 → 56 |  |
| `mess_fs_vfs_create` | 8 | ФС → VFS (ответ на `mess_vfs_fs_create`) | ответ на `REQ_CREATE`, `m_type` = код результата | 56 → 56 |  |
| `mess_fs_vfs_getdents` | 8 | ФС → VFS (ответ на `mess_vfs_fs_getdents`) | ответ на `REQ_GETDENTS`, `m_type` = код результата | 56 → **64** | Р1 В1 |
| `mess_fs_vfs_lookup` | 8 | ФС → VFS (ответ на `mess_vfs_fs_lookup`) | ответ на `REQ_LOOKUP`; `m_type` = `OK`, `EENTERMOUNT`, `ELEAVEMOUNT`, `ESYMLINK` (`minix/include/minix/vfsif.h`) или … | 56 → 56 |  |
| `mess_fs_vfs_newnode` | 8 | ФС → VFS (ответ на `mess_vfs_fs_newnode`) | ответ на `REQ_NEWNODE`, `m_type` = код результата | 56 → 56 |  |
| `mess_fs_vfs_rdlink` | 8 | ФС → VFS (ответ на `mess_vfs_fs_rdlink`) | ответ на `REQ_RDLINK`, `m_type` = код результата | 56 → **64** | Р1 В1 |
| `mess_fs_vfs_readsuper` | 8 | ФС → VFS (ответ на `mess_vfs_fs_readsuper`) | ответ на `REQ_READSUPER`, `m_type` = код результата | 56 → 56 |  |
| `mess_fs_vfs_readwrite` | 8 | ФС → VFS (ответ на `mess_vfs_fs_readwrite`) | ответ на `REQ_READ`, `REQ_WRITE`, `REQ_PEEK`; `m_type` = код результата | 56 → **64** | Р1 В1 |
| `mess_lbdev_lblockdriver_msg` | 8 | клиент блочного устройства (libbdev в ФС, VFS, драйверы-фильтры) → блочный драйвер (lib… | `BDEV_OPEN`, `BDEV_CLOSE`, `BDEV_READ`, `BDEV_WRITE`, `BDEV_GATHER`, `BDEV_SCATTER`, `BDEV_IOCTL` (`minix/include/min… | 56 → **64** | С1 У1 |
| `mess_lblockdriver_lbdev_reply` | 8 | блочный драйвер → клиент (libbdev, VFS, fbd, filter, blocktest) | `BDEV_REPLY` — ответ на все `BDEV_*` из `mess_lbdev_lblockdriver_msg` | 56 → 56 | У1 |
| `mess_lchardriver_vfs_reply` | 8 | символьный драйвер (libchardriver) → VFS | `CDEV_REPLY` — ответ на `CDEV_OPEN`, `CDEV_CLOSE`, `CDEV_READ`, `CDEV_WRITE`, `CDEV_IOCTL`, а также на `CDEV_CANCEL` … | 56 → 56 | У1 |
| `mess_lchardriver_vfs_sel1` | 8 | символьный драйвер → VFS | `CDEV_SEL1_REPLY` — немедленный ответ на `CDEV_SELECT` | 56 → 56 |  |
| `mess_lchardriver_vfs_sel2` | 8 | символьный драйвер → VFS (асинхронно, `asynsend3(AMF_NOREPLY)`) | `CDEV_SEL2_REPLY` — отложенное уведомление о готовности после `CDEV_SELECT` с `CDEV_NOTIFY` | 56 → 56 |  |
| `mess_lsockdriver_vfs_accept_reply` | 8 | драйвер сокетов (libsockdriver) → VFS | `SDEV_ACCEPT_REPLY` — ответ на `SDEV_ACCEPT` | 56 → 56 |  |
| `mess_lsockdriver_vfs_recv_reply` | 8 | драйвер сокетов → VFS | `SDEV_RECV_REPLY` — ответ на `SDEV_RECV` | 56 → 56 | У1 |
| `mess_lsockdriver_vfs_reply` | 8 | драйвер сокетов → VFS | `SDEV_REPLY` — общий ответ на `SDEV_BIND`, `SDEV_CONNECT`, `SDEV_LISTEN`, `SDEV_SEND`, `SDEV_IOCTL`, `SDEV_SETSOCKOPT… | 56 → 56 | У1 |
| `mess_lsockdriver_vfs_select_reply` | 8 | драйвер сокетов → VFS | `SDEV_SELECT1_REPLY` (немедленный ответ на `SDEV_SELECT`), `SDEV_SELECT2_REPLY` (отложенное уведомление при `SDEV_NOT… | 56 → 56 |  |
| `mess_lsockdriver_vfs_socket_reply` | 8 | драйвер сокетов → VFS | `SDEV_SOCKET_REPLY` — ответ на `SDEV_SOCKET`, `SDEV_SOCKETPAIR` | 56 → 56 |  |
| `mess_pty_ptyfs_req` | 8 | драйвер PTY → PTYFS (`ipc_sendrec`, endpoint по метке DS `"ptyfs"`) | `PTYFS_SET`, `PTYFS_CLEAR`, `PTYFS_NAME` (`minix/include/minix/com.h:901`) | 56 → 56 |  |
| `mess_ptyfs_pty_name` | 8 | PTYFS → PTY (ответ) | ответ на `PTYFS_NAME`, `m_type` = код результата | 56 → 56 |  |
| `mess_vfs_fs_breadwrite` | 8 | VFS → ФС (`fs_sendrec`, `minix/servers/vfs/comm.c:134`) | `REQ_BREAD`, `REQ_BWRITE` (ввод-вывод блочного спецфайла через буферный кэш ФС, на `v_bfs_e`), `REQ_BPEEK` (подкачка … | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_chmod` | 8 | VFS → ФС | `REQ_CHMOD` | 56 → 56 |  |
| `mess_vfs_fs_chown` | 8 | VFS → ФС | `REQ_CHOWN` | 56 → 56 |  |
| `mess_vfs_fs_create` | 8 | VFS → ФС | `REQ_CREATE` | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_flush` | 8 | VFS → ФС | `REQ_FLUSH` (сброс и инвалидация кэша блоков устройства) | 56 → 56 |  |
| `mess_vfs_fs_ftrunc` | 8 | VFS → ФС | `REQ_FTRUNC` (truncate/ftruncate и освобождение диапазона) | 56 → 56 |  |
| `mess_vfs_fs_getdents` | 8 | VFS → ФС | `REQ_GETDENTS` | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_inhibread` | 8 | VFS → ФС | `REQ_INHIBREAD` (lseek: подавить упреждающее чтение) | 56 → 56 |  |
| `mess_vfs_fs_link` | 8 | VFS → ФС | `REQ_LINK` | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_lookup` | 8 | VFS → ФС | `REQ_LOOKUP` | 56 → **72** | Р3 В5 |
| `mess_vfs_fs_mkdir` | 8 | VFS → ФС | `REQ_MKDIR` | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_mknod` | 8 | VFS → ФС | `REQ_MKNOD` | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_mountpoint` | 8 | VFS → ФС | `REQ_MOUNTPOINT` (пометить inode как точку монтирования) | 56 → 56 |  |
| `mess_vfs_fs_new_driver` | 8 | VFS → ФС | `REQ_NEW_DRIVER` (блочный драйвер устройства ФС перезапущен / сменил endpoint) | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_newnode` | 8 | VFS → ФС (на практике PFS) | `REQ_NEWNODE` (безымянный узел: pipe, cloned cdev, сокет) | 56 → 56 |  |
| `mess_vfs_fs_putnode` | 8 | VFS → ФС | `REQ_PUTNODE` (сбросить ссылки VFS на inode) | 56 → 56 |  |
| `mess_vfs_fs_rdlink` | 8 | VFS → ФС | `REQ_RDLINK` | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_readsuper` | 8 | VFS → ФС | `REQ_READSUPER` (монтирование; единственный запрос, принимаемый до монтирования, `fsdriver.c:39`) | 56 → **64** | Р1 В2 |
| `mess_vfs_fs_readwrite` | 8 | VFS → ФС | `REQ_READ`, `REQ_WRITE`, `REQ_PEEK` (заполнение кэша VM для mmap файла) | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_rename` | 8 | VFS → ФС | `REQ_RENAME` | 56 → **64** | Р2 В3 |
| `mess_vfs_fs_slink` | 8 | VFS → ФС | `REQ_SLINK` | 56 → **64** | Р2 В5 |
| `mess_vfs_fs_stat` | 8 | VFS → ФС | `REQ_STAT` | 56 → 56 |  |
| `mess_vfs_fs_statvfs` | 8 | VFS → ФС | `REQ_STATVFS` | 56 → 56 |  |
| `mess_vfs_fs_unlink` | 8 | VFS → ФС | `REQ_UNLINK`, `REQ_RMDIR` | 56 → **64** | Р1 В1 |
| `mess_vfs_fs_utime` | 8 | VFS → ФС | `REQ_UTIME` | 56 → 56 |  |
| `mess_vfs_lsockdriver_addr` | 8 | VFS → драйвер сокетов (`asynsend3(AMF_NOREPLY)`) | `SDEV_BIND`, `SDEV_CONNECT`, `SDEV_ACCEPT` | 56 → 56 |  |
| `mess_vfs_lsockdriver_getset` | 8 | VFS → драйвер сокетов (через `sdev_sendrec`, поток VFS ждёт ответ) | `SDEV_SETSOCKOPT`, `SDEV_GETSOCKOPT`, `SDEV_GETSOCKNAME`, `SDEV_GETPEERNAME` | 56 → 56 |  |
| `mess_vfs_lsockdriver_ioctl` | 8 | VFS → драйвер сокетов (`asynsend3`, процесс приостанавливается) | `SDEV_IOCTL` | 56 → **64** | С1 В3 |
| `mess_vfs_lsockdriver_select` | 8 | VFS → драйвер сокетов (`asynsend3`) | `SDEV_SELECT` | 56 → 56 |  |
| `mess_vfs_lsockdriver_sendrecv` | 8 | VFS → драйвер сокетов (`asynsend3`, процесс приостанавливается) | `SDEV_SEND`, `SDEV_RECV` | 56 → **64** | Р1 В6 |
| `mess_vfs_lsockdriver_simple` | 8 | VFS → драйвер сокетов | `SDEV_LISTEN`, `SDEV_SHUTDOWN`, `SDEV_CLOSE`, `SDEV_CANCEL` | 56 → 56 |  |
| `mess_vfs_lsockdriver_socket` | 8 | VFS → драйвер сокетов (через `sdev_sendrec`) | `SDEV_SOCKET`, `SDEV_SOCKETPAIR` | 56 → 56 |  |
| `mess_ds_reply` | 9 | DS → libsys (ответ) | ответ на `DS_RETRIEVE` (в `m_type` — код результата) | 56 → 56 | У1 |
| `mess_ds_req` | 9 | libsys → DS (запрос; для `DS_CHECK` — также ответ DS → libsys) | `DS_PUBLISH`, `DS_RETRIEVE`, `DS_RETRIEVE_LABEL`, `DS_DELETE`, `DS_SUBSCRIBE`, `DS_CHECK` (`DS_SNAPSHOT` объявлен в `… | 56 → 56 | У1 |
| `mess_i2c_li2cdriver_busc_i2c_exec` | 9 | шина I2C → libi2cdriver (ответ) | ответ на `BUSC_I2C_EXEC` | 56 → 56 |  |
| `mess_i2c_li2cdriver_busc_i2c_reserve` | 9 | шина I2C → libi2cdriver (ответ) | ответ на `BUSC_I2C_RESERVE` (`OK`, `EBUSY`, `EINVAL`…) | 56 → 56 |  |
| `mess_input_linputdriver_input_conf` | 9 | сервер INPUT → драйвер ввода (libinputdriver), одностороннее (asynsend, `AMF_NOREPLY`) | `INPUT_CONF` | 56 → 56 |  |
| `mess_input_linputdriver_setleds` | 9 | (1) INPUT → драйвер ввода; (2) TTY → INPUT | `INPUT_SETLEDS`. Отправители: `input_set_leds()` (`minix/servers/input/input.c:214-215`, рассылает всем клавиатурам);… | 56 → 56 |  |
| `mess_input_tty_event` | 9 | INPUT → TTY, одностороннее (`ipc_send`) | `TTY_INPUT_EVENT` | 56 → 56 |  |
| `mess_li2cdriver_i2c_busc_i2c_exec` | 9 | libi2cdriver (драйвер устройства I2C) → драйвер шины I2C | `BUSC_I2C_EXEC` | 56 → 56 |  |
| `mess_li2cdriver_i2c_busc_i2c_reserve` | 9 | libi2cdriver → драйвер шины I2C | `BUSC_I2C_RESERVE` | 56 → 56 |  |
| `mess_linputdriver_input_event` | 9 | драйвер ввода (libinputdriver) → INPUT, одностороннее (блокирующий `ipc_send`) | `INPUT_EVENT` | 56 → 56 |  |
| `mess_lsys_fi_ctl` | 9 | RS → любой сервис (SEF), одностороннее (`rs_asynsend`) | `COMMON_REQ_FI_CTL` (по команде `RS_FI` от `minix-service fi <label>`) | 56 → **64** | Р1 В1 |
| `mess_lsys_fi_reply` | 9 | предположительно сервис → RS (ответ на `COMMON_REQ_FI_CTL`) | нет | 56 → 56 |  |
| `mess_lsys_pci_busc_get_bar` | 9 | libsys → драйвер шины PCI | `BUSC_PCI_GET_BAR` | 56 → 56 | Ж1 |
| `mess_lsys_tty_fkey_ctl` | 9 | libsys → TTY | `TTY_FKEY_CONTROL` | 56 → 56 |  |
| `mess_lsys_vm_getref` | 9 | libc → VM (несмотря на `lsys` в имени, код в `minix/lib/libc/sys/mmap.c`) | `VM_GETREF` | 56 → **64** | А1 В1 |
| `mess_lsys_vm_info` | 9 | libsys → VM | `VM_INFO` | 56 → **72** | А2 |
| `mess_lsys_vm_map_phys` | 9 | libsys → VM | `VM_MAP_PHYS` | 56 → **72** | А1 Ф1 Р1 |
| `mess_lsys_vm_rusage` | 9 | PM (через libsys) → VM | `VM_GETRUSAGE` | 56 → **64** | А1 В1 |
| `mess_lsys_vm_unmap_phys` | 9 | libsys → VM | `VM_UNMAP_PHYS` | 56 → **64** | А1 |
| `mess_lsys_vm_update` | 9 | RS (libsys) → VM; также локально внутри VM | `VM_RS_UPDATE`, `VM_RS_PREPARE` | 56 → 56 |  |
| `mess_lsys_vm_vmremap` | 9 | libc → VM | `VM_REMAP`, `VM_REMAP_RO` | 56 → **72** | А3 Р1 |
| `mess_ndev_netdriver_init` | 9 | стек TCP/IP lwip (ndev) → сетевой драйвер (libnetdriver), asynsend | `NDEV_INIT` | 56 → 56 |  |
| `mess_ndev_netdriver_conf` | 9 | lwip ndev → сетевой драйвер | `NDEV_CONF` | 56 → 56 |  |
| `mess_ndev_netdriver_transfer` | 9 | lwip ndev → сетевой драйвер, asynsend | `NDEV_SEND`, `NDEV_RECV` | 56 → 56 |  |
| `mess_ndev_netdriver_status_reply` | 9 | lwip ndev → сетевой драйвер (подтверждение) | `NDEV_STATUS_REPLY` (ответ на `NDEV_STATUS`) | 56 → 56 |  |
| `mess_netdriver_ndev_init_reply` | 9 | сетевой драйвер → lwip ndev | `NDEV_INIT_REPLY` (ответ на `NDEV_INIT`) | 56 → 56 |  |
| `mess_netdriver_ndev_reply` | 9 | сетевой драйвер → lwip ndev | `NDEV_CONF_REPLY`, `NDEV_SEND_REPLY`, `NDEV_RECV_REPLY` | 56 → 56 |  |
| `mess_netdriver_ndev_status` | 9 | сетевой драйвер → lwip ndev (asynsend, `AMF_NOREPLY`), инициирует драйвер | `NDEV_STATUS` | 56 → 56 |  |
| `mess_net_netdrv_dl_conf` | 9 | (было) INET → сетевой драйвер | `DL_CONF` (`com.h:176`) | 56 → 56 |  |
| `mess_net_netdrv_dl_getstat_s` | 9 | (было) INET → сетевой драйвер | `DL_GETSTAT_S` | 56 → 56 |  |
| `mess_net_netdrv_dl_readv_s` | 9 | (было) INET → сетевой драйвер | `DL_READV_S` | 56 → 56 |  |
| `mess_net_netdrv_dl_writev_s` | 9 | (было) INET → сетевой драйвер | `DL_WRITEV_S` | 56 → 56 |  |
| `mess_netdrv_net_dl_conf` | 9 | (было) сетевой драйвер → INET | `DL_CONF_REPLY` | 56 → 56 |  |
| `mess_netdrv_net_dl_task` | 9 | (было) сетевой драйвер → INET | `DL_TASK_REPLY` | 56 → 56 |  |
| `mess_pci_lsys_busc_get_bar` | 9 | драйвер шины PCI → libsys (ответ) | ответ на `BUSC_PCI_GET_BAR` (`m_type` = `OK`/`EINVAL`; поля заполняются только при `OK`) | 56 → **64** | Ф1 Р1 У1 Ж1 В1 |
| `mess_rs_init` | 9 | RS → сервис (SEF), и обратно сервис → RS (ответ «init ready») | `RS_INIT` в обе стороны | 56 → **64** | А1 Р1 В1 |
| `mess_rs_req` | 9 | пользователь/сервис → RS | `RS_UP`, `RS_EDIT`, `RS_UPDATE` (поле `addr` → `struct rs_start`); `RS_DOWN`, `RS_REFRESH`, `RS_RESTART`, `RS_CLONE`,… | 56 → **72** | А2 В1 |
| `mess_rs_update` | 9 | (1) `minix-service` → RS (`RS_UPDATE`, вместе с `m_rs_req`); (2) RS → сервис (`RS_LU_PR… |  | 56 → 56 |  |
| `mess_tty_lsys_fkey_ctl` | 9 | TTY → libsys (ответ) | ответ на `TTY_FKEY_CONTROL` (`m_type` = `OK`/`EPERM`/`EINVAL`) | 56 → 56 |  |
| `mess_vmmcp` | 9 | ФС-сервер (libminixfs через libsys) → VM | `VM_MAPCACHEPAGE`, `VM_SETCACHEPAGE`, `VM_FORGETCACHEPAGE`, `VM_CLEARCACHE` | 56 → **64** | А2 В2 |
| `mess_vmmcp_reply` | 9 | VM → ФС (libsys), ответ | ответ на `VM_MAPCACHEPAGE` (`OK`/`ENOENT`/`ENOMEM`/…) | 56 → **64** | А1 В1 |

## 11. Сводка пометок

| Пометка | Полей (вкл. псевдонимы и протоколы на общих раскладках) |
|---|---|
| **А** | 132 |
| **Ф** | 16 |
| **Р** | 90 |
| **С** | 62 |
| **У** | 23 |
| **Ж** | 30 |
| **В** | 116 |

Поля, требующие решения при переработке в первую очередь — физические адреса (**Ф**), адреса и размеры в узких полях (**У**) и аппаратно-зависимые значения (**Ж**):


### 11.1 Физические адреса (Ф)

| Тип / раздел | Поле | Тип поля | Назначение (кратко) |
|---|---|---|---|
| `mess_krn_lsys_sys_umap` | `dst_addr` | `phys_bytes` | Физический адрес, соответствующий виртуальному адресу/гранту (`vm_lookup()`, `do_umap_remote.c:94-111`); используется драйверами для DMA. Пе |
| `mess_lsys_krn_readbios` | `addr` | `phys_bytes` | Физический адрес в BIOS-области; допускаются только [0, 0x4FF] и [0x90000, 0xFFFFF] (`do_readbios.c:21,32-34`, `memory.h:29-33,118-119`); ко |
| `mess_lsys_krn_sys_copy` | `src_addr` | `vir_bytes` | Адрес источника: виртуальный в `src_endpt` или физический при `NONE` (`do_copy.c:54`). |
| `mess_lsys_krn_sys_copy` | `dst_addr` | `vir_bytes` | Адрес приёмника: виртуальный или физический при `NONE` (`do_copy.c:55`). |
| `mess_lsys_krn_sys_copy` | `nr_bytes` | `phys_bytes` | Число байт; проверяется, что умещается в `vir_bytes` (`do_copy.c:56,77`). |
| `mess_lsys_krn_sys_memset` | `base` | `phys_bytes` | Начало области: **физический** адрес при `process == NONE` (VM чистит страницы) или виртуальный адрес в `process` (`memory.c:539-541`). Комм |
| `mess_lsys_krn_sys_memset` | `count` | `phys_bytes` | Длина области в байтах (комментарий в `do_memset.c` «returns physical address» ошибочен). |
| `mess_lsys_krn_sys_privctl` | `phys_start` | `phys_bytes` | Только `QUERY_MEM`: начало физического диапазона (MMIO/память), проверяемого по `s_mem_tab` (`do_privctl.c:239-250`). |
| `mess_lsys_krn_sys_privctl` | `phys_len` | `phys_bytes` | Только `QUERY_MEM`: длина физического диапазона в байтах (`do_privctl.c:240`). |
| (псевдонимы/протоколы, §5) | `SVMCTL_VALUE` | `m1_i3` | Многоцелевое: `MEMREQ_REPLY` — код результата VM (`do_vmctl.c:86`); `KERN_PHYSMAP`/`KERN_MAP_REPLY` — индекс отображения ядра (`:114,122`);  |
| (псевдонимы/протоколы, §5) | `SVMCTL_PTROOT` | `m1_i3` | `SETADDRSPACE`: **физический адрес корня таблиц страниц** (CR3 на i386, TTBR0 на earm) процесса `SVMCTL_WHO` (`pt->pt_dir_phys`, `minix/serv |
| (псевдонимы/протоколы, §5) | `SVMCTL_MAP_PHYS_ADDR` | `m2_l1` | Ответ `KERN_PHYSMAP`: физический адрес области, которую ядру нужно отобразить (MMIO LAPIC/IOAPIC/OXPCIe, видеопамять, страницы usermapped);  |
| (псевдонимы/протоколы, §5) | `SVMCTL_MAP_PHYS_LEN` | `m2_l2` | Ответ `KERN_PHYSMAP`: длина области в байтах (запись через `(phys_bytes *)`, `do_vmctl.c:117`). |
| `mess_lc_vm_getphys` | `ret_addr` | `void *` | Ответ: **физический** адрес страницы (`phys_bytes` из `map_get_phys()`, приведённый к `void *`, `mmap.c:456`); libc возвращает его как `unsi |
| `mess_lsys_vm_map_phys` | `phaddr` | `phys_bytes` | Физический адрес начала (не обязательно выровнен); право проверяется `map_perm_check()` → `sys_privquery_mem` (`mmap.c:337`). VM приводит ег |
| `mess_pci_lsys_busc_get_bar` | `base` | `int` | Базовый адрес BAR: физический MMIO-адрес или номер порта ввода-вывода (`pb_base`, `u32_t`) (`pci.c:2461`) → `u32_t *base`. 64-битные BAR не  |

### 11.2 Адреса и размеры в узких полях (У)

| Тип / раздел | Поле | Тип поля | Назначение (кратко) |
|---|---|---|---|
| `mess_lsys_krn_sys_umap` | `nr_bytes` | `int` | Длина области в байтах; должна быть физически непрерывной (`vm_lookup_range`, `do_umap_remote.c:106`). Libsys получает `vir_bytes bytes` и о |
| (псевдонимы/протоколы, §5) | `SVMCTL_VALUE` | `m1_i3` | Многоцелевое: `MEMREQ_REPLY` — код результата VM (`do_vmctl.c:86`); `KERN_PHYSMAP`/`KERN_MAP_REPLY` — индекс отображения ядра (`:114,122`);  |
| (псевдонимы/протоколы, §5) | `SVMCTL_MRG_ADDR` | `m2_i2` | Ответ `MEMREQ_GET`: начальный виртуальный адрес диапазона (`params.check.start` — `vir_bytes`, сужается до `int`, `do_vmctl.c:62`) (исх.: ME |
| (псевдонимы/протоколы, §5) | `SVMCTL_MRG_LENGTH` | `m2_i3` | Ответ `MEMREQ_GET`: длина диапазона в байтах (`params.check.length` — `vir_bytes`→`int`, `do_vmctl.c:64`) (исх.: MEMREQ_GET reply: length). |
| (псевдонимы/протоколы, §5) | `SVMCTL_PTROOT` | `m1_i3` | `SETADDRSPACE`: **физический адрес корня таблиц страниц** (CR3 на i386, TTBR0 на earm) процесса `SVMCTL_WHO` (`pt->pt_dir_phys`, `minix/serv |
| (псевдонимы/протоколы, §6) | `VFS_PM_PATH_LEN` | `m7_i2` | `VFS_PM_EXEC`: длина пути с нулём (из `size_t namelen`, `exec.c:47`; `vfs/main.c:683`). |
| (псевдонимы/протоколы, §6) | `VFS_PM_FRAME_LEN` | `m7_i3` | `VFS_PM_EXEC`: размер кадра в байтах (из `size_t framelen`, `exec.c:49`; `vfs/main.c:685`). (исх.: size of frame) |
| (псевдонимы/протоколы, §6) | `VFS_PM_PS_STR` | `m7_i5` | `VFS_PM_EXEC`: **адрес** `ps_strings` в новом стеке в `int`-поле (`exec.c:50`, `vfs/main.c:686`). (исх.: ps_strings pointer) |
| (псевдонимы/протоколы, §6) | `VFS_PM_NEWPS_STR` | `m7_i5` | `VFS_PM_EXEC_REPLY`: **адрес** `ps_strings` в `int`-поле (`vfs/main.c:699`, `pm/main.c:352`). (исх.: possibly-changed ps_strings ptr) |
| (псевдонимы/протоколы, §6) | `VMEN_ARGSSIZE` | `m1_i2` | **Не используется**; по смыслу — размер аргументов в `int`. |
| `mess_vm_vfs_mmap` | `vaddr` | `__uint32_t` | Виртуальный адрес сегмента ELF (из `vir_bytes`), отображение `MAP_FIXED` |
| `mess_vm_vfs_mmap` | `len` | `__uint32_t` | Длина отображения, байт (из `vir_bytes`/`size_t`) |
| (псевдонимы/протоколы, §7) | `VFS_VMCALL_LENGTH` | `m10_l3` | Там же: длина `FDIO` (PEEK) в байтах; VM передаёт `u32_t`, VFS читает в `u32_t` (`misc.c:392`) |
| `mess_lbdev_lblockdriver_msg` | `count` | `int` | READ/WRITE: длина буфера в байтах (`driver.c:206` → `iov_size`; отрицательное — `EINVAL`, `driver.c:202`); GATHER/SCATTER: число элементов в |
| `mess_lblockdriver_lbdev_reply` | `status` | `int` | Результат: для READ/WRITE/GATHER/SCATTER — число переданных байт (`ssize_t r` из `bdr_transfer`, `driver.c:216`) или отрицательная ошибка; д |
| `mess_lchardriver_vfs_reply` | `status` | `int` | OPEN: `OK` или новый minor с флагами `CDEV_CLONED`/`CDEV_CTTY` в старших битах (`cdev.c:231`); READ/WRITE: число переданных байт или ошибка; |
| `mess_lsockdriver_vfs_recv_reply` | `status` | `int` | Число принятых байт данных (≥ 0, `data_len` урезан до `INT_MAX`, `sockdriver.c:740`) или ошибка (`sdev.c:827`). |
| `mess_lsockdriver_vfs_reply` | `status` | `int` | `OK`/ошибка; для `SDEV_SEND` — число отправленных байт (`data_len` урезан до `INT_MAX`, `sockdriver.c:669`); для GETSOCKOPT/GETSOCKNAME/GETP |
| `mess_ds_reply` | `val_len` | `int` | Для STR/MEM — сколько байт фактически скопировано в буфер клиента = MIN(запрошено, длина записи) (`store.c:412,420`) → `*length` (`ds.c:144` |
| `mess_ds_req` | `val_len` | `int` | STR/MEM: длина буфера значения в байтах (PUBLISH — размер данных, `store.c:338`; RETRIEVE — ёмкость буфера клиента, `store.c:412`). Приходит |
| `mess_pci_lsys_busc_get_bar` | `base` | `int` | Базовый адрес BAR: физический MMIO-адрес или номер порта ввода-вывода (`pb_base`, `u32_t`) (`pci.c:2461`) → `u32_t *base`. 64-битные BAR не  |
| (псевдонимы/протоколы, §9) | `VM_RS_CTL_LEN` | `m2_i3` | `VM_RS_MEMCTL` *_PREALLOC: вход/выход — длина региона в байтах. libsys кладёт `size_t` в `int`, VM пишет по `(size_t*)&m2_i3` (`rs.c:378-384 |
| (псевдонимы/протоколы, §9) | `VPF_ADDR` | `m1_i1` | `VM_PAGEFAULT` (ядро → VM, `mini_send` от имени процесса): адрес сбоя — CR2 (`minix/kernel/arch/i386/exception.c:121`) / DFAR/IFAR (`kernel/ |

### 11.3 Аппаратно-зависимые значения (Ж)

| Тип / раздел | Поле | Тип поля | Назначение (кратко) |
|---|---|---|---|
| `mess_krn_lsys_sys_devio` | `value` | `__uint32_t` | Значение, прочитанное из порта ввода-вывода `inb/inw/inl` (`do_devio.c:74-83`); для байта/слова — младшие 8/16 бит. |
| `mess_krn_lsys_sys_trace` | `data` | `long` | Результат: слово `long` из памяти трассируемого (`T_GETINS/T_GETDATA`, `do_trace.c:96-102`), слово из `struct proc`/`struct priv` — в т.ч. с |
| `mess_lsys_krn_readbios` | `addr` | `phys_bytes` | Физический адрес в BIOS-области; допускаются только [0, 0x4FF] и [0x90000, 0xFFFFF] (`do_readbios.c:21,32-34`, `memory.h:29-33,118-119`); ко |
| `mess_lsys_krn_sys_devio` | `port` | `int` | Номер порта ввода-вывода x86; проверяется по `s_io_tab` при `CHECK_IO_PORT` и на выравнивание (`do_devio.c:46-67`). |
| `mess_lsys_krn_sys_devio` | `value` | `__uint32_t` | Для вывода — записываемое значение (`outb/outw/outl`, `do_devio.c:90-99`); при вводе не используется. |
| `mess_lsys_krn_sys_exec` | `ip` | `vir_bytes` | Точка входа; загружается в `p_reg.pc` (`do_exec.c:46` с приведением к `u32_t`, `memory.c:729`). |
| `mess_lsys_krn_sys_exec` | `stack` | `vir_bytes` | Начальный указатель стека; в `p_reg.sp` (`do_exec.c:47`, `u32_t`). |
| `mess_lsys_krn_sys_exec` | `ps_str` | `vir_bytes` | Адрес `struct ps_strings` в стеке процесса; передаётся в регистре (`p_reg.bx` на i386, `r0` на earm; `memory.c:731`, earm `memory.c:636`). |
| `mess_lsys_krn_sys_irqctl` | `vector` | `int` | Только `IRQ_SETPOLICY`: номер линии IRQ 0..`NR_IRQ_VECTORS-1`, проверяется по `s_irq_tab` при `CHECK_IRQ` (`do_irqctl.c:36,60-82`). |
| `mess_lsys_krn_sys_sdevio` | `port` | `long` | Номер порта ввода-вывода x86, проверка по `s_io_tab` (`do_sdevio.c:31,109-131`). |
| `mess_lsys_krn_sys_trace` | `data` | `long` | Записываемое слово (`T_SETINS/SETDATA`), значение регистра CPU (`T_SETUSER`; PSW/PSR — только разрешённые биты, сегментные регистры x86 запр |
| `mess_notify` | `interrupts` | `__uint64_t` | Только `m_source == HARDWARE`: битовая карта сработавших IRQ-хуков (`priv->s_int_pending`, бит = `notify_id` из `IRQ_SETPOLICY`), после чтен |
| (псевдонимы/протоколы, §5) | `SVMCTL_VALUE` | `m1_i3` | Многоцелевое: `MEMREQ_REPLY` — код результата VM (`do_vmctl.c:86`); `KERN_PHYSMAP`/`KERN_MAP_REPLY` — индекс отображения ядра (`:114,122`);  |
| (псевдонимы/протоколы, §5) | `SVMCTL_PTROOT` | `m1_i3` | `SETADDRSPACE`: **физический адрес корня таблиц страниц** (CR3 на i386, TTBR0 на earm) процесса `SVMCTL_WHO` (`pt->pt_dir_phys`, `minix/serv |
| (псевдонимы/протоколы, §5) | `SVMCTL_MAP_PHYS_ADDR` | `m2_l1` | Ответ `KERN_PHYSMAP`: физический адрес области, которую ядру нужно отобразить (MMIO LAPIC/IOAPIC/OXPCIe, видеопамять, страницы usermapped);  |
| (псевдонимы/протоколы, §5) | `PADCONF_PADCONF` | `m2_i1` | `SYS_PADCONF` (только earm/TI OMAP), драйверы → ядро: `sys_padconf()` (`minix/lib/libsys/sys_padconf.c`; `drivers/system/gpio/gpio.c:182`, ` |
| (псевдонимы/протоколы, §5) | `PADCONF_MASK` | `m2_i2` | Маска изменяемых бит 32-битного регистра (`set32(..., mask, value)`, `omap_padconf.c:57`) (исх.: mask to apply). |
| (псевдонимы/протоколы, §5) | `PADCONF_VALUE` | `m2_i3` | Значение регистра (`PADCONF_MUXMODE()`, `PADCONF_PULL_MODE_*`, `PADCONF_INPUT_ENABLE()`) (исх.: value to write). |
| `mess_lc_pm_ptrace` | `addr` | `vir_bytes` | Запрос: адрес в пространстве трассируемого (`T_GET/SETINS/DATA`, `*B_INS`), **смещение в структуре `proc` ядра** (`T_GETUSER`/`T_SETUSER`) и |
| `mess_lc_pm_ptrace` | `data` | `long` | Запрос: записываемое слово (`T_SET*`), код выхода (`T_EXIT`, `trace.c:151`), флаги `TO_*` (`T_SETOPT`, `trace.c:162`), номер сигнала (`T_RES |
| `mess_lc_pm_sprof` | `intr_type` | `int` | Запрос: источник прерываний профилирования `PROF_RTC` (CMOS RTC) / `PROF_NMI` (`minix/include/minix/profile.h:14`–`15`); обрабатывает ядро ( |
| `mess_pm_lc_ptrace` | `data` | `long` | Ответ: прочитанное слово памяти (`T_GETINS`/`T_GETDATA`), байт (`T_READB_INS`), значение из `proc` ядра/регистр (`T_GETUSER`) — результат `s |
| `mess_lsys_pci_busc_get_bar` | `port` | `int` | Смещение регистра BAR в конфигурационном пространстве: `PCI_BAR + 4*n` (0x10…0x24); сравнивается с `PCI_BAR+4*pb_nr` (`pci.c:2454-2456`). |
| `mess_pci_lsys_busc_get_bar` | `base` | `int` | Базовый адрес BAR: физический MMIO-адрес или номер порта ввода-вывода (`pb_base`, `u32_t`) (`pci.c:2461`) → `u32_t *base`. 64-битные BAR не  |
| (псевдонимы/протоколы, §9) | `VPF_ADDR` | `m1_i1` | `VM_PAGEFAULT` (ядро → VM, `mini_send` от имени процесса): адрес сбоя — CR2 (`minix/kernel/arch/i386/exception.c:121`) / DFAR/IFAR (`kernel/ |
| (псевдонимы/протоколы, §9) | `VPF_FLAGS` | `m1_i2` | `VM_PAGEFAULT`: аппаратный код ошибки — x86 `#PF errcode` (`exception.c:122`) / ARM DFSR/IFSR (`earm/exception.c:102`); VM разбирает через ` |
| (псевдонимы/протоколы, §9) | `BUSC_PCI_ATTR_R8/R16/R32` | `pci_attr_r8/16/32.c:19-21` | `do_attr_r8/16/32:341/367/393` |
| (псевдонимы/протоколы, §9) | `BUSC_PCI_ATTR_R8/R16/R32` | `pci_attr_r8/16/32.c:19-21` | `do_attr_r8/16/32:341/367/393` |
| (псевдонимы/протоколы, §9) | `BUSC_PCI_ATTR_W8/W16/W32` | `pci_attr_w8/16/32.c:17-20` | `do_attr_w8/16/32:419/439/459` |
| (псевдонимы/протоколы, §9) | `BUSC_PCI_ATTR_W8/W16/W32` | `pci_attr_w8/16/32.c:17-20` | `do_attr_w8/16/32:419/439/459` |

## 12. Найденные ошибки и нестыковки

Найдены при разборе протоколов; **код не менялся**. «Проверено» — место перечитано
при сведении документа; остальное — по разбору групп, со ссылками в разделах выше.

| Где | Что | Последствие | Проверено |
|---|---|---|---|
| `minix/kernel/system/do_safecopy.c:399-419` (`do_vsafecopy`) | число элементов `els` из `m_lsys_kern_vsafecopy.vec_size` не проверяется ни на `> SCPVEC_NR` (64), ни на `< 0` перед копированием `els * sizeof(struct vscp_vec)` байт в статический `vec[SCPVEC_NR]` | переполнение буфера ядра по запросу любого процесса с правом `SYS_VSAFECOPY` (серверы, драйверы) — **ошибка безопасности** | да |
| `minix/lib/libsys/fkey_ctl.c:24-25` ↔ `minix/drivers/tty/tty/arch/i386/keyboard.c:438-468` (`TTY_FKEY_CONTROL`) | TTY меняет `fkeys`/`sfkeys` в запросе `mess_lsys_tty_fkey_ctl` (смещения 4, 8), libsys читает ответ как `mess_tty_lsys_fkey_ctl` (смещения 0, 4) | вызывающий получает код запроса вместо маски клавиш | да |
| `minix/fs/ptyfs/ptyfs.c:59` (`make_name`) | `snprintf(name, sizeof(name), …)` — размер указателя вместо `size` | имя узла PTY обрезается до 3 цифр на i386 и до 7 на 64 битах | да |
| `minix/servers/vfs/mount.c:111-114` (`do_mount`) | проверяется `label_len`, копируется `sizeof(mount_label)` байт | чтение за концом строки метки вызывающего | да |
| `minix/drivers/bus/pci/main.c:509` ↔ `minix/lib/libsys/pci_rescan_bus.c:18` (`BUSC_PCI_RESCAN`) | клиент пишет `m1_i1`, сервер читает `m2_i1` | работает только потому, что оба поля по смещению 8 | да |
| `minix/servers/vm/rs.c:378-384` ↔ `com.h:747` (`VM_RS_MEMCTL`) | `VM_RS_CTL_LEN` = `m2_i3` (`int`), VM пишет в него через `(size_t *)` | на LP64 запись 8 байт в 4-байтовое поле (сейчас попадает в выравнивание; на big-endian — неверная половина) | да |
| fxp, rtl8139, libaudiodriver ↔ `amddev.c` (`IOMMU_MAP`) | отправители пишут `m2_i1…i3`, получатель читает `m1_i1…i3` | работает по совпадению смещений | по разбору (§9) |
| `minix/commands/minix-service` (`RS_UPDATE`) | в одном буфере заполняются `m_rs_update` и `m_rs_req.addr` | `addr` перекрывает `m_rs_update.flags` на i386 и `state_data_gid` на x86_64 | по разбору (§9) |
| `minix/kernel/system/do_irqctl.c:166` | `1 << notify_id` в `int` при `irq_id_t` = `unsigned long` | неопределённое поведение при `notify_id` ≥ 31 | по разбору (§5) |
| `minix/lib/libsys/sys_vmctl.c` (`VMCTL_MEMREQ_GET`) | `SVMCTL_MRG_EP2`, `SVMCTL_MRG_ADDR2` ядро не заполняет, libsys их читает | мусор в результате | по разбору (§5) |
| `minix/servers/vfs/request.c` (`req_lookup` и др.) | грант на `PATH_MAX`, а `path_size` = `PATH_MAX + 1`; сообщения не обнуляются (`memset`) | неиспользуемые поля уходят с мусором стека; `req_peek` паникует при позиции ≥ 4 ГБ (`request.c:909`) | по разбору (§8) |
| `fwd_msg()` (`forward.c`), `RTCDEV_PWR_OFF` в `do_reboot()`, `sched_nice()`, `SIGS_SIGNAL_RECEIVED` в PM | сообщения не обнуляются | мусор в неиспользуемых байтах (утечка содержимого стека другому процессу) | по разбору (§6) |
| `minix/lib/libsys/sys_umap.c`, `sys_sprof.c` | размер обрезается до `int` уже в libsys | длины > 2 ГБ | по разбору (§5, §6) |

## 13. Неиспользуемые типы и поля

- **Типы без отправителя и получателя:** старый сетевой протокол `DL_*`
  (`mess_net_netdrv_dl_conf`, `…_dl_getstat_s`, `…_dl_readv_s`, `…_dl_writev_s`,
  `mess_netdrv_net_dl_conf`, `…_dl_task` — заменён на `NDEV_*`);
  `mess_lsys_fi_reply`; `mess_i2c_li2cdriver_busc_i2c_exec` и `…_reserve`
  (ответ шины несёт только `m_type`); `mess_lc_pm_sysuname` (`PM_SYSUNAME`
  устарел, остался только обработчик).
- **Мёртвые псевдонимы:** `VMAD_*`, `VMDD_*`, `VMGD_*` (`VM_ADDDMA`, `VM_DELDMA`,
  `VM_GETDMA` — нет ни обработчика, ни отправителя); `VMEN_*` (`VM_EXEC_NEWMEM`);
  `USB_DRIVER_EP`.
- **Мёртвые поля в живых типах:** `mess_krn_lsys_schedule.acnt_*` (планировщик не
  читает), `mess_krn_lsys_sys_times.real_ticks`, `mess_lsys_krn_sys_getinfo.endpt`
  и `val_ptr2`, `mess_fs_vfs_readsuper.con_reqs` (и `device` в том же ответе),
  `mess_lc_vfs_pipe2._unused`/`oflags`, `mess_lsys_fi_ctl.gid`/`size`,
  `mess_vmmcp.flags_ptr`, `mess_vmmcp_reply.flags`.
- **Код без вызывающих:** ветка `SCHEDCTL_FLAG_KERNEL`, `VMCTL_I386_INVLPG`.

## 14. Выводы для переработки сообщений

1. **Почти все «не помещающиеся» типы растут из-за пары «адрес + длина» шириной в
   слово** (`vir_bytes` + `size_t`, указатель + `size_t`): адрес буфера или пути в
   памяти вызывающего и его длина. Реальные длины 32-битные (VFS сама передаёт
   число байт как `unsigned int`, ответы — в `int`). Единое представление
   «адрес `uint64_t` + длина `uint32_t`» убирает основную массу проблем
   (§8: все 20 «не помещающихся» типов протокола VFS↔ФС влезают в 56 байт при
   длинах `uint32_t`).
2. **Адреса в узких полях (У, §11.2) — тихие ошибки, а не ошибки сборки.** Их
   немного (23), но они на критических путях: корень таблицы страниц
   (`SVMCTL_PTROOT` = `m1_i3`), адрес `ps_strings` при `exec` (`VFS_PM_PS_STR` =
   `m7_i5`), адрес и длина сегмента ELF (`VM_VFS_MMAP` `u32_t`), адрес сбоя страницы
   (`VPF_ADDR`), `MEMREQ` (`SVMCTL_MRG_ADDR/LENGTH`), длины BAR и DS. Все они — в
   общих раскладках или старых протоколах.
3. **Физические адреса (Ф, §11.1) смешаны с виртуальными в одном поле**, смысл
   задаёт endpoint (`NONE` — физический): `sys_copy`, `sys_memset`, `sys_umap`,
   `vm_map_phys` (`void *`), `vm_getphys` (`void *`), `sdevio` (`phys_bytes` для
   виртуального адреса). Нужны отдельные поля фиксированной ширины для физических
   адресов и явный вид адресации — это же вопрос типа физического адреса
   (`types-audit.md` §9.1, `memory.md` §10 вопрос 7).
4. **Общие раскладки `mess_1`…`mess_10` надо выводить из употребления**: тип поля
   у них общий для всех протоколов (`long`, `int`, `char *`), смысл — только в
   имени псевдонима; на них держатся `SYS_VMCTL` (запрос `mess_1`, ответ `mess_2`,
   на совпадении смещений), PM↔VFS (`mess_7`), VM↔VFS (`mess_10`), PCI, USB,
   DEVMAN, VirtualBox, IOMMU. Все найденные нестыковки «отправитель пишет одно
   поле, получатель читает другое» (§12) — здесь.
5. **Ответ «на месте» другим типом** (DS, PCI, RS, fkey, VM): запрос и ответ
   используют один буфер и разные члены объединения; при переработке нужна явная
   пара типов запрос/ответ с согласованной раскладкой (ошибка `TTY_FKEY_CONTROL`
   — следствие).
6. **Поля со смешанным смыслом** (`semctl.opt` — значение или адрес;
   `ptrace.addr` — адрес, смещение в `struct proc` или адрес `ptrace_range`;
   `id`/`req_id` — endpoint или номер запроса; `path_len` — длина метки DS)
   — разделить на явные поля или объединения с признаком.
7. **Скрытый ABI вне сообщений** — структуры, передаваемые по адресу или гранту,
   раскладка которых тоже должна стать фиксированной: `struct stat`, `statvfs`,
   `flock`, `msghdr`/`iovec`, `timeval`, `vfs_ucred_t`, `iovec_s_t`
   (`BDEV_GATHER/SCATTER`), `vumap_phys`, `vscp_vec`, `cp_grant_t`, `sigmsg`,
   `sigcontext`, `mcontext_t`, `struct exec_info`, `sysgetenv`, внутренние таблицы
   `mproc`/`fproc`/`rproc`/`data_store` (`getsysinfo`), `struct proc`/`stackframe_s`
   (ptrace `T_GETUSER`, `GET_PROC`). Для 32-битных процессов на 64-битном ядре
   (если они будут) понадобится слой совместимости.
8. **Архитектурно-зависимые вызовы** (порты x86 `SYS_DEVIO`/`SDEVIO`/`VDEVIO`,
   `SYS_IOPENABLE`, `SYS_READBIOS`, `SYS_PADCONF`, `VMCTL_I386_*`, значения
   регистров CPU) — вынести в архитектурный раздел протокола ядра.
9. **Образец — `NDEV_*`** (`mess_ndev_netdriver_*`): все поля фиксированной ширины
   (`uint32_t`, гранты, `uint16_t len[8]`), явный `id` запроса, размеры на i386 и
   x86_64 одинаковы.
10. **Нулевой мусор**: многие отправители не обнуляют сообщение — неиспользуемые
    поля уносят содержимое стека другому процессу. Новая система должна обнулять
    сообщение при сборке (или ядро — при копировании).

## 14а. Решение: раскладка сообщений (владелец, 2026-10-07)

Сообщение остаётся **64 байта**: заголовок `m_source` + `m_type` (по 4 байта) и 56 байт
полезной нагрузки. Поля — **фиксированной ширины**, раскладка каждого типа одинакова
на всех архитектурах. Опирается на схему типов памяти (`types-audit.md` §9а).
Выполняется шагом MSG (`modernization.md`, план); сообщения, которые затрагивает
шаг А1 (физические адреса: `sys_copy`, `sys_memset`, `sys_umap`, `vm_map_phys`,
`vm_getphys`, `SYS_VMCTL`, `readbios`, `privctl`, `sdevio`), переводятся на эти
правила уже в А1.

**Правила.**

1. Заголовок не меняется; объединение полезной нагрузки выровнено не меньше чем
   по 8. Размер 64 байта сохраняется, поэтому копирование сообщения в ассемблере
   (`klib.S` i386 и earm) не меняется.
2. **Адреса** (`vir_addr_t`, `phys_addr_t`, `dma_addr_t`) — всегда `uint64_t`: одна
   раскладка на 32 и 64 битах, LPAE/PAE не меняет ABI, задел под 32-битные процессы
   на 64-битном ядре. Преобразование в тип схемы и обратно — в обёртках libsys и в
   получателе, с проверкой, что значение помещается (на 32-битной платформе).
3. **Длины — по смыслу:**
   - размер **области памяти** (`mmap`/`munmap`/`mprotect`, `shm`, `sys_copy`,
     `sys_memset`, `sys_umap`, вызовы VM) — `uint64_t`;
   - длина **передачи ввода-вывода, имени, пути, метки, буфера** (read/write,
     пути VFS↔ФС, sysctl, метки DS/RS) — `uint32_t` с проверкой. За одну передачу —
     не больше 2 ГБ (как `MAX_RW_COUNT` в Linux); больший запрос обрабатывается как
     частичное чтение/запись по POSIX.
4. **Бывшие `long`/`unsigned long`/`key_t`** — `int64_t`/`uint64_t`, где значение
   размером с машинное слово (данные `ptrace`, `key_t`, счётчики), иначе
   `int32_t`/`uint32_t` — по месту.
5. **Порядок полей** — 8-байтовые первыми, затем 4-, 2-, 1-байтовые: без дыр и с
   одинаковыми смещениями на i386 (там `uint64_t` в структуре выровнен по 4) и на
   64 битах.
6. **Без ручных заполнителей.** `padding[N]` удаляется из всех типов;
   `_ASSERT_MSG_SIZE` проверяет `sizeof ≤ 56`, размер сообщения задаёт
   `u8_t size[56]` в объединении.
7. **Обнуление.** Сообщение собирается обнулённым (`message m = {0}` или
   обнуление в обёртках libsys) — неиспользуемые байты не уносят содержимое стека
   (§14 п. 10, отложенное исправление 6 в `handoff.md`).
8. **Общие раскладки `mess_1…mess_10` выводятся из употребления** (§14 п. 4):
   каждый протокол на них (25 групп, 129 псевдонимов в §5–§9) получает свои типы
   запроса и ответа; все помещаются с запасом (наибольший — `SYS_VMCTL`, 5–6 полей
   после разделения запроса и ответа).
9. **Явная пара запрос/ответ** вместо ответа «на месте» другим членом объединения
   (§14 п. 5); поля со смешанным смыслом разделяются (§14 п. 6).
10. **Проверка на хосте:** тест abi64 получает правило «смещения всех полей
    одинаковы при `-m32` и `-m64`» и «каждый тип ≤ 56 байт» — вместо нынешнего
    списка типов, не помещающихся на LP64.

**Расчёт** (2026-10-07, по таблицам полей §5–§9: тип и пометки «64»; адрес/физический
адрес/узкое поле с адресом — 8 байт, 8-байтовые поля выровнены по 8). Из 245
отдельных типов:

| Вариант | Не помещается в 56 байт |
|---|---|
| сейчас на LP64 (`vir_bytes`, `size_t`, `long` по 8 байт) | 128, наибольший — 96 байт |
| адреса, длины и слова — все `uint64_t` | 7 (4 — с перестановкой полей) |
| **принятые правила** (адреса и длины областей памяти `uint64_t`, длины ввода-вывода и имён `uint32_t`) | **2** |

Типы на границе 56 байт и что с ними делается:

| Тип | Все поля 64 | По правилам | Причина | Решение |
|---|---|---|---|---|
| `mess_lc_mib_sysctl` | 80 | 72 | 3 адреса + встроенное имя `int name[8]` | объединение «`namep` \| `name[6]`», `CTL_SHORTNAME` 8 → 6 (6 — `kern.proc2` у `ps`); длинные имена — через `namep`, как сейчас |
| `mess_lc_vfs_path` | 64 | 64 | адрес + встроенный путь `char buf[40]` | `buf[40]` → `buf[36]` (8 + 4 + 4 + 4 + 36 = 56); короткие пути (до 35 символов) по-прежнему без копирования |
| `mess_lc_vfs_mount` | 72 | 56 | 4 адреса + 4 длины строк | длины `uint32_t` — впритык |
| `mess_vfs_fs_lookup` | 64 | 48 | три длины `size_t` | длины `uint32_t` |
| `mess_mib_lsys_call` | 64 | 48 | две длины `size_t` | длины `uint32_t` |
| `mess_lsys_vfs_mapdriver` | 64 | 56 | адрес, длина, `int domains[8]` | длина `uint32_t` |
| `mess_vm_vfs_mmap` | 64 | 56 | адрес и длина сегмента ELF в `u32_t` становятся шире | 8-байтовые поля первыми |

**Объём:** 128 отдельных типов меняют хотя бы одно поле (полей: адреса — 118,
длины — 82, машинные слова — 21, физические адреса — 12, узкие поля — 11), плюс 25
протоколов на общих раскладках; 117 типов от архитектуры не зависят. Обёртки libsys
и libc, отправители и получатели меняются вместе с типами (правило о драйверах и
серверах — `modernization.md`, «Рабочий процесс»).

## 15. Как проверять и поддерживать

- Размеры всех типов на i386 и x86_64 — тест `make -C docker -f build.mk host-test
  HOST_TESTS=abi64` (`types-audit.md` §2.2): `messages_56_bytes` перечисляет типы,
  не помещающиеся на LP64, «храповик» не даст добавить новый такой тип незаметно.
- Смещения полей в таблицах этого документа получены `clang -fdump-record-layouts`
  по `ipc.h` с деревом заголовков `minix/tests/host/abi64/mkinc.sh` для `-m32` и
  x86_64. При добавлении типа в `ipc.h` — добавить раздел сюда (направление,
  вызовы, поля, пометки «64»).

## 16. Связанные документы

- `docs/types-audit.md` — аудит 64-битных типов (§5 — сообщения IPC, §9 — решения о
  размере сообщения и типе физического адреса, §9а — схема типов памяти).
- `docs/memory.md` §10, вопрос 7 — тип физического адреса.
- `docs/threads.md` — IPC, планировщик, libmthread.
- `docs/architecture.md` — серверы, драйверы, IPC.
