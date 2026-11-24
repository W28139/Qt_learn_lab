#include "crc32.h"

// CRC-32 循环冗余校验。
// 作用：根据一段字节数据计算 32 位 CRC 校验值，用于检测数据是否损坏或被篡改。
// 说明：本文件只负责“计算校验值”，不负责把校验值附加到帧里，也不负责接收端比对。
namespace {


constexpr quint32 kPolynomial = 0xEDB88320u;

// 内部辅助函数：把一个字节合并进当前 CRC 值，返回更新后的 CRC。仅供 crc32() 内部使用。
quint32 update(quint32 crc, quint8 byte)
{
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit)
        crc = (crc & 1u) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
    return crc;
}

} // namespace

// 计算一段字节数据的 CRC-32 校验值。
// 参数：
//   data —— 要计算校验值的字节数据。
// 返回：
//   32 位无符号整数，即这段数据的 CRC-32 校验值。
quint32 crc32(QByteArrayView data)
{
    quint32 crc = 0xFFFFFFFFu;

    // 逐个字节计算；先转成无符号字节，避免 char 符号位影响结果。
    for (const char ch : data)
        crc = update(crc, static_cast<quint8>(static_cast<unsigned char>(ch)));

    // 按标准 CRC-32 约定，最终结果再取反一次。
    return crc ^ 0xFFFFFFFFu;
}