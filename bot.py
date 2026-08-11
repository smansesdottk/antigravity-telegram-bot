#!/usr/bin/env python3
"""Entry point for Antigravity Telegram Bot.

The public source is stored in ordered fragments under ``src_parts/``. They are
assembled in memory and executed as one Python module so users can still start
the project with the simple and documented command ``python bot.py``.
"""
from pathlib import Path

BASE = Path(__file__).resolve().parent / "src_parts"
parts = sorted(BASE.glob("part*.inc"))
if not parts:
    raise RuntimeError("Missing src_parts/part*.inc")

source = "".join(part.read_text(encoding="utf-8") for part in parts)
exec(compile(source, str(BASE / "assembled_bot.py"), "exec"), globals(), globals())
