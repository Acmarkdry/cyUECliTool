# Design: GitHub Actions CI

## Overview

单一 CI 工作流文件，包含并行的 lint/test job。

## Architecture

```
.github/workflows/ci.yml
├── lint job (black --check)
└── test job (pytest)
    ├── test_animgraph.py
    ├── test_context.py
    ├── test_materials_*.py
    ├── test_skills.py        ← Skill 系统完整性
    ├── test_schema_contract.py  ← Python↔C++ 契约
    └── test_unreal_logs_server.py
```

## Workflow 配置

```yaml
触发条件:
  - push: main, develop
  - pull_request: main, develop

Jobs:
  lint:
    - Python 3.10
    - pip install black
    - black --check Python/ tests/

  test:
    - Python 3.10
    - pip install -r Python/requirements-dev.txt
    - cd Python && pip install -e .
    - pytest tests/ -v
```

## 本地验证

使用 [act](https://github.com/nektos/act) 在本地测试：

```bash
# 安装 act (Windows)
choco install act-cli
# 或 scoop install act

# 运行 CI
act -j lint    # 仅 lint
act -j test    # 仅 test
act            # 全部
```

## 决策记录

| 决策 | 理由 |
|------|------|
| 不包含 mypy | 项目可能没有完整类型标注 |
| 不编译 C++ | 标准 Runner 无 UE 环境 |
| pytest 包含 schema_contract | 已集成在 pytest 测试中 |
| 使用 pip cache | 加速重复构建 |
