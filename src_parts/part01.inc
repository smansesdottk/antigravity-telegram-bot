#!/usr/bin/env python3
import socket
import os
import re
import json
import time
import html
import shutil
import zipfile
import logging
import asyncio
import secrets
from pathlib import Path
from datetime import datetime, timezone
from zoneinfo import ZoneInfo
from logging.handlers import RotatingFileHandler
from contextlib import suppress

from dotenv import load_dotenv

# Load .env from the directory containing this script, regardless of cwd.
BASE_DIR = Path(__file__).resolve().parent
load_dotenv(BASE_DIR / ".env")


def env_bool(name: str, default: bool = False) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def env_path(name: str, default: str) -> Path:
    return Path(os.path.expandvars(os.path.expanduser(os.getenv(name, default)))).resolve()


TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "").strip()
TELEGRAM_USER_ID_RAW = os.getenv("TELEGRAM_USER_ID", "").strip()
AGY_PATH = str(env_path("AGY_PATH", "~/.local/bin/agy"))
USER_HOME = str(Path.home().resolve())
PROJECT_ROOT = env_path("PROJECT_ROOT", "~/proyekku")
BOT_DATA_DIR = env_path("BOT_DATA_DIR", "~/.local/share/antigravity-telegram-bot")
BACKUP_ROOT = (BOT_DATA_DIR / "backups").resolve()
CONFIG_FILE = BOT_DATA_DIR / "bot_config.json"
SESSION_FILE = BOT_DATA_DIR / "conversation.json"
HISTORY_FILE = BOT_DATA_DIR / "history.json"
LOG_FILE = BOT_DATA_DIR / "bot.log"
AGY_CACHE_FILE = Path.home() / ".gemini/antigravity-cli/cache/last_conversations.json"
LOCAL_TIMEZONE = ZoneInfo(os.getenv("TIMEZONE", "UTC"))
AGY_PRINT_TIMEOUT = os.getenv("AGY_PRINT_TIMEOUT", "10m")
AGY_TASK_TIMEOUT = os.getenv("AGY_TASK_TIMEOUT", "30m")
BACKUP_EXCLUDE = {
    part.strip() for part in os.getenv(
        "BACKUP_EXCLUDE", ".venv,node_modules,__pycache__,.cache"
    ).split(",") if part.strip()
}

try:
    ALLOWED_USER_ID = int(TELEGRAM_USER_ID_RAW)
except ValueError:
    ALLOWED_USER_ID = 0

# Optional Telegram IPv4 workaround. It is disabled by default because many hosts
# have healthy IPv6. Enable TELEGRAM_FORCE_IPV4=true only when needed.
if env_bool("TELEGRAM_FORCE_IPV4", False):
    _original_getaddrinfo = socket.getaddrinfo

    def ipv4_only_getaddrinfo(host, port, family=0, type=0, proto=0, flags=0):
        return _original_getaddrinfo(host, port, socket.AF_INET, type, proto, flags)

    socket.getaddrinfo = ipv4_only_getaddrinfo

from telegram import (
    Update,
    InlineKeyboardButton,
    InlineKeyboardMarkup,
    ReplyKeyboardMarkup,
    BotCommand,
)
from telegram.ext import (
    ApplicationBuilder,
    CommandHandler,
    CallbackQueryHandler,
    MessageHandler,
    ContextTypes,
    filters,
)

# AGY subprocess environment.
agy_env = os.environ.copy()
agy_env["HOME"] = USER_HOME
agy_env["GODEBUG"] = os.getenv("AGY_GODEBUG", "netdns=cgo")
_runtime = f"/run/user/{os.getuid()}"
if os.path.isdir(_runtime):
    agy_env.setdefault("XDG_RUNTIME_DIR", _runtime)
    agy_env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path={_runtime}/bus")

BOT_DATA_DIR.mkdir(parents=True, exist_ok=True)
BACKUP_ROOT.mkdir(parents=True, exist_ok=True)
PROJECT_ROOT.mkdir(parents=True, exist_ok=True)

logger = logging.getLogger("antigravity_telegram")
logger.setLevel(logging.INFO)
if not logger.handlers:
    handler = RotatingFileHandler(
        LOG_FILE, maxBytes=2_000_000, backupCount=3, encoding="utf-8"
    )
    handler.setFormatter(logging.Formatter("%(asctime)s | %(levelname)s | %(message)s"))
    logger.addHandler(handler)
logging.getLogger("httpx").setLevel(logging.WARNING)
logging.getLogger("telegram").setLevel(logging.WARNING)

agy_lock = asyncio.Lock()
current_process = None
process_stopped_by_user = False
waiting_new_project_name = False
PROJECT_CALLBACKS = {}
RESTORE_CALLBACKS = {}


def main_keyboard():
    return ReplyKeyboardMarkup(
        [
            ["📁 Project", "➕ New Project"],
            ["🧠 Model", "📊 Usage"],
            ["🕘 History", "📂 Files"],
            ["💾 Backup", "♻️ Restore"],
            ["🌿 Git Status", "🛡 Work Mode"],
            ["💳 Credits", "ℹ️ Status"],
            ["🩺 Health", "📜 Logs"],
            ["🆕 New", "🔴 Stop"],
            ["💬 Session", "❓ Help"],
        ],
        resize_keyboard=True,
        one_time_keyboard=True,
        is_persistent=False,
    )


def now_iso():
    return datetime.now(timezone.utc).isoformat()


def timestamp_filename():
    return datetime.now(LOCAL_TIMEZONE).strftime("%Y%m%d-%H%M%S")


def format_duration(seconds):
    seconds = max(0, int(seconds))
    if seconds < 60:
        return f"{seconds} detik"
    minutes, seconds = divmod(seconds, 60)
    if minutes < 60:
        return f"{minutes} menit {seconds} detik"
    hours, minutes = divmod(minutes, 60)
    return f"{hours} jam {minutes} menit {seconds} detik"


def parse_datetime(value):
    if not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except Exception:
        return None


def local_datetime_string(value):
    dt = parse_datetime(value)
    if not dt:
        return value or "-"
    return dt.astimezone(LOCAL_TIMEZONE).strftime("%Y-%m-%d %H:%M %Z")


def reset_countdown(value):
    reset_dt = parse_datetime(value)
    if not reset_dt:
        return None
    seconds = int((reset_dt.astimezone(timezone.utc) - datetime.now(timezone.utc)).total_seconds())
    if seconds <= 0:
        return "sebentar lagi"
    days, rem = divmod(seconds, 86400)
    hours, rem = divmod(rem, 3600)
    minutes = rem // 60
    if days:
        return f"{days} hari {hours} jam"
    if hours:
        return f"{hours} jam {minutes} menit"
    return f"{max(1, minutes)} menit"


def is_inside_path(child: Path, parent: Path):
    try:
        child = child.resolve()
        parent = parent.resolve()
        return child == parent or parent in child.parents
    except Exception:
        return False


def is_inside_project_root(path: Path):
    return is_inside_path(path, PROJECT_ROOT)


def is_valid_project_name(name):
    if not name or len(name) > 64 or name in {".", ".."}:
        return False
    if "/" in name or "\\" in name or os.path.isabs(name) or name.startswith("."):
        return False
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
        return False
    return is_inside_project_root(PROJECT_ROOT / name)


def safe_filename(filename):
    filename = Path(filename).name
    filename = re.sub(r"[^A-Za-z0-9._() \-\[\]]", "_", filename).strip(" .")
    return (filename or f"telegram_file_{int(time.time())}")[:180]


def unique_target(directory: Path, filename):
    target = directory / safe_filename(filename)
    if not target.exists():
        return target
    stem, suffix = target.stem, target.suffix
    counter = 1
    while True:
        candidate = directory / f"{stem}_{counter}{suffix}"
        if not candidate.exists():
            return candidate
        counter += 1


def resolve_project_file(relative_path):
    project = get_selected_project()
    if not project or not relative_path:
        return None
    project_path = Path(project["path"]).resolve()
    user_path = Path(relative_path)
    if user_path.is_absolute():
        return None
    try:
        target = (project_path / user_path).resolve()
    except Exception:
        return None
    return target if is_inside_path(target, project_path) else None


def shorten_server_path(path_text):
    try:
        clean = path_text[len("file://"):] if path_text.startswith("file://") else path_text
        path = Path(clean)
        project = get_selected_project()
        if project:
            project_path = Path(project["path"]).resolve()
            with suppress(Exception):
                resolved = path.resolve()
                if is_inside_path(resolved, project_path):
                    relative = resolved.relative_to(project_path)
                    return project["name"] if str(relative) == "." else str(relative)
        with suppress(Exception):
            resolved = path.resolve()
            if is_inside_project_root(resolved):
                return str(resolved.relative_to(PROJECT_ROOT))
        return clean
    except Exception:
        return path_text


def inline_markdown_to_html(text):
    placeholders = {}
    counter = 0

    def placeholder(rendered):
        nonlocal counter
        key = f"@@TGPH{counter}@@"
        placeholders[key] = rendered
        counter += 1
        return key

    def replace_code(match):
        return placeholder(f"<code>{html.escape(match.group(1), quote=False)}</code>")

    text = re.sub(r"`([^`\n]+)`", replace_code, text)

    def replace_link(match):
        label, url = match.group(1).strip(), match.group(2).strip()
        if url.startswith("file://"):
            return placeholder(f"📄 <code>{html.escape(shorten_server_path(url))}</code>")
        if url.startswith(("https://", "http://")):
            return placeholder(
                f'<a href="{html.escape(url, quote=True)}">{html.escape(label)}</a>'
            )
        return f"{label} ({url})"

    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", replace_link, text)
    text = html.escape(text, quote=False)
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"__(.+?)__", r"<b>\1</b>", text)
    text = re.sub(r"~~(.+?)~~", r"<s>\1</s>", text)
    for key, rendered in placeholders.items():
        text = text.replace(key, rendered)
    return text


def markdown_to_telegram_html(markdown_text):
    if not markdown_text:
        return ""
    lines = markdown_text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    output, code_lines = [], []
    in_code_block = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("```"):
            if not in_code_block:
                in_code_block, code_lines = True, []
            else:
                output.append(f"<pre><code>{html.escape(chr(10).join(code_lines), quote=False)}</code></pre>")
                in_code_block, code_lines = False, []
            continue
        if in_code_block:
            code_lines.append(line)
            continue
        if stripped == "":
            output.append("")
            continue
        if re.fullmatch(r"\s*[-*_]{3,}\s*", line):
            output.append("──────────────")
            continue
        m = re.match(r"^\s*#{1,6}\s+(.+)$", line)
        if m:
            output.append(f"<b>{inline_markdown_to_html(m.group(1))}</b>")
            continue
        m = re.match(r"^\s*>\s?(.*)$", line)
        if m:
            output.append(f"<blockquote>{inline_markdown_to_html(m.group(1))}</blockquote>")
            continue
        m = re.match(r"^(\s*)[-*+]\s+(.+)$", line)
        if m:
            output.append(f"{'   ' * (len(m.group(1)) // 2)}• {inline_markdown_to_html(m.group(2))}")
            continue
        m = re.match(r"^(\s*)(\d+)[.)]\s+(.+)$", line)
        if m:
            output.append(f"{'   ' * (len(m.group(1)) // 2)}{m.group(2)}. {inline_markdown_to_html(m.group(3))}")
            continue
        output.append(inline_markdown_to_html(line))
    if in_code_block:
        output.append(f"<pre><code>{html.escape(chr(10).join(code_lines), quote=False)}</code></pre>")
    return re.sub(r"\n{4,}", "\n\n\n", "\n".join(output)).strip()


def split_markdown_for_telegram(text, max_chars=2800):
    if len(text) <= max_chars:
        return [text]
    chunks, current, in_fence = [], [], False
    current_len = 0
    for line in text.splitlines(keepends=True):
        if line.strip().startswith("```"):
            in_fence = not in_fence
        if current and current_len + len(line) > max_chars and not in_fence:
            chunks.append("".join(current).strip())
            current, current_len = [], 0
        current.append(line)
        current_len += len(line)
        if current_len > 3800 and in_fence:
            # Close/reopen huge fences so Telegram never receives an oversized message.
