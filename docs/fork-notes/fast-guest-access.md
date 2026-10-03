# 生成碼的客體記憶體存取：每個函式只讀一次快速路徑

## 問題（i5 剖析：生成碼 33%、記憶體存取 7%）

生成碼的每次 `PPC_LOAD_*`／`PPC_STORE_*` 都是 `sfr::active_memory->load/store`。建置用 `-fno-strict-aliasing`，
所以每次客體寫入之後，編譯器都得假設 `active_memory`、頁表指標、基底、釘選頁表與保留擁有者可能被改了，
下一次存取前重新從記憶體載入。用 clang -O2 編一段「讀、寫、讀」確認：第二次讀取前重新載入了全域指標與三個成員。

## 做法

- `GuestMemory::FastPath`：頁表、釘選頁表、保留擁有者與物件指標的副本；`GuestMemory::load/store(fast, base, address)`
  的判斷與成員版相同（整頁可存取、不跨頁才直接存取；寫入另需沒有自己的保留、頁面沒被釘選，監看頁照樣記錄），
  其餘一律交給成員版。資料用函式自己的 `base` 參數（就是 `base()`）。
- `src/diagnostic_hooks.h`：巨集改呼叫 `sfr::guest_load/guest_store(sfr_fast, base, ...)`。函式開頭有
  `SFR_FAST_PATH();` 時 `sfr_fast` 是區域的 `FastPath`；沒有時是命名空間層級的 `NoFastPath`，行為與以前完全相同。
- `scripts/fast_guest_access.py` 複製一份生成碼，在每個 `PPC_FUNC_PROLOGUE();` 後面加 `SFR_FAST_PATH();`。原本的目錄不動。
- `scripts/build_ab.ps1` 建兩次：`sfr_cpu_diagnostic_a.exe`（原樣）與 `_b.exe`（快速路徑），給 `run_benchmark.bat exe` 成對比較。

編譯後同一段「讀、寫、讀」：第二次讀取直接用暫存器裡的頁表與基底，沒有任何重新載入。

## 驗證

`guest_memory_test` 的 `fast_path_matches_members`：一般讀寫、跨頁、計算字、匯入變數、未對映、監看頁、
保留期間的寫入、釘選頁，結果都與成員版相同；拿掉釘選檢查時測試失敗。`tests/test_fast_guest_access.py` 檢查插入位置與換行。
**尚未在遊戲中跑過**；效果要在 i5 用 `exe` 模式成對量。

## 與暫存器區域變數疊加（`build_ab.ps1 -Local`）

`scripts/localize_registers.py`（自封存分支 `archive/upstream-kinect-20261003` 移回，產生器 `generate_diagnostic.py`
自那時起沒有變，16 個單元測試照樣通過）先把每個函式的暫存器改成區域變數（v0.4.5 時在 i5 約 +4%，6 輪 5 輪較快），
`fast_guest_access.py` 再加快速路徑，成為 `sfr_cpu_diagnostic_c.exe`。`run_benchmark.bat exe3` 讓 a（原樣）、b（快速路徑）、
c（兩者）在同一輪比較。三次完整建置，時間約平常的三倍。
