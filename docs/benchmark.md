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
