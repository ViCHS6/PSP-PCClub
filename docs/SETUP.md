# Окружение: от чистой Windows до PSP Go

## 1. Требования

- Windows 11 x64, включённая аппаратная виртуализация, интернет и права администратора для установки WSL.
- Заложите около 20 ГБ свободного места под Ubuntu, PSPDEV, репозиторий, LFS и сборки; это рабочий резерв, а не точный размер установки.
- PSP Go, USB-кабель с передачей данных и установленная CFW с поддержкой homebrew. Пример совместимой связки — системная прошивка 6.61 + ARK-4; установку CFW выполняет человек по инструкции её автора. Фактическую конфигурацию и запуск подтверждает T004. [Совместимость ARK-4](https://github.com/PSP-Archive/ARK-4/wiki) (проект перенесён в ARK-5).

Устройство (заполняет человек в T004):

| Модель | Прошивка | CFW |
|---|---|---|
| | | |

## 2. WSL2 и Ubuntu 24.04

В PowerShell **от администратора**:

```powershell
wsl --install -d Ubuntu-24.04
```

Перезагрузите Windows, откройте Ubuntu и создайте Linux-пользователя. Затем в PowerShell от администратора:

```powershell
wsl --update
wsl --set-version Ubuntu-24.04 2
wsl --set-default Ubuntu-24.04
wsl --list --verbose
```

В списке у Ubuntu-24.04 должна быть версия `2`. В Ubuntu команда `cat /etc/os-release` должна показывать `VERSION_ID="24.04"`. WSLg нужен для окна PC-версии. [Инструкция Microsoft](https://learn.microsoft.com/en-us/windows/wsl/install).

## 3. Клонирование на диск Windows

Репозиторий держим в обычной папке Windows. Установите [Git for Windows](https://git-scm.com/downloads/win), включая Git LFS. В обычном PowerShell выберите существующую родительскую папку и вставьте URL репозитория:

```powershell
$repoParent = Read-Host 'Полный путь к папке для проекта на диске Windows'
$repoUrl = Read-Host 'URL Git-репозитория'
Set-Location -LiteralPath $repoParent
git clone $repoUrl PSP_Game
Set-Location .\PSP_Game
git lfs install
git lfs pull
wsl
```

WSL откроется в той же папке. Проверьте `pwd`: путь будет вида `/mnt/<буква диска>/.../PSP_Game`. Пробелы в пути репозитория допустимы; в командах `cd` заключайте путь в кавычки. Сам PSPDEV устанавливается в Linux, в путь без пробелов. Все следующие Linux-команды выполняются из корня репозитория.

## 4. Установка окружения

В Ubuntu от обычного пользователя:

```bash
bash tools/setup_wsl.sh --dry-run
bash tools/setup_wsl.sh
source /etc/profile.d/pspdev.sh
```

Скрипт предупреждает при запуске вне Ubuntu 24.04/WSL, устанавливает через apt только отсутствующие пакеты, скачивает готовый [релиз PSPDEV](https://github.com/pspdev/pspdev/releases), проверяет структуру архива и устанавливает его в `/usr/local/pspdev`. `sudo` используется только при необходимости; от root команды выполняются без sudo. Запускается `git lfs install`; в конце проверяются PSP GCC, psp-cmake, CMake, Ninja и Pillow.

Пакеты для Ubuntu 24.04:

| Назначение | Пакеты |
|---|---|
| Проект и Git LFS | `cmake ninja-build g++ libsdl2-dev python3 python3-pil git git-lfs` |
| Готовый PSPDEV | `build-essential pkgconf libreadline8t64 libusb-0.1-4 libgpgme11t64 libarchive-tools fakeroot` |
| Загрузка и распаковка | `curl ca-certificates tar gzip` |

[README PSPDEV](https://github.com/pspdev/pspdev#readme) описывает установку готового тулчейна и ограничения пути. Список пакетов выше воспроизводит окружение T002. На 24.04 вместо имён `libreadline8` и `libgpgme11` используются [`libreadline8t64`](https://packages.ubuntu.com/noble/libreadline8t64) и [`libgpgme11t64`](https://packages.ubuntu.com/noble/libgpgme11t64); [`libusb-0.1-4`](https://packages.ubuntu.com/noble/libusb-0.1-4) сохраняет имя. `libsdl2-dev` подтягивает зависимости OpenGL для PC-сборки. В Ubuntu должны быть включены стандартные репозитории, включая Universe для Git LFS.

Повторный запуск не скачивает PSPDEV, если уже есть `<prefix>/bin/psp-gcc`. Файл `/etc/profile.d/pspdev.sh` перезаписывается только при отличии содержимого:

```sh
export PSPDEV=/usr/local/pspdev
export PATH="$PATH:$PSPDEV/bin"
```

В исходном окружении разработчика `VERSION_INSTALLED` равен `v20261001`. URL `latest` устанавливает актуальный релиз, поэтому номер на новой машине может отличаться. Для воспроизведения конкретного релиза передайте его URL через `--url`.

```bash
bash tools/setup_wsl.sh --update
source /etc/profile.d/pspdev.sh
```

`--update` сначала скачивает и проверяет архив во временном каталоге. Новый каталог готовится на той же файловой системе, что и установленный PSPDEV. Замена выполняется переименованиями: прежний PSPDEV остаётся в `<prefix>.bak`, при ошибке восстанавливается. При следующем успешном обновлении `.bak` заменяется предыдущей установкой. Не собирайте проект одновременно с обновлением.

Параметры можно сочетать: `--prefix /opt/pspdev` меняет место установки (абсолютный Linux-путь без пробелов), `--url URL` меняет архив, `--dry-run` показывает действия без изменений и без скачивания. Даже при пользовательском префиксе запись общего профиля может потребовать sudo. Новая настройка действует в новых login-shell; текущей оболочке нужен `source`, как выше.

## 5. Сборка, тесты и снимок экрана

В WSL из корня проекта, после загрузки профиля — команды **из AGENTS.md**:

```bash
# PC-сборка (отладка, тесты; позже — balance-runner)
cmake -S . -B build/pc -G Ninja && cmake --build build/pc
ctest --test-dir build/pc --output-on-failure

# Запуск PC-версии с автоматическим снимком экрана (окно через WSLg)
build/pc/pcclub --frames 90 --screenshot build/pc/shot.bmp

# PSP-сборка → build/psp/EBOOT.PBP
psp-cmake -S . -B build/psp -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/psp
```

Ожидается: сборки без предупреждений, все тесты проходят, появляется `build/pc/shot.bmp` и `build/psp/EBOOT.PBP`. CMake выбирает платформу по тулчейну: обычный `cmake` — PC (SDL2/OpenGL), `psp-cmake` — PSP, большая память отключена. При первой PC-конфигурации нужен интернет для скачивания doctest.

Следующие команды из AGENTS.md запускаются после появления соответствующих инструментов, а не для первого запуска T001:

```bash
# Данные (появится в T021)
python3 tools/build_data.py data/ -o build/data.pak

# Баланс (появится в T025)
build/pc/balance-runner --days 90 --seed 1 --strategy normal --out build/balance.csv
```

## 6. Python на Windows

Установите Python 3.12 с лаунчером `py` с [python.org](https://www.python.org/downloads/windows/). В обычном PowerShell:

```powershell
py -3.12 --version
py -3.12 -m pip install pillow
py -3.12 -c "import PIL; print(PIL.__version__)"
```

Инструменты `tools/*.py` можно запускать через `py -3.12` из корня проекта. Windows Python и WSL Python — отдельные установки; пакет `python3-pil` скрипт ставит только в WSL. Скрипты с `bpy` позднее запускаются внутри Blender.

## 7. Проверка в PPSSPP

Скачайте Windows-версию с [официальной страницы PPSSPP](https://www.ppsspp.org/download/), установите или распакуйте ZIP и запустите эмулятор. Через «Файл → Открыть» выберите `build/psp/EBOOT.PBP` в Windows-папке репозитория. Ожидается вращающийся куб и строка кириллицы. SELECT включает отладочный оверлей с FPS; назначение SELECT на клавиатуре проверьте в настройках управления PPSSPP.

## 8. Копирование и запуск на PSP Go

1. На Go включите «Настройки → USB-соединение» и подключите кабель к Windows. Должна появиться внутренняя память с каталогом `PSP\GAME`. На самой Go эта память обозначается `ef0:`; её букву Windows назначает отдельно.
2. В обычном Windows PowerShell из корня проекта сначала посмотрите план, затем скопируйте:

   ```powershell
   .\tools\deploy.ps1 -WhatIf
   .\tools\deploy.ps1
   ```

   Автопоиск проверяет доступные тома с `PSP\GAME`, включая тома, которые Windows считает несъёмными. Если кандидатов нет или несколько, скрипт перечисляет их и требует выбор. Явно указать накопитель можно так:

   ```powershell
   $pspDrive = Read-Host 'Буква накопителя PSP с двоеточием'
   .\tools\deploy.ps1 -Drive $pspDrive -WhatIf
   .\tools\deploy.ps1 -Drive $pspDrive
   ```

   Если Windows блокирует запуск локального скрипта, выполните его в отдельном процессе: `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\deploy.ps1 -WhatIf`, затем без `-WhatIf` для копирования.

3. Назначение — `<накопитель>\PSP\GAME\PCCLUB\`. `EBOOT.PBP` обязателен; отсутствие `build\data.pak` даёт предупреждение и не мешает демосцене T001. Файлы копируются только при отличии размера или SHA-256. Повторный запуск сообщает «без изменений». `-WhatIf` не создаёт даже каталога.
4. Безопасно извлеките накопитель в Windows, выйдите из USB-режима Go и откройте «Игра → Внутренняя память → Компьютерный клуб». Ожидается вращающийся куб и строка кириллицы, SELECT включает FPS/память. Цель — стабильные 30 FPS; фактический результат фиксируется в T004.

Параметры `-Build build\psp`, `-Data build\data.pak`, `-Name PCCLUB` можно переопределять. Относительные `Build`/`Data` считаются от корня репозитория независимо от текущей папки PowerShell. Для проверки `-Drive` также принимает полный путь к временному каталогу, имитирующему накопитель.

## 9. Частые проблемы

| Симптом | Проверка и решение |
|---|---|
| `bash: ... $'\r'`, `bad interpreter` | У `.sh` должны быть LF и UTF-8 без BOM. Сохраните `tools/setup_wsl.sh` с LF; `.gitattributes` уже задаёт LF, для `.ps1` — CRLF. |
| `psp-cmake: command not found` в неинтерактивном shell | Обычный `bash -c` может не читать `/etc/profile.d`. Из PowerShell, находясь в корне проекта, используйте `wsl -e bash -lc 'psp-cmake --version'`. Для команды с `cd` передайте свой `/mnt/...` путь в кавычках; в текущем WSL-shell выполните `source /etc/profile.d/pspdev.sh`. |
| Вместо PNG — текст `version https://git-lfs.github.com/spec/v1` | Выполните `git lfs install` и `git lfs pull` в репозитории; проверьте доступ к LFS на сервере. |
| Go заряжается, но накопителя нет | Включите «USB-соединение» в меню Go, проверьте кабель передачи данных и USB-порт; затем проверьте появление внутренней памяти в Проводнике. |
| Автопоиск не выбрал PSP | Смотрите список проверенных томов и кандидатов. Для новой структуры или нескольких устройств задайте `-Drive` явно; каталог `PSP\GAME\PCCLUB` скрипт создаст сам. |
| `apt` не находит пакет | Проверьте Ubuntu 24.04, интернет и стандартные репозитории/Universe. Список для 24.04 использует суффиксы `t64`; скрипт не переводит другую ОС на Ubuntu автоматически. |
| У PC-версии нет окна | Проверьте WSL2/WSLg (`wsl --update`), запуск из сеанса Windows и наличие графического окружения. Для проверки снимка нужен работающий OpenGL. |

## 10. Blender + MCP (A002)

Заполняется в A002; для сборки и запуска демосцены Blender не нужен.

| Компонент | Версия / настройка |
|---|---|
| Blender | |
| Аддон Blender MCP | |
| MCP-клиент и подключение | |
| Команда проверки соединения | |
