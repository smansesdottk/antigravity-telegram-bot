    for idx, item in enumerate(items[:10], 1):
        item_cid = item.get("conversation_id", "")
        kb.append([InlineKeyboardButton(f"{'✅' if item_cid == cid else '💬'} #{idx} · {local_datetime_string(item.get('last_used'))}", callback_data=f"history:{item_cid}")])
    await update.message.reply_text(f"🕘 History — {get_selected_project_name()}", reply_markup=InlineKeyboardMarkup(kb))


async def history_callback(update, context):
    if not await is_allowed(update): return
    query = update.callback_query; pid = get_selected_project_id(); cid = query.data.split(":", 1)[1]
    if not pid or not history_contains(pid, cid): await query.answer("Conversation tidak ditemukan.", show_alert=True); return
    await query.answer(); save_conversation_id(cid); record_conversation(pid, cid, get_selected_model())
    await query.edit_message_text(f"✅ Conversation aktif:\n\n💬 {cid}\n📁 {get_selected_project_name()}")


async def files_command(update, context):
    if not await is_allowed(update): return
    project = get_selected_project()
    if not project: await update.message.reply_text("📁 Pilih project terlebih dahulu."); return
    try: items = sorted(Path(project["path"]).iterdir(), key=lambda x: (not x.is_dir(), x.name.lower()))
    except Exception as exc: await update.message.reply_text(f"❌ Gagal membaca project.\n\n{exc}"); return
    lines = [f"📂 {project['name']}", ""]
    for item in items[:60]: lines.append(f"{'🔒' if item.name.startswith('.') else '📁' if item.is_dir() else '📄'} {item.name}")
    if len(items) > 60: lines.append(f"\n… +{len(items)-60} item lainnya")
    lines += ["", "Download: /getfile path/file.ext"]
    await send_long_plain_text(update.message, "\n".join(lines), reply_markup=main_keyboard())


async def getfile_command(update, context):
    if not await is_allowed(update): return
    if not context.args: await update.message.reply_text("📥 Gunakan: /getfile path/file.ext"); return
    rel = " ".join(context.args).strip(); target = resolve_project_file(rel)
    if not target or not target.exists() or not target.is_file(): await update.message.reply_text("❌ File tidak ditemukan atau path tidak diizinkan."); return
    try:
        with target.open("rb") as handle:
            await update.message.reply_document(document=handle, filename=target.name, caption=f"📥 {rel}\n📁 {get_selected_project_name()}")
    except Exception as exc: await update.message.reply_text(f"❌ Gagal mengirim file.\n\n{exc}")


async def document_handler(update, context):
    if not await is_allowed(update): return
    project = get_selected_project(); document = update.message.document
    if not project: await update.message.reply_text("📁 Pilih project sebelum upload."); return
    root = Path(project["path"]).resolve(); target = unique_target(root, document.file_name or f"telegram_file_{int(time.time())}")
    if not is_inside_path(target, root): await update.message.reply_text("⛔ Nama file tidak diizinkan."); return
    status = await update.message.reply_text("📤 Menyimpan file ke project...")
    try:
        tg_file = await context.bot.get_file(document.file_id); await tg_file.download_to_drive(custom_path=str(target))
        await status.edit_text(f"✅ File disimpan.\n\n📁 {project['name']}\n📄 {target.name}")
    except Exception as exc: await status.edit_text(f"❌ Upload gagal.\n\n{exc}")


async def health_command(update, context):
    if not await is_allowed(update): return
    wait = await update.message.reply_text("🩺 Memeriksa sistem...")
    lines = ["🩺 System Health", "", "✅ Telegram Bot: aktif"]
    lines.append("✅ AGY binary: ditemukan" if Path(AGY_PATH).exists() else "❌ AGY binary: tidak ditemukan")
    ok, out, err = await run_process(AGY_PATH, "--version", timeout=20)
    lines.append(f"{'✅' if ok else '❌'} AGY: {out or err or 'tidak terdeteksi'}")
    lines.append(f"{'✅' if PROJECT_ROOT.exists() else '❌'} Project root: {PROJECT_ROOT}")
    lines.append(f"📁 Project aktif: {get_selected_project_name()}")
    lines.append(f"🛡 Work Mode: {get_work_mode_label()}")
    lines.append(f"💬 Conversation: {'aktif' if load_conversation_id() else 'belum dibuat'}")
    if not agy_lock.locked():
        success, quota = await run_agy_admin("/usage", timeout_seconds=60)
        lines.append("✅ Login/quota Antigravity: OK" if success else f"❌ Login/quota: {quota[:200]}")
    else: lines.append("⏳ AGY sedang bekerja; quota check dilewati")
    lines.append(f"🌐 AGY GODEBUG: {agy_env.get('GODEBUG','-')}")
    await wait.edit_text("\n".join(lines))


async def logs_command(update, context):
    if not await is_allowed(update): return
    if not LOG_FILE.exists(): await update.message.reply_text("📜 Belum ada log."); return
    lines = LOG_FILE.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]
    await send_long_plain_text(update.message, "📜 Log terakhir\n\n" + "\n".join(lines), reply_markup=main_keyboard())


BUTTON_COMMANDS = {
    "📁 Project": project_command, "➕ New Project": newproject_command, "🧠 Model": model_command,
    "📊 Usage": usage_command, "🕘 History": history_command, "📂 Files": files_command,
    "💾 Backup": backup_command, "♻️ Restore": restore_command, "🌿 Git Status": gitstatus_command,
    "🛡 Work Mode": workmode_command, "💳 Credits": credits_command, "ℹ️ Status": status_command,
    "🩺 Health": health_command, "📜 Logs": logs_command, "🆕 New": new_command,
    "🔴 Stop": stop_command, "💬 Session": session_command, "❓ Help": help_command,
}


async def handle_message(update, context):
    global waiting_new_project_name
    if not await is_allowed(update) or not update.message or not update.message.text: return
    text = update.message.text.strip()
    if waiting_new_project_name: await handle_new_project_name(update); return
    if text in BUTTON_COMMANDS: await BUTTON_COMMANDS[text](update, context); return
    await execute_agy_request(update, text)


async def error_handler(update, context):
    logger.exception("Unhandled Telegram error", exc_info=context.error)
    try:
        if update and update.effective_user and update.effective_user.id == ALLOWED_USER_ID and update.effective_message:
            await update.effective_message.reply_text("⚠️ Error internal bot. Gunakan /logs.")
    except Exception: pass


async def post_init(application):
    await application.bot.set_my_commands([
        BotCommand("project", "Pilih project"), BotCommand("newproject", "Buat project baru"),
        BotCommand("model", "Pilih model"), BotCommand("workmode", "Read Only / Write Mode"),
        BotCommand("new", "Conversation baru"), BotCommand("history", "Riwayat conversation"),
        BotCommand("task", "Jalankan Agent Task"), BotCommand("usage", "Lihat quota Antigravity"),
        BotCommand("credits", "Lihat model credits"), BotCommand("backup", "Backup project aktif"),
        BotCommand("restore", "Restore project"), BotCommand("gitstatus", "Lihat status Git"),
        BotCommand("files", "Lihat file project"), BotCommand("getfile", "Download file project"),
        BotCommand("health", "Periksa kesehatan bot"), BotCommand("logs", "Lihat log terakhir"),
        BotCommand("status", "Lihat status bot"), BotCommand("session", "Lihat Conversation ID"),
        BotCommand("stop", "Hentikan Antigravity"), BotCommand("help", "Bantuan"),
    ])


def validate_startup():
    missing = []
    if not TOKEN: missing.append("TELEGRAM_BOT_TOKEN")
    if not ALLOWED_USER_ID: missing.append("TELEGRAM_USER_ID")
    if missing: raise RuntimeError("Konfigurasi .env belum lengkap: " + ", ".join(missing))
    if not Path(AGY_PATH).exists():
        raise RuntimeError(f"AGY tidak ditemukan di {AGY_PATH}. Atur AGY_PATH di .env.")


def main():
    validate_startup(); import_current_session_to_history()
    logger.info("Bot starting project=%s model=%s work_mode=%s", get_selected_project_name(), get_selected_model() or "default", get_work_mode())
    app = ApplicationBuilder().token(TOKEN).concurrent_updates(4).post_init(post_init).build()
    for name, fn in [
        ("start", start_command), ("help", help_command), ("status", status_command), ("project", project_command),
        ("newproject", newproject_command), ("cancel", cancel_command), ("model", model_command),
        ("workmode", workmode_command), ("new", new_command), ("history", history_command), ("task", task_command),
        ("usage", usage_command), ("credits", credits_command), ("backup", backup_command), ("restore", restore_command),
        ("gitstatus", gitstatus_command), ("files", files_command), ("getfile", getfile_command),
        ("health", health_command), ("logs", logs_command), ("session", session_command), ("stop", stop_command),
    ]:
        app.add_handler(CommandHandler(name, fn))
    app.add_handler(CallbackQueryHandler(model_callback, pattern=r"^model:"))
    app.add_handler(CallbackQueryHandler(project_callback, pattern=r"^project:"))
    app.add_handler(CallbackQueryHandler(workmode_callback, pattern=r"^workmode:"))
    app.add_handler(CallbackQueryHandler(history_callback, pattern=r"^history:"))
    app.add_handler(CallbackQueryHandler(restore_pick_callback, pattern=r"^restorepick:"))
    app.add_handler(CallbackQueryHandler(restore_confirm_callback, pattern=r"^restoreconfirm:"))
    app.add_handler(CallbackQueryHandler(restore_cancel_callback, pattern=r"^restorecancel$"))
    app.add_handler(MessageHandler(filters.Document.ALL, document_handler))
    app.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, handle_message))
    app.add_error_handler(error_handler)
    print("Antigravity Telegram Bot aktif...")
    print("Project root:", PROJECT_ROOT)
    print("Data dir:", BOT_DATA_DIR)
    print("Project aktif:", get_selected_project_name())
    print("Model:", get_selected_model() or "Otomatis (Default AGY)")
    print("Work mode:", get_work_mode_label())
    app.run_polling()


if __name__ == "__main__":
    main()
