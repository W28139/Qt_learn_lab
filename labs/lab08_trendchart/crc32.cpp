#include "crc32.h"

namespace {

// 反射形式的 CRC-32 多项式：0x04C11DB7 的位反转结果。
// 放在匿名命名空间里，只在本翻译单元可见。
constexpr quint32 kPolynomial = 0xEDB88320u;

// 处理一个字节：先把它异或进 CRC 的低 8 位，再逐位右移 8 次；
// 每次最低位为 1 就异或多项式（反射算法的标准写法）。
quint32 update(quint32 crc, quint8 byte)
{
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit)
        crc = (crc & 1u) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
    return crc;
}

} // namespace

// 标准 CRC-32 流程：初值 0xFFFFFFFF → 逐字节更新 → 结果再按位取反。
// 对 ASCII "123456789" 应得到 0xCBF43926。
quint32 crc32(QByteArrayView data)
{
    quint32 crc = 0xFFFFFFFFu;
    // char 是否有符号取决于平台；先转 unsigned char 再截为 quint8，
    // 防止负值符号扩展污染 CRC。
    for (const char ch : data)
        crc = update(crc, static_cast<quint8>(static_cast<unsigned char>(ch)));
    return crc ^ 0xFFFFFFFFu;
}