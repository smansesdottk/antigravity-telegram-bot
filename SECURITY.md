# Security Policy

## Reporting

Please do not open a public issue containing Telegram bot tokens, OAuth credentials, private server paths, conversation logs, or other secrets. Revoke any credential immediately if it was exposed.

## Deployment guidance

- Keep `.env` out of Git and set file mode `600` on Linux.
- Use one Telegram numeric user ID allowlist.
- Prefer Read Only mode unless edits are required.
- Configure Antigravity's fine-grained permissions conservatively.
- Never add `--dangerously-skip-permissions` to an unattended Telegram deployment.
- `PROJECT_ROOT` is an application boundary, not an OS sandbox. For stronger containment use a dedicated OS user and Antigravity sandbox/permission controls.
