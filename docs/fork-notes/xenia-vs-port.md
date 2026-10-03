# Xenia 與本移植的架構差異（從原始碼）

來源：xenia-canary `b083312`（2026-10 初的 master）；本移植 `claude/upstream-kinect`（v0.4.7 為基底）。
只讀程式碼、沒有量測。i7 上 Xenia 的截圖：整機 56%、12 個邏輯處理器負載平均，GPU 22%。

| | Xenia | 本移植 |
| --- | --- | --- |
| 客體執行緒 | 每條一個主機執行緒，**沒有全域鎖**；`ignore_thread_affinities` 預設 `true`，遊戲指定的處理器被忽略（`kernel/xthread.cc:31`），優先權也預設忽略 | 處理器 0 的執行緒（含主執行緒）共用**一個全域許可**；處理器 1–5 各一個核心許可（`SFR_PARALLEL_WORKER=cores`，`src/diagnostic_main.cpp:394`） |
| 繪圖 | 遊戲自己的 D3D 函式庫照原樣跑，只寫 PM4 封包到環形緩衝區；**另一條 GPU 執行緒**解析並翻譯成 D3D12（`gpu/command_processor.cc:352`），沒事做時先空轉 500 次再睡 | D3D 函式換成原生實作，翻譯在**主執行緒**上；渲染執行緒只接手最後的錄製 |
| 記憶體存取 | `membase + 位址`，沒有逐次檢查（`x64_seq_memory.cc` `ComputeMemoryAddress`） | 每次存取先查一張每 4 KB 一位元組的頁表，再看旗標（`src/guest_memory.h:165`） |
| `sync`／`eieio` | 譯成 `mfence`（`ppc_emit_memory.cc:749`、`x64_seq_memory.cc:2013`） | XenonRecomp 譯成**什麼都不做**（`recompiler.cpp` `PPC_INST_SYNC`） |
| `lwarx`／`stwcx.` | 原子操作 | 原子操作（條帶鎖加 CAS，`src/guest_memory.cpp:851`），平行安全 |

## 推論（未驗證）

1. **全部平行（`all`）當掉，可能和 `sync` 被拿掉有關。** x86 允許「先寫後讀」重新排序，
   PowerPC 的 `sync` 正是用來禁止這件事。執行緒輪流跑時看不出來，真的同時跑就可能出錯。
   Xenia 保留了這道屏障，而且忽略處理器指定，遊戲照樣能跑，表示遊戲本身大概可以接受平行。
   但當掉也可能是主機端共用狀態沒有保護，兩者都要查。
2. **主執行緒和處理器 0 的其他客體共用全域許可**：i7 上主執行緒每格排隊約 5 ms，Xenia 沒有這段。
3. **繪圖翻譯在主執行緒**：i7 不繪製時快 40%，Xenia 把這塊放在另一條執行緒。
4. 每次記憶體存取多查一次頁表，可能是 Z13（64 MB L3）快得不成比例的原因之一。

## 可以先做的小實驗

- `sync` 改成 `std::atomic_thread_fence(std::memory_order_seq_cst)`（產生器一行），再試 `SFR_PARALLEL_WORKER=all`
  是否還會當掉。`sync` 在遊戲裡很少見，成本可以忽略。
