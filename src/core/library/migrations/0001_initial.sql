-- 0001：初始資料表（核心擁有＋模組擁有）。已發佈後不再修改，改動一律新增下一個編號。
-- 曲目身分是 (path, subsong)；path_key 是比對用的鍵（去 \\?\、分隔符統一為 \、NFC、大小寫折疊）。
-- 時間一律存 Unix 毫秒（INTEGER）。

-- ===== 核心擁有 =====

CREATE TABLE library_roots (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL,
    path_key TEXT NOT NULL UNIQUE,
    added_at INTEGER NOT NULL
);

CREATE TABLE folders (
    id INTEGER PRIMARY KEY,
    root_id INTEGER NOT NULL REFERENCES library_roots(id) ON DELETE CASCADE,
    parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE,
    path TEXT NOT NULL,
    path_key TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL
);
CREATE INDEX folders_parent ON folders(parent_id);

CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    folder_id INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE,
    path TEXT NOT NULL,
    path_key TEXT NOT NULL UNIQUE,
    size INTEGER NOT NULL,
    mtime_ns INTEGER NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('ok', 'unreadable', 'unsupported')),
    error TEXT,
    scanned_at INTEGER NOT NULL
);
CREATE INDEX files_folder ON files(folder_id);

CREATE TABLE albums (
    id INTEGER PRIMARY KEY,
    album_artist TEXT NOT NULL,
    title TEXT NOT NULL,
    year INTEGER,
    folder_id INTEGER REFERENCES folders(id) ON DELETE SET NULL,
    cover_key TEXT,
    track_count INTEGER NOT NULL DEFAULT 0,
    UNIQUE (album_artist, title, folder_id)
);

CREATE TABLE tracks (
    id INTEGER PRIMARY KEY,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    subsong INTEGER NOT NULL DEFAULT 0,
    -- CUE 區段（1/75 秒）；整個檔案為 NULL
    start_cdframe INTEGER,
    end_cdframe INTEGER,
    title TEXT,
    artist TEXT,
    album_artist TEXT,
    album TEXT,
    composer TEXT,
    genre TEXT,
    year INTEGER,
    disc_no INTEGER,
    track_no INTEGER,
    duration_ms INTEGER,
    codec TEXT,
    sample_rate INTEGER,
    bits INTEGER,
    channels INTEGER,
    -- PCM 為 0
    dsd_rate INTEGER NOT NULL DEFAULT 0,
    -- 來自標籤，沒有時為 NULL
    rg_track_gain REAL,
    rg_track_peak REAL,
    rg_album_gain REAL,
    rg_album_peak REAL,
    has_embedded_lyrics INTEGER NOT NULL DEFAULT 0,
    album_id INTEGER REFERENCES albums(id) ON DELETE SET NULL,
    cover_key TEXT,
    search_text TEXT NOT NULL DEFAULT '',
    UNIQUE (file_id, subsong)
);
CREATE INDEX tracks_album ON tracks(album_id);
CREATE INDEX tracks_artist ON tracks(artist);
CREATE INDEX tracks_year ON tracks(year);
CREATE INDEX tracks_codec ON tracks(codec);
CREATE INDEX tracks_sample_rate ON tracks(sample_rate);

-- 搜尋索引：external content，由觸發器和 tracks 同步；trigram 讓中日文不用斷詞
CREATE VIRTUAL TABLE tracks_fts USING fts5(
    search_text,
    content = 'tracks',
    content_rowid = 'id',
    tokenize = 'trigram'
);
CREATE TRIGGER tracks_fts_insert AFTER INSERT ON tracks BEGIN
    INSERT INTO tracks_fts(rowid, search_text) VALUES (new.id, new.search_text);
END;
CREATE TRIGGER tracks_fts_delete AFTER DELETE ON tracks BEGIN
    INSERT INTO tracks_fts(tracks_fts, rowid, search_text) VALUES ('delete', old.id, old.search_text);
END;
CREATE TRIGGER tracks_fts_update AFTER UPDATE OF search_text ON tracks BEGIN
    INSERT INTO tracks_fts(tracks_fts, rowid, search_text) VALUES ('delete', old.id, old.search_text);
    INSERT INTO tracks_fts(rowid, search_text) VALUES (new.id, new.search_text);
END;

CREATE TABLE covers (
    key TEXT PRIMARY KEY,
    -- embedded:<path> 或 file:<path>
    source TEXT NOT NULL,
    thumb_path TEXT,
    dominant_rgb INTEGER,
    created_at INTEGER NOT NULL
);

CREATE TABLE scan_reports (
    id INTEGER PRIMARY KEY,
    started_at INTEGER NOT NULL,
    finished_at INTEGER,
    scanned INTEGER NOT NULL DEFAULT 0,
    added INTEGER NOT NULL DEFAULT 0,
    updated INTEGER NOT NULL DEFAULT 0,
    removed INTEGER NOT NULL DEFAULT 0,
    failed INTEGER NOT NULL DEFAULT 0,
    details_json TEXT
);

-- ===== 模組擁有（以身分 (path_key, subsong) 關聯，不用 tracks.id）=====

-- 進階輸出（OUT）
CREATE TABLE rg_computed (
    path_key TEXT NOT NULL,
    subsong INTEGER NOT NULL,
    track_gain REAL,
    track_peak REAL,
    album_gain REAL,
    album_peak REAL,
    algorithm TEXT NOT NULL DEFAULT 'ebur128',
    computed_at INTEGER NOT NULL,
    PRIMARY KEY (path_key, subsong)
);

-- 播放清單（PL）
CREATE TABLE playlists (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE playlist_items (
    playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
    pos INTEGER NOT NULL,
    path TEXT NOT NULL,
    path_key TEXT NOT NULL,
    subsong INTEGER NOT NULL DEFAULT 0,
    missing INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (playlist_id, pos)
);
CREATE INDEX playlist_items_path ON playlist_items(path_key);

CREATE TABLE path_remaps (
    id INTEGER PRIMARY KEY,
    old_root TEXT NOT NULL,
    new_root TEXT NOT NULL,
    created_at INTEGER NOT NULL
);

-- 歌詞與沉浸（LYR）
CREATE TABLE lyrics_cache (
    path_key TEXT NOT NULL,
    subsong INTEGER NOT NULL,
    source TEXT NOT NULL CHECK (source IN ('lrc', 'embedded', 'lrclib')),
    synced INTEGER NOT NULL DEFAULT 0,
    content TEXT,
    status TEXT NOT NULL CHECK (status IN ('found', 'notfound', 'error')),
    fetched_at INTEGER NOT NULL,
    PRIMARY KEY (path_key, subsong)
);
