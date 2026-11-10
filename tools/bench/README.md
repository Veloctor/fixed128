# 性能测试怎么做才可信

## 运行

性能数字以 Clang 的代码生成为准：Windows 上用 `clang-cl-release` 预设（Linux/macOS 上用 `release`，默认编译器即可，或 `-DCMAKE_CXX_COMPILER=clang++`）。MSVC 的数字只用来确认它没有严重落后。

```powershell
cmake --preset clang-cl-release; cmake --build --preset clang-cl-release
./build/clang-cl-release/benchmarks/fixed128_bench --json before.json        # 默认测 L1/L2/L3 三档
./build/clang-cl-release/benchmarks/fixed128_bench --sizes l1,l2,l3,mem      # 加上主存档（约 400 MiB 内存）
./build/clang-cl-release/benchmarks/fixed128_bench --filter scenario         # 只跑名字或标题包含该文本的项
./tools/bench/Compare-Bench.ps1 before.json after.json              # 对比两次结果
```

测试项：

- **吞吐**：独立输入，按工作集大小分档（L1 8 KiB、L2 256 KiB、L3 8 MiB、主存 128 MiB）。`experimental/candidates` 注册的每个候选都会出现，另有基线（标量硬件转换、GCC/Clang 上编译器自带的 `__int128` 转换），用 `-march=native` 构建时（CMake 选项 `FIXED128_BENCH_NATIVE`）在支持 AVX-512DQ/VL 的 CPU 上还有原生指令作为上限。
- **延迟**：double → fixed128 → double 的依赖链，每次结果作为下一次的输入。
- **场景**：相机相对坐标（坐标相减再转 double）和成对引力（N 体），各自与"位置直接用 double"的版本对比，回答"用 fixed128 存位置，实际多花多少"。

## 让数字可信

- **只看 Release 构建**，并且机器空闲。
- **固定 CPU 频率**（关闭睿频，或在 BIOS / 电源计划里锁频），否则 ns 随温度和负载漂移。锁频后用 `--ghz <频率>` 把 ns 换算成周期，不同频率的机器之间才能比较。Linux 上 nanobench 会直接读硬件计数器，表格里自带周期数和指令数。
- **看 err%**：nanobench 给出每项的离散度。超过几个百分点的结果不可信，重跑或排查干扰。
- **对比时环境必须一致**：`--json` 文件里记录了 CPU、编译器、编译选项和提交号，`Compare-Bench.ps1` 发现环境不同时会警告。
- **云端 CI 的计时没有参考价值**：CI 只保证 benchmark 能编译、能跑通，性能对比在本机做。
- **小心测试代码本身的伪影**：例如在计时循环里逐个 lane 拼出结构体再做向量加载，会测到"存储转发失败"（约十几个周期），而不是转换本身。输入应在计时循环外准备好。

## 静态分析（uiCA / llvm-mca）

单条指令的延迟和吞吐可以用 `tools/uops/` 查；整段代码的瓶颈（关键路径、端口争用）要对编译后的循环做静态分析：

1. 构建 `fixed128_kernels` 目标：`cmake --build build/clang-cl-release --target fixed128_kernels`。
   - clang-cl 与 MSVC：汇编清单写到 `build/<预设>/benchmarks/kernels.asm`。
   - GCC/Clang：`.s` 文件在 `build/release/benchmarks/CMakeFiles/fixed128_kernels.dir/` 下。
2. 从清单里找到 `loop_fixed128_to_double` / `loop_double_to_fixed128` 的循环体（从循环标签到回跳指令），粘贴到 [uiCA](https://uica.uops.info)（选微架构，如 ICL、ADL-P），或用 `llvm-mca -mcpu=icelake-client` 分析。
3. uiCA 给出的每次迭代周期数除以 4，就是每个值的理论吞吐，可以和实测的 `--ghz` 换算结果对照。源码注释里的延迟/吞吐数字（Ice Lake）应当能这样复现。
