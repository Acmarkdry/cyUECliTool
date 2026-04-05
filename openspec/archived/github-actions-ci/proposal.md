# Proposal: GitHub Actions CI

## Summary

为 UEEditorMCP 项目添加 GitHub Actions 持续集成工作流，自动化代码质量检查、测试验证和系统完整性检查。

## Problem

当前项目缺乏自动化 CI：
- PR 合并前没有自动代码质量检查
- 测试需要手动运行，容易遗漏
- Python ↔ C++ Schema 契约可能在变更后失去同步（~95 个 C++ Action 与 Python ActionDef 的映射）
- Skill 系统完整性可能被破坏（9 个 Skill 覆盖所有 Action）
- Workflow markdown 文件可能缺失
- 代码格式不一致影响协作

## Solution

添加 GitHub Actions 工作流，包含以下检查：

### 1. 代码格式检查 (Lint)
- 使用 `black --check` 验证 Python 代码格式
- 不包含 mypy（项目可能没有完整类型标注）

### 2. Python 单元测试 (Test)
- 使用 `pytest` 运行所有测试用例
- 覆盖 6 个测试模块：
  - `test_animgraph.py` - AnimGraph ActionDef 结构验证
  - `test_context.py` - 上下文记忆系统测试
  - `test_materials_analysis.py` - 材质分析 Action 测试
  - `test_materials_analysis_properties.py` - 材质属性测试
  - `test_skills.py` - Skill 系统完整性
  - `test_unreal_logs_server.py` - 日志服务器测试

### 3. Schema 契约检查 (Contract)
- 运行 `test_schema_contract.py` 专项验证：
  - Python tool → C++ action 映射一致性
  - C++ action → Python tool 暴露覆盖
  - Required 参数双向一致
  - Property 名称拼写一致

### 4. Skill 系统检查 (Skills)
- 验证所有 Skill 能正确加载
- 检查 Action ID 在 Registry 中存在
- 验证 Workflow markdown 文件存在
- 确保所有 Registry Action 被 Skill 覆盖

## Scope

### In Scope
- `.github/workflows/ci.yml` - 主 CI 工作流
- 支持 push/PR 触发（main/develop 分支）
- Python 3.10 环境配置
- pip 依赖缓存加速构建
- 并行运行 lint/test/contract 检查

### Out of Scope
- C++ 插件编译（需要 Unreal Engine 5.6+ 环境）
- 自托管 Runner 配置
- CD/发布流程
- Commandlet 模式集成测试

## Success Criteria

- [ ] PR 创建时自动触发 CI
- [ ] 代码格式不符合 black 规范时 CI 失败
- [ ] pytest 测试失败时 CI 失败
- [ ] Schema 契约不一致（ERROR 级别）时 CI 失败
- [ ] Skill 系统不完整时 CI 失败
- [ ] CI 运行时间 < 2 分钟
