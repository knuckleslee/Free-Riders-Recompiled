# 無人操作的比賽基準

日期：2026-09-29。量效能改動有沒有用，要同一段畫面、同樣的設定、跑好幾趟比。
`scripts/benchmark.ps1` 讓遊戲自己從開機走到 Free Race、在沒人操作的比賽裡跑一段、
到指定的格數自己結束，每種設定輪流跑，最後由 `scripts/benchmark_summary.py`
整理成表格。整段不需要人碰。

## 用法

先建置遊戲本體（`scripts\build_tools.ps1 -Diagnostic`），並用 launcher 安裝過遊戲。

```powershell
cd C:\Users\<你>\Documents\free-riders-recompiled
.\scripts\benchmark.ps1
```

預設是暖機一趟，再把 `baseline`、`sequential`、`serial`、`skip-draws` 各跑兩趟，
輪流交錯（A B C D A B C D），免得某一種設定剛好碰上機器比較忙的時段。一趟大約
5-6 分鐘（開機到比賽約 3 分多鐘，比賽量 3600 格），全部大約 50 分鐘。

跑完後的東西在 `out/bench/<日期時間>/`：

- `summary.md`：每一趟的表格，加上每種設定取各趟中位數、和 `baseline` 比較的表格。
- `<設定>-<趟>.log`：遊戲的完整 log。
- `warmup-*.bmp`：暖機那趟每 1500 格一張截圖。某一趟沒進到比賽時，看這些圖就知道
  選單停在哪裡。
- `info.txt`：commit、CPU、顯示卡、這次的參數。

常用參數：

| 參數 | 意思 |
| --- | --- |
| `-Configs baseline,all` | 只跑這幾種設定 |
| `-Repeats 3` | 每種設定跑幾趟（預設 2） |
| `-NoWarmup` | 不跑暖機那趟 |
| `-Capped` | 限制 60 fps，和平常玩一樣；預設不限，才看得出離 60 還有多遠 |
| `-PresentLimit 18000` | 在第幾格結束（預設 15600，比賽約在 12000 格開始） |
| `-Skip 600` | 比賽開始後前幾格不算（倒數、第一次用到的著色器） |

也可以直接整理現有的 log：`python scripts\benchmark_summary.py out\bench\<日期時間>`。

## 設定

每一種都是在 launcher 的預設（`cores`、頂點快取、GPU pipeline）上只改一個開關。
另外所有設定都不限速、關掉音效、視窗固定 1280×720：

| 名稱 | 改了什麼 | 用來回答 |
| --- | --- | --- |
| `baseline` | 什麼都不改 | 基準 |
| `sequential` | `SFR_HOST_PROCESSORS=sequential` | 客體處理器排到不同實體核心有沒有幫助 |
| `serial` | `SFR_PARALLEL_WORKER=0` | 客體執行緒並行省了多少 |
| `skip-draws` | `SFR_SKIP_DRAWS=1` | 完全不繪製時的天花板：剩下的全是遊戲程式本身 |
| `all` | `SFR_PARALLEL_WORKER=all` | 所有客體執行緒都並行（實驗性，曾經當掉） |
| `render-every-2` | `SFR_RENDER_EVERY=2` | 比賽隔一格畫一次 |
| `no-vertex-cache` | `SFR_VERTEX_CACHE=0` | 跨幀頂點快取的效果 |
| `no-gpu-pipeline` | `SFR_GPU_PIPELINE=0` | GPU 晚一格的效果 |
| `vulkan` | `SFR_GRAPHICS=vulkan` | Vulkan 對 Direct3D 12 |
| `held` | `SFR_PARALLEL_HELD=1` | 哪些 import／hook 讓脫離的客體執行緒回到全域許可、每格持有多久（`summary.md` 最後一張表） |
| `main-spin` | `SFR_MAIN_SPIN_US=1000` | 主執行緒排隊時先原地等待最多 1 ms 再睡，省掉被作業系統重新排上 CPU 的延遲 |
| `strict-memory` | `SFR_STRICT_MEMORY=1` | 一般寫入照舊檢查「自己的保留中」與「非同步讀檔的輸出範圍」；baseline 從 A1 起不檢查（`GuestMemory::strict_stores`） |
| `no-render-thread` | `SFR_RENDER_THREAD=0` | 繪製指令改回在主執行緒錄進命令列表；baseline 從 C 起把它們丟給渲染執行緒錄（`NativePresentation::record_async`） |
| `no-suspend-notify` | `SFR_SUSPEND_NOTIFY=0` | 自我暫停的客體改回每 1 ms 輪詢；baseline 用通知喚醒（取自作者 `5653352`，尚未進 main） |
| `no-prewarm` | `SFR_PIPELINE_PREWARM=0` | 不在背景預先建 pipeline，遇到第一個要用的 draw 才建（仍會寫 manifest） |
| `profile` | `SFR_MAIN_PROFILE=1`、`SFR_PROFILE_AFTER=12200` | 比賽中每 1 ms 取樣主執行緒執行到哪裡；`profile.md` 依目的檔分類（遊戲生成碼、客體記憶體存取、畫圖、排程、系統與等待）並列出最熱的函式（`scripts/profile_summary.py`，需要建置時產生的 `sfr_cpu_diagnostic.map`，會複製到結果資料夾） |

## 怎麼走到比賽

用 [race-controls.md](race-controls.md) 記的做法：`SFR_NUI_HAND_CENTRED=1` 讓模擬的手
停在畫面中央，`SFR_SAY` 在指定的格數說出選單的語音詞（標題 `ok`、主選單、`right`
五次轉到 Free Race，再對規則、賽道、角色、裝備各說一次 `ok`）。格數是遊戲畫面的
格數，不是秒數，所以機器快慢不影響走的路。

比賽中沒有任何輸入，所以每趟跑的是同一段畫面。

每趟都用你存檔的**複本**（`out/build/host/save` 複製到這一趟自己的資料夾，跑完刪掉），
所以你的存檔不會被寫到，而每趟開始時的選單狀態也都一樣。Kinect、語音、音效、第二位
玩家都關掉。

## 怎麼算

遊戲每格印一行 `NATIVE_PRESENT`，其中 `racing=` 是遊戲自己的比賽旗標
（`[83E52F8C]`），`seconds=` 是從開始到這一格的秒數。一格的時間是它和前一格
`seconds` 的差。只算 `racing=1` 的格，扣掉前 `-Skip` 格。

表格欄位：平均 fps、每格時間的中位數與 P95／P99（尖峰）、主執行緒每格持有執行許可
的時間（`holders` 裡的客體 1）、主執行緒就緒卻在排隊的時間、繪製、present。

## 已知限制

- 選單的語音詞格數是在作者的機器上試出來的。如果某一趟 `summary.md` 寫「沒有比賽畫面」，
  先看 `warmup-*.bmp` 停在哪一頁，再用 `-Say` 調整格數。
- 一趟超過 `-TimeoutMinutes`（預設 25 分鐘）會被強制結束，表格的「結束」欄會寫
  沒有 STOP 行。

## 2026-09-30：i7-6850K／RTX 3080 Ti／Windows 10 的第一次結果

commit `05ef30a`，預設參數（不限速、每種設定兩趟），九趟全部走進比賽（第 11532 格開始）
並跑到第 15600 格。每種設定取兩趟的中位數：

| 設定 | 平均 fps | 每格中位數 ms | P95 ms | 相對 baseline |
| --- | ---: | ---: | ---: | ---: |
| baseline | 25.1 | 39.5 | 54.5 | +0% |
| sequential | 26.4 | 38.5 | 49.5 | +5% |
| serial | 16.7 | 61.0 | 81.5 | -34% |
| skip-draws | 35.3 | 29.0 | 39.0 | +40% |

baseline 一格約 39 ms 的去向（比賽中的平均）：

| | ms |
| --- | ---: |
| 主執行緒（客體 1）持有全域許可 | 30 |
| 　其中原生算繪器（`draw_ms`：錄製 2.8、貼圖 0.9、索引與收集 1.3、常數 0.7…） | 7.3 |
| 主執行緒就緒卻在排隊（被客體 7、37、15、16 佔著全域許可） | 5 |
| 主執行緒在等事件（多半是等客體 29 的工作） | 4 |

- **瓶頸是主執行緒自己的 30 ms**：CPU 單核速度決定一切。作者的 i9-14900KF 上同一段
  約 13 ms，差 2.3 倍，和兩顆 CPU 的單核差距相符。
- **並行一定要開**：`serial` 慢 34%，因為客體 29（工作執行緒，每格 12-15 ms）會回到全域許可上跟主執行緒搶。
- **實體核心排序沒有可量到的效果**：+5% 小於同一設定兩趟之間的差（baseline 兩趟差 7%）。
- **不繪製時主執行緒只持有 18-21 ms**：算繪在主執行緒上實際吃掉 9-12 ms，比 `draw_ms`
  多，因為還有繪製呼叫本身的 HLE。把它搬離主執行緒，是一格省最多的地方。

## 2026-09-30：主執行緒排隊的一半是交接空窗

`main_queued_ms` 可以拆成兩種：排在別人後面（`main_blockers`，有人持有許可）和
**許可已經沒人拿、主執行緒卻還沒醒來**（`main_ready_unowned_ms`）。第一次的 baseline：

| | 排隊 | 排在別人後面 | 交接空窗 |
| --- | ---: | ---: | ---: |
| baseline 兩趟 | 4.6–8.2 ms | 2.1–4.3 ms | 2.2–3.7 ms |
| serial | 27–31 ms | 24–27 ms | 2.0–2.2 ms |

原因在 `GuestExecution`：所有排隊等許可的客體（約 40 條）都睡在同一個條件變數上，每次
交接 `notify_all` 全部叫醒，每條醒來檢查是不是輪到自己、再睡回去；真正輪到的那一條常常
最後才被作業系統排到。現在每條排隊的客體睡在自己的條件變數上（`State::Ready::turn`），
狀態改變時只叫醒排在最前面的那一條（`State::notify`），停止時才叫醒全部。

40 條客體在 4 顆核心上輪流的壓力測試（每條跑一小段就檢查點、每 50 次短暫阻塞一次），
4 秒內：

| | 交接空窗 | 主執行緒持有許可 |
| --- | ---: | ---: |
| 全部叫醒 | 1773 ms | 291 ms |
| 只叫醒下一位 | 870–980 ms | 790–840 ms |

### `SFR_PARALLEL_HELD` 改成記主執行緒被擋住的原因

第一版記的是「脫離的客體回到許可後、到再次脫離為止」的牆鐘時間，但那段時間裡它可能在
import 裡等待（`NtSuspendThread` 自我暫停、等臨界區段），等待會放掉許可，所以表上出現
每格一百多毫秒的假數字。現在改由許可自己的計時做：脫離的客體拿回許可時把原因寫進
`GuestExecution::owner_reason`，主執行緒排隊的每一段時間記到當時持有者的原因上
（`Timing::main_ready_by_reason_ns`），present 行印成 `main_blockers_by_reason=`。
等待放掉許可的時間不會被算到。

那兩趟 `held`（commit `bcb99b9`）整體慢到 4–10 fps，排隊的時間幾乎都是交接空窗
（比賽後段每格 49–142 ms），而且越跑越慢；還不確定是當時電腦忙碌還是第一版的計時造成，
下一輪把 `baseline` 和 `held` 交錯跑來分辨。

## 2026-09-30：只叫醒下一位之後（commit `a2776cb`）

`baseline`、`held` 交錯各兩趟，四趟都正常跑完：

| 設定 | 平均 fps | 每格中位數 ms | P95 ms |
| --- | ---: | ---: | ---: |
| baseline | 25.5 | 39.4 | 53.9 |
| held | 25.7 | 39.7 | 51.5 |

- `held` 不再拖慢遊戲：上一輪的 4-10 fps 不是 `SFR_PARALLEL_HELD` 本身造成的。
- **只叫醒下一位在實際遊戲裡沒有效果**：交接空窗 2.6-5.0 ms，和之前的 2.4-3.1 ms 差不多；
  排在別人後面的時間略少（1.5-2.1 ms，之前 2.1-3.9 ms）。實際遊戲裡的空窗不是叫醒太多條
  造成，而是每次交接都要等主機把下一位重新排上 CPU。
- 擋住主執行緒的原因很分散，最大幾項每格 0.1-0.25 ms：處理器 0 上客體 18、37、16 自己的
  程式、客體 37 的 `NtSuspendThread`、客體 15 在深度畫面 hook（`82439530`）、客體 30 的
  `NtReadFile`、客體 11 在骨架 hook（`827707B0`）。全部改成不必拿許可也省不到 1 ms。

下一步試 `main-spin`：主執行緒排隊時先原地等待，不必等主機把它重新排上 CPU。

## 2026-09-30：主執行緒剖析（commit `f11bb30`）與第一批改動

`profile` 兩趟，比賽中取樣 13 萬次：

| 類別 | 比例 |
| --- | ---: |
| 執行檔以外（等待、顯示卡驅動、系統） | 34.2% |
| 遊戲生成碼 | 33.8% |
| 畫圖 | 14.5% |
| 客體記憶體存取（檢查過的路徑） | 6.5% |
| HLE 與診斷 | 5.4% |
| C 執行階段（多半是 memcpy） | 4.4% |
| 執行許可與排程 | 1.2% |

最熱的主機端函式：`native_draw` 2.9%、`sub_824F56E8`（索引繪製 hook）1.8%、`load_vector_left` 1.6%、
`check_store_access` 1.4%、`call_indirect` 1.2%、`store_vector_memory` 1.1%、`NativeRenderer::texture` 1.1%、
`GuestMemory::check` 1.1%、`guest_checkpoint_permit` 1.0%；生成碼裡 `__savegprlr_*`／`__restgprlr_*` 合計約 2.6%。
`main-spin` 沒有效果（−1%），維持關閉。

依此做的改動（同一個建置一起量）：

1. **寫入路徑不檢查除錯用的條件**（A1，`GuestMemory::strict_stores`，`SFR_STRICT_MEMORY=1` 還原）。
2. **部分向量讀寫**（`lvlx`、`lvrx`、`stvlx`、`stvrx`）在快速頁面上一次完成，不再先做完整檢查再逐位元組處理。
3. **原子操作**（`lwarx`／`stwcx.` 與 64 位元版本）在快速頁面上不做 `check_store_access` 的版面搜尋。
4. **連續繪製沿用上一次的 pipeline**：鍵相同就不雜湊、不查表。
5. **間接呼叫查平坦的表**（以 `(位址 - PPC_CODE_BASE) / 4` 為索引），不查 `unordered_map`。
6. **檢查點每 256 次才呼叫許可**（原本 32 次）；許可自己每 64 次呼叫才看一次時間，仍遠在 2 ms 配額之內。
7. **PowerPC 暫存器改成區域變數**（`scripts/localize_registers.py`）：見下。

### 暫存器區域變數

XenonRecomp 能把 CR、CTR、XER、保留值，以及呼叫之間不必保留的（r0、r2、r11、r12、f0、v32–v63）和由被呼叫者
保存的（r14–r31、f14–f31、v14–v31、v64–v127）暫存器輸出成每個函式自己的區域變數
（`cr_as_local`…`non_volatile_as_local`，Unleashed Recompiled 全部開啟）；本專案全部關閉。欄位是記憶體，
這個建置沒有 strict aliasing，遊戲每一次寫入記憶體之後編譯器都得重新讀回；區域變數可以留在主機暫存器裡。

`localize_registers.py` 對 `generate_diagnostic.py` 已經寫好的程式做同樣的事，作者的改寫與檢查一個都不動：
每個函式的 `ctx.<暫存器>` 改成區域變數，只把暫存器從堆疊搬回 context 的 `__rest*` 輔助函式不再呼叫。
另外幾件 XenonRecomp 的選項只憑呼叫慣例相信的事，這裡照顧到：

- **寫入前就先讀的暫存器是輸入**（不算序言把被呼叫者保存暫存器存到堆疊指標下方）：例如堆疊探測這類
  輔助函式，或被 XenonRecomp 切成兩段的函式的後段（沿用前段的 r31、測前段的比較結果）。它在那個函式
  裡留在 context；呼叫它的函式（直接呼叫，或經過完全沒碰那個暫存器的函式）在呼叫前把區域變數存回
  context、呼叫後再取回。
- **主機端程式會讀呼叫者的 r14–r31**（hook、import 看 r20–r31 判斷情境）：呼叫 hook、import、間接
  呼叫之前先把區域的 r14–r31 存回 context。`synchronize_resource_memory` 還會讀兩個堆疊位置，所以呼叫
  它的函式整個不轉換；顯示裝置全域變數（0x82000664）的處理程式在 `sub_824F19E8` 執行中讀它的 r31 與
  堆疊上的連結暫存器，這個函式也不轉換。條件儲存之後把 cr0 從 context 取回。
- **沒轉換的函式仍把暫存器放在 context**：它呼叫的已轉換函式若為了 hook 或輸入把區域的被呼叫者保存
  暫存器存進 context，返回時（`SfrRestore` 的解構子）把進入時的值放回去，呼叫者看到的跟呼叫前一樣。
- **區域變數的初值取自 context**：分析沒看到的路徑（跳過第一次寫入）讀到的仍是 context 的值，序言存上
  堆疊的也是呼叫者的值；每條路徑都先寫的，編譯器會把這次讀取刪掉。
- **`__save*` 照舊呼叫**（先把 r12、r11 存回 context）：堆疊上仍有被呼叫者保存暫存器和連結暫存器，
  切開的後段裡留下的 `__rest*`、讀堆疊的主機端程式都拿得到正確的值。

第一版（commit 377c293）少了後三項，遊戲開機就停在 `STOP memory-access @0x0`：沒轉換的函式（例如
呼叫 `synchronize_resource_memory` 的，或 r31 被判定為輸入的）呼叫已轉換的函式後，自己的 r14–r31 已經
被換成對方的值。`tests/test_localize_registers.py` 的
`test_callers_find_their_registers_as_they_left_them` 實際編譯執行這個情況。

修正後（commit ad06b57）開機流程與原本逐行相同，直到作者的「未選取使用者」稽核：`XamUserGetSigninState`
這個 import 讀的是再上一層函式的 r30／r31（使用者紀錄與管理者），中間那層完全沒碰這兩個暫存器，上層
就沒有在呼叫前存回 context。現在這類主機端讀取列在 `HOST_INPUTS`（兩個 import、顯示裝置全域變數，以及
`SFR_DIAGNOSTIC_ENTRIES=1` 才開的幾個函式進入點稽核），算成那個函式的輸入，往上傳到真正持有值的函式。

接著（commit 7a22eee）開機完成、進了選單，約 3400 格後同一個稽核誤報：它以「進入 `__restgprlr_27`
（0x82A560B4，名稱轉換返回後的函式結尾）」當作結束的訊號，這個呼叫被刪掉了，稽核一直沒關。
`KEPT_HELPERS` 讓它照舊呼叫。`SFR_DIAGNOSTIC_ENTRIES=1` 的稽核另外還看其他幾個 `__rest*` 的進入，
仍不支援。

用法（不必重跑 XenonRecomp）：

```powershell
py scripts\localize_registers.py out\recomp\diagnostic out\recomp\diagnostic-local
.\scripts\build_tools.ps1 -Diagnostic -DiagnosticDirectory out/recomp/diagnostic-local
```

改回原本的程式碼：用 `-DiagnosticDirectory out/recomp/diagnostic` 再建置一次。

## 2026-09-30：第一批改動加暫存器區域變數的結果（commit `bc9af04`）

同一台 i7-6850K／RTX 3080 Ti，完整跑進比賽，每種設定兩趟：

| | 平均 fps | 中位數 ms | P95 ms | 主執行緒持有 ms |
| --- | ---: | ---: | ---: | ---: |
| 改動前（commit `a2776cb`，20260930-042938） | 25.8 | 36.9 | 54.0 | 約 28.4 |
| 改動後 baseline | 31.6 | 32.5 | 41.0 | 25.8 |
| 改動後 strict-memory（`SFR_STRICT_MEMORY=1`） | 30.6 | 33.7 | 43.2 | 26.5 |

- 平均 **+22%**，P95 從 54 ms 降到 41 ms，卡頓明顯變少。這是第一批（記憶體與向量快速路徑、平坦的
  間接呼叫表、檢查點間隔）與暫存器區域變數合起來的效果，這次沒有分開量。
- 一般儲存略過保留／釘住檢查（非 strict）約值 3%。
- 剖析：遊戲生成碼 37.0%、執行檔以外 29.6%、畫圖 17.4%、客體記憶體 5.8%。剩下的 `__savegprlr_*`
  約 1.3%，`load/store_vector_memory` 約 2.4%，`native_draw` 3.7%。

## 渲染執行緒（階段 C）

`NativeRenderer::draw` 原本在主執行緒算 pipeline key、把常數複製進上傳環，
再把 `setPipeline`、描述符、`drawIndexedInstanced` 這些呼叫錄進命令列表
（`record_ms`，約 1.5 ms／格）。現在最後一步交給另一條執行緒：

- `NativePresentation::record_async` 把一個只擷取「值」的 lambda 複製進固定大小
  （256 位元組）的佇列槽，連同當時的視埠／裁切與 `list_generation`；
  佇列容量 4096，單一生產者、單一消費者，不配置記憶體。
- 其他會碰命令列表的動作（`clear`、`record`、`present`、`flush`、開新列表、
  `draw_player_model`、解構）都先 `drain()` 等佇列清空，所以指令順序不變。
- 上傳環、紋理上傳與 pipeline 建立仍在主執行緒；GPU 看到的內容和順序與以前相同。
- 渲染執行緒出錯時例外留到下一次 `record_async` 或 `drain()` 在主執行緒重新丟出。
- `SFR_RENDER_THREAD=0` 關閉（同步執行，行為與舊版相同）；benchmark 用
  `no-render-thread` 做 A/B。

佇列的睡眠／喚醒邏輯抽出來在 Linux 上壓測（200 輪 × 20000 筆，穿插
`drain()` 與睡眠，g++ 執行緒檢查器），順序與筆數正確、沒有死結。
真正的 D3D12／Vulkan 錄製路徑這裡沒有 GPU，只做過編譯，需要在 PC 上跑。

## 預先建立 pipeline（階段 D）：改用作者的版本

我們自己做的 manifest 預建（commit `3872cb4`）在 2026-10-01 合併作者的 v0.4.3 時拿掉了：
作者的版本做了同一件事，而且更完整（開機時的進度條與取消、打包進發行版、已錄好的
`data/pipeline-manifests/` 清單、Android），見 `docs/pipeline-preparation.md`。
環境變數 `SFR_PIPELINE_PREWARM=0`（benchmark 的 `no-prewarm`）在作者的版本裡同樣有效。

留下的觀察：本 PC 上舊版（鍵含 blend／stencil 的 padding）一場比賽會建 5,000～7,000
個 pipeline，作者修正鍵之後整份清單只有約 280（D3D12）～336（Vulkan）筆，所以先前那個數字
多半是同樣狀態被重複建立。

## 量測雜訊（2026-09-30）

這一天有一次不小心量到三個「設定」其實是同一個執行檔（編譯失敗、舊檔留著，
環境變數沒有作用）：平均 fps 27.2、19.1、26.3，P95 51～124 ms，主執行緒
`draw_ms` 7.6～10.8。**同一個程式在這台機器上可以差 30%**，所以：

- 兩趟之間差幾個百分點不能當成改善或退步；至少重複 6 次、輪流跑、PC 閒置。
- 比較時看每趟的分布（P95、超過 80 ms 的格數）比只看平均可靠，但同樣要重複。
- 這份對照是 eGPU（Thunderbolt）＋ i7-6850K，別的機器的雜訊不同。
- `benchmark.ps1` 現在會在執行檔比原始碼舊時停下來，避免再把舊執行檔當成新的量。

## 把結果給別人（去識別化）

`benchmark.ps1` 跑完會多做一份 `<資料夾>-shareable` 和 `.zip`（`scripts/anonymize_benchmark.py`），
原資料夾不動。**原資料夾裡的 `info.txt` 和 log 不是去識別化的**：裡面有 Windows 使用者名稱
（每個路徑 `C:\Users\<名稱>\…`，包含 `info.txt` 的 `generated=`）、語言與國家
（`NATIVE_USER_LANGUAGE`／`NATIVE_USER_COUNTRY`）、存檔設定檔的識別碼資料夾名稱，
以及 10 張遊戲畫面截圖。給別人的請用 `-shareable.zip`：路徑中的使用者名稱變成 `USER`，
`generated=` 移除，語言與國家的值清掉，設定檔識別碼變成 `E000XXXXXXXXXXXX`，電腦名稱與
使用者名稱（環境變數）變成 `XXXX`，截圖不放進去（`--keep-screenshots` 才保留）。

保留的：commit、CPU 與 GPU 名稱、Windows 版本、所有計時，因為比較機器需要它們。
CPU 與 GPU 型號本身仍然說明你用什麼硬體；送出前請自己打開 `info.txt` 和一份 log 看過，
這個工具只拿掉它知道要找的東西，不保證沒有其他能識別你的內容。
單獨用法：`py scripts\anonymize_benchmark.py out\bench\20260930-151014 --also 電腦名稱`。

## 在別台機器跑（不用 git、不用編譯）

在編好的那台：

```powershell
.\scripts\make_benchmark_kit.ps1 -IncludeGame
```

它會在 repo 旁邊做出 `sfr-benchmark-kit` 資料夾和 `.zip`，只含 benchmark 需要的東西：
`run_benchmark.bat`、四個腳本、`out\build\host`（執行檔，不含 `settings.ini`、`.pdb` 等）、
`out\shaders`（著色器包，內容來自你的遊戲，所以不能散發）、`data\pipeline-manifests`，
加上 `-IncludeGame` 時你自己的遊戲影像與素材（很大）。不加 `-IncludeGame` 的話，另一台要有自己的遊戲
資料夾，`run_benchmark.bat` 會問它們在哪。

在另一台：解壓縮，雙擊 `run_benchmark.bat`（`run_benchmark.bat 6` 可改成每種設定 6 趟）。
需要 Windows 10 22H2 以上；`py`（Python）有的話會自動產生摘要與去識別化版本。
跑完它會開啟資料夾並選取 `out\bench\<時間>-shareable.zip`，把這個檔案傳回來。
每個檔案不超過 29 MB（`anonymize_benchmark.py --limit-mb` 可改）；結果放不下時分成 `<時間>-shareable-part1of3.zip` 這樣的幾份，
整份（所有 part）傳回來，解壓縮到同一個資料夾即可。單一檔案本身超過上限時切成 `名稱.001`、`名稱.002`，用 `copy /b` 接回。

`benchmark.ps1` 現在會把 `data\pipeline-manifests` 裡作者附的清單當成「玩家拿到的清單」使用
（`SFR_PIPELINE_MANIFEST`；如果 `out\shaders` 旁邊已經有同名清單就用那份），所以 `baseline` 與
`no-prewarm` 的差別是作者的預建有沒有開。

## 2026-10-01：ROG Flow Z13-KJP（Ryzen AI MAX+ 395／Radeon 8060S）／Windows 11 的第一次 `speed` 結果

機器：ASUS ROG Flow Z13-KJP（平板形筆電）：AMD Ryzen AI MAX+ 395、Radeon 8060S（內顯，與 CPU 共用記憶體；
驅動 32.0.31032.1003）、Windows 11（26200）、接電、電源計畫 Turbo、開始前背景 CPU 6%。**結果只代表接電＋Turbo；
筆電的散熱與功耗限制會隨電源模式改變，其他模式沒有量。**D3D12，不封頂。三種設定輪流跑 6 輪，**19 趟裡 8 趟沒有走進比賽**
（選單腳本太早送出指令，見下），所以每種設定只有 3～4 趟可用。計畫見 `docs/benchmark-plan-2026-10-01.md`。

| 設定（可用趟數） | 平均 fps | P95 ms | 主執行緒持有 ms | `record_ms` |
| --- | ---: | ---: | ---: | ---: |
| baseline（4） | 94.7–97.1 | 15.6–16.0 | 6.9–7.3 | 0.39–0.40 |
| no-render-thread（3） | 93.1–100.7 | 15.0–16.4 | 6.9–7.7 | 0.49–0.53 |
| no-suspend-notify（4） | 82.5–88.4 | 15.3–17.0 | 7.8–8.5 | 0.42–0.47 |

- **這台（Z13）一格約 11.8 ms，約 95 fps，遠高於 60**；主執行緒只持有約 7 ms（i7-6850K 上約 26–28 ms），
  繪製只要約 1.8 ms（i7 上約 7.5 ms）。
- **渲染執行緒（C）：量不到差別**（95.7 對 95.7 fps）。這台的錄製成本本來就小（`record_ms` 只有
  0.5 ms，開渲染執行緒降到 0.4），能搬走的只有約 0.1 ms，不到一格的 1%。C 的價值要在主執行緒更慢的
  機器（i7）上看，這台不能證明也不能否定。
- **暫停通知（作者）：這台上通知比輪詢快約 9%**。baseline 的四趟（94.7–97.1）與 no-suspend-notify 的
  四趟（82.5–88.4）完全不重疊；四對四全部同方向的機率是 1/70（約 1.4%）；成對的三輪都是輪詢較慢
  （0.85、0.90、0.92）。`summary.md` 因為成對輪數不足 4 而沒有下判定，這裡是用不成對的檢定補上；
  樣本仍小，需要再多幾趟。`main_blocked_ms` 從 1.5 升到 1.9、主執行緒持有 7.1 升到 8.0 ms，方向
  與機制（輪詢最多多等 1 ms）一致。
- baseline 自己各趟只差 2%，這台雜訊很小，比 i7 那台乾淨。
- 比賽中仍有 1～4 格因現場建 pipeline 而超過 30 ms（`編譯卡頓格`），每場現場建約 210–232 個 pipeline；
  這份沒有著色器包，作者的預建清單全部過期，所以沒有預建。

### 為什麼 8 趟沒有走進比賽

選單腳本（`SFR_SAY`）是用「第幾次 present」排時間，但選單回應指令要等載入與動畫，那些是實際秒數。這台的選單
每格極快（15600 格在沒走進比賽的 19 秒內就跑完），走進比賽的趟與沒走進的趟，各步驟的實際秒數差約 1～2 秒，
所以腳本有時早一步。現在多了 `SFR_SAY_MIN_SECONDS`（benchmark 設為 3）：每個字至少距離上一個 3 秒才說。
在 i7（約每步 13 秒）上不會有任何影響。**這個改動要重新編譯。**

更新已經做好的 kit（只換程式與腳本，不重送遊戲）：在編好的那台執行
`.\scripts\make_benchmark_kit.ps1 -UpdateOnly -Destination X:\sfr-benchmark-kit`，
它直接把有變動的檔案（比對內容，相同的不動）複製進那個 kit 資料夾，不產生壓縮包，也不刪任何東西
（著色器包、DXC、遊戲、舊結果與 `save` 都不會動），結尾列出這次換了哪些檔案。目的資料夾必須已經存在；
還沒有 kit 的機器才用不加 `-UpdateOnly` 的完整版。
`make_benchmark_kit.ps1` 會在結尾列出這個 kit 該有的東西哪些在、哪些缺（`ok`／`MISSING`／`absent`），
並且不會刪除含有 `out\bench` 結果的資料夾。

## 選單腳本依 FPS 等比例拉長（已撤回）

試過讓選單字依 FPS 等比例拉長（`SFR_SAY_REFERENCE_FPS`、`SFR_PRESENT_LIMIT_AFTER_SAY`），
benchmark 現在回到原本的做法：字依 present 次數說、至少間隔 3 秒，`-PresentLimit`（預設 15600）結束。
這兩個環境變數還留在程式裡，需要時可手動設定，benchmark 不再使用。

### 兩種沒進比賽的原因（i7、i5 的 log）與 `-Stretch`

每一格的 `frame_ms` 加起來顯示：標題載入時，PC 每秒送出上百格（i5 的 baseline 15600 格只用了約 27 秒），
載入卻要幾秒的真實時間。所以：

- i7：17 個字照 present 排程全說了，但前幾個 `ok` 說得太早，標題還沒在聽（沒有出現兩個對話框，`box=0`），後面全錯位。
- i5：標題要到第 11000～15000 格才開始聽，字才說到一半，第 15600 格的上限就到了（說了 6～8 個字，`box=2`）。

`-Stretch`（現在是預設；`run_benchmark.bat speed 2 nostretch` 或 `-NoStretch` 關閉）同時處理兩者：每個字等 P/100 秒才說（第一個字約 22 秒，整串約 2 分鐘；`-ReferenceFps` 可調），說完最後一個字後再跑 4200 格才結束
（15600 格的上限只留作 60000 的保險）。
快的 PC 一趟 15600 格只要十幾秒，而每個字至少隔 3 秒、共 17 個字要 51 秒以上，所以按格數結束的舊做法在那種 PC 上不可能走完。這需要程式裡有 `SFR_PRESENT_LIMIT_AFTER_SAY`（8140e9b 之後編的），
舊的 exe 會被擋下。

開頭三個 `ok` 提早為 1750／2150／2550（原 2200／2600／3000）：i5 實測標題在第 15.4～17.3 秒就緒，
`-ReferenceFps 100` 時第一個字約在第 17.5 秒說出，而不是 22 秒。三個 `ok` 互為備援。

### 選單腳本重排（約 63 秒）

i5 warmup 實測每一步都被 3 秒的最小間隔卡住，從 `start` 到比賽要 40 秒；i7 較早的 log 顯示 `right` 每 1.5 秒也有反應。
現在最小間隔 2 秒，字的時間點（倍率 100，單位 present＝百分之一秒）：`ok` 1750／2100／2450、`start` 2800、`ok` 3000、
`right` 3200～4000（每 2 秒）、`ok` 4200、再 7 個 `ok` 每 3 秒（4500～6300）。整串約 63 秒，原本 114 秒。
若選單開始漏字，把 `SFR_SAY_MIN_SECONDS` 調回 3。

### 主選單改用 `left`（待驗證）

主選單環有六項，Free Race 是第六個（`docs/race-controls.md`），環會繞回，所以從 World Grand Prix 說一次 `left` 就到，
不必 `right` 五次。腳本：`ok@1750,ok@2100,ok@2450,start@2800,ok@3000,left@3200,ok@3400,ok@3700…ok@5200`（約 52 秒）。
**這只是從文件推的**：要看 warmup 的 log 在 `left` 之後有沒有 `NUI_MENU_FADE`，並且最後有進比賽。
不行的話換回 `right@3200,3400,3600,3800,4000` 再接 `ok@4200,ok@4500…ok@6300`。

### 選單腳本（使用者調整後，約 49 秒）

`ok@1750,ok@2100,ok@2450,start@2800,ok@3000,left@3200,ok@3400,ok@3700,ok@4000,ok@4300,right@4600,ok@4900`：
在角色與裝備之間多一個 `right`，並去掉最後兩個 `ok`。

### 標題等待縮成三分之一（約 37 秒）

`-ReferenceFps` 的計時起點是第一次讀輸入的那一刻（約標題就緒），不是程式啟動。所以原本 17.5 秒的第一個字是「就緒後」再等的。
改為：`ok@580,ok@930,ok@1280,start@1630,ok@1830,left@2030,ok@2230,ok@2530,ok@2830,ok@3130,right@3430,ok@3730`，
字與字的間隔不變，整串約 37 秒。若開頭的 `ok` 被吃掉（沒有 `NUI_MESSAGE_BOX`），把前三個 `ok` 往後挪。

## 2026-10-02：i5-3470／RX 480 的 `ab` 與 `fast`（commit `d253c59`，合併 v0.4.5 之後）

同一台 i5，每種設定 6 趟輪流跑，每趟 3468 個比賽格。上傳時 `fast` 的結果分成兩個檔案（zip 與 7z），合併後彙總。

**`ab`：plain 對 local（暫存器區域變數）**

| | 平均 fps | 主執行緒持有 ms | >50 ms 格 |
| --- | ---: | ---: | ---: |
| plain | 29.2 | 25.8 | 94 |
| local | 30.4 | 24.9 | 61 |

逐輪比值 1.07 1.02 1.05 1.04 1.07 0.99，中位數 1.045，6 輪中 5 輪較快（符號檢定 p 約 0.22，未達確立標準）。
區域變數在作者的 `__savegprlr` 快速路徑之後約 +4%，遠小於先前以剖析推估的份量。

**`fast`**

| 設定 | 平均 fps | 繪製 ms | 主執行緒持有 ms | >50 ms 格 | 逐輪比值（扣除 baseline 第 2 趟） |
| --- | ---: | ---: | ---: | ---: | --- |
| baseline | 30.6 | 5.3 | 24.7 | 68 | - |
| no-render-thread | 27.3 | 7.4 | 27.8 | 208 | 0.92 0.88 0.88 0.90 0.88 |
| no-suspend-notify | 29.6 | 5.5 | 25.6 | 68 | 0.96 0.97 1.00 0.94 0.94 |
| strict-memory | 29.9 | 5.3 | 25.2 | 68 | 0.97 0.97 0.97 0.98 0.97 |

- baseline 的第 2 趟是離群值（27.9 fps、P99 105 ms、`present_ms` 1.9，其他趟 0.3）。它讓第 2 輪的三個比值都偏高（1.01、1.09、1.08），
  所以上表扣掉那一輪；含它時 summary 把 no-suspend-notify 與 strict-memory 判為「在雜訊內」。
- **渲染執行緒**：關掉後 −11%，扣掉離群輪後 5 輪全同向，繪製時間從 5.3 ms 升到 7.4 ms，主執行緒持有從 24.7 升到 27.8 ms。
  機制與效果大小互相吻合，與前一次 i5 的 −10% 一致。
- **暫停通知**：關掉後約 −3%（比值 0.94–1.00），方向與 i7（+5%）、Z13（+9%）一致，但每台都小。
- **strict-memory**：關掉略過檢查約 −3%（比值 0.97 到 0.98，五輪幾乎相同），與 i7 單獨量到的約 3% 一致。

## 2026-10-02：i5 的 `paths`（x86 向量儲存、pipeline 沿用、檢查點間隔）

同一台 i5（commit `d253c59`），每種設定 6 趟輪流跑。結果同樣分成 zip 與 7z 兩個檔，合併後彙總。baseline 各趟差 5%。

| 設定 | 平均 fps | 相對 baseline | 逐輪比值 | 較快的輪數 |
| --- | ---: | ---: | --- | ---: |
| baseline | 30.2 | - | - | - |
| no-partial-stores | 30.7 | +2% | 1.02 1.01 1.03 0.99 1.04 1.02 | 5/6 |
| no-pipeline-reuse | 30.2 | 0% | 1.01 1.00 0.99 0.97 1.00 1.02 | 3/6 |
| checkpoint-32 | 28.6 | −5% | 0.97 0.99 0.93 0.92 0.94 0.94 | 0/6 |

- **檢查點間隔 256（對 32）**：換成 32 之後每一輪都較慢，中位數比值 0.94，主執行緒持有從 24.9 升到 26.4 ms 左右。
  這是 #3、#5、#6 裡唯一站得住的一項，約 +6%。後四輪比前兩輪更慢（0.92 到 0.94 對 0.97 到 0.99），可能有時間相關的飄移，但方向沒有一輪反過來。
- **x86 向量儲存快速路徑（#3）**：關掉它反而略快（5/6、+2%），在雜訊內。這項沒有收益，不送。
- **pipeline 沿用（#5）**：0%，不送。

## 2026-10-02：i7 確認檢查點間隔（commit `d253c59`）

i7-6850K／RTX 3080 Ti，`baseline`（間隔 256）對 `checkpoint-32` 各 6 趟輪流跑，baseline 各趟差 4%。

| 設定 | 平均 fps | 中位數 ms | 主執行緒持有 ms | >50 ms 格 | 逐輪比值 |
| --- | ---: | ---: | ---: | ---: | --- |
| baseline | 37.5 | 29.5 | 20.4 | 10 | - |
| checkpoint-32 | 34.9 | 30.9 | 21.6 | 16 | 0.91 0.99 0.93 0.94 0.91 0.97 |

改回 32 之後 6 輪全慢，中位數 0.94，與 i5（0.94，0/6）相同。檢查點間隔 256 在兩台機器上都約 +6%。
合併 v0.4.5 之後 i7 的 baseline 是 37.5 fps（合併前最近一次 35.4），開機與比賽都正常。

間隔放寬的代價：許可每 64 次呼叫才看一次時間，所以兩次看時間之間是 64 × 間隔次進入／迴圈標籤。
以每秒約一千萬次估計（沒有量過），間隔 256 約 1.6 ms，32 約 0.2 ms：交接時間的誤差從約 0.2 ms 變成約 1.6 ms，
仍在 2 ms 配額的同一個量級，而且是有上限的延遲，不是漏掉。取消與停止也是同樣的延遲。
下一個問題是更大的間隔還有沒有收益：`run_benchmark.bat ckpt` 比 256、1024、4096。

## 2026-10-02：ROG Flow Z13-KJP の `z13`（commit `9a73181`，合併 v0.4.5 之後）

AMD Ryzen AI MAX+ 395／Radeon 8060S／Windows 11，接電、電源計畫 Turbo；開始前背景 CPU 25%（偏高，各設定同樣承受）。
每種設定 6 趟輪流跑，每趟 3468 個比賽格，不限速（約 100 fps，每格約 10 ms）。baseline 各趟差 8%。結果分成 part1of2／part2of2 兩個檔，合併後彙總。

| 設定 | 平均 fps | 每格中位數 ms | 主執行緒持有 ms | 逐輪比值 | 較快的輪數 |
| --- | ---: | ---: | ---: | --- | ---: |
| baseline | 106.2 | 10.8 | 6.7 | - | - |
| no-suspend-notify | 95.8 | 11.0 | 7.5 | 0.90 0.95 0.91 0.92 0.88 0.87 | 0/6 |
| checkpoint-32 | 97.8 | 11.5 | 7.3 | 0.93 0.95 0.91 0.96 0.89 0.92 | 0/6 |
| no-render-thread | 104.8 | 10.9 | 6.9 | 1.01 1.01 1.01 0.98 0.99 0.99 | 3/6 |

- **暫停通知**：關掉後 6 輪全慢，中位數 0.91，約 +10%。
- **檢查點間隔 256**：改回 32 之後 6 輪全慢，中位數 0.93，約 +7%。
- **渲染執行緒**：比值 1.00，在雜訊內。這台沒有收益也沒有變慢，繪製時間 1.6 ms 對 1.8 ms，能搬走的只有約 0.1 ms。
- 每格約 10 ms，所以每省 1 ms 就是約 10%，這解釋了為什麼固定成本類的改動（通知、檢查點）在這台上的比例特別大。

## 三台機器的合計（合併 v0.4.5 之後）

| 改動 | i5-3470 | i7-6850K | Z13（Ryzen AI MAX+ 395） |
| --- | ---: | ---: | ---: |
| 渲染執行緒 | +11%（5/5 同向） | 約 0（前一次 −2%） | 0（1.00，3/6） |
| 檢查點間隔 256（對 32） | +6%（6/6） | +6%（6/6） | +7%（6/6） |
| 暫停通知 | +3%（扣掉離群輪後 4/5） | +5%（前一次，6 對 6） | +10%（6/6） |
| 略過保留／釘住檢查 | +3%（5/5） | 約 +3%（前一次，單獨量） | 未量 |
| 暫存器區域變數 | +4%（5/6） | 未量 | 未量 |

## 2026-10-02 evening: i5 `speed` again on the same v0.4.5-based build (commit `d253c59`)

Not the v0.4.6 render thread branch: `info.txt` shows `commit=d253c59` and the settings of the old `speed` mode,
so this is the same executable as the earlier i5 runs, measured once more. Six interleaved rounds, baseline spread 3%.

| Setting | Mean fps | Per-round ratios to baseline | Median | Faster rounds |
| --- | ---: | --- | ---: | ---: |
| baseline | 30.5 | - | - | - |
| render thread off | 27.4 | 0.87 0.90 0.89 0.89 0.90 0.91 | 0.89 | 0/6 |
| suspend notification off | 29.9 | 0.95 0.99 0.96 0.95 0.99 0.99 | 0.98 | 0/6, inside the noise |

Third time the i5 shows the render thread at -10% to -11% when it is off, and this run has no outlier round. The suspend
notification is about 2% here (earlier runs on this PC: 3-4%), so on the i5 it stays a small effect.
