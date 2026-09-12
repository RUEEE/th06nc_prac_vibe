> **本程序由 Vibe Coding 生成，未经充分测试，可能存在 Bug、崩溃、Replay 不兼容或游戏数据异常；请自行承担使用风险。**

> ** This program was generated through vibe coding and is not fully tested. It may contain bugs, crashes, replay incompatibilities, or game-state issues. Use it at your own risk.**

# th06nc_prac_vibe

**版本 / Version:** 0.3.1
**作者 / Author:** RUEEE (GPT used)

[中文](#中文说明) | [English](#english)

## 更新日志 / Changelog

### 0.3.2 — 2026-09-12（9.12）
- 修复部分道中，修复 ESC 菜单。

- Fixed some portion. Fixed ESC menu. 

### 0.3.1 — 2026-09-12（9.12）

- 修复 Windows 7 下 DLL 注入后 ImGui 不显示的问题：改用 MinHook 挂接
  D3D11 `Present`，延迟初始化渲染器，并兼容 Win7 字体文件布局。
- 使用预编译 Shader Model 4 字节码，移除运行时 D3DCompiler 依赖；加入可由
  `%APPDATA%\shanghaialice\th06nc\input.ini` 中 `debug=1` 开启的分阶段诊断。
- 加强 Launcher 的目标进程、主模块、文件路径、架构和代码签名检查，并完善
  注入失败信息。
- 修复 Backspace 与全屏菜单无法触发首次 ImGui 初始化的问题；F9–F12 现在
  均可切换全屏菜单，并阻止 Win7 的 F10 标题栏菜单。
- 修正确认键映射：自定义确认键会写入与 Z 相同的原生菜单确认位，但不射击。
- 按键捕获期间使用全局普通状态屏蔽其他键盘操作，并仅接受物理按键上升沿。

- Fixed missing ImGui rendering after DLL injection on Windows 7 by detouring
  D3D11 `Present` with MinHook, initializing lazily, and supporting Win7 font
  filenames.
- Replaced runtime shader compilation with embedded Shader Model 4 bytecode
  and added opt-in staged diagnostics through `debug=1` in the input INI.
- Strengthened launcher validation of the target process, main module, image
  path, architecture, and code signatures, with more useful injection errors.
- Fixed first-use activation of the Backspace and full-screen menus. F9–F12
  now toggle the panel, including suppression of Win7's F10 caption menu.
- Fixed configurable Confirm so it supplies Z's native menu-confirm bit without
  supplying Shoot.
- Key capture now globally suppresses other keyboard actions while listening
  and accepts only a physical rising edge, using ordinary single-thread state.

### 0.3.0 — 2026-09-12（9.12）

- 重构练习配置与 ECL 跳转请求，使用 `PracticeParam`、命名 replay 字段和
  非 atomic 的结构化状态。
- 增强练习 replay 改为在单个 `.rpy` trailer 中保存 UTF-8
  `name=value` 字段，并加入按实际使用功能计算的兼容协议。
- 完善 ESC 暂停菜单、循环导航、鼠标 hover、滚动，以及练习配置的即时修改。
- 新增 `TH06NC_ST5_BOSS6` 默认/快速/慢速模式，并修正对应 Sub62 跳转时间。
- 完善练习 replay 保存、读取和自动恢复跳转及初始资源的流程。

- Refactored practice parameters and ECL jump requests into structured,
  non-atomic state backed by `PracticeParam`.
- Practice replays now store UTF-8 `name=value` fields in the single `.rpy`
  trailer, with compatibility protocol selection based on features in use.
- Improved the ESC pause menu, cyclic navigation, mouse hover, scrolling, and
  live editing of practice settings.
- Added Default/Fast/Slow modes for `TH06NC_ST5_BOSS6` by patching the relevant
  Sub62 jump-time operands.
- Improved practice replay saving, loading, warp restoration, and initial
  resource restoration.

## 中文说明

`th06nc_prac_vibe` 是面向 Steam 64 位版本 `th06nc.exe` 的非官方练习器。
启动器负责寻找或启动游戏并注入 DLL；DLL 在游戏进程内挂钩游戏流程、输入和
DirectX 输出，并提供 ImGui 练习界面。

本项目不是官方作品，不包含游戏本体或游戏数据。所有功能仅针对当前分析过的
Steam 可执行文件；游戏更新后地址或指令可能变化，此时相关 Hook 会失效。

### 已完成功能

#### 增强练习选择

- 替换原版 Practice 选择界面，并保留原版菜单背景、音效和进入游戏的转场。
- 原版练习和增强练习均可跳过额外的二次确认界面。
- 通过游戏逻辑输入的上下选择项目、左右修改 Combo/数值、确认进入；键盘自定义映射和手柄均可用，同时支持鼠标。
- UI 输入值在游戏进程存活期间持续保存，关闭并重新打开界面不会重置。
- 自动使用进入菜单前已经选择的难度，不再显示重复的难度或 Rank 选项。
- 保存主线难度；进入 Extra 后再返回其他面不会残留 Extra 难度。

#### 跳转功能

“跳转到”包含：无、道中、道中 Boss、关底 Boss、非符、符卡和帧。

- 道中跳转直接修改敌人管理器的 timeline 时间。
- “帧”允许输入自定义 timeline 时间。
- 道中 Boss/关底 Boss、非符和符卡通过修改已加载的 Steam 版 ECL 实现。
- Boss 列表按面数、当前难度和类型自动筛选。
- 支持是否保留 Boss 前对话。
- 支持 Stage 4 Patchouli 的自机类型伪装（Fake Shot）。
- Stage 4 第四道中支持六本魔法书的 X/Y 固定、镜像、轮换、随机化以及剪贴板复制/粘贴。
- Extra 的 `QED「495年的波纹」` 可选“发狂495”，直接从最后阶段开始。

ECL 修改前会验证关卡、文件大小和内容指纹，避免把某一面的偏移写入错误脚本。

#### 练习流程修复

- Boss 练习自动选择 Boss BGM，道中和道中 Boss 保留道中 BGM。
- 退出增强练习后不会把 Boss BGM 或增强模式状态泄漏到普通游戏。
- 游戏内重开时保持增强练习模式、跳转和残机/Bomb/分数/Power/擦弹/蓝点设置。
- 练习开局跳过原版约 120 帧入场状态，因此不会产生开局消弹和入场无敌。
- 练习跳转不会生成错误的关卡标题块。
- Stage 6 与 Extra 的关底 Boss 使用原生符卡练习的背景预推进和特殊
  `eff06`/`eff07` 设置，不再错误使用道中 Boss 背景。

#### F9–F12 全屏菜单

- F9、F10、F11、F12 均可打开/关闭覆盖整个窗口的设置界面，并可与 Practice 设置界面同时打开。
- 界面按窗口高度自动缩放，以 1440 高度对应 2.5x 为基准。
- 中文、英文、日文切换；默认语言根据系统代码页选择。
- 显示统一版本号和默认折叠的许可证/第三方声明。
- 自动射击：V 切换，按下当前绑定的射击/Bomb 键或 V 取消；`Shift+D` 可开启功能。
- 可在全屏菜单中分别重绑上、下、左、右、低速、射击和 Bomb；支持下拉列表与“选择按键”直接捕获。
- 重试、直接退出和菜单确认键也可修改（默认 R、Q、Enter）；确认键写入与 Z 相同的原生菜单确认位，但不会触发射击。
- 自动射击时左上角显示 `A`，其逻辑输入可正常写入 Replay。
- 仅练习模式可开启判定显示，包含弹幕、矩形判定、自机判定和旋转激光判定。
- 可调整游戏 FPS/速度。
- D3D11 拉伸模式将游戏画面以右侧为锚点进行水平放大，ImGui 不参与拉伸。

#### Backspace 辅助菜单

- 窗口位于右上角，以只读热键列表显示状态；已开启项目显示为绿色。
- **F1 无敌：**阻止弹幕和激光碰撞把 Player State 写成 DIE。
- **F2 锁残：**只在残机已经为 0 时阻止疮痍，0 残之前保持原版体验。
- **F3 锁 Bomb：**允许正常释放 Bomb，但不减少 Bomb 数量。
- **F4 锁 Power：**阻止死亡流程中的多处 Power 减少写入。
- **F5 锁时：**冻结 timeline 与活动敌人的局部计时器，并保留道中 Boss 登场所需的 timeline 例外。
- **F6 自动 Bomb：**在原版允许决死的模式中跳过 X 键边沿判断，直接进入原生决死分支；不伪造 Replay 输入。
- **F7 永续 BGM：**使用游戏原生 `KeepBgm` 重开机制保持增强练习 BGM；退出练习时清除标志。
- **F8 禁止丢 B：**仅移除实时游戏的 Bomb 逻辑输入，不影响菜单按键及 Replay 内已有的 Bomb。

#### ESC 与练习录像

- 增强练习中按 Esc 打开独立暂停菜单，可继续、重新开始、退出并进入原生录像保存确认，或跳过保存直接退出。
- 暂停菜单支持方向键与 Z，也可直接按 Esc、R；`Esc+Q` 直接退出且不进入录像保存界面。
- 增强练习保存时只生成一个 `.rpy`；练习参数以带版本和摘要校验的 trailer 附加在同一文件末尾。
- trailer 只记录练习选择界面的跳转、初始资源、Fake Shot 和魔法书参数。Backspace 菜单的 F1–F8 状态不保存、不恢复。
- 从游戏原生 Replay 菜单加载该 `.rpy` 时自动恢复练习参数。增强练习录像不保证脱离本练习器后可由原版游戏播放。
- 普通游戏、原版练习和原生符卡练习的录像不会附加 trailer，保持原版 `.rpy` 格式和兼容性。

#### 判定与画面工具

- 遍历完整的 `0x280` 项弹幕池，并补充捕获矩形/特殊碰撞调用。
- 支持旋转激光轮廓、中心点、尺寸文字、颜色、填充和坐标校准。
- 判定绘制包含自机半径，尺寸文字保留弹幕原始半径/半宽高。
- 可跳过高低两层舞台背景并仅将游戏区域清成黑色，不遮挡自机、敌人、弹幕和 HUD。
- 同时支持 D3D11/DXGI 与 D3D9 的 ImGui 注入路径；当前画面拉伸后处理仅支持 D3D11。

### 使用方法

将以下两个文件放在同一目录：

```text
th06nc_test_launcher.exe
th06nc_test.dll
```

- 对于Steam版，请勿放在与游戏文件同一目录，随后直接启动 th06nc_test_launcher.exe
- 对于学习版，请放在统一目录后，启动 th06nc_test_launcher.exe

其它打开方式：

```powershell
th06nc_test_launcher.exe --attach
th06nc_test_launcher.exe --pid 12345
th06nc_test_launcher.exe --direct "D:\Game\th06nc.exe"
```

launcher 与游戏必须处于相同权限级别。注入、内存修改和代码 Hook 可能触发杀毒
软件误报。

### 构建

使用 Visual Studio 2022 或更新版本打开 `th06nc_test.sln`，生成
`Release | x64`，或执行：

```powershell
msbuild .\th06nc_test.sln /m /p:Configuration=Release /p:Platform=x64
```

### 文档

逆向结果、结构体、偏移、ECL 和 Hook 说明见 [docs/README.md](docs/README.md)。

### 许可证与致谢

本项目采用 [MIT License](LICENSE)，与参考项目 thprac 的许可证一致。

特别感谢 [thprac](https://github.com/touhouworldcup/thprac) 及其作者、贡献者。
本项目的练习菜单思路、部分游戏功能设计以及多处 TH06 逆向方向参考了 thprac
的开源实现。thprac 的版权归 Ack 及其贡献者所有。

ESC 暂停与练习录像元数据流程还参考了
[zxxsmart/thprac-th06nc](https://github.com/zxxsmart/thprac-th06nc) 的 Steam 新典适配。

界面使用 [Dear ImGui](https://github.com/ocornut/imgui)，其采用 MIT License；
ImGui 所带 stb 组件按 MIT License 或 Public Domain 提供。完整声明见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和
[third_party/imgui/LICENSE.txt](third_party/imgui/LICENSE.txt)。

东方 Project、东方红魔乡及相关素材版权属于上海爱丽丝幻乐团/ZUN。本项目不隶属于
或代表上海爱丽丝幻乐团、ZUN、Steam 或游戏发行方。

---

## English

`th06nc_prac_vibe` is an unofficial practice tool for the 64-bit Steam build
of `th06nc.exe`. Its launcher locates or starts the game and injects a DLL. The
DLL hooks game flow, input, and DirectX presentation and supplies ImGui-based
practice interfaces.

The project does not include the game or its data. Every game address targets
the currently analyzed Steam executable. A game update may move instructions
or data and make individual hooks unavailable.

### Implemented features

#### Enhanced Practice selection

- Replaces the native Practice selector while retaining its background,
  sounds, fade, and stage-loading flow.
- Skips the redundant confirmation screen for both Original and Enhanced modes.
- Reads the game's logical Up/Down/Left/Right/Confirm actions, so custom key
  bindings and controllers work alongside mouse input.
- Keeps all entered values for the lifetime of the game process, even when the
  window is closed and reopened.
- Uses the difficulty selected before entering Practice; there is no duplicate
  difficulty or rank control.
- Preserves the last main-game difficulty when entering and leaving Extra.

#### Warping

The unified destination control contains None, Stage portion, Midboss, Boss,
Nonspell, Spell, and Frame.

- Stage portions and custom frames change the enemy manager's timeline time.
- Boss phases patch the already loaded Steam ECL after validating its stage,
  size, and content identity.
- Boss choices are filtered by stage, active difficulty, and phase type.
- Optional pre-Boss dialogue is supported.
- Stage 4 supports Patchouli fake-shot routing.
- Stage 4 chapter 4 supports fixed positions for six books, mirroring,
  rotation, randomization, and validated clipboard copy/paste.
- Extra `QED "Ripples of 495 Years"` has a Raging 495 option that starts
  directly from its final phase.

#### Practice-flow corrections

- Main-Boss practice selects Boss BGM; stage portions and midbosses keep stage BGM.
- Enhanced state and Boss music do not leak into a later normal or native
  spell-practice run.
- In-game restart preserves enhanced mode, the queued warp, and configured
  lives, Bombs, score, power, graze, and point items.
- Enhanced entry skips the stock approximately 120-frame entry state, removing
  the unwanted opening bullet clear and entry invulnerability.
- Warped practice does not create a misleading stage-title block.
- Stage 6 and Extra main Bosses reuse native spell-practice presentation
  pre-advance and special `eff06`/`eff07` setup for the correct background.

#### F9–F12 full-screen menu

- F9, F10, F11, and F12 all toggle the full-window panel, which may coexist
  with the Practice setup UI.
- UI scaling follows window height, using 2.5x at 1440 pixels as the reference.
- Chinese, English, and Japanese localization with system-code-page default.
- Shared version display and a collapsed licenses/third-party section.
- Auto-shoot has a configurable toggle key (V by default), with `Shift+D`
  enable shortcut and top-left `A` indicator. Generated shooting remains
  replay-compatible.
- The full-screen menu can independently rebind Up, Down, Left, Right, Focus, Shoot, Bomb, and
  the auto-shoot toggle through a full keyboard list or direct "press a key"
  capture. Arrow-key and WASD buttons restore the corresponding seven-key
  gameplay presets without changing the auto-shoot toggle. Bindings and the
  auto-shoot enabled state persist in
  `%APPDATA%\\shanghaialice\\th06nc\\input.ini`.
- Retry, direct-exit, and menu-confirm controls are also configurable
  (defaults: R, Q, and Enter). Confirm injects the same native logical menu bit
  as Z, without adding Shoot. Only keys named by the built-in VK-name map are
  accepted; unsupported saved values fall back to the corresponding arrow-set
  defaults. Left/right Shift and Ctrl are normalized to their generic keys.
- Direction bindings support configurable SOCD handling: native/no filtering,
  last-input priority, first-input priority, or neutral on opposites. The SOCD
  selection is stored in the same input configuration.
- Practice-only bullet, rectangle, player, and rotated-laser hitbox display.
- Configurable FPS/game speed.
- D3D11 right-anchored horizontal game stretch without stretching ImGui.

#### Backspace helper menu

- The top-right window is a read-only hotkey/status list; enabled entries are
  highlighted in green, following the presentation used by the reference fork.
- **F1 Invincible:** prevents bullet and laser collision from writing DIE state.
- **F2 Lock lives:** prevents game over only after lives are already zero.
- **F3 Lock Bombs:** preserves Bomb count while retaining native Bomb behavior.
- **F4 Lock Power:** blocks all confirmed death-path power-loss stores.
- **F5 Lock Time:** freezes the timeline and active enemy-local timers while
  retaining native midboss-introduction timeline exceptions.
- **F6 Auto-Bomb:** enters the native deathbomb success branch in modes where
  the game itself permits deathbombing, without synthesizing replay input.
- **F7 Persistent BGM:** uses the game's native `KeepBgm` retry mechanism for
  enhanced Practice and clears it on exit.
- **F8 Disable Bomb:** removes only live logical Bomb input; menu controls and
  Bomb actions already stored in replay playback remain available.

#### ESC and practice replays

- Esc opens an Enhanced-Practice pause menu with Resume, Restart, save-and-exit,
  and direct exit without saving.
- The menu supports Up/Down and Z, direct Esc/R shortcuts, and mouse input;
  `Esc+Q` exits directly without opening the replay-save screen.
- Enhanced Practice produces one `.rpy`; a versioned, digest-checked trailer
  containing the Practice-selection warp, starting resources, Fake Shot, and
  book settings is appended to that same file.
- Backspace F1-F8 assist states are not serialized or restored.
- Selecting the file in the native Replay menu restores the embedded Practice
  data. Enhanced-Practice replays are not guaranteed to play without this tool.
- Normal runs, Original Practice, and native Spell Practice retain the exact
  native `.rpy` format and compatibility.

#### Hitbox and rendering tools

- Reads the complete `0x280`-entry bullet pool and supplements it with captured
  rectangle/special collision calls.
- Draws rotated lasers, centers, dimensions, configurable colors/fills, and
  calibrated stage coordinates.
- Can suppress both stage-background layers and clear only the playfield to
  black while preserving gameplay objects and HUD.
- Supports D3D11/DXGI and D3D9 ImGui injection. The game-stretch post-process
  currently supports D3D11 only.

### Usage

- For the Steam version, do not place it in the same directory as the game files. Instead, simply launch the th06nc_test_launcher.exe file.
- For the trial version, place it in the common directory and then launch the th06nc_test_launcher.exe file.

Advanced modes are:

```powershell
th06nc_test_launcher.exe --attach
th06nc_test_launcher.exe --pid 12345
th06nc_test_launcher.exe --direct "D:\Game\th06nc.exe"
```

The launcher and game must run at the same privilege level. DLL injection and
runtime code patching may trigger antivirus false positives.

### Building

Open `th06nc_test.sln` in Visual Studio 2022 or newer and build
`Release | x64`, or run:

```powershell
msbuild .\th06nc_test.sln /m /p:Configuration=Release /p:Platform=x64
```

### Documentation

See [docs/README.md](docs/README.md) for reverse-engineering notes, recovered
structures, RVAs, ECL behavior, and hook design.

### License and acknowledgements

This project is released under the [MIT License](LICENSE), matching the license
used by the referenced thprac project.

Special thanks to [thprac](https://github.com/touhouworldcup/thprac), its
authors, and its contributors. The practice-menu design, several gameplay-tool
concepts, and parts of the TH06 reverse-engineering direction were informed by
its open-source implementation. Copyright in thprac remains with Ack and its
contributors.

The UI uses [Dear ImGui](https://github.com/ocornut/imgui), released under the
MIT License. The stb components bundled by ImGui are available under the MIT
License or Public Domain. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and [third_party/imgui/LICENSE.txt](third_party/imgui/LICENSE.txt).

Touhou Project, Embodiment of Scarlet Devil, and related assets belong to
Team Shanghai Alice/ZUN. This project is not affiliated with or endorsed by
Team Shanghai Alice, ZUN, Steam, or the game's publisher.
