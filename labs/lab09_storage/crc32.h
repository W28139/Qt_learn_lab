#pragma once

#include <QByteArrayView>
#include <QtGlobal>

// 计算 CRC-32（IEEE 802.3：多项式 0xEDB88320，反射算法，初值/末值取反）。
// 参数用 QByteArrayView：只借用调用者的内存，不拷贝、不拥有，
// 调用期间源数据必须保持有效。
// 用途：发现传输/存储中的意外损坏；它挡不住能重算 CRC 的恶意篡改。
quint32 crc32(QByteArrayView data);