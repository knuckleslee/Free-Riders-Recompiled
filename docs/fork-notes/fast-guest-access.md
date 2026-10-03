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

## 檢查點只放在迴圈上（`build_ab.ps1 -Loops`）

產生器在**每一個** `loc_` 標籤後插入 `sfr::guest_checkpoint();`，不只迴圈。大多數標籤只是往前跳的目標（if/else 匯合、
提早離開），走到那裡時並沒有繞回過任何東西。許可只需要每個迴圈繞一圈至少遇到一次檢查點，加上每個函式入口
（`sfr::enter_function`）本來就有，任何長時間執行的程式碼仍然一定會經過檢查點。

`scripts/loop_checkpoints.py` 複製一份生成碼，只保留「同一函式中，標籤之後還有程式碼提到它」的檢查點（往回跳的
`goto`、跳躍表的 case；連註解提到也保留，寧可多留）。其餘刪掉。檢查點每次本身很便宜（執行緒區域的倒數），
但它是一次編譯器得假設「什麼都可能被改」的呼叫，會打斷暫存器的保留；刪掉後，直線程式碼可以一路留在暫存器。

- 疊在目前改最多的版本上（有 `-Local` 時是 c，否則是 b），成為 `sfr_cpu_diagnostic_d.exe`，所以 d 與它只差這一點。
- `run_benchmark.bat exe4`（a、b、c、d）或 `exe-loops`（a、b、d）。
- 許可數的是檢查點次數，不是時間（`SFR_CHECKPOINT_INTERVAL`，預設 256）：檢查點變少，每一輪就變長。若 d 較快但
  `all`／`cores` 模式的交接變慢，可以再試較小的間隔。
- 測試：`tests/test_loop_checkpoints.py`（往回跳、跳躍表、同名標籤在別的函式、`loc_5` 與 `loc_50`、CRLF、重複處理）；
  三種邏輯錯誤各自會讓測試失敗。**尚未在遊戲中跑過。**

## 剖析：執行檔以外按 DLL 與呼叫者分類

`SFR_MAIN_PROFILE` 取樣落在執行檔以外時（i5 上佔 34%），剖析器在執行緒暫停期間用 `ReadProcessMemory` 複製最多
8 KB 堆疊（到堆疊底部會失敗而不是當掉，也不拿使用者模式的鎖），恢復執行後找出前兩個「指向本執行檔程式碼、
且前面是 call 指令」的值（`src/host_profile_stack.h`）。結束時依位址查出 DLL，只留檔名（不留資料夾，以免帶出
使用者名稱），印出 `HOST_PROFILE_OUTSIDE module=... caller=0x... caller2=0x... 次數`。

`profile_summary.py` 多兩張表：停在哪個 DLL（ntdll＝等待／鎖、d3d12、AMD 驅動……），以及程式裡哪兩層呼叫它。
不展開系統 DLL 內部的堆疊，所以 ntdll 裡的等待要靠呼叫者分辨是等事件、等許可還是驅動程式。
