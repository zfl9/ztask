# ztask — C++23 结构化并发库

- C 风格的 C++23
- gcc、clang、zig c++
- 克制使用 C++ 特性、标准库
- “显式优于隐式”，推崇 Zig 哲学
- 基于 computed goto 的结构化并发

## 核心约束

### 对象生命周期

- **构造 + 析构 + move 构造（按需定义）**，其余一律 `= delete`
- **禁止 `operator=` 重载**：自赋值检测与旧资源清理的语义复杂性使其得不偿失
- **RAII 贯穿始终**：侵入式 u32 引用计数，避免 `std::shared_ptr` 的强制原子操作开销
- **侵入式数据结构**：`z_List<T, &T::field>` 配合 `z_container_of`，零额外动态分配开销

### 内存布局

- 非必要：不继承、不 `virtual`、不 `template`
- struct 字段通常按类型的 `alignment` 降序排列
- 宏注入 `fields`、`methods` 是 C/C++ 的务实之选

### 函数与闭包

- 所有公开接口 **必须 `noexcept`**
- 无捕获的 lambda **必须 `static noexcept`**
- `template` 的非类型参数等价于 Zig 的 `comptime` 函数参数

### 命名与风格

- 头文件全部 `#pragma once`
- `z_` 简洁前缀（源于项目名 `ztask`）
- C 风格 `snake_case`，`T *p`、`T &r`、`T &&r`
- 日志使用 `log.h` 的 `log_info` / `log_warning` / `log_error`
- 模板约束用 **C++20 concepts**（如 `z_pure_c_type`），不用 SFINAE
