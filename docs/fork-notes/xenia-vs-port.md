# Xenia 與本移植的架構差異（從原始碼）

來源：xenia-canary `b083312`（2026-10 初的 master）；本移植 `claude/upstream-kinect`（v0.4.7 為基底）。
只讀程式碼、沒有量測。i7 上 Xenia 的截圖：整機 56%、12 個邏輯處理器負載平均，GPU 22%。

| | Xenia | 本移植 |
| --- | --- | --- |
| 客體執行緒 | 每條一個主機執行緒，**沒有全域鎖**；`ignore_thread_affinities` 預設 `true`，遊戲指定的處理器被忽略（`kernel/xthread.cc:31`），優先權也預設忽略 | 處理器 0 的執行緒（含主執行緒）共用**一個全域許可**；處理器 1–5 各一個核心許可（`SFR_PARALLEL_WORKER=cores`，`src/diagnostic_main.cpp:394`） |
| 繪圖 | 遊戲自己的 D3D 函式庫照原樣跑，只寫 PM4 封包到環形緩衝區；**另一條 GPU 執行緒**解析並翻譯成 D3D12（`gpu/command_processor.cc:352`），沒事做時先空轉 500 次再睡 | D3D 函式換成原生實作，翻譯在**主執行緒**上；渲染執行緒只接手最後的錄製 |
| 記憶體存取 | `membase + 位址`，沒有逐次檢查（`x64_seq_memory.cc` `ComputeMemoryAddress`） | 每次存取先查一張每 4 KB 一位元組的頁表，再看旗標（`src/guest_memory.h:165`） |
| `sync`／`eieio` | 譯成 `mfence`（`ppc_emit_memory.cc:749`、`x64_seq_memory.cc:2013`） | XenonRecomp 本身不輸出，但作者的 `scripts/generate_diagnostic.py` `rewrite_barriers` 補上 `atomic_thread_fence`（2026-09-23，`0cc2cdf`），**兩邊相同** |
| `lwarx`／`stwcx.` | 原子操作 | 原子操作（條帶鎖加 CAS，`src/guest_memory.cpp:851`），平行安全 |

## 推論（未驗證）

1. **`all` 當掉的原因大多已經修掉，但 `all` 沒有重測過。** 當掉記在 2026-09-22（`docs/performance.md`）。
   作者查到的兩個原因都在之後修了：條件寫入的 ABA（版本化條帶，`docs/reservations.md`），
   以及遊戲 16 ms 等待逾時就重用工作項目（`SFR_WORK_SHARE_WAIT_MS`，`docs/job-dispatch.md`）。
   記憶體屏障也在隔天補上。作者另一個推測（同一硬體執行緒上的執行緒同時跑）沒有直接證據，
   而 Xenia 預設就這樣跑。當時 `all` 比 `cores` 慢（23.7 對 20.6 ms），也是很早期的版本。
2. **主執行緒和處理器 0 的其他客體共用全域許可**：i7 上主執行緒每格排隊約 5 ms，Xenia 沒有這段。
   `all` 讓主執行緒以外的客體脫離許可，正好測這一段。
3. **繪圖翻譯在主執行緒**：i7 不繪製時快 40%，Xenia 把這塊放在另一條執行緒。
4. 每次記憶體存取多查一次頁表，可能是 Z13（64 MB L3）快得不成比例的原因之一。

## 實驗

`run_benchmark.bat par`：`baseline`（`cores`）對 `all`，同一個程式，不用改程式碼。
看三件事：`all` 有沒有跑完（沒跑完的趟數）、fps 比值、主執行緒排隊時間。
