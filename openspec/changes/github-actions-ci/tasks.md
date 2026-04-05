# Tasks: GitHub Actions CI

## Task 1: 创建 CI 工作流文件

**文件**: `.github/workflows/ci.yml`

**内容**:
- 触发条件: push/PR to main, develop
- lint job: black --check
- test job: pytest -v
- pip 缓存

**验收标准**:
- [ ] 文件语法正确
- [ ] push 到 GitHub 后 Actions 页面显示工作流
- [ ] lint 和 test job 并行运行
