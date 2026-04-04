# Spec 工作流约定

## 目录结构（统一使用 openspec）

```
openspec/
  config.yaml              # 项目级配置
  changes/                 # 活跃 spec（进行中）
    {feature-name}/
      .openspec.yaml       # spec 元数据
      proposal.md          # 背景、动机、影响范围
      design.md            # 技术设计
      tasks.md             # 实现任务列表
  archived/                # 已完成归档
    {feature-name}/        # 同上结构
    index.md               # 归档索引
```

## 文件说明

| 文件 | 对应 Kiro 概念 | 说明 |
|------|--------------|------|
| `proposal.md` | requirements.md | Why / What Changes / Impact |
| `design.md` | design.md | 技术设计、接口、数据模型、正确性属性 |
| `tasks.md` | tasks.md | 实现任务列表，`[x]` 表示完成 |
| `.openspec.yaml` | .config.kiro | spec 元数据（schema, created） |

## 归档前置条件

1. `tasks.md` 所有必需任务均已标记 `[x]`
2. 代码已通过测试
3. 已提交到 git

## 归档步骤

```powershell
$feature = "my-feature-name"
New-Item -ItemType Directory -Path "openspec/archived/$feature" -Force
Move-Item "openspec/changes/$feature/*" "openspec/archived/$feature/"
Remove-Item "openspec/changes/$feature" -Recurse -Force
```

然后在 `openspec/archived/index.md` 追加一行：

```
| feature-name | YYYY-MM-DD | 简述 | commit-hash |
```

## 归档索引格式

```markdown
| Feature | 归档日期 | 说明 | Commit |
|---------|---------|------|--------|
| borrow-uecli-improvements | 2026-04-04 | Python exec、异步命令、Commandlet | f949225 |
```

## Kiro 用户注意

Kiro 会在 `.kiro/specs/` 下生成 spec 文件，完成后需手动迁移到 `openspec/changes/`：
- `requirements.md` → `proposal.md`（内容可直接复用）
- `design.md` → `design.md`（直接复制）
- `tasks.md` → `tasks.md`（直接复制）
- `.config.kiro` → `.openspec.yaml`（只需 `schema: spec-driven` + `created: YYYY-MM-DD`）
