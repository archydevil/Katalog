# Shared agent entry point

These repository instructions apply to Copilot, Codex and other agents that
read `AGENTS.md`. Claude Code has the same entry point linked from `CLAUDE.md`.

Before resuming work:

1. Read [PROJECT_MEMORY.md](PROJECT_MEMORY.md) for the cross-agent handoff.
2. Read [CLAUDE.md](CLAUDE.md) for repository development rules, including the
   fork exceptions. Follow the applicable filesystem permissions of the
   current environment; do not assume the historical machine paths apply.
3. Check the actual branch, worktree and relevant specification before acting.

The shared memory records context, not requirements or permission to change
code. Only the applicable approved `Spec*.md` requirements authorise feature
work. Use the specifications gate before implementation; if the named agent
is unavailable, report that limitation rather than claiming its approval.

Keep cross-agent handoff updates in `PROJECT_MEMORY.md`, not separate copies
for each assistant. Distinguish decisions, proposals, implementation and
verification. Do not store credentials, personal data or raw conversation logs.
Update only the sections affected by the work performed, and preserve unrelated
worktree changes.
