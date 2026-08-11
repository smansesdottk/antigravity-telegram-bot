    set_selected_project(project_id)
    if old_project != project_id: clear_conversation_id()
    await query.edit_message_text(f"✅ Project aktif:\n\n📁 {project['name']}\n📂 {project['path']}\n\nGunakan /history untuk conversation lama.")


async def newproject_command(update, context):
    global waiting_new_project_name
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⚠️ Gunakan /stop terlebih dahulu."); return
    waiting_new_project_name = True
    await update.message.reply_text("➕ Kirim nama project baru.\nContoh: inventory-app\n\n/cancel untuk batal.")


async def cancel_command(update, context):
    global waiting_new_project_name
    if not await is_allowed(update): return
    waiting_new_project_name = False
    await update.message.reply_text("❎ Input dibatalkan.", reply_markup=main_keyboard())


async def handle_new_project_name(update):
    global waiting_new_project_name
    name = update.message.text.strip()
    if not is_valid_project_name(name):
        await update.message.reply_text("❌ Nama tidak valid. Gunakan huruf, angka, titik, dash, atau underscore."); return
    target = PROJECT_ROOT / name
    if target.exists(): await update.message.reply_text("⚠️ Folder sudah ada. Gunakan /project."); return
    try: target.mkdir(parents=False, exist_ok=False)
    except Exception as exc: await update.message.reply_text(f"❌ Gagal membuat project.\n\n{exc}"); return
    waiting_new_project_name = False; set_selected_project(name); clear_conversation_id()
    await update.message.reply_text(f"✅ Project dibuat dan aktif.\n\n📁 {name}\n📂 {target}", reply_markup=main_keyboard())


async def model_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⚠️ Antigravity sedang bekerja."); return
    waiting = await update.message.reply_text("🔄 Mengambil daftar model...")
    models = await get_available_models()
    with suppress(Exception): await waiting.delete()
    if not models: await update.message.reply_text("❌ Gagal mengambil daftar model."); return
    selected = get_selected_model(); keyboard = [[InlineKeyboardButton(
        "✅ Otomatis (Default AGY)" if not selected else "Otomatis (Default AGY)", callback_data="model:DEFAULT"
    )]]
    for mid, name in models:
        keyboard.append([InlineKeyboardButton(f"✅ {name}" if selected == mid else name, callback_data=f"model:{mid}")])
    await update.message.reply_text("🧠 Pilih model:", reply_markup=InlineKeyboardMarkup(keyboard))


async def model_callback(update, context):
    if not await is_allowed(update): return
    query = update.callback_query
    if agy_lock.locked(): await query.answer("Antigravity sedang bekerja.", show_alert=True); return
    await query.answer(); selected = query.data.split(":", 1)[1]
    if selected == "DEFAULT": set_selected_model(None); await query.edit_message_text("✅ Model: Otomatis (Default AGY)"); return
    models = dict(await get_available_models())
    if selected not in models: await query.edit_message_text("❌ Model tidak ditemukan."); return
    set_selected_model(selected); await query.edit_message_text(f"✅ Model: {models[selected]}\nID: {selected}")


async def workmode_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⚠️ Tidak dapat mengubah mode saat AGY bekerja."); return
    current = get_work_mode()
    kb = [
        [InlineKeyboardButton("✅ 🔒 Read Only" if current == "readonly" else "🔒 Read Only", callback_data="workmode:readonly")],
        [InlineKeyboardButton("✅ ✏️ Write Mode" if current == "write" else "✏️ Write Mode", callback_data="workmode:write")],
    ]
    await update.message.reply_text(
        "🛡 Pilih Work Mode\n\nRead Only menggunakan AGY plan mode. Write Mode menggunakan accept-edits. "
        "Permission Antigravity tetap berlaku; bot tidak memakai --dangerously-skip-permissions.",
        reply_markup=InlineKeyboardMarkup(kb),
    )


async def workmode_callback(update, context):
    if not await is_allowed(update): return
    query = update.callback_query
    if agy_lock.locked(): await query.answer("Antigravity sedang bekerja.", show_alert=True); return
    mode = query.data.split(":", 1)[1]
    if not set_work_mode(mode): await query.answer("Mode tidak valid.", show_alert=True); return
    await query.answer(); await query.edit_message_text(f"✅ Work Mode: {get_work_mode_label()}")


async def usage_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⏳ Tunggu AGY selesai atau /stop."); return
    waiting = await update.message.reply_text("📊 Mengambil quota Antigravity...")
    success, output = await run_agy_admin("/usage")
    if not success: await waiting.edit_text(f"❌ Gagal mengambil usage.\n\n{output[:3500]}"); return
    try: await waiting.edit_text(format_usage_html(output), parse_mode="HTML")
    except Exception: await waiting.edit_text(f"📊 Antigravity Usage\n\n{output}")


async def credits_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⏳ Tunggu AGY selesai atau /stop."); return
    waiting = await update.message.reply_text("💳 Mengambil credits...")
    success, output = await run_agy_admin("/credits")
    if not success: await waiting.edit_text(f"❌ Gagal mengambil credits.\n\n{output[:3500]}"); return
    await waiting.edit_text(f"<b>💳 Antigravity Credits</b>\n\n<pre>{html.escape(output)}</pre>", parse_mode="HTML")


async def backup_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⚠️ Tunggu AGY selesai sebelum backup."); return
    project = get_selected_project()
    if not project: await update.message.reply_text("📁 Pilih project terlebih dahulu."); return
    status = await update.message.reply_text("💾 Membuat backup project...")
    try:
        path, count = await asyncio.to_thread(create_project_backup, project, "backup")
        size_mb = path.stat().st_size / 1024 / 1024
        await status.edit_text(f"✅ Backup berhasil.\n\n📦 {path.name}\n📄 {count} file\n💽 {size_mb:.2f} MB\n📂 {path.parent}")
    except Exception as exc:
        logger.exception("backup failed"); await status.edit_text(f"❌ Backup gagal.\n\n{exc}")


async def restore_command(update, context):
    if not await is_allowed(update): return
    if agy_lock.locked(): await update.message.reply_text("⚠️ Tunggu AGY selesai sebelum restore."); return
    project = get_selected_project()
    if not project: await update.message.reply_text("📁 Pilih project terlebih dahulu."); return
    backups = list_project_backups(project["id"])
    if not backups: await update.message.reply_text("♻️ Belum ada backup. Gunakan /backup."); return
    RESTORE_CALLBACKS.clear(); kb = []
    for backup in backups[:10]:
        token = secrets.token_hex(4); RESTORE_CALLBACKS[token] = str(backup)
        kb.append([InlineKeyboardButton(f"📦 {backup.name} ({backup.stat().st_size/1024/1024:.1f} MB)", callback_data=f"restorepick:{token}")])
    await update.message.reply_text("♻️ Pilih backup. Safety backup akan dibuat sebelum restore.", reply_markup=InlineKeyboardMarkup(kb))


async def restore_pick_callback(update, context):
    if not await is_allowed(update): return
    query = update.callback_query; token = query.data.split(":", 1)[1]; raw = RESTORE_CALLBACKS.get(token)
    if not raw: await query.answer("Backup kadaluarsa. Buka /restore lagi.", show_alert=True); return
    path = Path(raw)
    if not path.exists(): await query.answer("Backup tidak ditemukan.", show_alert=True); return
    await query.answer(); kb = [
        [InlineKeyboardButton("✅ YA, RESTORE", callback_data=f"restoreconfirm:{token}")],
        [InlineKeyboardButton("❌ Batal", callback_data="restorecancel")],
    ]
    await query.edit_message_text(f"⚠️ KONFIRMASI RESTORE\n\n📦 {path.name}\n\nIsi project aktif akan diganti. Safety backup dibuat dulu.", reply_markup=InlineKeyboardMarkup(kb))


async def restore_cancel_callback(update, context):
    if not await is_allowed(update): return
    await update.callback_query.answer(); await update.callback_query.edit_message_text("❎ Restore dibatalkan.")


async def restore_confirm_callback(update, context):
    if not await is_allowed(update): return
    query = update.callback_query
    if agy_lock.locked(): await query.answer("Antigravity sedang bekerja.", show_alert=True); return
    token = query.data.split(":", 1)[1]; raw = RESTORE_CALLBACKS.get(token); project = get_selected_project()
    if not raw or not project: await query.answer("Backup/project tidak tersedia.", show_alert=True); return
    backup = Path(raw); project_path = Path(project["path"]).resolve()
    if not backup.exists(): await query.answer("Backup tidak ditemukan.", show_alert=True); return
    await query.answer(); await query.edit_message_text("♻️ Restore sedang diproses...")
    try:
        await asyncio.to_thread(validate_zip_archive, backup, project_path)
        safety, count = await asyncio.to_thread(create_project_backup, project, "pre-restore")
        await asyncio.to_thread(clear_project_directory, project_path)
        try:
            await asyncio.to_thread(safe_extract_zip, backup, project_path)
        except Exception:
            logger.exception("restore failed, rollback")
            await asyncio.to_thread(clear_project_directory, project_path)
            await asyncio.to_thread(safe_extract_zip, safety, project_path)
            raise RuntimeError("Restore gagal; project dikembalikan dari safety backup.")
        clear_conversation_id()
        await query.edit_message_text(f"✅ Restore berhasil.\n\n📦 {backup.name}\n🛡 Safety: {safety.name} ({count} file)\n💬 Conversation direset.")
    except Exception as exc:
        logger.exception("restore failed"); await query.edit_message_text(f"❌ Restore gagal.\n\n{exc}")


async def gitstatus_command(update, context):
    if not await is_allowed(update): return
    project = get_selected_project()
    if not project: await update.message.reply_text("📁 Pilih project terlebih dahulu."); return
    path = project["path"]
    ok, inside, _ = await run_process("git", "-C", path, "rev-parse", "--is-inside-work-tree", timeout=20)
    if not ok or inside.strip() != "true": await update.message.reply_text("🌿 Project ini bukan repository Git."); return
    _, status, status_err = await run_process("git", "-C", path, "status", "--short", "--branch", timeout=30)
    _, log, _ = await run_process("git", "-C", path, "log", "-1", "--pretty=format:%h | %an | %ad | %s", "--date=short", timeout=30)
    _, diff, _ = await run_process("git", "-C", path, "diff", "--stat", timeout=30)
    text = f"🌿 Git Status\n\n📁 {project['name']}\n\n{status or status_err or 'Working tree clean.'}\n\nCommit terakhir:\n{log or '-'}\n\nDiff summary:\n{diff or 'Tidak ada perubahan tracked.'}"
    await send_long_plain_text(update.message, text, reply_markup=main_keyboard())


async def task_command(update, context):
    if not await is_allowed(update): return
    if not context.args:
        await update.message.reply_text("🧰 Gunakan: /task <instruksi>\n\nContoh: /task periksa bug login, perbaiki, test, lalu laporkan."); return
    await execute_agy_request(update, " ".join(context.args).strip(), task_mode=True)


async def history_command(update, context):
    if not await is_allowed(update): return
    pid = get_selected_project_id()
    if not pid: await update.message.reply_text("📁 Pilih project terlebih dahulu."); return
    cid = load_conversation_id()
    if cid: record_conversation(pid, cid, get_selected_model())
    items = get_project_history(pid)
    if not items: await update.message.reply_text("🕘 Belum ada history untuk project ini.", reply_markup=main_keyboard()); return
    kb = []
