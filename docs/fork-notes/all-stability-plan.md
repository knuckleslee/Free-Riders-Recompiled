# `all`（客體執行緒全部平行）的穩定性驗證

`all` 在 i5 量到約 +18%（`benchmark-log-zh.md` 2026-10-03 `par`）。擋住它的是 2026-09-22 的 R6025：
Grand Prix 載入時，工作分派 `823B5D40` 在等待逾時後重用了工作項目（`docs/job-dispatch.md`），
以及條件寫入的 ABA（`docs/reservations.md`）；兩者之後都修了。當時關掉新執行緒 2 ms 的啟動延遲
（`SFR_THREAD_START_DELAY_US=0`）就能穩定重現（8 次裡 2 次停住）。

## 測什麼

- `run_benchmark.bat stab`：`all` 與 `all-stress`（再加上延遲為 0、60 秒沒有新畫面就印出每條執行緒在等什麼）各 10 趟短比賽
  （`-AfterSay 1500`）。每趟都完整載入一次賽道，走同一個工作分派。summary 的「結束」欄與沒跑完的趟數就是結果。
- `gp-all`：只用 `ok` 進主選單第一項 World Grand Prix。後面的畫面沒有記錄，能不能進到比賽要先在 i7 用一趟確認
  （截圖：`-ScreenshotEvery 1500`）。進得去的話再加進 `stab`。

## 判讀

- 20 趟全部跑完、沒有 `HANG_` 行：在這個情境下沒有找到問題。這仍不是證明，Grand Prix 與其他模式另外測。
- 有趟數沒跑完：看那趟 log 的最後幾十行與 `HANG_PERMIT`／`HANG_THREAD`（只留在本機，含遊戲位址），
  只回報停在哪一類（import、hook、等待）。
