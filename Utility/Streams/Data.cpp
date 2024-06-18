// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Data.h"
#include "../../Core/Prefix.h"
#include "../StringUtils.h"
#include "../MemoryUtils.h"
#include "../PtrUtils.h"
#include "../StringFormat.h"
#include "../../OSServices/RawFS.h"
#include "Stream.h"
#include "StreamTypes.h"
#include <vector>
#include <assert.h>

namespace Utility
{

template <typename Ptr>
    void SafeDelete(Ptr& ptr) { delete ptr; ptr = nullptr; }

template <typename Ptr>
    void SafeDeleteArray(Ptr& ptr) { delete[] ptr; ptr = nullptr; }

char* DupString(const char* src)
{
    size_t size = XlStringSize(src) + 1;
    char* dst = new char[size];
    XlCopyMemory(dst, src, size);
    return dst;
}

Data::Data(const char* value):
    value(DupString(value)),
    child(0),
    next(0),
    prev(0),
    parent(0),
    preComment(0),
    postComment(0),
    meta(0),
    lineNum(0)
{
}

Data::~Data()
{
    Clear();
    delete [] value;
    delete [] preComment;
    delete [] postComment;
}

int Data::Index() const
{
    int i = 0;
    const Data* p = this;
    while (p) {
        p = p->prev;
        ++i;
    }
    return i-1;
}

int Data::Size() const
{
    int i = 0;
    Data* p = child;
    while (p) {
        ++i;
        p = p->next;
    }
    return i;
}

Data* Data::ChildAt(int i) const
{
    Data* p = child;
    while (p && i--) {
        p = p->next;
    }
    return p;
}

Data* Data::ChildWithValue(const char* v) const
{
    for (Data* data = child; data; data = data->next) {
        if (XlEqString(data->value, v)) {
            return data;
        }
    }
    return 0;
}

Data* Data::NextWithValue(const char* v) const
{
    for (Data* data = next; data; data = data->next) {
        if (XlEqString(data->value, v)) {
            return data;
        }
    }
    return 0;
}

Data* Data::PrevWithValue(const char* v) const
{
    for (Data* data = prev; data; data = data->prev) {
        if (XlEqString(data->value, v)) {
            return data;
        }
    }
    return 0;
}

const char* Data::ValueAt(int i, const char* def) const
{
    Data* p = ChildAt(i);
    return p ? p->value : def;
}

Data* Data::Attribute(const char* path) const
{
    Data* data = Find(path);
    if (data)
        return data->child;
    return 0;
}

int Data::IntAttribute(const char* path, int def) const
{
    Data* data = Attribute(path);
    if (data)
        return XlAtoI32(data->value);
    return def;
}

int64 Data::Int64Attribute(const char* path, int64 def /* = 0 */) const
{
    Data* data = Attribute(path);
    if (data) {
        return XlAtoI64(data->value);
    }
    return def;
}

float Data::FloatAttribute(const char* path, float def) const
{
    Data* data = Attribute(path);
    if (data)
        return XlAtoF32(data->value);
    return def;
}

double Data::DoubleAttribute(const char* path, double def) const
{
    Data* data = Attribute(path);
    if (data)
        return XlAtoF64(data->value);
    return def;
}

const char* Data::StrAttribute(const char* path, const char* def) const
{
    Data* data = Attribute(path);
    if (data)
        return data->value;
    return def;
}

bool Data::BoolAttribute(const char *path, bool def) const
{
    Data* data = Attribute(path);
    if (data)
        return XlAtoBool(data->value);
    return def;
}

bool Data::HasBoolAttribute(const char *path, bool* out) const
{
    Data* data = Attribute(path);
    if (!data) {
        return false;
    }
    if (out) {
        *out = XlAtoBool(data->value);
    }
    return true;
}

bool Data::HasIntAttribute(const char* path, int* out) const
{
    Data* data = Attribute(path);
    if (!data) {
        return false;
    }
    if (out) {
        *out = XlAtoI32(data->value);
    }
    return true;
}

bool Data::HasInt64Attribute(const char* path, int64* out) const
{
    Data* data = Attribute(path);
    if (!data) {
        return false;
    }
    if (out) {
        *out = XlAtoI64(data->value);
    }
    return true;
}

bool Data::HasFloatAttribute(const char* path, float* out) const
{
    Data* data = Attribute(path);
    if (!data) {
        return false;
    }
    if (out) {
        *out = XlAtoF32(data->value);
    }
    return true;
}

bool Data::HasDoubleAttribute(const char* path, double* out) const
{
    Data* data = Attribute(path);
    if (!data) {
        return false;
    }
    if (out) {
        *out = XlAtoF64(data->value);
    }
    return true;
}

bool Data::HasStrAttribute(const char* path, const char** out) const
{
    Data* data = Attribute(path);
    if (!data) {
        return false;
    }
    if (out) {
        *out = data->value;
    }
    return true;
}

void Data::SetAttribute(const char* name, Data* v)
{
    Data* data = ChildWithValue(name);
    if (!data) {
        data = new Data(name);
        Add(data);
        data->Add(v);
        return;
    }
    data->Clear();
    data->Add(v);
}

void Data::SetAttribute(const char* name, bool v)
{
    char buf[16];
    XlFormatString(buf, dimof(buf), "%s", v ? "true" : "false");
    SetAttribute(name, new Data(buf));
}

void Data::SetAttribute(const char* name, int v)
{
    char buf[16];
    XlFormatString(buf, dimof(buf), "%d", v);
    SetAttribute(name, new Data(buf));
}

void Data::SetAttribute(const char* name, uint32 v)
{
    char buf[16];
    XlFormatString(buf, dimof(buf), "%u", v);
    SetAttribute(name, new Data(buf));
}

void Data::SetAttribute(const char* name, float v)
{
    char buf[16];
    XlFormatString(buf, dimof(buf), "%g", v);
    SetAttribute(name, new Data(buf));
}

void Data::SetAttribute(const char* name, double v)
{
    char buf[32];
    XlFormatString(buf, dimof(buf), "%lg", v);
    SetAttribute(name, new Data(buf));
}

void Data::SetAttribute(const char* name, const char* v)
{
    SetAttribute(name, new Data(v));
}

void Data::SetAttribute(const char* name, int64 valued)
{
    char buf[32];
    XlFormatString(buf, dimof(buf), "%lld", valued);
    SetAttribute(name, new Data(buf));
}

int Data::IntValue() const
{
    return XlAtoI32(value);
}

int64 Data::Int64Value() const
{
    return XlAtoI64(value);
}

float Data::FloatValue() const
{
    return XlAtoF32(value);
}

double Data::DoubleValue() const
{
    return XlAtoF64(value);
}

const char* Data::StrValue() const
{
    return value;
}

bool Data::BoolValue() const
{
    return XlAtoBool(value);
}

void Data::SetValue(bool v)
{
    char buf[16];
    XlFormatString(buf, dimof(buf), "%s", v ? "true" : "false");
    SetValue(buf);
}

void Data::SetValue(int v)
{
    char buf[16];
    XlFormatString(buf, dimof(buf), "%d", v);
    SetValue(buf);
}

void Data::SetValue(float v)
{
    char buf[16];
    XlFormatString(buf, dimof(buf), "%g", v);
    SetValue(buf);
}

void Data::SetValue(double v)
{
    char buf[32];
    XlFormatString(buf, dimof(buf), "%lg", v);
    SetValue(buf);
}

void Data::SetValue(const char* v)
{
    delete [] value;
    value = DupString(v);
}

bool Data::operator==(const Data& n) const
{
    if (!XlEqString(value, n.value))
        return false;

    Data* a = child;
    Data* b = n.child;

    while (a || b) {
        if (!a) return false;
        if (!b) return false;
        if (!(*a == *b)) return false;
        a = a->next;
        b = b->next;
    }

    return true;
}

void Data::Clear()
{
    Data* p = child;
    while (p) {
        Data* t = p;
        p = p->next;
        delete t;
    }
    child = 0;

    delete meta;
    meta = 0;
}

Data* Data::Clone() const
{
    Data* data = new Data(value);
    foreachData (n, this) {
        data->Add(n->Clone());
    }
    return data;
}

void Data::Add(Data* n)
{
    assert(!n->parent);
    assert(!n->next);
    assert(!n->prev);

    Data* p = child;
    while (p && p->next) {
        p = p->next;
    }
    if (p) {
        p->next = n;
        n->prev = p;
    } else {
        child = n;
    }

    n->parent = this;
}

void Data::Remove()
{
    if (prev)
        prev->next = next;
    else
        parent->child = next;
    if (next)
        next->prev = prev;

    next = 0;
    prev = 0;
    parent = 0;
}

void Data::SetPreComment(const char* comment)
{
    SafeDeleteArray(preComment);
    preComment = DupString(comment);
}

void Data::SetPostComment(const char* comment)
{
    SafeDeleteArray(preComment);
    postComment = DupString(comment);
}

void Data::SetMeta(Data* d)
{
    SafeDelete(meta);
    meta = d;
}

// --------------------------------------------------------------------------
// Path Language
// --------------------------------------------------------------------------

// TODO: this is mostly copied from the OGDL path language reference 
// implementation.   Write our own version

static bool IsPathWordChar(int c)
{
    switch (c) {
    case '.':
    case '[':
    case '(':
    case '{':
    case ']':
    case ')':
    case '}':         
    case '"':
    case ';':
    case ',':
    case ' ':
    case '\t':
    case 0:
        return false;
    }
    return true;
}

static const char* PathElement(const char* path, char* buf)
{
    char c;

    if (!*path)
        return 0;

    switch (c = *path) {
    case '.':
        *buf++ = *path++;
        break;

    case '[' :
        while ( (c = *path) != '\0' && c != ']' ) {
            *buf++ = *path++;
        }
        path++;
        break;

    case '{':
        while ( (c = *path) != '\0' && c != '}' ) {
            *buf++ = *path++;
        }
        path++;
        break;
        
    case '\'':
        path++;
        while ( ( c =*path) != '\0' && c != '\'' ) {
            *buf++ = *path++;
        }
        path++;
        break;
    
    case '"':
        path++;
        while ( (c = *path) != '\0' && c != '"' ) {
            *buf++ = *path++;
        }
        path++;
        break;
    
    default:
        while ( (c = *path) != '\0' && IsPathWordChar(c) ) {
            *buf++ = *path++;
        }
    }
    
    *buf = 0;
        
    return path;
}

static bool IsValidIndex(const char* p)
{
    while (*p && XlIsDigit(*p))
        ++p;

    return (*p == '\0');
}

static void FindHelper(Data* g, const char* path, std::vector<Data*>& result)
{
    Data* up = 0;
    const char* p = path;

    char e[256], last[256];
    last[0] = 0;    // used also to distinguish between x.[n] and x[n]
    
    while ((p = PathElement(p,e)) != 0) {
        if (e[0] == '[') {

            if (!IsValidIndex(e+1))
                return;

            int n = XlAtoI32(e+1);  

            if (!last[0]) {
                if (n < 0 || !e[1]) {   // allow [0], but not []
                    return;           // syntax error
                }
                if (g->Size() > n) {
                    g = g->ChildAt(n);
                } else {
                    return;
                }
            } else {
                if (!up) {
                    return;
                }
                
                if (e[1] == 0) {        // this means []
                    // new data and get all elements with this value
                    for (int i = 0; i < up->Size(); ++i) {
                        Data* node = up->ChildAt(i);
                        if (!XlComparePrefix(node->value, last, 256)) {
                            result.push_back(node);
                        }
                    }
                    // XXX We only support this operator at the end of a path.
                    return;

                } else if (n >= 0) {
                    g = 0;
                    for (int i = 0; i < up->Size(); ++i) {
                        Data* node = up->ChildAt(i);
                        if (!XlComparePrefix(node->value, last, 256)) {
                            if (!n--) {
                                g = node;
                                break;
                            }
                        }
                    }

                    // Did not find [n] index.
                    if (!g)
                        return;

                } else if (n < 0) {
                    // Negative index is not allowed.
                    return;
                }
                last[0] = 0;
            }
        } else if (e[0] == '.' && e[1] == '\0') {
            last[0] =  0;
        } else {
            up = g;
            if ((g = g->ChildWithValue(e)) == 0) {
                return;
            }
            XlCopyString(last, dimof(last), e);
        }
    }
    result.push_back(g);
}

Data* Data::Find(const char* path) const
{
    std::vector<Data*> result;
    FindHelper(const_cast<Data*>(this), path, result);
    if (!result.empty())
        return result[0];
    return 0;
}

static void ValuePath(const Data* data, char* dst, int count)
{
    const Data* other;
    const char* value = data->value;

    int i;
    for (i = 0, other = data->PrevWithValue(value); other; 
            other = other->PrevWithValue(value))
    {
        ++i;
    }

    char buf[16];
    if (i > 0)
        XlFormatString(buf, dimof(buf), "[%d]", i);
    else
        buf[0] = '\0';

    // Look for '.' characters.
    const char* p;
    for (p = value; *p && *p != '.'; ++p)
        ;

    // Quote the string if we found a period character.  This will need to be
    // smarter to handle all conditions.
    if (*p) {
        XlFormatString(dst, count, "'%s'%s", value, buf);
    } else {
        XlFormatString(dst, count, "%s%s", value, buf);
    }
}

void Data::Path(char* dst, int count)
{
    if (!parent || !parent->parent) {
        ValuePath(this, dst, count);
    } else {
        parent->Path(dst, count);
        XlCatString(dst, count, ".");
        size_t len = XlStringSize(dst);
        ValuePath(this, dst + len, int(count - len));
    }
}

// --------------------------------------------------------------------------
// Parser
// --------------------------------------------------------------------------

#define BSIZE 16384
#define MAX_INDENT 128

static bool IsEscapeableChar(int c)
{
    return (c == '\'' || c == '\"' || c == '\\');
}

static bool IsBreakChar(int c)
{
    return (c == '\n' || c == '\r');
}

// static bool IsSpaceChar(int c)
// {
//     return (c == ' ' || c == '\t');
// }

static bool IsWordChar(int c)
{
    // check separator characters
    if ( c == ' '  || c == '\t' ||
         c == '\n' || c == '\r' ||
         c == '('  || c == ')'  || c == ',' )
        return false;

    // check ranges
    return ( c > ' ' );
}

static bool IsValidChar(int c)
{
    return ( c == -1 || c >= ' ' || c == '\t' || c == '\n' || c == '\r' );
}

class DataParser {
public:
    DataParser(Data* root);
    ~DataParser();

    bool InitFromFile(OSServices::BasicFile& file);
    void InitFromString(const char* str, int len);

    int Space();
    void Newline();
    Data* Scalar();
    Data* List();
    Data* Group();
    void Line();
    void Graph();

    bool Error() { return _error; }

private:
    void Init();
    void Report(const char* msg);
    void NextChar();
    void ClearBuffer();
    void SaveChar(int c);
    void SaveCurrent();
    void Match(int t);
    void MatchClose(int t, int lineNo);

    char* _data;
    int _count;
    const char* _p;
    int _lookahead;
    std::unique_ptr<char[]> _buf;
    int _offset;
    Data* _lineParent;
    int _parentIndent[MAX_INDENT];
    Data* _parent[MAX_INDENT];
    int _level;
    int _nest;
    bool _startOfLine;
    char* _comment;
    int _lineNum;

    // error reporting
    bool _reportTabs;
    bool _reportToken;
    bool _error;
};

DataParser::DataParser(Data* root)
{
    _data = NULL;
    _count = 0;
    _p = _data;
    _lookahead = -1;
    _offset = 0;
    _lineParent = 0;
    _parent[0] = root;
    _parentIndent[0] = -1;
    _level = 0;
    _nest = 0;
    _startOfLine = true;
    _comment = 0;
    _lineNum = 1;

	_buf = std::make_unique<char[]>(BSIZE);

    _reportTabs = true;
    _reportToken = true;
    _error = false;
}

DataParser::~DataParser()
{
    delete [] _data;
    free(_comment);
}

void DataParser::Report(const char* msg)
{
//    LogSave(LOG_PRI_ERROR, _parent[0]->value, _lineNum);
//    CommonDllWarning("%s", msg);
    _error = true;
    _lookahead = -1;
}

void DataParser::Init()
{
    assert(XlIsValidUtf8((const utf8*)_p, _count));

    // scan for encoding tags
    if (XlHasUtf8Bom((const utf8*)_p)) {
        _p += 3;
        _count -= 3;
    }

    NextChar();
}

bool DataParser::InitFromFile(OSServices::BasicFile& file)
{
    // MemoryMap* mm = XlOpenMemoryMap(filename);
    // if (!mm) {
    //     //CommonDllWarning("Failed to open file: %s", filename);
    //     return false;
    // }
    // 
    // _data = new char[mm->len];
    // XlCopyMemory(_data, mm->addr, mm->len);
    // _count = mm->len;
    // _p = _data;
    // 
    // XlCloseMemoryMap(mm);

    delete[] _data; _data = NULL;

    size_t size = 0;
    {
        auto start = file.TellP();
		file.Seek(0, OSServices::FileSeekAnchor::End);
        size = file.TellP() - start;
        file.Seek(start);

        _data = new char[size];
        file.Read(_data, 1, size);
    }
    _count = (int)size;
    _p = _data;

    Init();

    return true;
}

void DataParser::InitFromString(const char* str, int len)
{
    _data = new char[len];
    XlCopyMemory(_data, str, len);

    _p = _data;
    _count = len;
    Init();
}

void DataParser::NextChar()
{
    if (_count == 0) {
        _lookahead = -1;
    } else {
        if (IsBreakChar(_lookahead)) {
            _startOfLine = true;
            _lineNum++;
        } else if (IsWordChar(_lookahead)) {
            _startOfLine = false;
        }

        _lookahead = *(uint8*)_p++;
        _count--;

        if (_lookahead == '\r' && _count > 0) {
            if (*_p == '\n') {
                _p++;
                _count--;
            }
        }

        if (_lookahead == '\t') {
            if (_reportTabs) {
                Report("Data stream contains tabs.");
                _reportTabs = false;
            }
        }
    }
}

void DataParser::ClearBuffer()
{
    _offset = 0;
    _reportToken = true;
}

void DataParser::SaveChar(int c)
{
    if (_offset >= BSIZE) {
        if (_reportToken) {
            Report("lexer token too large");
            _reportToken = false;
        }
    } else {
        _buf.get()[_offset++] = (char)c;
    }
}

void DataParser::SaveCurrent()
{
    SaveChar(_lookahead);
    NextChar();
}

void DataParser::Match(int t)
{
    if (_lookahead == t) {
        NextChar();
    } else {
        char buf[50];
        XlFormatString(buf, dimof(buf), "Syntax error.  Expecting '%c'", t);
        Report(buf);
    }
}

void DataParser::MatchClose(int t, int lineNo)
{
    if (_lookahead == t) {
        NextChar();
    } else {
        char buf[50];
        XlFormatString(buf, dimof(buf), "Syntax error.  Expecting '%c' opened on line %d", t, lineNo);
        Report(buf);
    }
}

int DataParser::Space()
{
    int i = 0;
    while (_lookahead == ' ' || _lookahead == '\t' || 
                (_nest && (_lookahead == '\r' || _lookahead == '\n'))) {
        NextChar();
        ++i;
    }
    return i;
}

void DataParser::Newline()
{
    if (_lookahead == '\r') {
        NextChar();
        if (_lookahead == '\n') {
            NextChar();
        }
    } else if (_lookahead == '\n') {
        NextChar();
    } else {
        char buf[50];
        XlFormatString(buf, dimof(buf),
            "Syntax error.  Expecting newline, got '%c'", _lookahead);
        Report(buf);
    }
}

Data* DataParser::Scalar()
{
    int startLineNum = _lineNum;
    ClearBuffer();

    if (_lookahead == '#') {
        bool startOfLine = _startOfLine;
        NextChar();

        if (_lookahead == '?') {
            NextChar();
            Data* meta = new Data("#?");

            if (_comment) {
                meta->SetPreComment(_comment);
                free(_comment);
                _comment = 0;
            }

            Data* oldParent = _lineParent;
            _lineParent = meta;
            List();
            _lineParent = oldParent;

            if (_level > 1) {
                Report("meta data can only exist at the top level");
                delete meta;
            } else {
                // This replaces any previous meta data.
                 _parent[0]->SetMeta(meta);
            }

            return 0;
        } else {
            SaveChar('#');

            while (_lookahead > 0 && !IsBreakChar(_lookahead))
                SaveCurrent();

            if (startOfLine) {
                SaveChar('\n');
                SaveChar('\0');
                size_t oldLen = _comment ? XlStringSize(_comment) : 0;
                size_t newLen = oldLen + XlStringSize(_buf.get()) + 1;
                auto newComment = (char*)realloc(_comment, newLen);
				if (newComment) {
					_comment = newComment;
					_comment[oldLen] = '\0';
					XlCatString(_comment, newLen, _buf.get());
				} else {
						// failure during realloc...
						//	Just cleanup _comment
					assert(0);
					free(_comment);
					_comment = nullptr;
				}
            } else {
                SaveChar('\0');
                _lineParent->SetPostComment(_buf.get());
            }

            return 0;
        }
    }

    char quote;
    if (_lookahead == '\'')
        quote = '\'';
    else if (_lookahead == '\"')
        quote = '\"';
    else
        quote = 0;

    if (quote) {
        bool reportTabs = _reportTabs;
        _reportTabs = false;
        NextChar();
        while (_lookahead >= 0 && _lookahead != quote) {
            if (_lookahead == '\\') {
                NextChar();
                if (!IsEscapeableChar(_lookahead))
                    SaveChar('\\');
                SaveCurrent();

            } else if (IsBreakChar(_lookahead)) {
                // All EOL's become \n in the value.
                Newline();
                SaveChar('\n');

            } else {
                SaveCurrent();
            }
        }

        if (_lookahead != quote)
            Report("missing end quote");

        NextChar();
        _reportTabs = reportTabs;
    } else if (_lookahead == '\\') {
        NextChar();
        Space();

        int saveLookahead = _lookahead;
        int saveLineNum = _lineNum;
        int saveCount = _count;
        const char* saveP = _p;

        Newline();
        startLineNum = _lineNum;
        int minIndent = _parentIndent[_level];
        int baseIndent = Space();
        int indent = baseIndent;
        while (_lookahead >= 0 && (indent > minIndent || IsBreakChar(_lookahead))) {

            // save indentation past base indent
            for (int i = 0; i < (indent - baseIndent); ++i) {
                SaveChar(' ');
            }

            // save to end of line
            while (_lookahead > 0 && !IsBreakChar(_lookahead))
                SaveCurrent();

            saveLookahead = _lookahead;
            saveLineNum = _lineNum;
            saveCount = _count;
            saveP = _p;

            Newline();
            indent = Space();
            SaveChar('\n');
        }
        // unget extra space that has been parsed
        _lookahead = saveLookahead;
        _startOfLine = false;
        _lineNum = saveLineNum;
        _count = saveCount;
        _p = saveP;

    } else {
        if (!(IsValidChar(_lookahead))) {
            Report("illegal character in file");
            SaveCurrent();
        } else {
            while (IsWordChar(_lookahead))
                SaveCurrent();
        }
    }

    Data* n;
    if (_offset > 0 || quote) {
        SaveChar('\0');

        n = new Data(_buf.get());
        if (_comment) {
            if (!_lineParent->parent && !_lineParent->child) {
                // special case for first comment in the file
                _lineParent->SetPreComment(_comment);
            } else {
                n->SetPreComment(_comment);
            }
            free(_comment);
            _comment = 0;
        }
        n->lineNum = startLineNum;

        _lineParent->Add(n);
        _lineParent = n;
    } else {
        n = 0;
    }

    return n;
}

Data* DataParser::List()
{
    Data* p = _lineParent;
    Data* n = Group();

    for (;;) {
        Space();
        if (_lookahead < 0 || _lookahead == ')' || 
                (!_nest && (_lookahead == '\r' || _lookahead == '\n')))
            break;
        if (_lookahead == ',') {
            Match(',');
            _lineParent = p;
        }

        Space();
        Group();
    }
    return n;
}

Data* DataParser::Group()
{
    Data* n;

    if (_lookahead == '(') {
        int lineNum = _lineNum;
        Match('(');
        ++_nest;
        Space();
        n = List();
        Space();
        --_nest;
        MatchClose(')', lineNum);
    } else {
        n = Scalar();
    }

    return n;
}

void DataParser::Line()
{
    if (_level >= MAX_INDENT - 1) {
        _error = true;
        // XlLogError("level overflow in line parsing!!");
        return;
    }

    int level = _level;

    int indent = Space();

    if (indent > _parentIndent[_level]) {
        ++_level;
    } else {
        while (indent <= _parentIndent[_level-1]) {
            --_level;
        }
    }

    _lineParent = _parent[_level-1];
    Data* n = List();
    Space();

    if (n) {
        _parentIndent[_level] = indent;
        _parent[_level] = n;
    } else {
        _level = level;
    }
}


void DataParser::Graph()
{
    Line();
    while (_lookahead >= 0 && !_error) {
        Newline();

        if (_count == 0) {
            break;
        }

        Line();
    }

    if (_comment) {
        _parent[0]->SetPostComment(_comment);
    }
}

// --------------------------------------------------------------------------
// Serialization
// --------------------------------------------------------------------------

#define MAX_LINE 78

bool Data::Load(const char* ptr, int len)
{
    if (len <= 0) {
        return true;
    }

    DataParser parser(this);
    parser.InitFromString(ptr, len);

    Clear();
    parser.Graph();
    return !parser.Error();
}

bool Data::LoadFromFile(OSServices::BasicFile& src)
{
    DataParser parser(this);
    if (!parser.InitFromFile(src)) 
        return false;

    Clear();
    parser.Graph();
    return !parser.Error();
}

static void PrintIndent(OutputStream& f, int level)
{
    for (int i = 0; i < level; ++i) {
        f.Write((const utf8*)"    ");
    }
}

static bool DepthCounter(const Data* data, int i)
{
    if (i >= 3) return false;

    foreachData (child, data) {
        if (!DepthCounter(child, i + 1))
            return false;
    }

    return true;
}

static void PrintText(OutputStream& f, const Data* data)
{
    bool needsQuote = false;
    size_t len = XlStringSize(data->value);
    for (size_t i = 0; i < len; ++i) {
        if (!IsWordChar(data->value[i])) {
            needsQuote = true;
            break;
        }
    }

    if (needsQuote) {
        f.WriteChar('\"');
        const char* p = data->value;
        while (*p) {
            if (IsBreakChar(*p)) {
                f.Write((const utf8*)"\r\n");
            } else if (*p == '\\') {
                if (IsEscapeableChar(*(p + 1)))
                    f.WriteChar('\\');
                f.WriteChar(*p);
            } else if (*p == '\"') {
                f.WriteChar('\\');
                f.WriteChar(*p);
            } else {
                f.WriteChar(*p);
            }
            ++p;
        }
        f.WriteChar('\"');
        return;
    } else if (!data->value[0]) {
        f.Write((const utf8*)"\"\"");
        return;
    }

    f.Write((const utf8*)data->value);
}

static bool IsSingleLine(const char* p)
{
    while (*p) {
        if (IsBreakChar(*p)) {
            return false;
        }
        ++p;
    }
    return true;
}

static bool IsSingleLine(const Data* data, int& count)
{
    if (data->meta) return false;
    if (data->preComment) return false;
    if (data->postComment) return false;
    if (!DepthCounter(data, 0)) return false;
    if (!IsSingleLine(data->value)) return false;

    count += (int)XlStringSize(data->value);

    if (data->child) {
        if (data->child->next)
            count += 3;
        else
            count += 1;

        if (!IsSingleLine(data->child, count)) return false;

        for (Data* child = data->child->next; child; child = child->next) {
            count += 2;
            if (!IsSingleLine(child, count)) return false;
        }

        if (data->child->next)
            count += 2;
    }

    if (count >= MAX_LINE) return false;

    return true;
}

static void PrintSingleLine(OutputStream& f, const Data* data)
{
    PrintText(f, data);

    if (data->child) {
        if (data->child->next)
            f.Write((const utf8*)" ( ");
        else
            f.WriteChar(' ');

        PrintSingleLine(f, data->child);

        for (Data* child = data->child->next; child; child = child->next) {
            f.Write((const utf8*)", ");
            PrintSingleLine(f, child);
        }

        if (data->child->next)
            f.Write((const utf8*)" )");
    }
}

static void PrintComment(OutputStream& f, int level, const char* p)
{
    while (*p) {
        if (*p == '\n') {
            f.Write((const utf8*)"\r\n");
            PrintIndent(f, level);
            ++p;
        } else {
            f.WriteChar(*p++);
        }
    }
}

static void PrettyPrint(OutputStream&f, int level, const Data* data, bool includeComment = true)
{
    PrintIndent(f, level);
    int count = 0;
    if (IsSingleLine(data, count)) {
        PrintSingleLine(f, data);
        f.Write((const utf8*)"\r\n");
        return;
    }

    if (data->preComment && includeComment) {
        PrintComment(f, level, data->preComment);
    }

    PrintText(f, data);

    if (data->postComment && includeComment) {
        f.WriteChar(' ');
        f.Write((const utf8*)data->postComment);
    }

    f.Write((const utf8*)"\r\n");

    foreachData(child, data) {
        PrettyPrint(f, level+1, child, includeComment);
    }
}

bool Data::SavePrettyValue(char* s, int* len) const
{
    FixedMemoryOutputStream<char> stream(s, (size_t)*len);
    PrintText(stream, this);
    stream.WriteChar(0);
    *len = (int)stream.GetBuffer().LengthChars();
    return true;
}

void Data::SaveToOutputStream(OutputStream& f, bool includeComment) const
{
    if (meta) {
        if (meta->preComment && includeComment)
            PrintComment(f, 0, meta->preComment);

        PrettyPrint(f, 0, meta, includeComment);
        f.Write((const utf8*)"\r\n\r\n");
    }

    if (preComment && includeComment) {
        PrintComment(f, 0, preComment);
    }
    foreachData(childq, this) {
        PrettyPrint(f, 0, childq, includeComment);
    }
    if (postComment && includeComment) {
        PrintComment(f, 0, postComment);
    }
}

bool Data::SaveToBuffer(char* s, int* len) const
{
    FixedMemoryOutputStream<char> stream(s, (size_t)*len);
    SaveToOutputStream(stream);
    stream.WriteChar(0);
    return true;
}

}

