# Roadmap

## 首次啟動體驗：pipeline 預先編譯

第一次啟動時沒有磁碟上的 pipeline cache，每遇到新畫面都要當場編譯，造成頓一下。
這是新使用者對遊戲的第一印象，所以列為優先項目。

現況：`src/native_pipeline_cache.cpp` 已經會載入並儲存 `pipeline-cache/vulkan.bin`，
所以第二次以後啟動大多命中快取。尚未解決的是「第一次」。

進度：
1. 已實作：manifest 記錄 + 背景預先建立（`docs/benchmark.md` 的「階段 D」）；
   第二次啟動起生效，待 PC 量測冷／熱兩次的差異（`no-prewarm` 可對照）。
2. 待辦：隨發行版附上 manifest（只有著色器雜湊與狀態，不含遊戲資料），
   讓第一次啟動也受惠；需要確認雜湊在不同機器上一致。
3. 待辦：首次啟動時在啟動器顯示進度，避免看起來像當機。

## 效能（ADCB）

- A 記憶體快速路徑：完成
- D pipeline 預先編譯：已實作（第二次啟動起生效），待 PC 量測
- C 渲染執行緒：已實作，待 PC 量測（預估每格省 1.5–2.5 ms；`no-render-thread` 可對照）
- B 真平行客體：待辦，風險最高
- 刪減不必要的 `__savegprlr` 呼叫：待辦（約 1%）

詳細數據見 `docs/benchmark.md`。
