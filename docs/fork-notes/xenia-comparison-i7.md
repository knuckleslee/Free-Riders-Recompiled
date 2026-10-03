# i7 上與 Xenia 對照

目的：同一台 i7、同一場比賽，看 Xenia 與本移植的差別在哪裡：是一條執行緒滿載但比較快
（繪圖翻譯不在主執行緒），還是負載分散到幾條執行緒（客體執行緒真的平行）。

## 條件

- 同一場自由比賽：同賽道、同角色、同裝備，無人操作。
- 都是 1280×720 視窗。
- Xenia 關掉垂直同步與影格上限：在 `xenia-canary.config.toml` 裡找 `vsync` 和 `framerate_limit`，
  設成 `false` 和 `0`。版本不同，名稱可能不同。如果關不掉，就記下它鎖在多少 fps。
- 量測時不開工作管理員，也不切換視窗。

## 步驟（i7，PowerShell，在 repo 目錄）

1. 開 Xenia，進比賽，倒數結束後在 PowerShell 執行：
   `.\scripts\thread_load.ps1 -ProcessName xenia_canary -Label xenia -Delay 5 -Seconds 60`
   （先按 Enter 叫出指令，再切回遊戲；5 秒後開始取樣。）
2. 用啟動器開本移植，進同一場比賽，倒數結束後：
   `.\scripts\thread_load.ps1 -ProcessName sfr_cpu_diagnostic -Label port -Delay 5 -Seconds 60`
   本移植的 fps 用量測腳本另外量：`.\scripts\benchmark.ps1 -Configs baseline -Repeats 3`。
3. 結果在 `out\thread-load\<時間>-xenia` 與 `-port`，各有 `summary.md`、`threads.csv`、
   `cores.csv`、`titles.txt`。內容不含電腦名稱與使用者名稱，可以直接傳。

## 判讀

| Xenia 的情況 | 代表 | 對本移植的改法 |
| --- | --- | --- |
| 一條執行緒約 100%，fps 比本移植高 | 主執行緒做的事比較少 | 把繪圖翻譯整段移出主執行緒 |
| 兩三條執行緒都很忙，沒有一條滿載 | 客體執行緒真的平行 | 檢查許可串行化；`all` 模式當掉的原因在主機端 |
| fps 和本移植差不多 | 「順」來自影格上限或畫面感受 | 先處理卡頓格（超過 50 ms 的格數） |

Xenia 的執行緒沒有名字可讀，只能看忙碌程度的分布。
