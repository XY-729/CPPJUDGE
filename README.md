# cppjudge

**cppjudge** 是一个用于 C++ 在线评测（OJ）的小型系统，专注于 C++ 代码提交与自动评测，支持浮点数比较、内存监控、JSON 日志输出以及命令行参数配置，适合作为教学或练习使用。

---

## 功能特点

- **C++ 在线评测**：只针对 C++ 提交进行编译、运行和评测  
- **浮点比较**：支持浮点数输出精度比较，默认允许误差 `1e-6`  
- **时间 & 内存监控**：记录每个样例运行时间（ms）和内存使用（MB）  
- **JSON 日志输出**：自动生成 `build/judge_log.json`，保存每个测试样例结果  
- **命令行参数支持**：可自定义提交文件、题目目录、时间和内存限制  
- **沙箱运行**：安全执行用户程序，限制资源消耗  
- **CMake 构建**：支持跨平台构建  

---

## 项目结构


cppjudge/
├─ src/ # 源代码文件
├─ include/ # 头文件（json.hpp 等）
├─ build/ # 构建输出、用户输出、日志
├─ problems/ # 测试题目数据
├─ submissions/ # 用户提交代码
├─ README.md
├─ CMakeLists.txt
├─ .gitignore


---

## 安装与依赖

1. 安装编译器：

```bash
sudo dnf install gcc-c++ make cmake
安装 JSON 库（nlohmann）：
# 将 json.hpp 放入 include/ 下即可
构建项目：
mkdir -p build
cd build
cmake ..
make
使用方法
# 运行评测
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128
参数说明：
提交文件（默认 submissions/solution.cpp）
题目目录（默认 problems/A+B）
时间限制 ms（默认 1000）
内存限制 MB（默认 128）
结果：
终端显示最终 verdict 和通过数量
JSON 日志文件：build/judge_log.json
用户输出：build/user_output/
GitHub 提交注意
已经配置 .gitignore，build/、中间文件和可执行文件不会提交
本地修改后：
git add .
git commit -m "描述修改内容"
git push
示例
{
    "submission": "submissions/solution.cpp",
    "results": [
        {
            "case": "1",
            "time_ms": 15,
            "memory_mb": 12,
            "verdict": "AC"
        }
    ],
    "final_verdict": "Accepted",
    "passed": 1,
    "total": 1
}
未来改进方向
增加多语言支持
增加在线网页提交界面
支持更多评测模式（例如交互题、批量评测）
更完善的沙箱安全机制
作者

XY-729