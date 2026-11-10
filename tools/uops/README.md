# uops.info 指令数据查询

[uops.info](https://uops.info) 实测了 x86 指令在各代微架构上的延迟、吞吐和端口占用。原始数据库 `instructions.xml` 约 140 MB，不适合整个读入（无论是人还是 AI），所以先转成一张"一行一条（指令变体, 微架构）"的表，再按需查询。

## 准备（一次）

1. 从 https://uops.info/xml.html 下载 `instructions.xml`，放到 `tools/uops/data/`（该目录被 git 忽略）。
2. 运行 `./tools/uops/Convert-UopsXml.ps1`，生成 `tools/uops/data/uops.tsv`（约 1 秒）。

## 查询

```powershell
./tools/uops/Find-Uops.ps1 VPSRLVQ                          # 所有操作数形式，默认微架构
./tools/uops/Find-Uops.ps1 VPSRLVQ,VPSLLVQ,VPBLENDVB -Ymm     # 只看 256 位寄存器形式
./tools/uops/Find-Uops.ps1 'VPBLENDVB \(YMM' -Arch ICL,ZEN4
./tools/uops/Find-Uops.ps1 VCVTQQ2PD -Arch all
```

也可以直接用 ripgrep 搜表（AI agent 推荐这样用，结果最紧凑）：

```bash
rg '^VPSRLVQ \(YMM, YMM, YMM\)\t(ICL|ZEN4)\t' tools/uops/data/uops.tsv
```

表的列：`instr`（如 `VPSRLVQ (YMM, YMM, YMM)`）、`arch`、`uops`、`ports`、`tp`（倒数吞吐，周期）、`lat`（延迟，周期；多个操作数对时写成 `最小..最大`）、`lat_detail`（每个"源操作数 → 目标操作数"的延迟，`<=` 表示上界）、`ext`、`iform`、`summary`。只收录实测数据，不含 IACA 预测。

查的是**汇编助记符**，不是 intrinsic 名。常用对应：`_mm256_srlv_epi64` → `VPSRLVQ`，`_mm256_sllv_epi64` → `VPSLLVQ`，`_mm256_blendv_epi8` → `VPBLENDVB`，`_mm256_cmpgt_epi64` → `VPCMPGTQ`，`_mm256_add_pd` → `VADDPD`，`_mm256_blend_epi32` → `VPBLENDD`，`_mm256_blend_epi16` → `VPBLENDW`。完整对应关系见 Intel Intrinsics Guide。

常用微架构缩写：`HSW` Haswell、`SKL` Skylake、`ICL` Ice Lake、`TGL` Tiger Lake、`ADL-P` Alder Lake 大核、`ADL-E` 小核、`ARL-P` Arrow Lake、`ZEN2`–`ZEN5`。

`data/xmlToAssembler.py` 是 uops.info 随数据附带的脚本：把 XML 里每条指令各生成一行汇编，用来检查汇编器是否支持这些指令。对查询延迟和吞吐没有用处，可以忽略。

单条指令的数字只是参考；整段代码的瓶颈（关键路径、端口争用）要用 [uiCA](https://uica.uops.info) 对编译后的循环做静态分析，再用 benchmark 实测确认。
