# 整晚量測與主執行緒獨佔核心

## `run_benchmark.bat night`（`scripts/night.ps1`）

依序跑完所有待量的比較，每一步都是一般的量測，放在 `out\bench\night-<時間>\<步驟>`：

| 步驟 | 設定 | 輪數 | 條件 |
| --- | --- | ---: | --- |
| pgo | exe-a-all、exe-e-all | 6 | 有 `_a`、`_e` 才跑 |
| stab | all、all-stress（`-AfterSay 1500`） | 10 | |
| exe | `_a` 到 `_d` 中有的，都在 `all` 模式（exe-a-all ……） | 6 | 至少兩個 |
| core | baseline、main-core、all、all-main-core | 5 | |
| prof | profile | 2 | |

- 缺執行檔的步驟會跳過並記下；某一步失敗不會中斷後面的步驟。
- 結束時把各步「已匿名化的分享副本」裡的 `summary.md`、`profile.md`、`info.txt` 收進
  `night-<時間>-overview.zip`（只有文字，很小），先傳這個；完整的 `-shareable` 壓縮檔有需要再傳。
- `-Steps pgo,stab` 可以只跑其中幾步。參數用雜湊表傳給 `benchmark.ps1`（用陣列傳時 `-AfterSay` 會被當成值，
  用假的 `benchmark.ps1` 試跑時抓到的）。
- 約 80 趟，5 到 6 小時。

## `SFR_MAIN_CORE=reserve`（`src/host_placement.h`）

主執行緒固定在處理器順序的第一個（`pin_current_guest_processor(0)`），但工作執行緒的處理器池也包含它，
客體處理器 0 的理想處理器就是同一個；繪圖執行緒則完全交給系統排程。i5-3470 只有四核、沒有超執行緒，
主執行緒是瓶頸時，這些都會和它搶同一個核心。

開關打開時：

- 主執行緒固定後，記下它所在實體核心的全部邏輯處理器（有 SMT 時含另一個硬體執行緒）。
- 客體工作執行緒（含音訊）的處理器順序與允許的遮罩都去掉這個核心。
- 繪圖執行緒開始時把自己的親和性設成「整個行程減去這個核心」。
- 剩下不到兩個處理器時不保留（兩核心的電腦照舊共用）。
- 主執行緒用 CPU Sets 等外部設定、沒有被固定時，不保留。

預設關閉。量測設定 `main-core`、`all-main-core`，`run_benchmark.bat core` 與 `all` 一起比較；
`all` 模式下同時跑的執行緒最多，最可能受益，也最可能因為少一個核心而變慢。只做在 Windows。
測試：`host_placement_test`（四核、SMT、兩核、未保留），三種邏輯錯誤各自會讓測試失敗。**尚未在遊戲中跑過。**

## 先做功能檢查：`run_benchmark.bat smoke`（`scripts/smoke.ps1`）

比較性能之前，先確認每個版本、每個設定都能正常跑：每個設定只跑一趟比賽（長度與量測相同，最後一個指令後
4200 格，不暖機），約 10 到 12 趟、45 到 60 分鐘。

第一次功能檢查（f2aa263，900 格）10/10 都「正常」，但每趟比賽只有 4 到 11 秒：遊戲把約 25 秒的起跑線
畫面與倒數也算成比賽中（每格約 120 次繪製、60 fps 以上），正式比賽（每格約 800 次、i5 約 23 fps）一格都
沒量到。格數上限換算成秒數，畫得越快的設定越短（`all` 開場 170 fps，只有 5 秒）。所以改用量測的長度，
檢查也加上：比賽要跑滿 45 秒，繪製與幀率只算開賽 30 秒後的正式比賽。用兩批真實記錄驗證：第一次的 10 趟
全部判為有問題，之前 `exe` 的 12 趟完整比賽全部正常。設定是 baseline、`out\build\host` 裡有的 `_a` 到 `_e`、main-core、all、
all-main-core、all-stress、profile。

`scripts/smoke_check.py` 逐趟判斷，寫成 `smoke.md`（另複製一份到 `out\bench\smoke-<時間>-smoke.md`，傳這個）：

- 是否在畫面上限正常結束（不是當掉、逾時），沒有卡住報告；
- 比賽至少 45 秒、格數至少 300、正式比賽每格有繪製、索引快取有命中；
- 該設定的功能記錄：`MAIN_CORE_RESERVED`（主核心保留）、至少三條 `PARALLEL_WORKER`（並行）、
  `HOST_PROFILE` 與 `HOST_PROFILE_OUTSIDE`（剖析）。

表裡只有設定名稱、次數、結束類別（不含位址）和一個不拿來比較的幀率。全部正常才跑 `night`；
有問題的設定先修，不放進性能比較。用假的 `benchmark.ps1` 在 pwsh 試跑過整個流程；檢查本身有單元測試。

## 版本比較改在 `all` 模式下

之前的版本比較（`exe`、`exe3`、`exe4`）都在預設的 `cores` 模式下跑；`all` 只用一般版跑過。`all` 是最可能成為預設的
模式，而工作執行緒跑的也是生成碼，快速路徑、區域變數、迴圈檢查點與函式入口的改動在它底下效果可能不同，所以：

- 新增 `exe-a-all` 到 `exe-e-all`（`SFR_EXE` 加 `SFR_PARALLEL_WORKER=all`）。
- `night` 的 exe 與 pgo 步驟改用它們；`core` 步驟照舊比較一般版在兩種模式下的差別。
- 功能檢查預設也跑它們；`run_benchmark.bat smoke-all` 只跑這幾個。`run_benchmark.bat exe-all` 是單獨的比較。
- 快速路徑在並行時的判斷與一般存取完全相同（整頁可直接存取才直接讀寫，其餘交給一般存取，並行需要的
  `slow_access_hook` 在那裡），所以可以直接在 `all` 下比較。
