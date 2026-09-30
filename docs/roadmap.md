# Roadmap

## 首次啟動體驗：pipeline 預先編譯

第一次啟動時沒有磁碟上的 pipeline cache，每遇到新畫面都要當場編譯，造成頓一下。
這是新使用者對遊戲的第一印象，所以列為優先項目。

現況：`src/native_pipeline_cache.cpp` 已經會載入並儲存 `pipeline-cache/vulkan.bin`，
所以第二次以後啟動大多命中快取。尚未解決的是「第一次」。

待辦：
1. 量測：刪掉 `pipeline-cache/` 後跑一次，記錄新建 pipeline 的次數與卡頓（P99）。
2. 預先編譯：在載入畫面或背景執行緒中，依已知的 pipeline key 清單先建好。
3. 隨發行版附上針對常見 GPU 可用的 key 清單（不含遊戲資料，只有 key）。
4. 首次啟動時在啟動器顯示進度，避免看起來像當機。

## 效能（ADCB）

- A 記憶體快速路徑：完成
- D pipeline 預先編譯：見上，已列入
- C 渲染執行緒：待辦（預估每格省 1.5–2.5 ms）
- B 真平行客體：待辦，風險最高
- 刪減不必要的 `__savegprlr` 呼叫：待辦（約 1%）

詳細數據見 `docs/benchmark.md`。
