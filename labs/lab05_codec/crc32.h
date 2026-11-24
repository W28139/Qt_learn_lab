#pragma once

#include <QByteArrayView>
#include <QtGlobal>

// 计算 CRC-32（IEEE 802.3：多项式 0xEDB88320，反射算法，初值/末值取反）。
quint32 crc32(QByteArrayView data);