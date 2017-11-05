/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 *
 * Copyright: 2012-2017 Boomaga team https://github.com/Boomaga
 * Authors:
 *   Alexander Sokoloff <sokoloff.a@gmail.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */


#include "math.h"
#include "pdfreader.h"
#include "pdfxref.h"
#include "pdfobject.h"
#include "pdftypes.h"
#include "pdfvalue.h"
#include <QFile>
#include <QTextCodec>
#include <QDebug>

using namespace PDF;


/************************************************
 *
 ************************************************/
Reader::Reader():
    mFile(nullptr),
    mData(nullptr),
    mSize(0),
    mPagesCount(-1)
{

}


/************************************************
 *
 ************************************************/
Reader::~Reader()
{
    if (mFile && mData)
            mFile->unmap(const_cast<uchar*>(reinterpret_cast<const uchar*>(mData)));

    delete mFile;
}


/************************************************
 *
 ************************************************/
Value Reader::readValue(qint64 *pos) const
{
    char c = mData[*pos];
    switch (c) {
    // Link or Number .................
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    {
        bool ok;
        double n1 = readNum(pos, &ok);
        if (!ok)
            throw ParseError(*pos, QString("Unexpected symbol '%1', expected a number.").arg(mData[*pos]));

        if (n1 != quint64(n1))
            return Number(n1);

        qint64 p = *pos;
        p = skipSpace(p);

        quint64 n2 = readUInt(&p, &ok);
        if (!ok)
            return Number(n1);

        p = skipSpace(p);

        if (mData[p] != 'R')
            return Number(n1);

        *pos = p+1;
        return Link(n1, n2);
    }
    // Float number ...................
    case '-':
    case '+':
    case '.':
    {
        bool ok;
        double n = readNum(pos, &ok);
        if (!ok)
            throw ParseError(*pos, QString("Unexpected symbol '%1', expected a number.").arg(mData[*pos]));

        return Number(n);
    }


    // Array ..........................
    case '[':
    {
        Array res;
        *pos = readArray(*pos, &res);
        return res;

    }

    // Dict or HexString ..............
    case '<':
    {
        if (mData[*pos+1] == '<')
        {
            Dict res;
            *pos = readDict(*pos, &res);
            return res;
        }
        else
        {
            String res;
            *pos = readHexString(*pos, &res);
            return res;
        }
    }

    // Name ...........................
    case '/':
    {
        return Name(readNameString(pos));
    }

    //LiteralString ...................
    case '(':
    {
        String res;
        *pos = readLiteralString(*pos, &res);
        return res;
    }

    // Bool ...........................
    case 't':
    case 'f':
    {
        if (compareWord(*pos, "true"))
        {
            *pos += 4;
            return Bool(true);
        }

        if (compareWord(*pos, "false"))
        {
            *pos += 5;
            return Bool(true);
        }

        throw ParseError(*pos, QString("Unexpected symbol '%1', expected a boolean.").arg(mData[*pos]));
    }

    // None ...........................
    case 'n':
    {
        if (!compareWord(*pos, "null"))
            throw ParseError(*pos, QString("Invalid PDF null on pos %1").arg(*pos));

        *pos += 4;
        return Null();
    }

    // Comment ........................
    case '%':
    {
        while (*pos < mSize && mData[*pos] != '\n' && mData[*pos] != '\r')
            ++(*pos);

        *pos = skipSpace(*pos);
        return readValue(pos);
    }
    }

    QByteArray d(mData + *pos, qMin(mSize - *pos, 20ll));
    throw UnknownValueError(*pos, QString("Unknown object type on %1: '%2'").arg(*pos).arg(d.data()));
}


/************************************************
 *
 ************************************************/
qint64 Reader::readArray(qint64 start, Array *res) const
{
    qint64 pos = start + 1;

    while (pos < mSize)
    {
        pos = skipSpace(pos);

        if (pos == mSize)
            throw ParseError(start);

        if (mData[pos] == ']')
        {
            res->setValid(true);
            return pos + 1;
        }

        res->append(readValue(&pos));
    }

    throw ParseError(start, "The closing array marker ']' was not found.");
}


/************************************************
 *
 ************************************************/
qint64 Reader::readDict(qint64 start, Dict *res) const
{
    qint64 pos = start + 2;         // skip "<<" mark

    while (pos < mSize - 1)
    {
        pos = skipSpace(pos);

        if (mData[pos]     == '>' &&
            mData[pos + 1] == '>' )
        {
            res->setValid(true);
            return pos += 2;        // skip ">>" mark
        }

        QString name = readNameString(&pos);
        pos = skipSpace(pos);
        res->insert(name, readValue(&pos));
    }

    throw ParseError(start, "The closing dictionary marker '>>' was not found.");
}


/************************************************
 * Strings may be written in hexadecimal form, which is useful for
 * including arbitrary binary data in a PDF file. A hexadecimal
 * string is written as a sequence of hexadecimal digits (0–9 and
 * either A –F or a–f ) enclosed within angle brackets (< and >):
 *    < 4E6F762073686D6F7A206B6120706F702E >
 *
 * Each pair of hexadecimal digits defines one byte of the string.
 * White-space characters (such as space, tab, carriage return,
 * line feed, and form feed) are ignored.
 *
 * If the final digit of a hexadecimal string is missing—that is,
 * if there is an odd number of digits—the final digit is assumed
 * to be 0. For example:
 *     < 901FA3 >
 * is a 3-byte string consisting of the characters whose hexadecimal
 * codes are 90, 1F, and A3, but
 *    < 901FA >
 * is a 3-byte string containing the characters whose hexadecimal
 * codes are 90, 1F , and A0.
 ************************************************/
qint64 Reader::readHexString(qint64 start, String *res) const
{
    QByteArray data;
    data.reserve(1024);

    bool first = true;
    char r = 0;
    for (qint64 pos = start+1; pos < mSize; ++pos)
    {
        char c = mData[pos];
        switch (c)
        {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        {
            if (first)
                r = c - '0';
            else
                data.append(r * 16 + c -'0');

            first = !first;
            break;
        }

        case 'A': case 'B': case 'C':
        case 'D': case 'E': case 'F':
        {
            if (first)
                r = c - 'A' + 10;
            else
                data.append(r * 16 + c -'A' + 10);

            first = !first;
            break;
        }

        case 'a': case 'b': case 'c':
        case 'd': case 'e': case 'f':
        {
            if (first)
                r = c - 'a' + 10;
            else
                data.append(r * 16 + c -'a' + 10);

            first = !first;
            break;
        }

        case ' ':  case '\t': case '\n':
        case '\v': case '\f': case '\r':
            break;

        case '>':
        {
            if (!first)
                data.append(r * 16);

            res->setValue(QTextCodec::codecForUtfText(data, QTextCodec::codecForName("UTF-8"))->toUnicode(data));
            res->setEncodingType(String::HexEncoded);
            return pos + 1;
        }

        default:
            throw ParseError(pos, QString("Invalid PDF hexadecimal string on pos %1").arg(pos));
        }
    }

    throw ParseError(start, "The closing hexadecimal string marker '>' was not found.");
}


/************************************************
 * Literal Strings
 *
 * A literal string is written as an arbitrary number of characters enclosed
 * in parentheses. Any characters may appear in a string except unbalanced
 * parentheses and the backslash, which must be treated specially.
 * Balanced pairs of parentheses within a string require no special treatment.
 *
 * The following are valid literal strings:
 *  ( This is a string )
 *  ( Strings may contain newlines
 *  and such. )
 *  ( Strings may contain balanced parentheses ( ) and
 *  special characters ( * ! & } ^ % and so on ) . )
 *  ( The following is an empty string . )
 *  ( )
 *  ( It has zero ( 0 ) length . )
 *
 * Within a literal string, the backslash (\) is used as an escape character
 * for various purposes, such as to include newline characters, nonprinting
 * ASCII characters, unbalanced parentheses, or the backslash character
 * itself in the string. The character immediately following the backslash
 * determines its precise interpretation (see Table 3.2). If the character
 * following the backslash is not one of those shown in the table, the
 * backslash is ignored.
 *
 * SEQUENCE     MEANING
 *  \n          Line feed (LF)
 *  \r          Carriage return (CR)
 *  \t          Horizontal tab (HT)
 *  \b          Backspace (BS)
 *  \f          Form feed (FF)
 *  \(          Left parenthesis
 *  \)          Right parenthesis
 *  \\          Backslash
 *  \ddd        Character code ddd (octal)
 *
 * If a string is too long to be conveniently placed on a single line,
 * it may be split across multiple lines by using the backslash character at
 * the end of a line to indicate that the string continues on the following
 * line. The backslash and the end-of-line marker following it are not
 * considered part of the string. For example:
 *  ( These \
 *  two strings \
 *  are the same . )
 *  ( These two strings are the same . )
 *
 * If an end-of-line marker appears within a literal string without a
 * preceding backslash, the result is equivalent to \n (regardless of
 * whether the end-of-line marker was a carriage return, a line feed,
 * or both). For example:
 *  ( This string has an end−of−line at the end of it .
 *  )
 *  ( So does this one .\n )
 *
 * The \ddd escape sequence provides a way to represent characters outside
 * the printable ASCII character set. For example:
 *  ( This string contains \245two octal characters\307 . )
 * The number ddd may consist of one, two, or three octal digits, with
 * high-order overflow ignored. It is required that three octal digits be
 * used, with leading zeros as needed, if the next character of the string
 * is also a digit. For example, the literal
 *  ( \0053 )
 * denotes a string containing two characters, \005 (Control-E) followed
 * by the digit 3, whereas both
 *  ( \053 )
 * and
 *  ( \53 )
 * denote strings containing the single character \053, a plus sign (+).
 ************************************************/
qint64 Reader::readLiteralString(qint64 start, String *res) const
{
    QByteArray data;
    data.reserve(1024);

    int level = 1;
    bool esc = false;
    for (qint64 i = start+1; i < mSize; ++i)
    {
        char c = mData[i];

        switch (c)
        {

        // Backslash .......................
        case '\\':
            esc = !esc;
            if (!esc)
                data.append(c);
            break;

        // Line feed (LF) ..................
        case 'n':
            data.append(esc ? '\n' : 'n');
            esc = false;
            break;

        // Carriage return (CR) ............
        case 'r':
            data.append(esc ? '\r' : 'r');
            esc = false;
            break;

        // Horizontal tab (HT) .............
        case 't':
            data.append(esc ? '\t' : 't');
            esc = false;
            break;

        // Backspace (BS) ..................
        case 'b':
            data.append(esc ? '\b' : 'b');
            esc = false;
            break;

        // Form feed (FF) ..................
        case 'f':
            data.append(esc ? '\f' : 'f');
            esc = false;
            break;

        // Character code ddd (octal) ......
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            if (esc)
            {
                esc = false;
                char n = c-'0';
                int l = qMin(i+3, mSize);
                for(++i; i<l; ++i)
                {
                    c = mData[i];
                    if (c < '0' || c > '7' )
                        break;

                    n = n * 8 + c - '0';
                }
                data.append(n);
                --i;
            }
            else
            {
                data.append(c);
            }
            break;

        case '\n':
            if (esc)
            {
                if (i+1<mSize && mData[i+1] == '\r')
                    ++i;
            }
            else
            {
                data.append('\n');
            }
            esc = false;
            break;

        case '\r':
            if (esc)
            {
                if (i+1<mSize && mData[i+1] == '\n')
                    ++i;
            }
            else
            {
                data.append('\r');
            }
            esc = false;
            break;

        case '(':
            if (!esc)
                ++level;
            data.append(c);
            esc = false;
            break;

        case ')':
            if (!esc)
            {
                --level;

                if (level == 0)
                {
                    res->setValue(QTextCodec::codecForUtfText(data, QTextCodec::codecForName("UTF-8"))->toUnicode(data));
                    res->setEncodingType(String::LiteralEncoded);
                    return i + 1;
                }
            }
            esc = false;
            data.append(c);
            break;

        default:
            esc = false;
            data.append(c);
            break;
        }
    }

    throw ParseError(start, "The closing literal string marker ')' was not found.");
}


/************************************************
 *
 ************************************************/
qint64 Reader::readObject(qint64 start, Object *res) const
{
    qint64 pos = start;

    bool ok;
    res->setObjNum(readUInt(&pos, &ok));
    if (!ok)
        throw ParseError(pos);

    pos = skipSpace(pos);
    res->setGenNum(readUInt(&pos, &ok));
    if (!ok)
        throw ParseError(pos);

    pos = indexOf("obj", pos) + 3;
    pos = skipSpace(pos);

    res->setValue(readValue(&pos));
    pos = skipSpace(pos);

    if (compareWord(pos, "stream"))
    {
        pos = skipCRLF(pos + strlen("stream"));

        qint64 len = 0;
        Value v = res->dict().value("Length");
        switch (v.type()) {
        case Value::Type::Number:
            len = v.asNumber().value();
            break;

        case Value::Type::Link:
            len = getObject(v.asLink().objNum(), v.asLink().genNum()).value().asNumber().value();
            break;

        default:
            throw ParseError(pos, QString("Incorrect stream length in object at %1.").arg(mData[start]));
        }

        res->setStream(QByteArray::fromRawData(mData + pos, len));
        pos = skipSpace(pos + len);
        if (compareWord(pos, "endstream"))
            pos += strlen("endstream");
    }

    return pos;
}


/************************************************
 *
 ************************************************/
qint64 Reader::readXRefTable(qint64 pos, XRefTable *res) const
{
    pos = skipSpace(pos);
    if (!compareWord(pos, "xref"))
        throw ParseError(pos, "Incorrect XRef. Expected 'xref'.");
    pos +=4;

    pos = skipSpace(pos);

    // read XRef table ..........................
    do {
        bool ok;
        uint startObjNum = readUInt(&pos, &ok);
        if (!ok)
            throw ParseError(pos, "Incorrect XRef. Can't read object number of the first object.");

        uint cnt = readUInt(&pos, &ok);
        if (!ok)
            throw ParseError(pos, "Incorrect XRef. Can't read number of entries.");
        pos = skipSpace(pos);

        for (uint i=0; i<cnt; ++i)
        {
            if (!res->contains(startObjNum + i))
            {
                if (mData[pos + 17] == 'n')
                {
                    res->insert(startObjNum + i,
                                XRefEntry(
                                    strtoull(mData + pos,     nullptr, 10),
                                    startObjNum + i,
                                    strtoul(mData + pos + 11, nullptr, 10),
                                    XRefEntry::Used));
                }
                else
                {
                    res->insert(startObjNum + i,
                                XRefEntry(
                                    0,
                                    startObjNum + i,
                                    strtoul(mData + pos + 11, nullptr, 10),
                                    XRefEntry::Free));

                }
            }

            pos += 20;
        }

        pos = skipSpace(pos);
    } while (!compareStr(pos, "trailer"));

    return pos;
}


/************************************************
 *
 ************************************************/
Object Reader::getObject(const Link &link) const
{
    return getObject(link.objNum(), link.genNum());
}


/************************************************
 *
 ************************************************/
Object Reader::getObject(uint objNum, quint16 genNum) const
{
    Q_UNUSED(genNum)
    Object obj;
    if (mXRefTable.value(objNum).pos)
        readObject(mXRefTable.value(objNum).pos, &obj);
    return obj;
}


/************************************************
 *
 ************************************************/
const Value Reader::find(const QString &path) const
{
    QStringList objects = path.split('/', QString::SkipEmptyParts);
    if (objects.first() == "Trailer")
        objects.removeFirst();
    QString val = objects.takeLast();

    Dict dict = trailerDict();
    foreach (const QString &obj, objects)
    {
        dict = getObject(dict.value(obj).asLink()).dict();
    }
    return dict.value(val);
}


/************************************************
 *
 ************************************************/
quint32 Reader::pageCount()
{
    if (mPagesCount < 0)
        mPagesCount = find("/Root/Pages/Count").asNumber().value();

    return mPagesCount;
}


/************************************************
 *
 ************************************************/
QString Reader::readNameString(qint64 *pos) const
{
    if (mData[*pos] != '/')
        throw ParseError(*pos, QString("Invalid PDF name on pos %1").arg(*pos));

    qint64 start = *pos;
    for (++(*pos); *pos < mSize; ++(*pos))
    {
        if (isDelim(*pos))
        {
            return QString::fromLocal8Bit(mData + start + 1, *pos - start - 1);
        }
    }

    throw ParseError(start);
}


/************************************************
 *
 ************************************************/
void Reader::open(const QString &fileName, quint64 startPos, quint64 endPos)
{
    mFile = new QFile(fileName);

    if(!mFile->open(QFile::ReadOnly))
        throw Error(0, QString("I can't open file \"%1\":%2").arg(fileName).arg(mFile->errorString()));

    int start = startPos;
    int end   = endPos ? endPos : mFile->size();

    if (end < start)
        throw Error(0, QString("Invalid request for %1, the start position (%2) is greater than the end (%3) one.")
            .arg(fileName)
            .arg(startPos)
            .arg(endPos));

    mFile->seek(start);
    mSize  =  end - start;
    mData  = reinterpret_cast<const char*>(mFile->map(start, mSize));
    load();
}


/************************************************
 *
 ************************************************/
void Reader::open(const char * const data, quint64 size)
{
    mData = data;
    mSize = size;
    load();
}


/************************************************
 *
 ************************************************/
void Reader::load()
{
    // Check header ...................................
    if (indexOf("%PDF-") != 0)
        throw HeaderError(0);

    bool ok;
    // Get xref table position ..................
    qint64 startXRef = indexOfBack("startxref", mSize - 1);
    if (startXRef < 0)
        throw ParseError(0, "Incorrect trailer, the marker 'startxref' was not found.");

    qint64 pos = startXRef + strlen("startxref");
    quint64 xrefPos = readUInt(&pos, &ok);
    if (!ok)
        throw ParseError(pos, "Error in trailer, can't read xref position.");

    // Read xref table ..........................
    pos = readXRefTable(xrefPos, &mXRefTable);
    pos = skipSpace(pos+strlen("trailer"));
    readDict(pos, &mTrailerDict);

    qint64 parentXrefPos = mTrailerDict.value("Prev").asNumber().value();

    while (parentXrefPos)
    {
        pos = readXRefTable(parentXrefPos, &mXRefTable);
        pos = skipSpace(pos+strlen("trailer"));
        Dict dict;
        readDict(pos, &dict);
        parentXrefPos = dict.value("Prev").asNumber().value();
    }
}


/************************************************
 *
 ************************************************/
bool Reader::isDelim(qint64 pos) const
{
    return isspace(mData[pos]) ||
            strchr("()<>[]{}/%", mData[pos]);
}


/************************************************
 *
 ************************************************/
qint64 Reader::skipSpace(qint64 pos) const
{
    while (pos < mSize && isspace(mData[pos]))
        pos++;

    return pos;
}


/************************************************
 *
 ************************************************/
qint64 Reader::indexOf(const char *str, qint64 from) const
{
    qint64 len = strlen(str);

    for (qint64 i = from; i<mSize-len; i++)
    {
        if (strncmp(mData + i, str, len) == 0)
            return i;
    }

    return -1;
}


/************************************************
 *
 ************************************************/
qint64 Reader::indexOfBack(const char *str, qint64 from) const
{
    qint64 len = strlen(str);

    for (qint64 i = from - len + 1 ; i>0; i--)
    {
        if (strncmp(mData + i, str, len) == 0)
            return i;
    }

    return -1;
}


/************************************************
 *
 ************************************************/
qint64 Reader::skipCRLF(qint64 pos) const
{
    for (; pos >= 0; ++pos)
    {
        if (mData[pos] != '\n' && mData[pos] != '\r')
            return pos;
    }

    return 0;
}


/************************************************
 *
 ************************************************/
quint32 Reader::readUInt(qint64 *pos, bool *ok) const
{
    char *end;
    quint32 res = strtoul(mData + *pos, &end, 10);
    *ok = end != mData + *pos;
    *pos = end - mData;
    return res;
}


/************************************************
 *
 ************************************************/
double Reader::readNum(qint64 *pos, bool *ok) const
{
    const char * str = mData + *pos;
    int sign = 1;
    if (str[0] == '-')
    {
        ++str;
        sign = -1;
    }

    char *end;
    double res = strtoll(str, &end, 10);
    if (end[0] == '.')
    {
        ++end;
        if (isdigit(end[0]))
        {
            str = end;
            long long fract = strtoll(str, &end, 10);
            if (str < end)
                res += fract / pow(10.0, end - str);
        }
    }
    *ok = end != mData + *pos;
    *pos = end - mData;
    return res * sign;
}


/************************************************
 *
 ************************************************/
bool Reader::compareStr(qint64 pos, const char *str) const
{
    int len = strlen(str);
    return (mSize - pos > len) && strncmp(mData + pos, str, len) == 0;
}


/************************************************
 *
 ************************************************/
bool Reader::compareWord(qint64 pos, const char *str) const
{
    int len = strlen(str);
    return (mSize - pos > len + 1) &&
            strncmp(mData + pos, str, len) == 0 &&
            isDelim(pos + len);
}
