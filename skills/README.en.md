# Skills

This directory stores repo-local skills used by AI coding agents when working in this repository.

Current item:

- `vibe-coding`
  Repository-specific development constraints, layout rules, and documentation conventions

Typical skill layout:

```text
skills/<skill-name>/
├── SKILL.md
├── agents/
│   └── openai.yaml
└── references/
    └── *.md
```

Notes:

- Keep `SKILL.md` lean and focused on triggers and workflow
- Put detailed repo knowledge in `references/` for progressive loading
- Use `agents/openai.yaml` for UI metadata
