# Roadmap

## 首次啟動體驗：pipeline 預先準備（已由作者的 v0.4.3 完成）

作者在 v0.4.3 做了「按下 Play 後、第一格之前先準備已知的 pipeline」：進度條、可取消、
已錄好的清單（`data/pipeline-manifests/`）隨發行版附上、沒涵蓋到的組合照常即時編譯並記錄，
見 `docs/pipeline-preparation.md`。我們自己的版本（commit `3872cb4`）已拿掉，改用作者的。

還要做的：
1. 在本 PC（D3D12）量測作者版的效果：乾淨快取的第一次啟動、第二次啟動，各看
   `PIPELINE_PREWARM` 的完成與命中數，以及比賽中剩下的 `pipelines`／`pipeline_ms`。
2. 清單的涵蓋率（其他關卡、角色、道具、雙人）：作者清單目前來自單人比賽與選單，
   他自己也寫明不是完整涵蓋。要補錄時照他的文件：用同一份發行版著色器包玩過、
   正常結束、把 `pipeline-cache/<backend>.manifest` 複製成 `pipelines-<backend>.manifest`。
3. 沒涵蓋到的組合仍會在那個 draw 即時編譯而卡一下。Unleashed 式的「先略過、背景建好再畫」
   作者沒做，要不要加取決於涵蓋率補完後還剩多少卡頓。

驗收標準（我們自己訂的）：用乾淨安裝＋隨發行版的清單跑一場完整的 Free Race，
「因當場建 pipeline 而超過 30 ms 的格」每場 0 格、最多 1～2 格。依據：舊版沒有清單時，
一場約 8 格因編譯花 87～91 ms（整格 150～180 ms，2026-09-30、i7-6850K／D3D12）。
`benchmark_summary.py` 之後要補這個欄位。

## 效能（ADCB）

- A 記憶體快速路徑：完成
- D pipeline 預先編譯：作者的 v0.4.3 已完成，我們的版本已拿掉；待 PC 量測
- C 渲染執行緒：已實作，待 PC 量測（預估每格省 1.5–2.5 ms；`no-render-thread` 可對照）
- B 真平行客體：待辦，風險最高
- 刪減不必要的 `__savegprlr` 呼叫：待辦（約 1%）

詳細數據見 `docs/benchmark.md`。
