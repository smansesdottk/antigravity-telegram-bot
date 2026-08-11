            current.append("```\n")
            chunks.append("".join(current).strip())
            current = ["```\n"]
            current_len = 4
    if current:
        chunks.append("".join(current).strip())
    return [c for c in chunks if c]


async def send_agy_response(message, markdown_text, reply_markup=None):
    chunks = split_markdown_for_telegram(markdown_text or "(Antigravity tidak memberikan output teks.)")
    for index, chunk in enumerate(chunks):
        formatted = markdown_to_telegram_html(chunk)
        try:
            await message.reply_text(
                formatted,
                parse_mode="HTML",
                disable_web_page_preview=True,
                reply_markup=reply_markup if index == len(chunks) - 1 else None,
            )
        except Exception as exc:
            logger.warning("HTML render fallback: %s", exc)
            await message.reply_text(
                chunk,
                reply_markup=reply_markup if index == len(chunks) - 1 else None,
            )


async def send_long_plain_text(message, text, reply_markup=None):
    text = text or ""
    chunks = [text[i:i + 3900] for i in range(0, len(text), 3900)] or [""]
    for i, chunk in enumerate(chunks):
        await message.reply_text(chunk, reply_markup=reply_markup if i == len(chunks) - 1 else None)


def scan_projects():
    projects = []
    try:
        for item in PROJECT_ROOT.iterdir():
            if not item.is_dir() or item.name.startswith("."):
                continue
            resolved = item.resolve()
            if not is_inside_project_root(resolved):
                continue
            projects.append({"id": item.name, "name": item.name, "path": str(resolved)})
    except Exception as exc:
        logger.exception("Project scan failed: %s", exc)
    return sorted(projects, key=lambda x: x["name"].lower())


def get_project_by_id(project_id):
    return next((p for p in scan_projects() if p["id"] == project_id), None)


def default_config():
    return {"model": None, "project": None, "work_mode": "readonly"}


def load_config():
    if not CONFIG_FILE.exists():
        return default_config()
    try:
        config = default_config()
        config.update(json.loads(CONFIG_FILE.read_text(encoding="utf-8")))
        return config
    except Exception:
        return default_config()


def save_config(config):
    CONFIG_FILE.write_text(json.dumps(config, indent=2), encoding="utf-8")


def get_selected_model():
    return load_config().get("model")


def set_selected_model(model):
    config = load_config(); config["model"] = model; save_config(config)


def get_work_mode():
    mode = load_config().get("work_mode", "readonly")
    return mode if mode in {"readonly", "write"} else "readonly"


def set_work_mode(mode):
    if mode not in {"readonly", "write"}:
        return False
    config = load_config(); config["work_mode"] = mode; save_config(config); return True


def get_work_mode_label():
    return "✏️ Write Mode" if get_work_mode() == "write" else "🔒 Read Only"


def get_agy_mode():
    return "accept-edits" if get_work_mode() == "write" else "plan"


def get_selected_project_id():
    project_id = load_config().get("project")
    return project_id if project_id and get_project_by_id(project_id) else None


def get_selected_project():
    project_id = get_selected_project_id()
    return get_project_by_id(project_id) if project_id else None


def get_selected_project_name():
    project = get_selected_project()
    return project["name"] if project else "Belum dipilih"


def set_selected_project(project_id):
    if not get_project_by_id(project_id):
        return False
    config = load_config(); config["project"] = project_id; save_config(config); return True


def load_conversation_data():
    if not SESSION_FILE.exists():
        return {}
    try:
        return json.loads(SESSION_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {}


def load_conversation_id():
    data = load_conversation_data(); project_id = get_selected_project_id()
    if not project_id or data.get("project") != project_id:
        return None
    return data.get("conversation_id")


def save_conversation_id(conversation_id):
    project_id = get_selected_project_id()
    if project_id:
        SESSION_FILE.write_text(
            json.dumps({"project": project_id, "conversation_id": conversation_id}, indent=2),
            encoding="utf-8",
        )


def clear_conversation_id():
    with suppress(FileNotFoundError):
        SESSION_FILE.unlink()


def get_agy_home_conversation():
    if not AGY_CACHE_FILE.exists():
        return None
    try:
        return json.loads(AGY_CACHE_FILE.read_text(encoding="utf-8")).get(USER_HOME)
    except Exception:
        return None


def load_history():
    if not HISTORY_FILE.exists():
        return {}
    try:
        data = json.loads(HISTORY_FILE.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def save_history(history):
    HISTORY_FILE.write_text(json.dumps(history, indent=2), encoding="utf-8")


def record_conversation(project_id, conversation_id, model=None):
    if not project_id or not conversation_id:
        return
    history = load_history(); items = history.setdefault(project_id, []); stamp = now_iso()
    existing = next((i for i in items if i.get("conversation_id") == conversation_id), None)
    if existing:
        existing["last_used"] = stamp
        if model: existing["model"] = model
    else:
        items.append({"conversation_id": conversation_id, "created_at": stamp, "last_used": stamp, "model": model})
    items.sort(key=lambda x: x.get("last_used", ""), reverse=True)
    history[project_id] = items[:50]; save_history(history)


def get_project_history(project_id):
    return sorted(load_history().get(project_id, []), key=lambda x: x.get("last_used", ""), reverse=True)


def history_contains(project_id, conversation_id):
    return any(i.get("conversation_id") == conversation_id for i in get_project_history(project_id))


def import_current_session_to_history():
    data = load_conversation_data(); pid, cid = data.get("project"), data.get("conversation_id")
    if pid and cid and get_project_by_id(pid):
        record_conversation(pid, cid, get_selected_model())


async def is_allowed(update: Update):
    user = update.effective_user
    if not user or user.id != ALLOWED_USER_ID:
        if update.message:
            await update.message.reply_text("⛔ Akses ditolak.")
        elif update.callback_query:
            await update.callback_query.answer("Akses ditolak.", show_alert=True)
        return False
    return True


async def run_process(*command, timeout=60, cwd=USER_HOME):
    process = None
    try:
        process = await asyncio.create_subprocess_exec(
            *command, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
            cwd=cwd, env=agy_env,
        )
        stdout, stderr = await asyncio.wait_for(process.communicate(), timeout=timeout)
    except asyncio.TimeoutError:
        if process:
            with suppress(Exception): process.kill(); await process.wait()
        return False, "", "Timeout."
    except Exception as exc:
        return False, "", str(exc)
    return (
        process.returncode == 0,
        stdout.decode("utf-8", errors="replace").strip(),
        stderr.decode("utf-8", errors="replace").strip(),
    )


async def run_agy_admin(prompt, timeout_seconds=120):
    ok, output, error = await run_process(
        AGY_PATH, "--output-format", "text", "--print-timeout", "2m", "--print", prompt,
        timeout=timeout_seconds,
    )
    if not ok:
        return False, error or output or "Perintah AGY gagal."
    if not output:
        return False, error or "AGY tidak memberikan output."
    return True, output


def quota_bar(percent):
    percent = max(0, min(100, percent)); filled = round(percent / 10)
    return "█" * filled + "░" * (10 - filled)


def quota_status_icon(percent):
    return "🟢" if percent >= 75 else "🟡" if percent >= 40 else "🟠" if percent >= 15 else "🔴"


def parse_usage_output(raw):
    pattern = re.compile(
        r"^(Gemini Models|Claude and GPT models)\s+"
        r"(Weekly Limit Remaining|Five Hour Limit Remaining)\s+(\d+)%\s+(\S+)$"
    )
    entries = []
    for line in raw.splitlines():
        m = pattern.match(line.strip())
        if m:
            entries.append({"family": m.group(1), "limit": m.group(2), "remaining": int(m.group(3)), "reset": m.group(4)})
    return entries


def format_usage_html(raw):
    entries = parse_usage_output(raw)
    if not entries:
        return f"<b>📊 Antigravity Usage</b>\n\n<pre>{html.escape(raw)}</pre>"
    result = ["<b>📊 Antigravity Usage</b>", ""]
    for family_id, icon, name in [
        ("Gemini Models", "🔷", "Gemini Models"),
        ("Claude and GPT models", "🟣", "Claude & GPT Models"),
    ]:
        values = [e for e in entries if e["family"] == family_id]
        if not values: continue
        result += [f"{icon} <b>{name}</b>", ""]
        order = ["Five Hour Limit Remaining", "Weekly Limit Remaining"]
        values.sort(key=lambda e: order.index(e["limit"]) if e["limit"] in order else 99)
        for item in values:
            pct = item["remaining"]
            label = "Limit 5 Jam" if item["limit"].startswith("Five") else "Limit Mingguan"
            result += [
                f"{quota_status_icon(pct)} <b>{label}</b>",
                f"<code>{quota_bar(pct)}</code> <b>{pct}%</b> tersisa",
                f"🔄 Reset: {html.escape(local_datetime_string(item['reset']))}",
            ]
            countdown = reset_countdown(item["reset"])
            if countdown: result.append(f"⏳ Sekitar {html.escape(countdown)} lagi")
            result.append("")
    result.append("ℹ️ Persentase menunjukkan <b>sisa quota</b>.")
    return "\n".join(result).strip()


async def get_available_models():
    for attempt in range(1, 4):
        ok, output, error = await run_process(AGY_PATH, "models", timeout=60)
        if ok and output:
            models = []
            for line in output.splitlines():
                parts = line.strip().split(None, 1)
                if len(parts) >= 2: models.append((parts[0], parts[1]))
            if models: return models
        logger.warning("models attempt %s failed: %s", attempt, error or output)
        if attempt < 3: await asyncio.sleep(2)
    return []


def get_project_backup_dir(project_id):
    directory = BACKUP_ROOT / safe_filename(project_id)
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def create_project_backup(project, prefix="backup"):
    project_root = Path(project["path"]).resolve()
    if not is_inside_project_root(project_root):
        raise RuntimeError("Project berada di luar PROJECT_ROOT.")
    backup_dir = get_project_backup_dir(project["id"])
    backup_path = backup_dir / f"{prefix}-{timestamp_filename()}.zip"
    file_count = 0
    with zipfile.ZipFile(backup_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for root, dirs, files in os.walk(project_root, followlinks=False):
            root_path = Path(root)
            dirs[:] = [
                d for d in dirs
                if d not in BACKUP_EXCLUDE and not (root_path / d).is_symlink()
            ]
            for filename in files:
                source = root_path / filename
                if source.is_symlink() or not source.is_file(): continue
                archive.write(source, arcname=str(source.relative_to(project_root)))
                file_count += 1
    return backup_path, file_count


def list_project_backups(project_id):
    backups = [p for p in get_project_backup_dir(project_id).iterdir() if p.is_file() and p.suffix.lower() == ".zip"]
    return sorted(backups, key=lambda p: p.stat().st_mtime, reverse=True)


def is_zip_symlink(info):
