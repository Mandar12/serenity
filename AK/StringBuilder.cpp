#include "StringBuilder.h"
#include <LibC/stdarg.h>
#include "printf.cpp"
#include <AK/StdLibExtras.h>

namespace AK {

inline void StringBuilder::will_append(size_t size)
{
    if ((m_length + size) > m_buffer.size())
        m_buffer.grow(max(16u, m_buffer.size() * 2 + size));
}

void StringBuilder::append(const String& str)
{
    if (str.is_empty())
        return;
    will_append(str.length());
    memcpy(m_buffer.pointer() + m_length, str.characters(), str.length());
    m_length += str.length();
}

void StringBuilder::append(char ch)
{
    will_append(1);
    m_buffer.pointer()[m_length] = ch;
    m_length += 1;
}

void StringBuilder::appendf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printfInternal([this] (char*&, char ch) {
        append(ch);
    }, nullptr, fmt, ap);
    va_end(ap);
}

ByteBuffer StringBuilder::to_byte_buffer()
{
    m_buffer.trim(m_length);
    return m_buffer;
}

String StringBuilder::build()
{
    return String((const char*)m_buffer.pointer(), m_length);
}

}

