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
