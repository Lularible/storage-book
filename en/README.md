# Storage: Building a Reliable Data Home on Unreliable Hardware

A technical book that starts with knot records, passes through Flash physics and file-system theory, and ends with a working filesystem implementation.

> **Status:** This is the initial English translation. The Chinese edition remains authoritative for content; feedback on terminology and clarity is welcome.

<p align="center">
  <a href="../README.md"><img src="../assets/lang-zh.svg" alt="中文" height="30"></a>&nbsp;&nbsp;&nbsp;<a href="README.md"><img src="../assets/lang-en-active.svg" alt="English" height="30"></a>
</p>

> 🌐 Chinese version: [storage-book (中文)](../README.md). Chinese chapters live in [`chapters/`](../chapters/).

## Read Online

[Browse this book online (Chinese)](https://web-l.github.io/lularible-books/storage-book/index.html)

The English edition is currently Markdown-only.

## Live Result

<img width="910" height="774" alt="knotfs" src="https://github.com/user-attachments/assets/71361d1a-2491-4d0a-bfc8-3af04c456b9f" />

## What This Book Covers

The book has 39 sections in five parts:

- **Part 1 (5 sections): The Nature of Storage: From Knots to Flash.** Knot records, clay tablets, paper, magnetic cores, semiconductors, and the three original sins of storage: wear, interruption, and noise.
- **Part 2 (5 sections): The Physics of Flash.** Floating-gate transistors, NOR/NAND, SLC/MLC/TLC, the three original sins of Flash, and the journey from package to engine bay.
- **Part 3 (8 sections): File-System Theory.** A thought experiment with 100 sheets of paper, block-device abstraction, FAT, log-structured filesystems, a map of embedded Flash filesystems, metadata, wear leveling, and power-loss safety.
- **Part 4 (6 sections): LittleFS Source Walkthrough.** Metadata pairs, CTZ skip-lists, the block allocator, directories, and the engineering wisdom of a single-file implementation. The walkthrough follows littlefs v2.11 (`LFS_VERSION 0x0002000b`).
- **Part 5 (15 sections): Building KnotFS from Scratch.** A teaching-grade asynchronous log-structured filesystem in pure C, roughly 1,770 lines including the header and tests, with production-integration notes.

No prior knowledge of storage hardware is required. If you have written C and are curious how a filesystem survives power loss, this book is for you.

## Quick Start

Read the Markdown files under `en/chapters/`, in section order (1.1 through 5.15).

Running the KnotFS teaching code:

```bash
git clone https://github.com/Lularible/storage-book.git
cd storage-book/knotfs
make && make test
```

## License

Book content: [CC BY-NC-ND 4.0](../LICENSE) · KnotFS source code: MIT

## Companion Books

This book is one volume of the "Automotive Electronics Septet" series. The other six volumes are already published:

- **[From Sand to Ruts — An Engineer's Understanding](https://github.com/Lularible/from-sand-to-ruts)** — From Turing machines to the CAN bus, from semiconductor physics to AUTOSAR; a panoramic primer written for automotive electronics engineers.
- **[PTP — From Thought Experiments to a Working Implementation](https://github.com/Lularible/ptp-book)** — Time synchronization, from thought experiments to a working PTP implementation.
- **[HSM — From Thought Experiments to a Security Cornerstone](https://github.com/Lularible/hsm-book)** — From cave-painting cryptography to hardware security modules, covering the full technical chain of the automotive HSM.
- **[UDS — From Diagnosis to a UDS Protocol Implementation](https://github.com/Lularible/uds-book)** — From the meta-questions of diagnostics through ISO 14229 and the AUTOSAR DCM source code to building a UDS stack by hand.
- **[Functional Safety — ISO 26262 Analysis and Code Implementation](https://github.com/Lularible/safety-book-iso26262)** — A functional-safety book narrated through the immune-system metaphor, combining ISO 26262 analysis, source-code dissection, and hands-on implementation.
- **[Automotive Embedded Software Engineering — Engineering Through an Architectural Metaphor](https://github.com/Lularible/swe-book)** — The engineering-methodology volume: architecture principles and quality infrastructure, with a runnable CI-pipeline teaching project, eng-lite.

## Acknowledgements

- Thanks to [@web-l](https://github.com/web-l) for building and maintaining the [mdBook site](https://web-l.github.io/lularible-books/) for this series, which makes it easier for everyone to read.

---

If you find it useful, a ⭐ is the best support. And if you can pass it on to someone who needs it, even better. 🚗💨
