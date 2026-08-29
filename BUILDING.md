# Сборка и запуск / Build & Run

🇷🇺 [Русский](#русский) &nbsp;|&nbsp; 🇬🇧 [English](#english)

---

## Русский

Инструкция ниже написана так, чтобы после клонирования репозитория достаточно было выполнить пару команд — без ручной настройки CMake.

> Дев-скрипты (`build-*`, `run-*`) — только для разработки. Когда появится установщик для продакшена, процесс развёртывания будет отдельным.

### Предварительные требования

**Общее для обеих платформ:**
- Git
- [vcpkg](https://github.com/microsoft/vcpkg) — менеджер зависимостей C++
- .NET SDK 8.0+ — только если планируете запускать клиент (`FreeCord.Desktop`)

**Windows:**
- Visual Studio 2022 с компонентом «Разработка классических приложений на C++»
- CMake 3.20+ (идёт в комплекте с Visual Studio)
- PowerShell (входит в Windows по умолчанию)

**Linux** (нативный дистрибутив или WSL2):
- `build-essential`, `cmake` (3.20+), `git`, `curl`, `zip`, `unzip`, `tar`, `pkg-config` — нужны vcpkg для сборки зависимостей из исходников:
  ```bash
  sudo apt update
  sudo apt install -y build-essential cmake git curl zip unzip tar pkg-config
  ```

### Шаг 0 — установка vcpkg (если ещё не стоит)

Скрипты ожидают vcpkg по умолчанию в `C:\vcpkg` (Windows) или `~/vcpkg` (Linux). Если он у вас в другом месте — задайте переменную окружения `VCPKG_ROOT`.

**Windows:**
```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

**Linux:**
```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
```

Сами зависимости (`sqlite3`, `sqlitecpp`, `openssl`, `nlohmann-json`) ставить руками не нужно — их поставит скрипт сборки.

### Шаг 1 — клонирование

```bash
git clone <URL_репозитория> FreeCord
cd FreeCord
```

**Важно для WSL:** если репозиторий физически лежит на примонтированном Windows-диске (`/mnt/c/...`, `/mnt/d/...`), сборка на Linux упадёт с ошибкой CMake — файловая система WSL для смонтированных дисков (9p) не поддерживает нужные операции. Клонируйте (или скопируйте) проект в файловую систему самого Linux, например в `~/FreeCord`.

### Шаг 2 — сборка

**Windows** (PowerShell, из корня репозитория):
```powershell
.\build-windows.ps1
```

**Linux:**
```bash
./build-linux.sh
```

Каждый скрипт сам находит корень проекта (можно переносить папку куда угодно), ставит зависимости через vcpkg, конфигурирует и собирает проект в `build-windows/` или `build-linux/` соответственно, останавливает уже запущенные сервисы (чтобы пересборка не упёрлась в залоченный файл) и всегда чистит старые `*.db` — каждая сборка начинает с чистой базы.

### Шаг 3 — запуск сервера

Один скрипт поднимает все четыре сервиса (`auth`, `room`, `message`, `gateway`) и сводит их вывод в одну консоль, с цветом по сервису:

**Windows:**
```powershell
.\run-windows.ps1
```

**Linux:**
```bash
./run-linux.sh
```

`Ctrl+C` останавливает все четыре сервиса разом.

### Шаг 4 — запуск клиента

```bash
cd client/FreeCord.Desktop
dotnet run
```

Avalonia кроссплатформенная, поэтому клиент запускается так же и на Windows, и на Linux. Адрес сервера указывается в окне входа — по умолчанию `127.0.0.1:6000`, менять не нужно, если сервер и клиент на одной машине (в том числе если сервер — в WSL2: он сам форвардит порт на `localhost` для Windows).

### Настройка портов и путей к БД

Порты, пути к файлам БД, адрес хоста для внутренних вызовов, таймауты и лимит длины сообщения задаются в `config.json` в корне репозитория. SQL-запросы и схемы таблиц — в `db/<сервис>/`. Меняйте файлы в корне репозитория (не копии в `build-*/` — они перезаписываются при каждой сборке), затем пересоберите проект.

### Если что-то пошло не так

- **`vcpkg не найден`** — установите vcpkg (шаг 0) или укажите путь через `$env:VCPKG_ROOT` (Windows) / `export VCPKG_ROOT=...` (Linux).
- **Сборка на Linux падает с ошибкой CMake про `configure_file`** — проект лежит на примонтированном Windows-диске внутри WSL, см. примечание в шаге 1.
- **Не собирается клиент** — нужен .NET SDK 8.0 или новее (`dotnet --version`).

---

## English

These instructions are written so that after cloning the repository, a couple of commands are enough — no manual CMake setup required.

> The dev scripts (`build-*`, `run-*`) are for development only. Production deployment will get its own proper installer later.

### Prerequisites

**Both platforms:**
- Git
- [vcpkg](https://github.com/microsoft/vcpkg) — C++ package manager
- .NET SDK 8.0+ — only if you plan to run the client (`FreeCord.Desktop`)

**Windows:**
- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.20+ (bundled with Visual Studio)
- PowerShell (included with Windows by default)

**Linux** (native distro or WSL2):
- `build-essential`, `cmake` (3.20+), `git`, `curl`, `zip`, `unzip`, `tar`, `pkg-config` — vcpkg needs these to build dependencies from source:
  ```bash
  sudo apt update
  sudo apt install -y build-essential cmake git curl zip unzip tar pkg-config
  ```

### Step 0 — install vcpkg (if you don't have it yet)

The scripts default to `C:\vcpkg` (Windows) or `~/vcpkg` (Linux). If yours lives elsewhere, set the `VCPKG_ROOT` environment variable.

**Windows:**
```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

**Linux:**
```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
```

You don't need to install the actual dependencies (`sqlite3`, `sqlitecpp`, `openssl`, `nlohmann-json`) by hand — the build script does that for you.

### Step 1 — clone

```bash
git clone <repository_URL> FreeCord
cd FreeCord
```

**Important for WSL users:** if the repository lives on a mounted Windows drive (`/mnt/c/...`, `/mnt/d/...`), the Linux build will fail with a CMake error — WSL's filesystem driver for mounted drives (9p) doesn't support the operations CMake needs. Clone (or copy) the project into WSL's native Linux filesystem instead, e.g. `~/FreeCord`.

### Step 2 — build

**Windows** (PowerShell, from the repo root):
```powershell
.\build-windows.ps1
```

**Linux:**
```bash
./build-linux.sh
```

Each script locates its own repo root (so the folder can be moved anywhere), installs dependencies via vcpkg, configures and builds the project into `build-windows/` or `build-linux/`, stops any already-running services (so a rebuild isn't blocked by a locked binary), and always wipes old `*.db` files — every build starts from a clean database.

### Step 3 — run the server

One script starts all four services (`auth`, `room`, `message`, `gateway`) and merges their output into a single console, color-coded per service:

**Windows:**
```powershell
.\run-windows.ps1
```

**Linux:**
```bash
./run-linux.sh
```

`Ctrl+C` stops all four services at once.

### Step 4 — run the client

```bash
cd client/FreeCord.Desktop
dotnet run
```

Avalonia is cross-platform, so the client runs the same way on both Windows and Linux. The server address is entered on the login screen — the default `127.0.0.1:6000` works as-is if the server and client are on the same machine (including when the server runs inside WSL2, which forwards the port to `localhost` for Windows automatically).

### Configuring ports and database paths

Ports, database file paths, the internal service host, timeouts, and the message length limit are set in `config.json` at the repo root. SQL queries and table schemas live under `db/<service>/`. Edit the files at the repo root (not the copies under `build-*/` — those get overwritten on every build), then rebuild.

### Troubleshooting

- **`vcpkg not found`** — install vcpkg (step 0) or point to it via `$env:VCPKG_ROOT` (Windows) / `export VCPKG_ROOT=...` (Linux).
- **Linux build fails with a CMake `configure_file` error** — the project is on a mounted Windows drive inside WSL, see the note in step 1.
- **Client won't build** — you need .NET SDK 8.0 or newer (`dotnet --version`).
