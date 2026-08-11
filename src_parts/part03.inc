    return ((info.external_attr >> 16) & 0o170000) == 0o120000


def validate_zip_archive(zip_path, destination):
    destination = destination.resolve(); total_size = 0; file_count = 0
    with zipfile.ZipFile(zip_path, "r") as archive:
        for info in archive.infolist():
            name = info.filename.replace("\\", "/")
            if not name: continue
            path_obj = Path(name)
            if path_obj.is_absolute() or ".." in path_obj.parts:
                raise RuntimeError("ZIP mengandung path yang tidak aman.")
            if is_zip_symlink(info):
                raise RuntimeError("ZIP mengandung symbolic link.")
            target = (destination / path_obj).resolve()
            if not is_inside_path(target, destination):
                raise RuntimeError("ZIP mencoba keluar dari project.")
            if not info.is_dir():
                file_count += 1; total_size += info.file_size
                if file_count > 100_000: raise RuntimeError("ZIP berisi terlalu banyak file.")
                if total_size > 20 * 1024**3: raise RuntimeError("Hasil ekstraksi melebihi 20 GB.")
    return file_count, total_size


def clear_project_directory(project_path):
    project_path = project_path.resolve()
    if not is_inside_project_root(project_path):
        raise RuntimeError("Path project tidak diizinkan.")
    for item in project_path.iterdir():
        if item.is_symlink() or item.is_file(): item.unlink()
        elif item.is_dir(): shutil.rmtree(item)


def safe_extract_zip(zip_path, destination):
    destination = destination.resolve(); validate_zip_archive(zip_path, destination)
    with zipfile.ZipFile(zip_path, "r") as archive:
        for info in archive.infolist():
            rel = Path(info.filename.replace("\\", "/"))
            if str(rel) in {"", "."}: continue
            target = destination / rel
            if info.is_dir(): target.mkdir(parents=True, exist_ok=True); continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info, "r") as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst)


async def run_agy(prompt, *, print_timeout=None, task_mode=False):
    global current_process, process_stopped_by_user
    process_stopped_by_user = False
    project = get_selected_project()
    if not project: return False, "Belum ada project aktif."
    project_path = Path(project["path"]).resolve()
    if not is_inside_project_root(project_path): return False, "Project berada di luar root yang diizinkan."
    conversation_id, selected_model = load_conversation_id(), get_selected_model()
    command = [
        AGY_PATH, "--add-dir", str(project_path), "--mode", get_agy_mode(),
        "--output-format", "text", "--print-timeout", print_timeout or AGY_PRINT_TIMEOUT,
    ]
    if conversation_id: command += ["--conversation", conversation_id]
    if selected_model: command += ["--model", selected_model]
    final_prompt = prompt
    if get_work_mode() == "readonly":
        final_prompt = (
            "MODE READ ONLY AKTIF. Analisis project sesuai kebutuhan, tetapi jangan mengubah, "
            "membuat, memindahkan, atau menghapus file project.\n\nPermintaan pengguna:\n" + prompt
        )
    if task_mode:
        final_prompt = (
            "Ini adalah TASK MODE. Kerjakan tugas secara menyeluruh. Lakukan verifikasi/test bila relevan, "
            "lalu berikan ringkasan, file yang diperiksa/diubah, hasil testing, dan masalah tersisa.\n\n" + final_prompt
        )
    command += ["--print", final_prompt]
    logger.info("AGY start project=%s model=%s mode=%s task=%s", project["id"], selected_model or "default", get_work_mode(), task_mode)
    current_process = await asyncio.create_subprocess_exec(
        *command, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        cwd=USER_HOME, env=agy_env,
    )
    try:
        stdout, stderr = await current_process.communicate()
        output = stdout.decode("utf-8", errors="replace").strip()
        error = stderr.decode("utf-8", errors="replace").strip()
        if process_stopped_by_user: return None, None
        if current_process.returncode != 0: return False, error or output or "Antigravity gagal."
        if not output and error: return False, error
        new_cid = get_agy_home_conversation()
        if new_cid:
            save_conversation_id(new_cid); record_conversation(project["id"], new_cid, selected_model)
        return True, output
    finally:
        current_process = None


async def progress_worker(status_message, started_at, project_name, model_name, label="Antigravity"):
    try:
        while True:
            await asyncio.sleep(15)
            elapsed = time.monotonic() - started_at
            with suppress(Exception):
                await status_message.edit_text(
                    f"⏳ {label} masih bekerja...\n\n🧠 {model_name}\n📁 {project_name}\n"
                    f"🛡 {get_work_mode_label()}\n⏱ {format_duration(elapsed)}\n\n🔴 /stop untuk menghentikan."
                )
    except asyncio.CancelledError:
        raise


async def execute_agy_request(update, prompt, *, task_mode=False):
    project = get_selected_project()
    if not project:
        await update.message.reply_text("📁 Belum ada project aktif. Gunakan 📁 Project.", reply_markup=main_keyboard()); return
    if agy_lock.locked():
        await update.message.reply_text("⏳ Antigravity masih bekerja. Gunakan 🔴 Stop."); return
    async with agy_lock:
        model = get_selected_model() or "Otomatis (Default AGY)"
        started = time.monotonic(); label = "Agent Task" if task_mode else "Antigravity"
        status = await update.message.reply_text(
            f"🤖 {label} sedang mengerjakan...\n\n🧠 {model}\n📁 {project['name']}\n🛡 {get_work_mode_label()}\n⏱ 0 detik"
        )
        progress = asyncio.create_task(progress_worker(status, started, project["name"], model, label))
        try:
            success, response = await run_agy(prompt, print_timeout=AGY_TASK_TIMEOUT if task_mode else AGY_PRINT_TIMEOUT, task_mode=task_mode)
        finally:
            progress.cancel()
            with suppress(asyncio.CancelledError): await progress
        elapsed = time.monotonic() - started
        if success is None:
            with suppress(Exception): await status.edit_text(f"🔴 Antigravity dihentikan.\n\n⏱ {format_duration(elapsed)}")
            return
        if success:
            with suppress(Exception): await status.edit_text(
                f"✅ {label} selesai.\n\n🧠 {model}\n📁 {project['name']}\n🛡 {get_work_mode_label()}\n⏱ {format_duration(elapsed)}"
            )
            await send_agy_response(update.message, response, reply_markup=main_keyboard())
        else:
            with suppress(Exception): await status.edit_text(f"❌ {label} gagal.\n\n⏱ {format_duration(elapsed)}")
            await send_long_plain_text(update.message, f"❌ Error Antigravity\n\n{response}", reply_markup=main_keyboard())


async def start_command(update, context):
    if not await is_allowed(update): return
    await update.message.reply_text(
        f"🚀 Antigravity Telegram aktif.\n\n📁 Root project: {PROJECT_ROOT}\n🛡 {get_work_mode_label()}\n\nGunakan ❓ Help untuk bantuan.",
        reply_markup=main_keyboard(),
    )


async def help_command(update, context):
    if not await is_allowed(update): return
    text = (
        "❓ Bantuan Antigravity Telegram\n\n"
        "/project — pilih project\n/newproject — buat project baru\n/model — pilih model\n"
        "/workmode — Read Only / Write Mode\n/new — conversation baru\n/history — riwayat conversation\n"
        "/task <instruksi> — Agent Task panjang\n/usage — quota Antigravity\n/credits — model credits\n"
        "/backup — backup project ke ZIP\n/restore — restore dengan safety backup\n/gitstatus — status Git\n"
        "/files — isi root project\n/getfile <path> — kirim file ke Telegram\n"
        "Kirim dokumen langsung untuk upload ke project aktif.\n/health — health check\n/logs — log bot\n"
        "/status — status bot\n/session — Conversation ID\n/stop — hentikan AGY\n/cancel — batalkan input\n/help — bantuan"
    )
    await send_long_plain_text(update.message, text, reply_markup=main_keyboard())


async def status_command(update, context):
    if not await is_allowed(update): return
    project = get_selected_project(); model = get_selected_model() or "Otomatis (Default AGY)"
    await update.message.reply_text(
        f"🟢 Antigravity Telegram\n\n🤖 Status: {'sibuk' if agy_lock.locked() else 'siap'}\n"
        f"🧠 Model: {model}\n🛡 Work Mode: {get_work_mode_label()}\n"
        f"📁 Project: {project['name'] if project else 'Belum dipilih'}\n"
        f"📂 Path: {project['path'] if project else '-'}\n"
        f"💬 Conversation: {'aktif' if load_conversation_id() else 'belum dibuat'}",
        reply_markup=main_keyboard(),
    )


async def new_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⚠️ Gunakan /stop terlebih dahulu."); return
    cid, pid = load_conversation_id(), get_selected_project_id()
    if cid and pid: record_conversation(pid, cid, get_selected_model())
    clear_conversation_id()
    await update.message.reply_text("🆕 Conversation baru siap. Conversation lama tetap ada di /history.", reply_markup=main_keyboard())


async def session_command(update, context):
    if not await is_allowed(update): return
    cid = load_conversation_id()
    await update.message.reply_text(
        f"💬 Conversation aktif:\n\n{cid}\n\n📁 {get_selected_project_name()}" if cid else "💬 Belum ada conversation aktif.",
        reply_markup=main_keyboard(),
    )


async def stop_command(update, context):
    global current_process, process_stopped_by_user
    if not await is_allowed(update): return
    if current_process is None:
        await update.message.reply_text("ℹ️ Tidak ada proses Antigravity yang sedang berjalan.", reply_markup=main_keyboard()); return
    process_stopped_by_user = True
    try:
        current_process.terminate()
        try: await asyncio.wait_for(current_process.wait(), timeout=5)
        except asyncio.TimeoutError: current_process.kill(); await current_process.wait()
        await update.message.reply_text("🔴 Proses Antigravity dihentikan.", reply_markup=main_keyboard())
    except ProcessLookupError:
        await update.message.reply_text("ℹ️ Proses sudah selesai.", reply_markup=main_keyboard())


async def project_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⚠️ Gunakan /stop sebelum mengganti project."); return
    projects = scan_projects()
    if not projects: await update.message.reply_text("📁 Belum ada project. Gunakan /newproject."); return
    PROJECT_CALLBACKS.clear(); keyboard = []; selected = get_selected_project_id()
    for project in projects:
        token = secrets.token_hex(4); PROJECT_CALLBACKS[token] = project["id"]
        keyboard.append([InlineKeyboardButton(
            f"{'✅' if project['id'] == selected else '📁'} {project['name']}", callback_data=f"project:{token}"
        )])
    await update.message.reply_text("📁 Pilih project:", reply_markup=InlineKeyboardMarkup(keyboard))


async def project_callback(update, context):
    if not await is_allowed(update): return
    query = update.callback_query
    if agy_lock.locked(): await query.answer("Antigravity sedang bekerja.", show_alert=True); return
    token = query.data.split(":", 1)[1]; project_id = PROJECT_CALLBACKS.get(token)
    project = get_project_by_id(project_id) if project_id else None
    if not project: await query.answer("Project tidak ditemukan. Buka /project lagi.", show_alert=True); return
    await query.answer(); old_project, old_cid = get_selected_project_id(), load_conversation_id()
    if old_project and old_cid: record_conversation(old_project, old_cid, get_selected_model())
