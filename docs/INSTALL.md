# Installation Checklist

1. Install and authenticate `agy` as the same OS user that will run the bot.
2. Confirm `agy models` and `agy -p "/usage"` succeed.
3. Create a Telegram bot with @BotFather.
4. Clone the repository.
5. Create `.venv` and install `requirements.txt`.
6. Copy `.env.example` to `.env`, set token and numeric user ID, then `chmod 600 .env`.
7. Create `PROJECT_ROOT`.
8. Review Antigravity permissions; headless `Ask` actions cannot be interactively approved.
9. Run `python bot.py` manually and verify `/start`, `/project`, a read-only prompt, `/usage`, and `/health`.
10. Stop the manual process and install the systemd user service with `scripts/install-systemd.sh`.
11. Enable linger if the bot must start at boot before login.
12. Reboot once and verify `/health` from Telegram.
