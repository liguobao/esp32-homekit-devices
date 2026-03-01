# 本地技能

这个目录存放仓库内的本地 skill，主要用于让 AI 编码代理在修改本项目时遵守既有结构和约束。
英文版说明见 [README.en.md](README.en.md)。

当前包含：

- `vibe-coding`
  面向本仓库的开发约束、目录约定和文档约定

常见 skill 结构：

```text
skills/<skill-name>/
├── SKILL.md
├── agents/
│   └── openai.yaml
└── references/
    └── *.md
```

说明：

- `SKILL.md` 保持精简，写触发条件和简要流程
- 细节放在 `references/`，按需读取
- `agents/openai.yaml` 提供 UI 元数据
