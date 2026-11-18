#pragma once

// .fpl 測試（T-53）共用：照 foobar2000 的 .fpl 格式產生 .fpl，以及壞檔測試要改的欄位位置。
#include "playlist_fpl.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QtEndian>

namespace auralis::test {

/// 產生 .fpl 用的一筆。
struct FplTrack {
    QByteArray path;
    quint32 subsong = 0;
    QByteArray artist;
    QByteArray title;
    QByteArray cueEmbedded; ///< 空的＝沒有這個次要欄位
    bool meta = true;
};

inline const QByteArray kFplHeader = QByteArray::fromHex("e1a09c91f83c7742852c3bcc1401d3f2");

inline void appendU32(QByteArray& bytes, quint32 value) {
    char raw[4];
    qToLittleEndian(value, raw);
    bytes.append(raw, 4);
}

inline quint32 u32At(const QByteArray& bytes, qsizetype at) {
    return qFromLittleEndian<quint32>(bytes.constData() + at);
}

inline void setU32(QByteArray& bytes, qsizetype at, quint32 value) {
    qToLittleEndian(value, bytes.data() + at);
}

/// 照 .fpl 格式產生 .fpl（字串去重；標籤 artist、title；次要欄位 cue_embedded）。
inline QByteArray buildFpl(const QList<FplTrack>& tracks) {
    QByteArray table;
    QHash<QByteArray, quint32> offsets;
    const auto offset = [&](const QByteArray& text) {
        if (!offsets.contains(text)) {
            offsets.insert(text, static_cast<quint32>(table.size()));
            table += text;
            table += '\0';
        }
        return offsets.value(text);
    };
    QByteArray body;
    for (const FplTrack& track : tracks) {
        appendU32(body, track.meta ? 0x11 : 0x10);
        appendU32(body, offset(track.path));
        appendU32(body, track.subsong);
        if (track.meta) {
            body.append(40, '\0');
        }
        QList<QPair<QByteArray, QByteArray>> tags;
        if (!track.artist.isEmpty()) {
            tags.append({"artist", track.artist});
        }
        if (!track.title.isEmpty()) {
            tags.append({"title", track.title});
        }
        QList<quint32> entries{static_cast<quint32>(tags.size()),
                               track.cueEmbedded.isEmpty() ? 0U : 1U, 0};
        for (qsizetype j = 0; j < tags.size(); ++j) {
            entries.append(static_cast<quint32>(j));
            entries.append(offset(tags.at(j).first));
        }
        entries.append(static_cast<quint32>(tags.size()));
        for (const auto& tag : tags) {
            entries.append(offset(tag.second));
        }
        if (!track.cueEmbedded.isEmpty()) {
            entries.append(offset("cue_embedded"));
            entries.append(offset(track.cueEmbedded));
        }
        appendU32(body, static_cast<quint32>(entries.size()));
        for (const quint32 value : entries) {
            appendU32(body, value);
        }
    }
    QByteArray bytes = kFplHeader;
    appendU32(bytes, static_cast<quint32>(table.size()));
    bytes += table;
    appendU32(bytes, static_cast<quint32>(tracks.size()));
    bytes += body;
    return bytes;
}

/// 一筆、有 meta、標籤 artist＋title 的樣本，各欄位的位置（壞檔測試改這些位置）。
struct OneTrack {
    QByteArray bytes;
    qsizetype tableSize = 0; ///< N 的位置是 16
    qsizetype countAt = 0;
    qsizetype pathAt = 0; ///< path_offset
    qsizetype entryCountAt = 0;
    qsizetype entriesAt = 0; ///< entries[0]
};

inline OneTrack oneTrack(const FplTrack& track = {"file://C:\\音樂\\a.flac", 0, "藝人", "曲名",
                                                  "no"}) {
    OneTrack sample;
    sample.bytes = buildFpl({track});
    sample.tableSize = u32At(sample.bytes, 16);
    sample.countAt = 20 + sample.tableSize;
    sample.pathAt = sample.countAt + 4 + 4;
    sample.entryCountAt = sample.countAt + 4 + 12 + (track.meta ? 40 : 0);
    sample.entriesAt = sample.entryCountAt + 4;
    return sample;
}

inline bool fplUnsupported(const QByteArray& bytes) {
    return !mod::pl::parseFpl(bytes).has_value();
}

} // namespace auralis::test
