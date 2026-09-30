# Roadmap

## 首次啟動體驗：讓第一次啟動的玩家也流暢（比照 Unleashed Recomp）

目標：玩家第一次啟動就不會在遊戲裡頓卡。做法和 Unleashed 相同：發行版附上
「要建哪些 pipeline」的清單，第一次啟動時在載入畫面把需要的東西全部準備好，
進遊戲時已經是熱的。

現況：
- `src/native_pipeline_cache.cpp` 的磁碟 cache 只有 Vulkan 有，D3D12 沒有。
- 已實作（commit 3872cb4）：執行時記錄每條 pipeline 到
  `pipeline-cache/pipelines-<backend>.bin`，下次啟動在背景預先建立
  （細節見 `docs/benchmark.md`「階段 D」）。只對第二次以後的啟動有效。

要補的三件事，缺一不可：
1. **覆蓋率錄製**：讓遊戲自己把所有場景都走過一遍，產出一份完整的 manifest。
2. **隨發行版附上 manifest**：內容只有著色器雜湊與狀態，不含遊戲資料，可以放進 repo。
3. **第一次啟動的準備畫面**：在啟動器顯示進度，並行做完兩件事再進遊戲：
   a. 著色器轉譯：目前是遊戲載入時才即時轉譯並用 DXC 編譯（`runtime_shader_cache`）。
      轉譯結果來自玩家自己的遊戲檔案，**不能隨發行版附上**，只能在玩家機器上產生，
      所以要移到準備畫面，不能留到遊戲中。
   b. pipeline：依 manifest 全部建好。

技術注意：
- manifest 以著色器 container 的雜湊（`shader_identity`）對應，需驗證不同機器、
  不同遊戲版本（地區）下一致；不一致的記錄直接略過，不影響正確性。
- 記錄的格式與 pipeline 的鍵一旦改動，舊 manifest 就要重錄，所以要等格式穩定、
  階段 D 在 PC 上驗證沒問題之後再錄。
- 不同 GPU 的驅動各自有 pipeline 編譯快取；manifest 與 GPU 無關，換 GPU 只是
  編譯時間不同。

### 錄製計畫與時程

| 步驟 | 內容 | 誰做、用什麼 | 建議時間 |
| --- | --- | --- | --- |
| 1 | 階段 C、D 的 PC 測試（已交給你） | 你，跑 benchmark | 10/1 |
| 2 | 寫 `scripts/record_pipelines.ps1`（自動走完多個賽道／角色／裝備／選單）與合併工具 `scripts/merge_pipeline_manifest.py`（合併去重、剔除失效記錄） | Claude（Sonnet 即可，範圍小、可在這裡測試合併邏輯） | 10/1 |
| 3 | 在你的 PC 上無人值守錄製：`record_pipelines.ps1` 連跑數小時，不用另開 Claude Code | 你只需按下執行，建議睡前跑 | 10/1 晚上 |
| 4 | 補錄腳本走不到的場景（多人、結算畫面、Kinect 選單）：自己玩一段，遊戲照樣記錄 | 你，約 30 分鐘 | 10/2 |
| 5 | 合併、檢查覆蓋率（以「新玩家第一場比賽中還需要當場建的 pipeline 數」為準，目標 0），放進 `packaging/` | Claude | 10/2 之後 |
| 6 | 啟動器的準備畫面與首次啟動流程 | Claude（這是設計較多的部分，10/2 之後用較強的模型） | 10/2 之後 |

選 10/1 晚上錄製的理由：錄製本身不消耗 token（在你的 PC 上跑），
但要等步驟 1 確認階段 D 的記錄與預建沒問題、格式定案；
腳本與合併工具是小工作，節約期間用 Sonnet 做就夠。
準備畫面屬於架構性工作，放到 10/2 之後再做。

## 效能（ADCB）

- A 記憶體快速路徑：完成
- D pipeline 預先編譯：已實作（第二次啟動起生效），待 PC 量測
- C 渲染執行緒：已實作，待 PC 量測（預估每格省 1.5–2.5 ms；`no-render-thread` 可對照）
- B 真平行客體：待辦，風險最高
- 刪減不必要的 `__savegprlr` 呼叫：待辦（約 1%）

詳細數據見 `docs/benchmark.md`。
