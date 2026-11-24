#include "csvutils.h"

namespace csv {

/*
    对单个 CSV 字段进行转义处理
 */
QString escapeField(const QString &value)
{
    // 判断字段是否包含需要加引号包裹的特殊字符
    const bool needsQuotes = value.contains(QLatin1Char(','))
                             || value.contains(QLatin1Char('"'))
                             || value.contains(QLatin1Char('\n'))
                             || value.contains(QLatin1Char('\r'));
    // 无需转义时直接返回原值
    if (!needsQuotes)
        return value;

    // 字段内部的每个双引号需替换为两个双引号（CSV 标准转义方式）
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    // 用双引号包裹整个字段
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

/*
 将一组字段序列化为一行 UTF-8 编码的 CSV 文本
*/
QByteArray toUtf8Line(const QStringList &fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());  // 预分配容量，避免多次扩容
    // 逐个字段进行转义
    for (const QString &field : fields)
        escaped.append(escapeField(field));

    // 逗号拼接字段，追加 \r\n 行结束符，最后转换为 UTF-8 字节数组
    return (escaped.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8();
}

} // namespace csv