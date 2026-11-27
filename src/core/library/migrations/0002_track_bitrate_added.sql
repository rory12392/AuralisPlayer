-- 0002：曲目的位元率與加入時間（第二輪，AC-10-5）。只加欄位與索引，不改既有資料；
-- 既有列的值由背景補值工作填（中斷後下次啟動接著做），和這個升級分開。

-- kbps 整數；未知時 NULL
ALTER TABLE tracks ADD COLUMN bitrate INTEGER;
-- 第一次收錄進音樂庫的時間（UTC Unix 毫秒，和 library_roots.added_at 一致）；掃描只在新增時寫
ALTER TABLE tracks ADD COLUMN added_at INTEGER;
-- 1＝bitrate、added_at 還沒補（升級前就在的列），0＝已處理（NULL 代表確實未知）。
-- 預設 1 讓既有列不用 UPDATE 就是「待補」；掃描新增的列明確寫 0
ALTER TABLE tracks ADD COLUMN meta_backfill INTEGER NOT NULL DEFAULT 1;

CREATE INDEX tracks_added_at ON tracks(added_at);
