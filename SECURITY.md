# Security Policy

## Reporting

Please do not open a public issue containing Telegram bot tokens, OAuth credentials, private server paths, conversation logs, or other secrets. Revoke any credential immediately if it was exposed.

## Deployment guidance

- Keep `.env` out of Git and set file mode `600` on Linux.
- Use one Telegram numeric user ID allowlist.
- Prefer Read Only mode unless edits are required.
- Configure Antigravity's fine-grained permissions conservatively.
- For headless `agy --print`, explicitly allow only the project-root file access you need, for example `read_file(/home/YOUR_USER/proyekku)` and, only when edits are required, `write_file(/home/YOUR_USER/proyekku)`.
- Keep `trustedWorkspaces` scoped to the intended project root instead of the entire home directory.
- File permissions and `command(...)` permissions are separate; restrict both.
- Avoid wildcard permissions such as `read_file(*)`, `write_file(*)`, or `command(*)` unless unrestricted access is intentional.
- Never add `--dangerously-skip-permissions` to an unattended Telegram deployment.
- `PROJECT_ROOT` is an application boundary, not an OS sandbox. For stronger containment use a dedicated OS user and Antigravity sandbox/permission controls.
