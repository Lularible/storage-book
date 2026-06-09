# 存储技术书——在不可靠的硬件上构建可靠的数据家园

一本从结绳记事到 Flash 物理、从文件系统理论到动手实现的存储技术书。

## 运行效果
<img width="910" height="774" alt="knotfs" src="https://github.com/user-attachments/assets/71361d1a-2491-4d0a-bfc8-3af04c456b9f" />


## 这本书讲了什么

全书 39 节，分五章：

- **第一章（5 节）**：存储的本质——从结绳记事到 Flash，存储的原罪（磨损/中断/噪音）
- **第二章（5 节）**：Flash 物理世界——浮栅晶体管、NOR/NAND、SLC/MLC/TLC、磨损的物理根源
- **第三章（8 节）**：文件系统理论——思想实验、FAT、日志结构化、磨损均衡、掉电安全
- **第四章（6 节）**：LittleFS 源码解析——Metadata Pair、CTZ Skip-List、Block Allocator
- **第五章（15 节）**：从零构建 KnotFS——教学级异步日志结构化文件系统（纯 C，~840 行，含生产集成思考）

## 快速开始

```bash
cd knotfs
make && make test
```

## 许可证

书籍内容：[CC BY-NC-ND 4.0](LICENSE) · KnotFS 源码：MIT

## 姊妹篇

本书是**汽车电子七部曲系列**中的存储之卷，另外三部已发布：

- **[从沙子到车辙——一个工程师的理解](https://github.com/Lularible/from-sand-to-ruts)** — 汽车电子技术总纲
- **[PTP 技术书——从思想实验到协议实现](https://github.com/Lularible/ptp-book)** — 时间同步协议
- **[HSM 技术书——从思想实验到安全基石](https://github.com/Lularible/hsm-book)** — 硬件安全模块

"汽车电子七部曲"是一个持续更新的系列——还有诊断、功能安全、软件工程四本在打磨中。
如果觉得这系列对你有用，不妨给个 ⭐ 关注进度。
