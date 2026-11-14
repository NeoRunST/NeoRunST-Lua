# neorunst-lua — Lua 5.5 on NeoRunST

把 Lua 5.5.1 官方源码移植到 NeoRunST 的 NEP 用户态程序。

> **⚠️ 实验性 / 不稳定**：这是新移植，未经完整官方测试套件验证，
> 存在已知功能缺失和行为差异（见下文）。欢迎提 issue / PR。

## 目录

```
neorunst-lua\
├── main.c        移植层（唯一非官方源码的 C 文件）
├── lua\          Lua 5.5.1 官方源码（原样，未做任何修改）
├── test\
│   ├── smoke.lua 自编冒烟测试（约 100 项断言）
│   └── testes\   官方测试套件（部分用例依赖未移植的功能，会失败）
└── readme.md
```

## 构建

需要 NeoRunST dev 工具链（`buildnep.exe` + `dev\tools` 头文件，
见 NeoRunST 仓库 `dev\` 目录），内核需支持 syscall 43（堆扩容）与
syscall 44（TICKS，2026-11-14 新增）：

```
buildnep neorunst-lua\main.c dev\tools\math\*.c -o lua.nep
```

产物约 400KB（NEP 镜像上限 512KB 内）。

## 用法

```
lua.nep                    # 交互式 REPL (lua> 提示符, 支持多行续行 ...)
lua.nep test/smoke.lua     # 跑脚本; 无盘符路径默认 AA://
lua.nep script.lua a b c   # 参数通过全局 arg 表传递 (arg[-1]=解释器, arg[0]=脚本)
```

## 已支持

- Lua 5.5 完整语言：整数/浮点、goto、位运算、协程、元表、闭包、GC
- 标准库全部注册：base / coroutine / table / io / os / string /
  math / utf8 / debug（debug.traceback 可用）
- 文件 I/O：`AA://`–`ZZ://` 盘符路径（最多同时开 8 个文件）
- math：musl 移植（sin/cos/log/pow/... + frexp/ldexp/log2，~1ulp）
- 浮点显示 `%.14g`（bigint 精确十进制转换，末位舍入正确）

## 未移植 / 已知限制

**运行时功能缺失：**

| 功能 | 状态 |
|---|---|
| `os.execute` / `io.popen` | 无子进程机制，不可用 |
| `os.rename` | 内核无 rename syscall，恒失败 |
| `os.tmpname` | 不支持 |
| `os.getenv` | 无环境变量，恒返回 nil |
| `require` C 模块 / `package.loadlib` | 无动态加载，不可用 |
| `os.date` 时区 / 本地化 | 无 RTC：`os.time` = 开机秒数，localtime = UTC |
| `os.date` 格式 | 只支持 `%Y %m %d %H %M %S %y %p %I %A %a %B %b %c %x %X %j %%` |

**行为差异 / 精度问题：**

- `stdout`/`stderr` 走同一控制台，行缓冲（无换行要凑满一行或显式
  换行才落屏），`io.flush` 是空操作
- `io.stdin` 是行模式的控制台输入（`n` 格式可用，无原始字节流语义）
- 数字解析 `strtod` 为自实现：尾数只保留 19 位有效数字、指数缩放走
  `pow`，极端十进制字面量可能与最近舍入值相差 1ulp，个别值的
  `tostring` 会回退到 `%.17g` 长格式
- 格式化输出走自实现 printf（SDK stdio），支持的格式子集见 dev 包说明
- REPL 无 `-e/-l/-i` 命令行选项（官方 lua.c 未移植，REPL 是自写的）
- 单文件脚本读取上限 64KB，REPL 单 chunk 上限 8KB
- 浮点 math 函数为 musl 移植（约 1ulp），`log2` 由 `log` 换算（约 1-2ulp）

**官方 testes 中已知跑不了的用例：**
`all.lua`、`files.lua`、`main.lua`（依赖 popen / rename / stdin 管道 /
进程行为），其余大部分用例可通过。

## 测试

```
lua.nep test/smoke.lua                  # 自编冒烟测试 → "smoke: all pass"
lua.nep test/testes/basics.lua          # 官方测试, 逐个跑
lua.nep test/testes/strings.lua
... (literals constructs closure coroutine metatable math sort pm errors utf8)
```

## 许可

Lua 本体遵循 Lua 许可证（MIT 风格，见 LICENSE 与 `lua\` 各文件头）。
移植层 main.c 同样按 Lua 许可发布。
