# 构建毛球桌面宠物

目标平台：Windows 10 / 11 x64。程序使用 C++17、Win32 和 Windows 自带的 GDI+，将猫咪图片、图标及应用清单嵌入 EXE。运行时不需要外置素材或额外的 C++ 运行库；用户导入的图片仍通过其原路径加载。

## 项目文件

| 文件 | 用途 |
| --- | --- |
| `src/main.cpp` | 透明窗口、渲染、鼠标互动、托盘、图片导入及设置 |
| `src/pet_state.h` | 动作状态、动画曲线及位置边界逻辑 |
| `src/pet.rc` | Windows 图片、图标、版本与清单资源 |
| `src/pet.manifest` | 普通用户权限及每显示器 DPI 支持 |
| `assets/cat-idle.png` | 内置常态猫咪，透明 PNG |
| `assets/cat-sleep.png` | 同一角色的睡眠姿势，透明 PNG |
| `assets/pet.ico` | 程序和托盘图标 |
| `tests/state_test.cpp` | 不依赖 Windows 的状态逻辑测试 |
| `tests/windows_smoke.cpp` | Windows 消息及窗口行为冒烟测试工具源码 |
| `build.sh` | Linux 交叉编译并运行状态测试 |
| `build-windows.bat` | Windows 上使用 MinGW-w64 编译 |
| `licenses/` | 所用工具链及运行库的许可文本 |

内置猫咪素材由图像生成工具生成。替换资源文件后需重新编译资源和程序。自定义外形仅加载一张图片，不会据此自动生成睡眠姿势。

## 本次交付的构建记录

本次 `FluffyPet.exe` 已使用 **LLVM-MinGW 20260922** 在 Linux 上成功交叉编译，目标为 x86_64 Windows。编译选项为 C++17、`-O2 -Wall -Wextra -Wpedantic -Werror -municode -mwindows -static -Wl,--strip-all`；独立状态测试已通过。交付包中的 `SHA256SUMS.txt` 记录 EXE 的 SHA-256。

已在 **Wine 10 + Xvfb** 环境通过 **26 项自动冒烟检查**，覆盖透明窗口样式、尺寸、常态 / 睡眠 / 喂食渲染、双击消息、散步开关、置顶、拖动、隐藏到托盘与窗口恢复、单实例及正常退出，全部通过，无跳过项。该测试通过 Win32 消息驱动程序；渲染检查确认画面非空及动作间发生变化，不等同于全面的人工视觉检查。另已查看截图，确认增加字体回退后中文气泡文字正常显示。

实际 Windows 10 / 11、混合 DPI 多显示器和完整手动交互尚未测试。

## Linux 交叉编译

准备以下两种工具链之一，并把其 `bin` 目录加入 `PATH`：

- MinGW-w64：`x86_64-w64-mingw32-g++` 和 `x86_64-w64-mingw32-windres`。
- LLVM-MinGW：`x86_64-w64-mingw32-clang++` 和 `x86_64-w64-mingw32-windres`，目标为 x86_64 Windows。

以下命令从项目根目录执行。资源编译必须进入 `src`，因为 `pet.rc` 使用相对路径引用图片和清单。

推荐直接运行：

```bash
bash build.sh
```

脚本优先选择 LLVM-MinGW，否则选择 MinGW-w64；可通过 `WINDOWS_CXX`、`WINDOWS_WINDRES` 环境变量指定工具绝对路径。还需要本机 `c++` 编译器运行状态测试。下面给出等价的手动编译命令。

### MinGW-w64

```bash
mkdir -p dist
cd src
x86_64-w64-mingw32-windres pet.rc -O coff -o ../dist/pet-res.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -municode -mwindows -static -Wl,--strip-all main.cpp ../dist/pet-res.o \
  -o ../dist/FluffyPet.exe \
  -lgdiplus -lole32 -lshell32 -lcomdlg32 -luser32 -lgdi32
cd ..
```

### LLVM-MinGW

```bash
mkdir -p dist
cd src
x86_64-w64-mingw32-windres pet.rc -O coff -o ../dist/pet-res.o
x86_64-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -municode -mwindows -static -Wl,--strip-all main.cpp ../dist/pet-res.o \
  -o ../dist/FluffyPet.exe \
  -lgdiplus -lole32 -lshell32 -lcomdlg32 -luser32 -lgdi32
cd ..
```

产物为 `dist/FluffyPet.exe`。交付 EXE 和 README 即可；`pet-res.o` 是构建中间文件。以上步骤可重建程序，但不同工具链版本和 PE 时间戳可能使产物字节不同，不保证哈希完全一致。

## Windows 本机编译

安装面向 **x86_64** 的 MinGW-w64，将含 `g++.exe` 和 `windres.exe` 的 `bin` 目录加入 `PATH`，然后在命令提示符中运行：

```bat
build-windows.bat
```

输出同为 `dist\FluffyPet.exe`。此批处理只编译程序，不运行状态测试；它是源码包提供的本机构建入口，尚未在实际 Windows 环境执行验证。

## 验证

在 Linux 上使用本机 C++ 编译器运行独立状态测试：

```bash
mkdir -p dist
c++ -std=c++17 -O2 -Wall -Wextra tests/state_test.cpp -o dist/state_test
./dist/state_test
```

测试覆盖动作时长、睡眠与拖动持续状态、散步恢复、无效时间值、动画幅度和屏幕边界计算，不包含 Win32 窗口或托盘。

构建后可用 `file dist/FluffyPet.exe` 检查产物应为 x86-64 PE32+ Windows GUI 程序，并用所选工具链的 `objdump -p` 或 `llvm-readobj --coff-imports` 检查导入库。静态链接选项用于避免额外的工具链运行库 DLL；系统 DLL 仍由 Windows 提供。

**实际 Windows 桌面运行验收待完成。** 在 Windows 10 / 11 x64 上还需手动确认：启动及透明边缘；单击与双击区分；拖动与混合 DPI 多显示器；右键所有菜单项；睡眠、喂食和逗猫；托盘隐藏及恢复；退出后设置恢复；导入透明 PNG 和普通 JPG；移除自定义图片后重新启动；重复启动只显示已有实例。Linux 编译、状态测试和 Wine 自动冒烟检查不能替代这些检查。
