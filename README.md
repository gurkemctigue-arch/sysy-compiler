# 曼巴奥特 SysY 编译器

这是内蒙古大学“曼巴奥特”队实现的 SysY 编译器。编译器使用 C++17 编写，可将 SysY 源程序编译为面向 RV64GC、LP64D ABI 的 RISC-V 汇编，并配套提供本地构建、交叉链接、QEMU 运行、结果比对和性能分析工具。

> 仓库整理日期：2026-10-12。核心源码已使用 MinGW-w64 GCC 16.1.0 执行 C++17 语法检查并通过；下表测试数字来自仓库中保存的最近一次完整测试记录，并非官方比赛成绩。

| 项目 | 内容 |
| --- | --- |
| 学校 | 内蒙古大学 |
| 队伍 | 曼巴奥特 |
| 队员 | 李东易 |
| 核心源码 | `compiler_main.cpp` |
| 源语言 | SysY |
| 实现语言 | C++17 |
| 目标架构 | RISC-V 64（RV64GC） |
| 目标 ABI | LP64D |
| 最近功能测试记录 | 140/140 AC（2026-06-01） |
| 最近性能探针记录 | 8/8 编译、链接并运行成功（2026-06-05） |
| 最近语法检查 | C++17 `-fsyntax-only` 通过（2026-10-12） |

## 功能概览

### 前端

- 手写词法分析器，支持标识符、关键字、注释、字符串、整数和单精度浮点字面量；
- 递归下降语法分析器，按 SysY 运算优先级构造抽象语法树；
- 支持 `int`、`float`、`void`、常量、数组、多维初始化和函数；
- 支持 `if`/`else`、`while`、`break`、`continue`、`return` 和短路逻辑；
- 独立语义分析，检查作用域、重定义、类型转换、数组索引、函数参数和返回类型；
- 支持 SysY 输入输出、数组输入输出、计时函数和 `putf`。

### 中间表示

- 稳定的 `SymbolId`、`FunctionId`、`ValueId`、`BlockId` 和 `InstructionId`；
- 类型化 SSA IR 和显式控制流图；
- 完整 use-def 链；
- sealed-block SSA 构造和 trivial-phi 消除；
- MemorySSA，包括 LiveOnEntry、MemoryUse、MemoryDef 和 MemoryPhi；
- IR 类型、CFG、use-def、支配关系和 MemorySSA 一致性验证。

### RISC-V 后端

- 生成 RV64GC GNU 汇编；
- 支持 32 位整数、单精度浮点、数组和函数调用；
- 遵循整数参数寄存器 `a0-a7`、浮点参数寄存器 `fa0-fa7` 和 16 字节栈对齐约定；
- 自动布局全局 `.data`/`.bss`、局部栈帧和保存寄存器；
- 仅在需要时保存 `ra`，并在可行时省略帧指针；
- 支持数组实参按 64 位基地址传递。

## 编译架构

```text
SysY 源文件
    |
    v
Lexer -> Token -> Parser -> AST
                           |
                           v
                   SemanticAnalyzer
                           |
                           v
            Typed SSA / CFG / MemorySSA
                           |
                           v
                      IRVerifier
                           |
                           v
                  AST RISC-V CodeGen
                           |
                           v
                    RISC-V 汇编文件
```

当前版本会构建并验证类型化 SSA/MemorySSA，但最终汇编仍由 AST 直达式 `CodeGen` 生成。IR 是强制执行的内部验证阶段，尚未作为最终后端输入。详细设计见[编译器设计文档](./编译器设计文档.md)。

## 主要优化

- 编译期常量求值、常量传播和全局初始化展平；
- RISC-V 立即数选择和常量缓存；
- 乘以 2 的幂转移位；
- 有符号常量除法的移位或 magic number 强度削弱；
- 常量余数和 2 的幂余数判零优化；
- 高频局部整数、参数、全局基址和常量的保存寄存器驻留；
- 加减表达式链扁平化；
- 表达式内重复标量加载复用；
- 目标寄存器感知的整数表达式生成和原地自更新；
- 小常量次数循环完全展开；
- 常量或运行期计数循环的二路、四路部分展开；
- 循环不变量加载缓存；
- 循环数组地址递增和线性下标强度削弱；
- 成本约束下的直接自尾调用消除；
- 直线基本块移动传播及冗余汇编删除。

所有模式优化均设置安全条件。遇到函数副作用、潜在别名写、不稳定循环边界或无法证明的表达式时，编译器回退到通用代码生成路径。

## 参赛经验分享

备赛初期，我先围绕功能测试补齐词法、语法、语义分析和代码生成，再用自动化脚本逐例比对输出。进入优化阶段后，我重点观察矩阵乘、卷积、CRC 等循环密集程序的汇编，从重复访存、地址计算和循环分支入手，逐步加入循环展开、地址强度削弱和热点寄存器驻留。比赛让我体会到，编译器优化不能只追求激进，正确性、可验证性和失败时回退同样重要。建议学弟学妹先打通稳定的端到端流程，保留测试基线，每次只解决一个问题，并从 AST、IR 和汇编三个层次定位原因。持续记录和复盘，往往比一次写出复杂优化更可靠。

## 环境要求

### 编译器构建

- Windows PowerShell；
- MinGW-w64 `g++`；
- 支持 C++17 的标准库。

### RISC-V 端到端测试

- WSL 发行版，默认名称为 `Ubuntu`；
- `riscv64-linux-gnu-gcc`；
- `qemu-riscv64`；
- 支持 RV64GC、LP64D 的运行环境。

若只需生成 `.s` 汇编文件，不需要安装 WSL、RISC-V 交叉工具链或 QEMU。

## 构建方法

在项目根目录运行：

```powershell
.\local_tools\build_compiler.ps1 `
    -Source .\compiler_main.cpp `
    -Output .\compiler.exe
```

等价的直接构建命令为：

```powershell
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ `
    .\compiler_main.cpp -o .\compiler.exe
```

构建脚本默认将临时文件放在纯 ASCII 路径中，以规避部分 MinGW 工具对中文临时路径处理不稳定的问题。可通过 `-TempDir` 修改该目录：

```powershell
.\local_tools\build_compiler.ps1 `
    -Source .\compiler_main.cpp `
    -Output .\compiler.exe `
    -TempDir D:\work\SysYToolchain\tmp
```

## 使用方法

```powershell
.\compiler.exe -S -o <输出汇编文件> <输入SysY文件> [-O1]
```

示例：

```powershell
.\compiler.exe -S -o .\output.s .\input.sy -O1
```

成功时编译器返回 0；词法、语法、语义、IR 验证或文件错误会写入标准错误并返回 1：

```text
compiler error: line <行号>: <错误信息>
```

编译器接受 `-O0` 和 `-O1`，但当前两者进入相同的后端与优化路径，尚未实现按优化级别切换 Pass 集合。

## 阶段耗时跟踪

设置 `SYSYC_TRACE_STAGES` 可输出各编译阶段耗时：

```powershell
$env:SYSYC_TRACE_STAGES = '1'
.\compiler.exe -S -o .\output.s .\input.sy -O1
Remove-Item Env:SYSYC_TRACE_STAGES
```

输出示例：

```text
[stage] parse: 0.001234s
[stage] semantic: 0.000456s
[stage] ir-build: 0.000789s
[stage] ir-verify: 0.000321s
[stage] legacy-codegen: 0.001012s
```

## 测试

### 功能测试

完整功能测试语料保存在本地开发环境中，未纳入本仓库。准备符合 SysY 测试目录约定的 `.sy`、可选 `.in` 和期望 `.out` 文件后，可运行：

```powershell
.\local_tools\run_riscv_cases.ps1 `
    -Compiler .\compiler.exe `
    -CasesRoot .\path\to\sysy_cases `
    -OutRoot .\local_riscv_out_all `
    -O1
```

脚本会逐例完成以下操作：

1. 将 `.sy` 编译为 `.s`；
2. 在 WSL 中使用 `riscv64-linux-gnu-gcc` 与 `local_tools/sysy_runtime.c` 静态链接；
3. 使用 `qemu-riscv64` 运行目标程序；
4. 合并标准输出和返回码；
5. 与对应 `.out` 文件比较；
6. 将结果写入输出目录的 `summary.csv`。

本地保存的最近一次功能测试汇总位于 `local_riscv_out_all/summary.csv`，记录为 140 个用例全部 AC；该生成结果目录不纳入 Git 仓库。

### 性能探针

本地性能用例包含：

- 矩阵乘风格计算；
- 卷积风格计算；
- 矩阵转置；
- 多矩阵运算；
- 背包动态规划；
- CRC；
- 密码学风格整数计算；
- 浮点 DCT。

性能语料和生成结果未纳入本仓库。准备对应文件后，可使用相同脚本指定本地性能目录：

```powershell
.\local_tools\run_riscv_cases.ps1 `
    -Compiler .\compiler.exe `
    -CasesRoot .\path\to\perf_cases `
    -OutRoot .\local_riscv_out_perf `
    -O1
```

本地保存的最终检查汇总位于 `local_riscv_out_perf_addr_store_constcache_finalcheck_20260605/summary.csv`，8 个探针均为 AC；该生成结果目录不纳入 Git 仓库。

## 项目结构

```text
.
|-- compiler_main.cpp                  # 编译器核心源码
|-- README.md                          # 项目入口文档
|-- 编译器设计文档.md                  # 完整分析、设计与实现文档
|-- PERFORMANCE_PROFILING.md           # 编译器自身性能分析说明
|-- local_tools/
|   |-- build_compiler.ps1             # Windows 构建脚本
|   |-- run_riscv_cases.ps1            # 交叉链接、QEMU 和结果比对
|   |-- generate_expected.ps1          # 期望结果生成辅助脚本
|   |-- profile_compiler.ps1           # WSL perf 分析脚本
|   |-- profile_compiler_gprof.ps1     # Windows gprof 分析脚本
|   |-- generate_design_docx.py        # 设计文档 Word 生成脚本
|   `-- sysy_runtime.c                  # SysY 测试运行库
`-- local_riscv_out_*/                 # 本地生成，不纳入仓库
```

测试输出目录包含生成的汇编、RISC-V 可执行文件、标准输出、实际结果和 CSV 汇总，通常不属于核心源码。

## 已知限制

- SSA/MemorySSA 尚未直接驱动最终代码生成；
- `-O0` 和 `-O1` 尚未对应不同优化集合；
- 后端使用启发式热点寄存器驻留，不是完整的全局寄存器分配器；
- MemorySSA 与 AST 后端的保守内存优化仍是两套逻辑；
- `putf` 会展开为多个基础输出函数调用；
- 主要实现集中在单个大型 C++ 文件中；
- 当前错误处理以报告首个错误为主，不执行多错误恢复。

## AI 辅助开发披露

本项目开发过程中使用了 OpenAI Codex（基础模型：GPT-5）。参赛者为李东易，学校为内蒙古大学，队伍为曼巴奥特。

根据 `compiler_main.cpp` 文件头的 2026-07-24 原始记录，Codex 参与生成或辅助生成的源码范围包括：语义分析、稳定符号/函数 ID、类型化 SSA IR、CFG、use-def 链、sealed-block SSA、MemorySSA、IR 验证、集成诊断、数组参数 ABI 兼容、循环不变量加载缓存、基本块复制传播、冗余指令删除、目标寄存器感知的整数表达式生成、常量余数原地更新、常量及运行期次数部分循环展开、循环指针强度削弱、整数累加更新、自尾调用消除和相关回归修复。

人工参与和修改情况：参赛者选择了系统架构、性能目标和实现范围；源码文件头记录表明，截至该记录形成时，没有对所述 AI 生成部分进行额外人工编辑。本文档和[完整设计文档](./编译器设计文档.md)也由 Codex 根据源码和测试记录生成初稿，参赛者提供了身份信息和文档范围。后续如有人工修改，应在源码披露或设计文档的“人工决策与修改情况”中按实际情况补充，不能删除既有披露。

2026-10-12，OpenAI Codex 根据参赛者的公众号经验分享需求更新了本 README 的项目状态和“参赛经验分享”段落，并辅助整理 Git 仓库忽略规则；本次未修改 `compiler_main.cpp`。

生成结果通过以下方式验证：

- 140 个本地功能用例全部 AC；
- 8 个 RISC-V 性能探针全部编译、链接并运行成功；
- 类型化 SSA 和 MemorySSA 在编译流程中经过 `IRVerifier`；
- 2026-10-12 使用 MinGW-w64 GCC 16.1.0 按 C++17 `-fsyntax-only` 检查核心源码，结果通过。

更完整的 AI 使用范围、人工修改状态和可追溯性措施见[编译器设计文档第 12 章](./编译器设计文档.md#12-人工智能辅助开发合规说明)。

## 文档

- [编译器分析、设计与实现文档（Markdown）](./编译器设计文档.md)
- [编译器性能分析说明](./PERFORMANCE_PROFILING.md)
