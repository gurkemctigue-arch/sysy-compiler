# VS Code 编译与性能分析

## 日常构建和调试

- `Ctrl+Shift+B`：使用 MinGW-w64、C++17、`-O0 -g3` 构建 `build/compiler-debug.exe`。
- `F5`：构建并用 GDB 调试编译器，默认输入为 `ascii_cases/functional/86_long_code2.sy`。
- 命令面板 `Tasks: Run Task` -> `C++: 构建 Release 编译器`：生成带符号和栈帧的优化版本。

这套 MinGW 的汇编器不能正确处理含中文的临时目录，因此 Windows 构建任务会自动使用 `D:\work\SysYToolchain\tmp`；源码和产物仍保留在当前项目中。

系统 PATH 中还存在 Anaconda 自带的另一套 MinGW DLL。构建任务使用静态 C++ 运行库，避免程序误加载不兼容 DLL；无需修改你的全局 PATH。

要调试其他用例，修改 `.vscode/launch.json` 中 `args` 的 `.sy` 路径。分析编译器自身性能时应使用 Release/Profile 构建；Debug 构建的热点通常没有参考价值。

## 火焰图和热点函数

运行命令面板 `Tasks: Run Task` -> `性能分析: 生成火焰图和热点报告`。任务会在 WSL 中：

1. 以 `-O2 -g -fno-omit-frame-pointer` 构建 Linux 版编译器。
2. 用 `perf` 重复编译较大的 SysY 用例并采样调用栈。
3. 生成并在 VS Code 中打开 `build/profile/flamegraph.svg` 和热点报告。

主要产物：

- `flamegraph.svg`：方块越宽，CPU 占用越多；纵向表示调用关系。可点击方块放大，`Ctrl+F` 搜索函数。
- `hotspots-self.txt`：函数自身消耗的 CPU 时间，适合找函数内部的慢操作。
- `hotspots-inclusive.txt`：函数及其调用的所有子函数总耗时，适合找耗时阶段和调用路径。
- `perf.data`：原始采样，可继续使用 `perf report -i perf.data` 分析。

默认会重复编译 200 次，以获得稳定的用户态 CPU 采样。分析更复杂或更短的用例时，可运行 `性能分析: 自定义用例生成火焰图` 调整重复次数。

## Windows 备用报告

运行 `性能分析: Windows gprof 热点报告`，结果写入 `build/gprof/hotspots-gprof.txt`。该任务会关闭 Windows ASLR，确保 MinGW 能把采样地址映射回函数。`gprof` 是插桩分析，调用次数很有价值，但计时粒度只有 10 ms，对这个短时程序的耗时排名和内联函数观察能力弱于 `perf`，因此以 WSL 火焰图为主。

## 已配置工具

- Windows：MinGW-w64 `g++`、`gdb`、`gprof`。
- VS Code：C/C++ 和 WSL 扩展。
- Ubuntu WSL：`g++`、`perf 6.8.12`、`/opt/FlameGraph`。
