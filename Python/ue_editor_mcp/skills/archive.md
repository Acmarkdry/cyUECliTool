# Spec 归档工作流指南

本文档描述如何将已完成的 Kiro spec 归档，保持 `.kiro/specs/` 目录整洁。

---

## 归档约定

- 活跃 spec 位于：`.kiro/specs/{feature-name}/`
- 已归档 spec 位于：`.kiro/specs/_archived/{feature-name}/`
- 归档索引文件：`.kiro/specs/_archived/index.md`

---

## 归档流程

### 前置条件

归档前确认 spec 已满足以下条件：

1. `tasks.md` 中所有必需任务（非 `*` 标注）均已标记 `[x]`
2. 相关代码已通过测试
3. 已提交到 git（`git status` 无未提交变更）

### 执行步骤

```
1. 在 .kiro/specs/_archived/ 下创建同名目录

2. 将以下文件移动到归档目录：
   - requirements.md
   - design.md
   - tasks.md
   - .config.kiro

3. 在 .kiro/specs/_archived/index.md 追加归档记录：
   | feature-name | 完成日期 | 简述 | 关联 commit |

4. 删除原 .kiro/specs/{feature-name}/ 目录
```

---

## 归档索引格式

`.kiro/specs/_archived/index.md` 维护所有已归档 spec 的索引：

```markdown
# Archived Specs

| Feature | 归档日期 | 说明 | Commit |
|---------|---------|------|--------|
| animation-graph-read | 2026-04-02 | AnimGraph MCP 18 个 Action | c4fda9e |
```

---

## 快速归档命令（PowerShell）

```powershell
# 归档指定 spec（在项目根目录执行）
$feature = "my-feature-name"
$src = ".kiro/specs/$feature"
$dst = ".kiro/specs/_archived/$feature"

New-Item -ItemType Directory -Path $dst -Force
Move-Item "$src/*" $dst
Remove-Item $src -Recurse -Force

Write-Host "Archived: $feature"
```

---

## 注意事项

- 归档不删除实现代码，只移动 spec 文档
- 归档后 spec 仍可通过 git 历史追溯
- 如需重新激活某个 spec，将其从 `_archived/` 移回 `specs/` 即可
