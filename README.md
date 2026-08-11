# Antigravity Telegram Bot

Remote-control **Google Antigravity CLI (`agy`)** from a private Telegram bot. The bot exposes project selection, model selection, conversation resume/history, quota monitoring, safe backup/restore, Git status, file upload/download, read-only/write modes, long-running tasks, progress updates, health checks, and a systemd deployment path.

> **Security model:** the bot only accepts one configured numeric Telegram user ID and only lists projects directly below `PROJECT_ROOT`. This is **not an OS-level sandbox**: the `agy` process still runs with the operating-system permissions of the local account. Configure Antigravity permissions conservatively.

## Features

- Private allowlist by Telegram numeric user ID
- Dynamic `/project` list from one root directory
- `/newproject` creates projects only below that root
- `/model`, `/usage`, `/credits`
- `/history` and conversation resume
- Telegram-friendly Markdown → HTML rendering
- File upload and `/getfile`
- `/backup` + guarded `/restore` with automatic pre-restore safety backup
- `/gitstatus`
- `/workmode`: Read Only (`plan`) or Write (`accept-edits`)
- `/task <instruction>` for longer agent work
- `/stop`, progress timer, `/health`, `/logs`
- Optional user-level systemd service

## Requirements

The primary tested deployment target is **Linux** (Ubuntu/Debian style host). You need:

- Python 3.10+
- Git
- A Telegram account and a bot created with **@BotFather**
- Google Antigravity CLI installed and authenticated
- An Antigravity account/plan that can run the models you select

Antigravity's official CLI installer for Linux/macOS is:

```bash
curl -fsSL https://antigravity.google/cli/install.sh | bash
```

The default binary location is `~/.local/bin/agy`.

## 1. Install and authenticate Antigravity CLI

Install `agy`, then run it interactively once:

```bash
agy
```

Complete the sign-in flow. On remote SSH hosts, Antigravity can provide a URL/code authentication flow.

Verify:

```bash
agy models
agy -p "/usage"
```

Do not continue until those commands work from the same Linux user that will run the Telegram bot.

## 2. Create a Telegram bot

1. Open Telegram and chat with **@BotFather**.
2. Run `/newbot` and follow the prompts.
3. Copy the bot token. Treat it like a password.
4. Obtain your own **numeric Telegram user ID** (for example with a reputable ID bot, or temporarily inspect an update while developing).

The bot will reject every Telegram account except the ID configured in `.env`.

## 3. Clone and create the Python environment

```bash
git clone https://github.com/smansesdottk/antigravity-telegram-bot.git
cd antigravity-telegram-bot
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
pip install -r requirements.txt
```

## 4. Configure `.env`

```bash
cp .env.example .env
chmod 600 .env
nano .env
```

Required values:

```dotenv
TELEGRAM_BOT_TOKEN=your_bot_token
TELEGRAM_USER_ID=your_numeric_telegram_user_id
```

Choose a project root. Every immediate subdirectory becomes selectable in `/project`:

```dotenv
PROJECT_ROOT=~/proyekku
```

Create it if necessary:

```bash
mkdir -p ~/proyekku
```

For Indonesia/Makassar, for example:

```dotenv
TIMEZONE=Asia/Makassar
```

If your `agy` is somewhere else, set `AGY_PATH` accordingly.

## 5. Antigravity permissions for headless mode

This bot invokes `agy --print`, so Antigravity cannot display an interactive approval card. Unconfigured sensitive actions normally fall back to **Ask** and may therefore be denied in headless execution.

Antigravity stores its global settings at:

```text
~/.gemini/antigravity-cli/settings.json
```

A conservative starting example is included as [`permissions.example.json`](permissions.example.json). **Review it before copying it.** Do not blindly allow `command(*)`, and do not add `--dangerously-skip-permissions` to this bot.

Example workflow:

```bash
mkdir -p ~/.gemini/antigravity-cli
cp permissions.example.json ~/.gemini/antigravity-cli/settings.json
```

If you already have a settings file, merge the `permissions` object instead of overwriting your existing preferences.

## 6. Run manually first

```bash
source .venv/bin/activate
python bot.py
```

You should see `Antigravity Telegram Bot aktif...`.

In Telegram:

1. Send `/start`.
2. Use `/project` or `/newproject`.
3. Choose `/model` if desired.
4. Send a read-only test prompt such as `Tampilkan struktur utama project. Jangan ubah file.`
5. Check `/usage` and `/health`.

The default work mode is **Read Only**.

## 7. Optional IPv4 workaround

If Telegram API calls time out over IPv6 but work with IPv4, set:

```dotenv
TELEGRAM_FORCE_IPV4=true
```

Leave this `false` on hosts with healthy IPv6.

`AGY_GODEBUG=netdns=cgo` is configurable separately for environments where Go's DNS behavior causes connectivity issues.

## 8. Install as a user systemd service (Linux)

Stop any manually running copy first, otherwise two processes may compete for Telegram long polling.

```bash
chmod +x scripts/install-systemd.sh
./scripts/install-systemd.sh
```

For automatic start after a server reboot even before an SSH login, run once:

```bash
sudo loginctl enable-linger "$USER"
```

Useful commands:

```bash
systemctl --user status antigravity-telegram
systemctl --user restart antigravity-telegram
systemctl --user stop antigravity-telegram
journalctl --user -u antigravity-telegram -f
```

## Commands

| Command | Purpose |
|---|---|
| `/project` | Select a project below `PROJECT_ROOT` |
| `/newproject` | Create and select a new project |
| `/model` | Select an available Antigravity model |
| `/workmode` | Read Only / Write Mode |
| `/new` | Start a new conversation |
| `/history` | Resume a locally recorded conversation |
| `/task <instruction>` | Longer agent task |
| `/usage` | Model quota / reset information |
| `/credits` | Model credits output |
| `/backup` | Create a project ZIP backup |
| `/restore` | Restore from a backup with confirmation and rollback safety copy |
| `/gitstatus` | Git branch/status/last commit/diff summary |
| `/files` | List project-root files |
| `/getfile <path>` | Send a project file to Telegram |
| `/health` | Check bot, AGY, auth/quota, project and session |
| `/logs` | Last bot log lines |
| `/status` | Active model/project/mode/session |
| `/session` | Active conversation ID |
| `/stop` | Stop the active AGY subprocess |
| `/help` | Help |

## Backups

Backups are stored outside `PROJECT_ROOT`, below `BOT_DATA_DIR/backups`. By default the backup skips `.venv`, `node_modules`, `__pycache__`, and `.cache`; configure `BACKUP_EXCLUDE` if you want different behavior.

Restore validates the ZIP, rejects path traversal and symbolic-link members, creates a pre-restore safety backup, and attempts rollback if extraction fails.

## Important security notes

- **Never commit `.env`**, OAuth tokens, `settings.json` containing personal paths, logs, conversation/history files, or backup archives.
- Keep `.env` permission-restricted (`chmod 600 .env`).
- This project-root restriction controls what the **bot UI selects**, but it is not a Linux filesystem jail. Antigravity commands can potentially reach other files accessible to the OS account unless you restrict Antigravity permissions/sandboxing.
- Review Write Mode permissions before using the bot from an internet-connected Telegram account.
- Do not use `--dangerously-skip-permissions`.
- Use a dedicated Linux user if you want a stronger operating-system boundary.

## Troubleshooting

### `jetski: no output produced ... command permission ... auto-denied`

Headless AGY needed a command permission that was still `Ask`. Add a narrow `allow` rule for only the command you actually need. See `permissions.example.json` and the official Antigravity permissions documentation.

### Telegram `ConnectTimeout`

Test whether HTTPS works over IPv4 but not IPv6. If that is the problem, try `TELEGRAM_FORCE_IPV4=true`.

### Bot works manually but not under systemd

Verify the same Linux user can run `agy -p "/usage"` and that `.env` exists in the repository directory. Inspect:

```bash
journalctl --user -u antigravity-telegram -n 100 --no-pager
```

### `Conflict: terminated by other getUpdates request`

You have more than one bot process using the same token. Stop the manual copy or the service so only one remains.

## Official Antigravity references

- CLI installation/auth: https://antigravity.google/docs/cli-install
- CLI permissions: https://antigravity.google/docs/cli-permissions
- CLI settings: https://antigravity.google/docs/cli-settings
- Model quotas (`/usage`): https://antigravity.google/docs/cli/commands/usage

## License

MIT. See [LICENSE](LICENSE).
