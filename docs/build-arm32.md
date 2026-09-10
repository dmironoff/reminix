# Сборка ReMinix для arm32 (earm / evbearm-el)

> Пошаговое описание процесса сборки и создания SD-образа для текущего ARM-порта,
> реконструированное по `releasetools/{arm_sdimage.sh,fetch_u-boot.sh,
> gen_uEnv.txt.sh,image.defaults,image.functions}` и `build.sh`. В отличие от x86
> (см. `docs/build-x86.md`), это **не общий ARMv7**, а конкретный порт под один
> семейный BSP — см. §6. Материал ниже фиксирует текущее (2018 года) состояние
> пайплайна как референс перед его обобщением/расширением в рамках плана
> портирования (`docs/porting.md`).

## 1. Общая схема

```
build.sh -m evbearm-el ...          # кросс-сборка мира под MACHINE=evbarm/earm
        │
        ▼
make build (корневой Makefile) → releasetools do-hdboot
        │   (тот же do-hdboot, что и для x86 — архитектурно-нейтрален,
        │    см. docs/build-x86.md §3)
        ▼
releasetools/arm_sdimage.sh
        ├─ fetch_u-boot.sh   → клонирует/обновляет форк U-Boot MINIX3 под нужный коммит
        ├─ gen_uEnv.txt.sh   → генерирует uEnv.txt (команды U-Boot для загрузки)
        └─ сборка SD-образа: FAT-раздел (U-Boot + ядро + модули) + root/usr/home (MFS)
```

## 2. Переменные архитектуры

```sh
: ${ARCH=evbearm-el}
: ${OBJ=../obj.${ARCH}}
: ${TOOLCHAIN_TRIPLET=arm-elf32-minix-}
: ${BUILDVARS=-V MKGCCCMDS=yes -V MKLLVM=no}
```

- `ARCH=evbearm-el` — передаётся в `build.sh -m evbearm-el`. Это **алиас**, а не
  самостоятельная пара `MACHINE`/`MACHINE_ARCH`: в таблице `valid_MACHINE_ARCH`
  внутри `build.sh` (см. `docs/build-x86.md` §2, `docs/porting.md`) есть строка
  ```
  MACHINE=evbarm   MACHINE_ARCH=earm   ALIAS=evbearm-el   DEFAULT
  ```
  То есть `evbearm-el` разворачивается в `MACHINE=evbarm`, `MACHINE_ARCH=earm` —
  именно поэтому директория в дереве исходников называется `minix/kernel/arch/earm`
  (по `MACHINE_ARCH`), а не `evbarm`/`arm`.
- `TOOLCHAIN_TRIPLET=arm-elf32-minix-` — префикс кросс-инструментов для этой
  архитектуры (аналог `i586-elf32-minix-` у x86).
- `BUILDVARS=-V MKGCCCMDS=yes -V MKLLVM=no` — **для ARM по умолчанию используется
  GCC, а не LLVM/Clang** (в отличие от общего описания тулчейна в
  `docs/architecture.md` §5, где LLVM упомянут как часть `tools/`). В файле рядом
  закомментирован альтернативный набор флагов для сборки с LLVM
  (`MKLIBCXX=no MKKYUA=no MKATF=no MKLLVMCMDS=no`) — то есть LLVM-путь для ARM
  существовал, но не был путём по умолчанию на момент заморозки проекта. Это важно
  учитывать при планировании тулчейна для aarch64/risc-v64 (см. `docs/porting.md`).

## 3. Целевая плата (BSP) — жёстко задана в скрипте

```sh
# Beagleboard-xm
: ${U_BOOT_BIN_DIR=build/omap3_beagle/}
: ${CONSOLE=tty02}

# BeagleBone (and black)  — закомментировано, вариант на выбор
#: ${U_BOOT_BIN_DIR=build/am335x_evm/}
#: ${CONSOLE=tty00}
```

Скрипт по умолчанию собирает образ под **BeagleBoard-xM** (TI OMAP3), с
альтернативой BeagleBone/BeagleBone Black (TI AM335x) через переключение
переменных — обе платы соответствуют `minix/kernel/arch/earm/bsp/ti/*`
(см. `docs/architecture.md` §7). Других BSP в этом скрипте не предусмотрено —
добавление платы вне семейства TI OMAP потребует и нового `bsp/<vendor>/` в ядре,
и веток в этом скрипте (или его переработки в параметризуемый вид).

## 4. U-Boot — отдельный проект, не часть основного дерева

`fetch_u-boot.sh` клонирует **форк U-Boot, поддерживаемый самим проектом MINIX3**
(`git://git.minix3.org/u-boot`, ветка `minix`), на зафиксированный коммит:

```sh
U_BOOT_GIT_VERSION=cb5178f12787c690cb1c888d88733137e5a47b15
```

Собранные бинарники (`MLO` — first-stage bootloader TI OMAP, `u-boot.img` — сам
U-Boot) берутся из `${RELEASETOOLSDIR}/u-boot/build/<плата>/`. **U-Boot нужно
собрать отдельно и заранее** (его сборка не описана в этих скриптах — предполагается
готовый чекаут с уже собранными бинарниками; сама сборка U-Boot под ARM-тулчейн —
отдельная задача, не покрытая `build.sh`). Это первое, на что стоит обратить
внимание при портировании на другую плату/архитектуру с U-Boot (aarch64, riscv64):
понадобится либо тот же форк с добавленной поддержкой новой платы, либо переход на
апстримный U-Boot.

`gen_uEnv.txt.sh` генерирует `uEnv.txt` — скрипт автозагрузки U-Boot:
- прописывает `bootargs` (консоль, `rootdevname=c0d0p1`, verbose, hz);
- прописывает `bootminix` — команда `go 0x80200000 "$bootargs"` (прямой прыжок на
  адрес, куда загружен `kernel.bin`, **без ELF-загрузчика** — U-Boot грузит
  плоский бинарник по фиксированным адресам);
- прописывает **фиксированную карту адресов** в памяти для ядра и каждого модуля
  (`kernel.bin`→`0x80200000`, `ds.elf`→`0x82000000`, `rs.elf`→`0x82800000`, ...,
  `init.elf`→`0x87800000`, шаг 0x800000 = 8 МБ между модулями);
- поддерживает два варианта загрузки: с SD/MMC (`mmcbootcmd`, использованный в
  `arm_sdimage.sh`) и по сети через TFTP (`netbootcmd`, с зашитыми IP-адресами
  `serverip=192.168.12.10`/`ipaddr=192.168.12.62` — вариант для разработки/отладки
  по сети, не для образа на SD).

## 5. Сборка SD-образа (`arm_sdimage.sh`)

Размер образа и разделов (по умолчанию ~2 ГБ, обязательно ≥2 ГБ — иначе SD-карта
может не определиться корректно в QEMU/на реальном железе):

| Раздел | Тип | Размер по умолчанию | Точка монтирования |
|---|---|---|---|
| FAT (`c:`) | FAT16 | 10 МБ (`FAT_SIZE`) | U-Boot, `uEnv.txt`, `kernel.bin`, `*.elf` (модули) |
| root | MFS | 64 МБ | `/` |
| usr | MFS | 1792 МБ | `/usr` |
| home | MFS | 128 МБ | `/home` |

Отличия от x86-пайплайна (`docs/build-x86.md` §4.1):

1. **FAT-раздел идёт первым** и содержит не gzip-сжатые `modNN_*`-файлы (как в
   `/boot/minix/.temp` для x86), а **отдельно собранные `.elf`-файлы** — каждый
   модуль копируется напрямую из объектной директории (`${OBJ}/minix/servers/...`
   и т.п.) и **стрипается** (`${CROSS_PREFIX}strip -s`), а ядро конвертируется в
   плоский бинарник: `${CROSS_PREFIX}objcopy ${OBJ}/minix/kernel/kernel -O binary
   ${ROOT_DIR}/kernel.bin`. Это связано с тем, что U-Boot грузит модули по фиксным
   адресам командой `fatload` (см. §4), а не через ELF/multiboot-загрузчик, как
   x86-загрузчик MINIX.
2. **`/etc/fstab` ссылается на другой набор разделов** (`c0d0p2`/`c0d0p3` вместо
   `c0d0p1`/`c0d0p2` у x86) — так как раздел 1 (`c0d0p1`) занят FAT-загрузчиком, а
   не root.
3. `nbpartition -f -m ${IMG} ${FAT_START} "c:${FAT_SIZE}*" 81:... 81:... 81:...` —
   первый раздел явно помечен типом `c` (FAT32 LBA) и как загрузочный (`*`).
4. Итоговый образ **не содержит отдельного boot-сектора** уровня x86
   (`nbinstallboot`/`bootxx_minixfs3`) — вместо этого весь бутстрэп идёт через
   `MLO`/`u-boot.img` на FAT-разделе, читаемые ROM-загрузчиком платы напрямую.

Команда, которую скрипт печатает в конце:
```
qemu-system-arm -M beaglexm -serial stdio -drive if=sd,cache=writeback,file=minix_arm_sd.img
```
(`-M beaglexm` — конкретно модель BeagleBoard-xM в QEMU; для BeagleBone-варианта
потребуется другая модель машины QEMU, в скрипте не подставляется автоматически.)

## 6. Важная оговорка про "arm32" в этом дереве

То, что в плане портирования (`docs/porting.md`) названо текущим ARM-портом
(`earm`), на практике — это **порт под один SoC-BSP (TI OMAP3/AM335x,
BeagleBoard/BeagleBone)**, а не универсальный ARMv7. В коде это видно на трёх
уровнях:

- `minix/kernel/arch/earm/bsp/ti/*` — только один вендор BSP.
- `arm_sdimage.sh` — жёстко зашитые адреса загрузки, `U_BOOT_BIN_DIR` только под
  две платы одного вендора.
- `gen_uEnv.txt.sh` — фиксированная карта памяти, привязанная к конкретной
  раскладке модулей этого образа.

Следствие для плана портирования: пункт "arm32 (обобщение earm)" в
`docs/porting.md` — это не косметическая доработка, а необходимость развести общий
ARMv7-уровень (регистры, MMU, обработка исключений — то, что действительно общее
для всех Cortex-A) и BSP-специфику (адреса периферии, тактирование, UART, u-boot
env) по образцу того, как это устроено в самом NetBSD (`sys/arch/arm` — общий
ARM-код, `sys/arch/evbarm` — конкретные платы).

## 7. Итоговый мини-рецепт (BeagleBoard-xM, с нуля)

```sh
cd reminix
JOBS=$(nproc) ./releasetools/arm_sdimage.sh   # соберёт всё, скачает/обновит u-boot, создаст minix_arm_sd.img
qemu-system-arm -M beaglexm -serial stdio -drive if=sd,cache=writeback,file=minix_arm_sd.img
```

Для сборки под BeagleBone/BeagleBone Black нужно переопределить `U_BOOT_BIN_DIR` и
`CONSOLE` перед запуском (раскомментировать соответствующие строки или передать
через окружение/`.settings`, см. `SETTINGS_MINIX` в начале скрипта).
