# ztask — C++23 结构化并发库

- C 风格的 C++23
- gcc、clang、zig c++
- 克制使用 C++ 特性、标准库
- “显式优于隐式”，推崇 Zig 哲学
- 基于 computed goto 的结构化并发

## 构造与资源管理

- **构造 + 析构 + move 构造（按需定义）**，其余一律 `= delete`
- **禁止 `operator=` 重载**：自赋值检测与旧资源清理的语义复杂性使其得不偿失
- **RAII 贯穿始终**：侵入式 u32 引用计数，避免 `std::shared_ptr` 的强制原子操作开销

## 数据结构与内存

- **侵入式数据结构**：`z_Node` 配合 `z_container_of`，零动态分配开销
- struct 字段按类型的 `alignment` 降序排列，善用位域优化布局
- 宏注入 `fields`、`methods` 是目前 C/C++ 的务实之选

## 接口与泛型

- 禁用 C++ 异常（代码膨胀、污染指令缓存）
- 所有公开接口、回调签名 **必须 `noexcept`**
- 无捕获的 lambda **必须 `static noexcept`**
- 对继承、virtual、模板保持克制——不炫技，不教条
- 模板约束用 **C++20 concepts**，避免老套&晦涩的 SFINAE
- `template` 非类型参数等价于 Zig 的 `comptime` 函数参数

## 命名与风格

- 头文件全部 `#pragma once`
- `z_` 简洁前缀（源于项目名 `ztask`）
- C 风格 `snake_case`，`T *p`、`T &r`、`T &&r`
- 小对象：值传参（零成本的寄存器传递），同时减少指针对优化器的干扰
- 复杂对象：指针传参（纯粹 C/Zig 风格），无需考虑复杂的 C++ 对象语义
- 不滥用引用：完美转发、转发引用等杂音在 C 风格指针传递面前鲜有用场

## 协程执行模型

- active task 独占当前任务的执行权和资源使用权，`z_Task::waiter`、`z_Task::timer` 由 active task 独占使用
- `z_call/z_return` 传递执行权：`z_call` 将执行权下放给子 task，`z_return`/`z_ret` 将执行权归还父 task
- 状态最小化：必须跨越 yield 的状态（如 `n_read`）才提升为结构体的字段，其余上下文由 `z_function` 参数提供
- 参数幂等性：resume 路径上的 `z_call` 参数表达式会被重新求值，表达式的值必须保证幂等（尤其警惕局部变量的指针）
- 取消意图是同步触达的，但 leaf task 可以异步完成“取消”（比如提交 io_uring 取消请求，并 yield 等待“取消”完成）

## 性能与代码组织

- 减少指针别名与指针逃逸，给编译器留足优化空间
- 避免 header-only 风格，头文件只放声明，保持纯粹
- 警惕代码膨胀，保持代码精简，多写优化器友好的代码
- release 构建由 LTO(full) 保证跨编译单元的性能优化
