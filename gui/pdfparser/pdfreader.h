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


#ifndef PDFREADER_H
#define PDFREADER_H

#include <QtGlobal>
#include <exception>
#include "pdfvalue.h"
#include "pdfxref.h"

class QFile;

namespace PDF {

class Object;

/**
    \brief The ContentHandler class provides an interface to report the logical content of PDF object.

    If the application needs to be informed of basic parsing events, it can implement this interface and
    activate it using Reader::setHandler(). The reader can then report basic document-related events
    like the end of PDF object through this interface.
*/

class ReaderHandler
{
public:
    virtual void trailerReady(const XRefTable &xRefTable, const Dict &trailerDict) = 0;
    virtual void objectReady(const Object &object) = 0;
};

/**
    \brief The Reader class provides an implementation of a simple PDF parser.

    This PDF reader is suitable for a wide range of applications. It is able to parse well-formed PDF and
    can report the elements to a content handler.

    The easiest pattern of use for this class is to create a reader instance, define an input source,
    specify the handler to be used by the reader, and parse the data.
*/

class Data;

/// The PDF::REader class provides a way to read PDF documents.
class Reader
{
public:
    /// Constructs a Reader object.
    Reader();

    ///Destroys the object and frees its resources.
    virtual ~Reader();

    /// Reads PDF document from the existing file, starting at position startPos until endPos.
    /// If endPos is 0 (the default), Reader reads all bytes starting at position startPos
    /// until the end of the file. If the fileName has no path or a relative path, the path
    /// used will be the application's current directory path at the time of the open() call.
    void open(const QString &fileName, quint64 startPos, quint64 endPos);

    /// Reads PDF document from first size bytes of the data.
    /// The bytes are not copied. The Reader will contain the data pointer. The caller
    /// guarantees that data will not be deleted or modified as long as this Reader exist.
    void open(const char * const data, quint64 size);

    const XRefTable &xRefTable() { return mXRefTable; }
    const Dict &trailerDict() const { return mTrailerDict; }
    Dict trailerDict() { return mTrailerDict; }

    Object getObject(const Link &link) const;
    Object getObject(uint objNum, quint16 genNum) const;

    const Value find(const QString &path) const;

    quint32 pageCount();

protected:
    void   load();
    Value  readValue(qint64 *pos) const;
    qint64 readArray(qint64 start, Array *res) const;
    qint64 readDict(qint64 start, Dict *res) const;
    qint64 readHexString(qint64 start, String *res) const;
    qint64 readLiteralString(qint64 start, String *res) const;

    qint64 readObject(qint64 start, Object *res) const;
    qint64 readXRefTable(qint64 start, XRefTable *res) const;

    QString readNameString(qint64 *pos) const;

    bool isDelim(qint64 pos) const;
    qint64 skipSpace(qint64 pos) const;
    qint64 skipCRLF(qint64 pos) const;

    qint64 indexOf(const char *str, qint64 from = 0) const;
    qint64 indexOfBack(const char *str, qint64 from) const;

    quint32 readUInt(qint64 *pos, bool *ok) const;
    double readNum(qint64 *pos, bool *ok) const;

    bool compareStr(qint64 pos, const char *str) const;
    bool compareWord(qint64 pos, const char *str) const;

private:
    QFile      *mFile;
    const char *mData;
    qint64      mSize;
    XRefTable   mXRefTable;
    Dict        mTrailerDict;
    int         mPagesCount;
};

} // namespace PDF
#endif // PDFREADER_H
