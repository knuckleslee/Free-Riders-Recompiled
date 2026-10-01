# 效能工作中途報告（2026-10-01）

分支 `claude/upstream-kinect`，已合併作者的 v0.4.5（`1bc7b16`）。目標是向作者提交「有明顯效能改進、
已分離並驗證」的改動。這份報告記錄現況、驗證的缺口、i5 的測試計畫，以及下一個值得做的改進。

## 一、機器與已有的數據

| 機器 | CPU／GPU | 用途 |
| --- | --- | --- |
| i7 | i7-6850K／RTX 3080 Ti（eGPU）／Windows 10 | 開發與建置 |
| Z13 | Ryzen AI MAX+ 395／Radeon 8060S／Windows 11 | 作者的目標硬體級別（掌上型） |
| i5 | i5-3470／RX 480／Windows | 慢 CPU，主執行緒被吃滿，最容易看出 CPU 端改動 |

**所有合併 v0.4.4／v0.4.5 之前的數字，對現在的程式都不能直接引用。** 作者那兩版自己改了熱路徑
（平坦函式表、`__savegprlr` 的 hook 快速路徑、部分頁面快速路徑），我們的改動在新基底上的收益可能變小。

## 二、改動清單與狀態

| # | 改動 | 開關 | 已有證據 | 狀態 |
| --- | --- | --- | --- | --- |
| 1 | 暫停通知取代每 1 ms 輪詢 | `SFR_SUSPEND_NOTIFY=0` 還原 | i7 約 +5%（6 對 0 對較快，即輪詢全輸）、Z13 約 +9%（四對四不重疊）、i5 +4% 在雜訊內 | 可送，需在新基底確認一次 |
| 2 | 一般儲存略過保留／釘住檢查 | `SFR_STRICT_MEMORY=1` 還原 | i7 單獨量約 +3% | 只量過一台 |
| 3 | 部分向量讀寫走快速頁面 | 無 | 只有 +22% 的合計 | 未單獨量；作者也做了 aarch64 版 |
| 4 | `lwarx`／`stwcx.` 快速頁面不做版面搜尋 | 無（受 #2 的開關影響） | 同上 | 未單獨量 |
| 5 | 連續繪製沿用上一個 pipeline | 無 | 同上 | 未單獨量 |
| 6 | 檢查點每 256 次才呼叫許可（原 32） | 無 | 同上 | 未單獨量 |
| 7 | PowerPC 暫存器改成區域變數（`scripts/localize_registers.py`） | 換生成碼目錄重建 | 同上；剖析顯示生成碼佔主執行緒約 34–37%，`__savegprlr`／`__restgprlr` 約 2.6% | 未單獨量，剖析推論為最大一項 |
| 8 | 渲染執行緒 | `SFR_RENDER_THREAD=0` 還原 | i5 −10% 關掉後（6 對 0 對較快）；i7 −2% 雜訊內；Z13 無效果 | 證據足，但是架構改動 |
| 9 | 只叫醒下一位 | 無 | 實際遊戲量不到效果 | 不當效能項送 |
| 10 | 主執行緒原地等待 | `SFR_MAIN_SPIN_US` | −1% | 預設關，不送 |
| 11 | 平坦函式表、aarch64 向量快速寫入 | | 作者 v0.4.4／v0.4.5 已有 | 已被取代，不送 |

`+22%`（i7，commit `bc9af04`，25.8 → 31.6 fps，P95 54 → 41 ms）是 #2–#7 合在一起、且只在合併前量的數字。
其中只有 #2 單獨量過（約 3%），其餘約 19% 的分配沒有證據。

## 三、驗證缺口與送作者的前置條件

1. 合併後的 `diagnostic_main.cpp` 還沒在 Windows 建置，Linux 無法編譯生成碼。第一步是 i7 重建並跑新 baseline。
2. 作者的 shader ABI 已是 10，用 `pipelines` 模式前要 `py scripts\pack_shaders.py` 重新產生。
3. 送出前每一項都要有「單獨一項、同一份原始碼、交錯跑、至少兩台機器、效果大於該機器雜訊」的數據。
   雜訊：i7 兩趟同設定可差 7–30%，i5 約 4%，Z13 約 2%。
4. 小 commit、每項一個，工具與文件（`docs/benchmark*.md`、`scripts/benchmark*`）不跟效能改動混在同一個送件。
5. 暫存器區域變數的後處理腳本不適合直接送。作者能接受的形式是在 `generate_diagnostic.py` 內直接開
   XenonRecomp 的 `*_as_local` 選項，再把我們的安全規則（輸入、hook 前存回、`HOST_INPUTS`、`KEPT_HELPERS`）
   移進去。這要等 A/B 確認值得之後才做。

## 四、i5 測試計畫

i5 的 kit 在 `X:\sfr-benchmark-kit`，一律用 `scripts\make_benchmark_kit.ps1 -UpdateOnly` 在原地更新。
每一階段都要確認 log 裡 `start` 之後出現兩行 `NUI_MESSAGE_BOX`，且有 `racing=1` 的格；沒進比賽的趟不計。

### 階段 0：i7 準備（先做，不在 i5）

```powershell
.\scripts\build_tools.ps1 -Diagnostic -DiagnosticDirectory out/recomp/diagnostic        # 原始生成碼
copy out\build\host\sfr_cpu_diagnostic.exe ..\sfr_plain.exe
.\scripts\build_tools.ps1 -Diagnostic -DiagnosticDirectory out/recomp/diagnostic-local  # 區域變數
copy out\build\host\sfr_cpu_diagnostic.exe ..\sfr_local.exe
```

兩個執行檔必須來自同一份 commit。做兩個 kit：把 `sfr_plain.exe` 與 `sfr_local.exe` 各放進一個
`make_benchmark_kit.ps1` 的輸出，分別命名 `sfr-benchmark-kit-plain` 和 `sfr-benchmark-kit-local`。
兩個 kit 除了 `out\build\host\sfr_cpu_diagnostic.exe` 之外必須逐檔相同。

### 階段 1：i5 上測區域變數（#7）

這是最需要的一項，且不必加任何開關。

1. 兩個 kit 放在 i5 的同一個磁碟上，遊戲資料夾共用（`-ImageDirectory`、`-AssetDirectory` 指到同一處）。
2. 交錯跑：plain、local、plain、local……各 6 趟，兩個 kit 輪流，不要先跑完一個再跑另一個。
   因為每個 kit 的 `run_benchmark.bat` 是自己迴圈，交錯要用手動：每輪各跑一次 `-Repeats 1`，共 6 輪。
   ```powershell
   foreach ($i in 1..6) {
       & X:\sfr-benchmark-kit-plain\scripts\benchmark.ps1 -SkipBuildCheck -Configs baseline -Repeats 1 -NoWarmup:($i -gt 1)
       & X:\sfr-benchmark-kit-local\scripts\benchmark.ps1 -SkipBuildCheck -Configs baseline -Repeats 1 -NoWarmup
   }
   ```
   （第一輪的 plain 留著 warmup，不計；每次呼叫各寫一個 `out\bench\<時間>` 資料夾，12 個資料夾要用各自的
   `summary.md` 對照，`benchmark_summary.py` 一次只彙總一個資料夾，成對比值要手動算或補一個彙總腳本。）
3. 判讀：比較「同一輪」的兩個 fps 比值；6 對中至少 5 對同方向，且中位數差大於 baseline 的 4% 雜訊，才算有效。
4. 同樣的步驟在 i7 再做一次。兩台都有效才進入送件準備。

### 階段 2：i5 上測已有開關的項目（#1、#2、#8）

不需要新程式，用合併後的同一個執行檔（`local` 或 `plain` 擇一，兩階段用同一個）：

```powershell
.\scripts\benchmark.ps1 -SkipBuildCheck -Configs baseline,no-suspend-notify,no-render-thread,strict-memory -Repeats 6
```

`strict-memory` 是 `benchmark.ps1` 已有的設定（`SFR_STRICT_MEMORY=1`）。
約 19–25 趟，i5 每趟約 9 分鐘，約 3–4 小時。

### 階段 3：i5 上測 #3–#6（需先加開關）

這四項現在沒有開關，不能在同一個 kit 比較。做法是各加一個啟動時讀一次的環境變數
（`SFR_FAST_PARTIAL_VECTOR=0`、`SFR_FAST_RESERVED=0`、`SFR_PIPELINE_REUSE=0`、`SFR_CHECKPOINT_INTERVAL=32`），
再各加一個 `benchmark.ps1` config，一份 kit 跑 5 個設定。開關只用於量測，送作者前移除或保留由作者決定。
效果小於 i5 雜訊（約 4%）的項目不單獨送。

### 階段 4：回報

每一階段把 `out\bench\<時間>-shareable.zip` 傳回，結果追加到 `docs/benchmark.md`，
並更新本檔第二節的「已有證據」欄。

## 五、下一個值得做的改進（推估，尚未量測）

主執行緒在 i7 上每格約 32.5 ms：

| 類別 | 比例 | 約 ms | 備註 |
| --- | ---: | ---: | --- |
| 遊戲生成碼 | 37% | 12 | #7 打這一塊 |
| 執行檔以外（等待、驅動、系統） | 30% | 9.6 | 含主執行緒排隊等許可 4.6–8.2 ms |
| 畫圖 | 17% | 5.6 | 渲染執行緒已搬走錄製的約 1.5 ms |
| 客體記憶體與向量存取 | 8% | 2.6 | #2–#4 打這一塊 |

1. **先把 #7 量出來**。它若確認有效，下一步是讓 `generate_diagnostic.py` 直接輸出區域變數，
   作者就不必維護後處理腳本。
2. **主執行緒排隊等許可（4.6–8.2 ms／格）**。只叫醒下一位與原地等待都沒用；剩下的來源是同在處理器 0 上、
   必須輪流的客體（18、37、16）。讓它們與主執行緒真正並行約有每格 2–4 ms（6–10%）的空間，
   但這是語意改動，風險是目前所有項目裡最高的，需在新基底用 `SFR_PARALLEL_HELD=1` 先重新量一次。
3. **i5 上的渲染執行緒收益（10%）說明慢 CPU 是 CPU 端瓶頸**。若階段 1 顯示 #7 在 i5 的收益大於 i7，
   後續優化應以 i5 為主要驗證機器。
4. 畫圖路徑（pipeline 鍵、常數複製、紋理查詢）在 i7 上沒有量到渲染執行緒的收益，目前不優先。

## 六、待決事項

- 渲染執行緒保留或移除：使用者先前回覆「拿掉」，之後 i5 數據顯示關掉會慢 10%。尚未得到使用者對新數據的答覆，
  目前程式維持保留。
- 是否先加 #3–#6 的開關（階段 3）：等階段 1、2 的結果再決定，不必先動。
- 沒有任何東西已送到作者的 repo。
