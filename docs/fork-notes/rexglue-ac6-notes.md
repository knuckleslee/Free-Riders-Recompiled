# ReXGlue（AC6_recomp）可借鏡的地方

來源：`sal063/AC6_recomp` `09144bb0`（2026-08-22），內含 `rexglue-sdk`。AC6 的 README 自己說效能是已知問題
（繪圖仍走 Xenia 的 GPU 指令處理器，原生繪圖器還沒做），所以參考的是 SDK 的設計，不是 AC6 的成績。

| 項目 | ReXGlue／Unleashed | 本移植 | 備註 |
| --- | --- | --- | --- |
| 遊戲自帶的 C 執行期函式 | `[rexcrt]` 把 memcpy、memmove、memset、XMemCpy、str* 與 RtlAllocateHeap 等換成原生實作（`src/kernel/crt/*.cpp`，堆積用 o1heap） | 照原樣跑重新編譯的 PPC 程式（`docs/video-mode.md` 提到 memcpy 仍走客體路徑） | 要先知道 Free Riders 這些函式的位址，以及它們在剖析裡佔多少 |
| 客體記憶體存取 | `base + 位址 + 常數偏移`，只有 GPU 暫存器範圍走 MMIO（`include/rex/ppc/memory.h`） | 每次存取先查每 4 KB 一位元組的頁表與旗標（`src/guest_memory.h:165`） | 和 Xenia 相同；本移植的特殊字（匯入變數、計算字）、保留、頁面監看都靠這個檢查，改動是設計層級 |
| 暫存器放區域變數（XenonRecomp 選項） | Unleashed 的 `SWA.toml` 八項全開（`skip_lr`、`skip_msr`、`ctr/xer/reserved/cr/non_argument/non_volatile_as_local`）；AC6 用預設（全關） | `config/freeriders.toml` 全關 | 作者的產生器重寫（`generate_diagnostic.py`）比對固定的輸出文字，hook 也讀 `ctx` 的暫存器；開啟需要配合修改。我們自己的區域變數實驗在 i5 約 +4% |
| 客體執行緒 | Xenia 核心，沒有全域許可 | 全域許可加每核心許可（`all` 在 i5 約 +18%） | |

## 先做什麼

在 i5 跑一次 `profile`，看主執行緒的時間落在哪：若遊戲的 memcpy／memset 類函式明顯，`[rexcrt]` 式的替換是最便宜的一步
（每個函式一個 hook）；若散在大量生成碼，暫存器區域變數與記憶體存取方式才是方向。

## GoldenEye-Recomp

`SunJaycy/GoldenEye-Recomp` `fdee4d1`（2026-06-18），同樣用 ReXGlue（SDK 0.8.0.0），約 3000 行，多半是遊戲修補、
滑鼠、線上、後製效果與除錯用的看門狗。效能方面沒有可借的東西：XBLA 版本身負擔輕，FPS 上限是 SDK 的 `max_fps`，
專案裡的難題是 GPU 指令處理器卡住與時脈欄位沒人更新（`src/ge_hooks.cpp` 的 `ge_dbg_now`），屬於正確性。

## Fable 2 Recomp 與 Edge of Time

**Fable 2**（`himdo/Fable-2-Recomp` `dbcbefc`，2026-10-01，ReXGlue 0.10.0）最有參考價值的是
`docs/hotfunc_overrides.md`：用剖析找出最熱的客體函式，以同名強符號覆蓋生成的版本（直接呼叫與間接呼叫都會被攔下），
再逐一手調。手法：

- **nv**：去掉單一函式內客體記憶體存取的 volatile，讓編譯器在基本區塊內合併讀寫。**本移植不能整體這樣做**：
  作者試過，平行的客體執行緒因此出現 R6025（`src/guest_memory.h:157` 的註解），所以只能逐函式、證明沒有跨執行緒讀寫才行。
  Fable 2 的教訓相同：把時間常數提到迴圈外，遊戲在載入畫面卡死。
- **批次 yield**：工作迴圈每圈呼叫 `NtYieldExecution`（`SwitchToThread`），佔整體 CPU 約 13.7%，改成每 N 次才真的讓出。
  本移植沒有實作 `NtYieldExecution`（`KeDelayExecutionThread` 有），Free Riders 有沒有類似的忙等要看剖析。
- **mfmsr／全域鎖**：遊戲的中斷鎖序列換成直接讀計數；本移植的對應物是 `docs/critical-section-locking.md`。
- **時間基準**：ReXGlue 每次讀時間都拿一把全域鎖；本移植的 `GuestClock::time_base` 只是 `steady_clock::now()`，沒有鎖。
- **剖析陷阱**：他們的剖析版本被誤設成 `-O0`，把一個函式的成本誇大成 7.57%。我們的剖析要確認是 Release 建置。
- `REX_PHYS_HOST_OFFSET` 的分支每次存取多約 5 個指令，他們也列為成本，與本移植每次存取查頁表是同一類問題。

**Edge of Time**（`goliathret/EdgeOfTimeRecomp` `08f785a`）的原始碼尚未公開（只有 `main.cpp`），
可看的只有設定：`[rexcrt]` 換掉堆積、memcpy、memmove、XMemSet128 與字串函式（`reeot_default_xex.toml`），
效能文件只談 GPU 設定。
