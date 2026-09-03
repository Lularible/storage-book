# 存储技术书——在不可靠的硬件上构建可靠的数据家园

一本从结绳记事到 Flash 物理、从文件系统理论到动手实现的存储技术书。

## 在线阅读

[📖 在线浏览本书](https://web-l.github.io/lularible-books/storage-book/index.html)

## 运行效果
<img width="910" height="774" alt="knotfs" src="https://github.com/user-attachments/assets/71361d1a-2491-4d0a-bfc8-3af04c456b9f" />


## 这本书讲了什么

全书 39 节，分五章：

- **第一章（5 节）**：存储的本质——从结绳记事到 Flash，存储的原罪（磨损/中断/噪音）
- **第二章（5 节）**：Flash 物理世界——浮栅晶体管、NOR/NAND、SLC/MLC/TLC、磨损的物理根源
- **第三章（8 节）**：文件系统理论——思想实验、FAT、日志结构化、磨损均衡、掉电安全
- **第四章（6 节）**：LittleFS 源码解析——Metadata Pair、CTZ Skip-List、Block Allocator
- **第五章（15 节）**：从零构建 KnotFS——教学级异步日志结构化文件系统（纯 C，~1770 行，含生产集成思考）

## 快速开始

```bash
cd knotfs
make && make test
```

## 许可证

书籍内容：[CC BY-NC-ND 4.0](LICENSE) · KnotFS 源码：MIT

## 姊妹篇

本书是"汽车电子七部曲"系列中的一部。另外六部已发布：

- **[从沙子到车辙——一个工程师的理解](https://github.com/Lularible/from-sand-to-ruts)** — 从图灵机到 CAN 总线，从半导体物理到 AUTOSAR，一部为汽车电子工程师写的全景入门
- **[PTP 技术书——从思想实验到协议实现](https://github.com/Lularible/ptp-book)** — 从时间同步的思想实验开始，到 PTP 协议实现，逐机制拆解 + 动手实践
- **[HSM 技术书——从思想实验到安全基石](https://github.com/Lularible/hsm-book)** — 从岩画密码学到硬件安全模块，完整覆盖车载 HSM 的技术链路
- **[UDS 技术书——从望闻问切到UDS协议实现](https://github.com/Lularible/uds-book)** — 一本从诊断元问题出发，直通ISO 14229协议规范与AUTOSAR DCM源码、再到亲手实现UDS栈的技术书
- **[功能安全——ISO 26262分析与代码实现](https://github.com/Lularible/safety-book-iso26262)** — 以免疫系统为叙事线索的功能安全技术书。兼顾ISO 26262标准分析、源码拆解与动手实现
- **[汽车嵌入式软件工程——用建筑学隐喻讲工程化](https://github.com/Lularible/swe-book)** — 工程方法论卷：架构原则与质量基础设施，附可运行的 CI 流水线教学项目 eng-lite

## 致谢

- 感谢 [@web-l](https://github.com/web-l) 构建并维护本系列的 [mdBook 在线阅读站](https://web-l.github.io/lularible-books/)，方便了大家阅读。

---

如果觉得有用，点个 ⭐ 就是最好的支持。当然，如果能顺手转发给身边需要的人，那就更棒了。🚗💨
