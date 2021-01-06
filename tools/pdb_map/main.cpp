#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <malloc.h>
#include <comdef.h>
#include <Windows.h>
#include <DbgHelp.h>
#include "cvconst.h"

#pragma comment(lib, "dbghelp.lib")

HANDLE handle;
DWORD64 address = 0x10000000;

char dt[255] = {};
int dtLen = 0;

bool GetInfo(ULONG index, IMAGEHLP_SYMBOL_TYPE_INFO typeInfo, PVOID pInfo)
{
    return SymGetTypeInfo(handle, address, index, typeInfo, pInfo) == TRUE;
}

struct Symbol;

Symbol* symbolStack = NULL;

const char* lastFile = NULL;

#define PUSH_SPACE    dt[dtLen++] = ' '; dt[dtLen++] = ' '; dt[dtLen++] = ' '; dt[dtLen++] = ' '; dt[dtLen] = '\0';
#define POP_SPACE     dt[dtLen -= 4] = 0;
#define SPACE         printf(dt);

int funcs = 0;

struct MyClass {
    uint32_t x : 5;
    uint32_t y : 8;
    uint32_t z : 2;
    virtual void foo() { printf(""); };
};

MyClass* abra;

struct Symbol {

    enum Type {
        TYPE_MAIN,
        TYPE_CONSTANT,
        TYPE_ENUM,
        TYPE_STRUCT,
        TYPE_CLASS,
        TYPE_UNION,
        TYPE_INTERFACE,
        TYPE_VARIABLE,
        TYPE_VTABLE,
        TYPE_FUNCTION,
    };

    Symbol* stack;
    Symbol* next;
    Symbol* childFirst;
    Symbol* childLast;

    ULONG64 symAddr;

    Type    type;
    DWORD   tag;
    ULONG   index;
    ULONG   typeIndex;
    ULONG   callConv;
    ULONG   dataKind;
    ULONG   baseClass;
    VARIANT value;
    int32_t childsCount;
    bool    showValue;
    bool    isBit;
    bool    isVirtual;

    char*   name;
    char*   file;
    int     line;

    Symbol(Symbol::Type type, ULONG index, DWORD tag, const char *name) :
        stack(NULL), next(NULL), childFirst(NULL), childLast(NULL), symAddr(0),
        type(type), index(index), tag(tag), typeIndex(0xFFFFFFFF), callConv(0), dataKind(DataIsUnknown), baseClass(0xFFFFFFFF),
        childsCount(0), showValue(true), isBit(false),
        name(NULL), file(NULL), line(0)
    {
        if (name) {
            int len = strlen(name);
            this->name = new char[len + 1];
            strcpy(this->name, name);
        }
        
        if (symbolStack && symbolStack->type == Symbol::TYPE_MAIN && GetInfo(index, TI_GET_ADDRESS, &symAddr))
        {
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(line);
            DWORD symDisp;
            if (SymGetLineFromAddr64(handle, symAddr, &symDisp, &line))
            {
                setSource(line.FileName, line.LineNumber);
            }
        }

        value.vt = VT_EMPTY;
        if (type == TYPE_VARIABLE) {
            if (!GetInfo(index, TI_GET_VALUE, &value)) {
                value.vt = VT_EMPTY;
            }
        }
    }

    ~Symbol()
    {
        Symbol* child = childFirst;

        while (child)
        {
            Symbol* next = child->next;
            delete child;
            child = next;
        }

        delete[] name;
        delete[] file;
    }

    static int cmpType(const void *arg1, const void *arg2)
    {
        Symbol* s1 = *(Symbol**)arg1;
        Symbol* s2 = *(Symbol**)arg2;

        int d = s1->type - s2->type;
        if (d) return d;

        return s1->index - s2->index;
    }

    static int cmpFile(const void *arg1, const void *arg2)
    {
        Symbol* s1 = *(Symbol**)arg1;
        Symbol* s2 = *(Symbol**)arg2;

        if (s1->file && s2->file) {
            int cmp = strcmp(s1->file, s2->file);
            if (cmp == 0)
                return s1->line - s2->line;
            return cmp;
        }

        return s1->file ? 1 : -1;
    }

    Symbol* findEqualSymbol(ULONG index, DWORD tag)
    {
        Symbol* c = childFirst;
        while (c) {
            if (tag == c->tag) {
                DWORD typeIndex = c->index;
                if (GetInfo(index, TI_IS_CLOSE_EQUIV_TO, &typeIndex)) {
                    return c;
                }
            }
            c = c->next;
        }
        return NULL;
    }

    Symbol* findChildByIndex(ULONG index)
    {
        Symbol* c = childFirst;
        while (c) {
            if (index == c->index) {
                return c;
            }
            c = c->next;
        }
        return NULL;
    }

    Symbol* addChild(Symbol* child)
    {
        childsCount++;

        if (childLast) {
            childLast->next = child;
            childLast = child;
        } else {
            childFirst = childLast = child;
        }
        return child;
    }

    void setSource(const char* file, int line)
    {
        if (!file) return;
        int len = strlen(file);
        this->file = new char[len + 1];
        strcpy(this->file, file);
        this->line = line;
    }

    void printFile()
    {
        if (!file)
            return;

        if (lastFile && strcmp(lastFile, file) == 0)
            return;

        lastFile = file;

        printf("#endif\n\n");
        SPACE
        printf("#if 1 // %s\n", file);
        SPACE
    }

    void printMods(ULONG index)
    {
        // TODO const modifier?
    }

    void printName(ULONG index, const char* space = "")
    {
        WCHAR *wName = NULL;
        if (GetInfo(index, TI_GET_SYMNAME, &wName)) {
            _bstr_t bstr(wName);
            const char *name = bstr;
            if (name && strcmp(name, "__formal") != 0) {
                if (strcmp(name, "HWND__") == 0) {
                    name = "HWND";
                }
                printf("%s%s", space, name);
            }
            LocalFree(wName);
        }
    }

    void printBaseType(ULONG index)
    {
        DWORD baseType;
        if (GetInfo(index, TI_GET_BASETYPE, &baseType)) {
            switch (baseType) {
                case btNoType  : printf("[! NO TYPE]"); return;
                case btVoid    : printf("void"); return;
                case btChar    : printf("char"); return;
                case btWChar   : printf("wchar_t"); return;
                case btInt     : printf("int32_t"); return;
                case btUInt    : printf("uint32_t"); return;
                case btFloat   : printf("float"); return;
                case btBool    : printf("bool"); return;
                case btLong    : printf("int32_t"); return;
                case btULong   : printf("uint32_t"); return;
                case btHresult : printf("HRESULT"); return;
                case btChar16  : printf("char16_t"); return;
                case btChar32  : printf("char16_t"); return;
                default        : printf("[! UNKNOWN BASE TYPE]"); return;
            }
        }
        printf("[! NO BASETYPE]");
    }

    void printArray(ULONG index)
    {
        ULONG nextIndex;

        if (GetInfo(index, TI_GET_TYPEID, &nextIndex)) {
            DWORD tag;
            if (GetInfo(nextIndex, TI_GET_SYMTAG, &tag) && tag == SymTagArrayType) {
                DWORD count;
                if (GetInfo(nextIndex, TI_GET_COUNT, &count)) {
                    printf("[%d]", count);
                    printArray(nextIndex);
                }
            }
        }
    }

    void printBits(ULONG index) {
        ULONG64 bits;
        if (GetInfo(index, TI_GET_LENGTH, &bits))
        {
            printf(" : %d", (int)bits);
        }
    }

    void printType(ULONG index)
    {
        DWORD tag;
        if (!GetInfo(index, TI_GET_SYMTAG, &tag)) {
            printf("[! NO TAG]");
        }

        ULONG nextIndex;

        switch (tag) {
            case SymTagData:
                if (GetInfo(index, TI_GET_TYPEID, &nextIndex)) {
                    printType(nextIndex);
                } else {
                    printf("[! NO TYPE]");
                }
                break;
            case SymTagEnum:
                printName(index);
                break;
            case SymTagFunctionType:
                if (GetInfo(index, TI_GET_TYPEID, &nextIndex)) {
                    printType(nextIndex);
                } else {
                    printf("[! NO FUNC TYPE]");
                }
                break;
            case SymTagPointerType:
                if (GetInfo(index, TI_GET_TYPEID, &nextIndex)) {
                    printType(nextIndex);
                } else {
                    printf("[! NO POINTER]");
                }

                BOOL isRef;
                if (GetInfo(index, TI_GET_IS_REFERENCE, &isRef) && isRef) {
                    printf("&");
                } else {
                    printf("*");
                }
                break;
            case SymTagArrayType:
                if (GetInfo(index, TI_GET_TYPEID, &nextIndex)) {
                    printType(nextIndex);
                } else {
                    printf("[! NO ARRAY]");
                }
                break;
            case SymTagUDT:
                printName(index);
                break;
            case SymTagBaseType:
                printBaseType(index);
                break;
            default:
                printf("[! BAD TAG]");
        }
    }

    int64_t valueAsInt()
    {
        switch (value.vt) {
            case VT_I1    : return value.bVal;
            case VT_I2    : return value.iVal;
            case VT_I4    : return (value.lVal == INT_MIN) ? 0x80000000 : value.lVal;
            case VT_UI1   : return value.cVal;
            case VT_UI2   : return value.uiVal;
            case VT_UI4   : return value.ulVal;
            default       : return 0;
        }
    }

    int32_t valueAsBit(int64_t x)
    {
        int32_t count = 0;
        while (x >> 1) {
            x >>= 1;
            count++;
        }
        return count;
    }

    void printValue()
    {
        switch (value.vt) {
            case VT_I1    :
            case VT_I2    :
            case VT_I4    :
            case VT_UI1   :
            case VT_UI2   :
            case VT_UI4   : {
                int64_t v = valueAsInt();
                if (v < 0) {
                    printf("%d", (int32_t)v);
                } else {
                    if ((v != 0) && isBit) {
                        printf("(1 << %d)", valueAsBit(v));
                    } else {
                        printf("%lu", (uint32_t)v);
                    }
                }
                break;
            }
            case VT_EMPTY : printf("[! EMPTY VALUE]"); break;
            default       : printf("[! BAD VALUE %d]", (int)value.vt);
        }
    }

    void printCallConv()
    {
        switch (callConv) {
            case CV_CALL_NEAR_FAST : printf(" __fastcall"); break;
            case CV_CALL_NEAR_STD  : printf(" __stdcall"); break;
            case CV_CALL_NEAR_SYS  : printf(" __syscall"); break;
        }
    }

    void checkEnumValues()
    {
        bool lastAsAlign = false;
        bool isSeq = true;
        bool isBit = true;

        int32_t count = 0;
        { // analyze values sequence
            int64_t lastValue = (childFirst ? childFirst->valueAsInt() : 0) - 1;
            Symbol* child = childFirst;
            while (child) {
                assert(child->type == TYPE_VARIABLE);
                assert(child->dataKind == DataIsConstant);

                int64_t value = child->valueAsInt();

                if (isSeq && (value - lastValue != 1)) {
                    if (child->next == NULL) {
                        lastAsAlign = true;
                    }
                    isSeq = false;
                }

                if (value < 0 || (value > 0 && (value & (value - 1)))) {
                    isBit = false;
                }

                count++;

                lastValue = value;

                child = child->next;
            }

        // we need more that 3 values to be sure
            if (isBit && count <= 3) {
                isBit = false;
            }

            if (lastAsAlign && count <= 3) {
                lastAsAlign = false;
            }
        }

        { // set params
            Symbol* child = childFirst;
            while (child) {
                if (!isSeq) {
                    if (lastAsAlign) {
                        child->showValue = child->next == NULL;
                    } else {
                        child->showValue = true;
                    }
                } else {
                    child->showValue = false;
                }

                if (child == childFirst) { // we allow first value to be non zero in enum sequence
                    if (child->valueAsInt() != 0) {
                        child->showValue = true;
                    } else {
                        if (isBit) {
                            child->showValue = false;
                        }
                    }
                }

                child->isBit = isBit;

                child = child->next;
            }
        }
    }

    void serialize()
    {
        switch (type) {
            case Symbol::TYPE_MAIN: {
                Symbol** childs = new Symbol*[childsCount];
                int index = 0;

                Symbol* c = childFirst;
                while (c)
                {
                    childs[index++] = c;
                    c = c->next;
                }

                qsort(childs, childsCount, sizeof(Symbol*), Symbol::cmpFile);

                printf("#if 1 // no source file\n");

                lastFile = NULL;
                for (int i = 0; i < childsCount; i++) {
                    if (childs[i]->type == TYPE_FUNCTION) funcs++;
                    childs[i]->serialize();
                }
                delete[] childs;

                printf("#endif\n");
                break;
            }
            case Symbol::TYPE_VARIABLE:
            {
                if (dataKind == DataIsStaticMember) {
                    printf("static ");
                }

                if (dataKind == DataIsConstant) {
                    printName(index);
                    if (showValue) {
                        printf(" = ");
                        printValue();
                    }
                } else {
                    printMods(index);
                    printType(index);
                    printName(index, " ");
                    printArray(index);
                    printBits(index);
                }

                switch (dataKind) {
                    case DataIsParam:
                        if (next) {
                            printf(", ");
                        }
                        break;
                    case DataIsFileStatic:
                    case DataIsGlobal: 
                    case DataIsMember:
                    case DataIsStaticMember:
                        printf(";\n");
                        break;
                    case DataIsConstant:
                        printf(",\n");
                        break;
                    default :
                        printf("[! DATA KIND]");
                }
                break;
            }

            case Symbol::TYPE_FUNCTION:
            {
                printFile();

                printType(typeIndex);
                printCallConv();

                printf(" %s(", name);

                Symbol* child = childFirst;
                while (child) {
                    child->serialize();
                    child = child->next;
                }

                printf(");\n");
                break;
            }

            case Symbol::TYPE_ENUM:
            {
                checkEnumValues();

                if (strcmp(name, "__unnamed") != 0) {
                    printf("enum %s", name);
                } else {
                    printf("enum");
                }
                printf("\n");
                SPACE
                printf("{\n");

                PUSH_SPACE
                Symbol* child = childFirst;
                while (child) {
                    SPACE
                    child->serialize();
                    child = child->next;
                }
                POP_SPACE

                SPACE
                printf("};\n\n");
                break;
            }

            case Symbol::TYPE_STRUCT:
            case Symbol::TYPE_CLASS:
            case Symbol::TYPE_UNION:
            case Symbol::TYPE_INTERFACE:
            {
                printFile();

                switch (type) {
                    case Symbol::TYPE_STRUCT    : printf("struct"); break;
                    case Symbol::TYPE_CLASS     : printf("class"); break;
                    case Symbol::TYPE_UNION     : printf("union"); break;
                    case Symbol::TYPE_INTERFACE : printf("interface"); break;
                }
                
                printf(" %s", name);
                if (baseClass != 0xFFFFFFFF) {
                    if (type == Symbol::TYPE_CLASS) {
                        printName(baseClass, " : public ");
                    } else {
                        printName(baseClass, " : ");
                    }
                }
                printf("\n");
                SPACE
                printf("{\n");

                PUSH_SPACE

                // sort child
                Symbol** childs = new Symbol*[childsCount];
                int index = 0;
                Symbol* c = childFirst;
                while (c)
                {
                    childs[index++] = c;
                    c = c->next;
                }

                qsort(childs, childsCount, sizeof(Symbol*), Symbol::cmpType);
                
                Type lastType = TYPE_MAIN;
                for (int i = 0; i < childsCount; i++) {
                    if (lastType == TYPE_VARIABLE && childs[i]->type != TYPE_VARIABLE) {
                        printf("\n");
                    }
                    SPACE
                    childs[i]->serialize();

                    lastType = childs[i]->type;
                }
                delete[] childs;

                POP_SPACE

                SPACE
                printf("};\n\n");
                break;
            }

            case Symbol::TYPE_VTABLE:
            {
                printf("// TODO vtable\n");
                PUSH_SPACE
                Symbol* child = childFirst;
                while (child) {
                    SPACE
                    child->serialize();
                    child = child->next;
                }
                POP_SPACE
                printf("\n");
                break;
            }
        }
    }

};

Symbol* addSymbol(Symbol* symbol)
{
    return symbolStack->addChild(symbol);
}

Symbol* pushSymbol(Symbol* symbol)
{
    symbol->stack = symbolStack;
    symbolStack = symbol;

    return symbol;
}

void popSymbol()
{
    assert(symbolStack);
    symbolStack = symbolStack->stack;
}

void addSymbol(ULONG index);

bool addChilds(ULONG index)
{
    DWORD childsCount = 0; 
    GetInfo(index, TI_GET_CHILDRENCOUNT, &childsCount);

    if (childsCount > 0)
    {
        TI_FINDCHILDREN_PARAMS *childs = (TI_FINDCHILDREN_PARAMS*)alloca(sizeof(TI_FINDCHILDREN_PARAMS) + childsCount * sizeof(ULONG));
        childs->Count = childsCount;
        childs->Start = 0;
        GetInfo(index, TI_FINDCHILDREN, childs);

        for (DWORD i = 0; i < childsCount; i++)
        {
            if (childs->ChildId[i] == index)
                continue;

            addSymbol(childs->ChildId[i]);
        }
    }

    return childsCount > 0;
}

void addUDT(ULONG index, DWORD tag, const char *name)
{
    Symbol* symbol = pushSymbol(addSymbol(new Symbol(Symbol::TYPE_STRUCT, index, tag, name)));

    DWORD udtKind;
    if (!GetInfo(index, TI_GET_UDTKIND, &udtKind)) {
        printf("[! UDT KIND]\n");
        return;
    }

    switch (udtKind) {
        case UdtStruct    : symbol->type = Symbol::TYPE_STRUCT; break;
        case UdtClass     : symbol->type = Symbol::TYPE_STRUCT; break; // TYPE_CLASS TODO class access modifiers
        case UdtUnion     : symbol->type = Symbol::TYPE_UNION; break;
        case UdtInterface : symbol->type = Symbol::TYPE_INTERFACE; break;
    }

    addChilds(index);

    popSymbol();
}

void addVTable(ULONG index, DWORD tag, const char *name)
{
    Symbol* symbol = pushSymbol(addSymbol(new Symbol(Symbol::TYPE_VTABLE, index, tag, name)));

    // TODO

    popSymbol();
}

void addData(ULONG index, DWORD tag, const char *name)
{
    Symbol* symbol = pushSymbol(addSymbol(new Symbol(Symbol::TYPE_VARIABLE, index, tag, name)));

    if (!GetInfo(index, TI_GET_DATAKIND, &symbol->dataKind))
    {
        symbol->dataKind = DataIsUnknown;
    }

    popSymbol();
}

void addTypedef(ULONG index, DWORD tag, const char *name)
{
    DWORD typeIndex;
    if (GetInfo(index, TI_GET_TYPEID, &typeIndex)) {
        addSymbol(typeIndex);
    }
}

void addEnum(ULONG index, DWORD tag, const char *name)
{
    Symbol* symbol = pushSymbol(addSymbol(new Symbol(Symbol::TYPE_ENUM, index, tag, name)));

    addChilds(index);

    popSymbol();
}

void addFunction(ULONG index, DWORD tag, const char *name)
{
    Symbol* symbol = pushSymbol(addSymbol(new Symbol(Symbol::TYPE_FUNCTION, index, tag, name)));

    if (GetInfo(index, TI_GET_TYPEID, &symbol->typeIndex)) {
        if (!GetInfo(symbol->typeIndex, TI_GET_CALLING_CONVENTION, &symbol->callConv)) {
            symbol->callConv = 0xFFFFFFFF;
        }
    }

    addChilds(index);

    popSymbol();
}

void addSymbol(ULONG index)
{
    DWORD tag = SymTagNull;
    if (!GetInfo(index, TI_GET_SYMTAG, &tag))
        return;
    
    WCHAR *wName = NULL;
    GetInfo(index, TI_GET_SYMNAME, &wName);
    _bstr_t bstr(wName);
    const char *name = bstr;

    // filter
    if (symbolStack && symbolStack->type == Symbol::TYPE_FUNCTION) {
        if (!name)
            return;

        if (symbolStack->callConv == CV_CALL_THISCALL && strcmp(name, "this") == 0)
            return; // skip this for methods

        if (tag != SymTagData)
            return;

        DWORD dataKind;
        if (!GetInfo(index, TI_GET_DATAKIND, &dataKind))
            return;

        if (dataKind != DataIsParam)
            return;
    }

    if (tag == SymTagEnum || tag == SymTagUDT) {
        DWORD nested;
        if (GetInfo(index, TI_GET_NESTED, &nested)) {
            if (nested) {
                if (symbolStack->type == Symbol::TYPE_MAIN) {
                    return;
                }
            }
        }
    }

    if (tag == SymTagFunction) {
        if (strcmp(name, "__vecDelDtor") == 0) return;
        if (strcmp(name, "__local_vftable_ctor_closure") == 0) return;
    }

    switch (tag) {
        case SymTagFunction:
            if (!symbolStack->findEqualSymbol(index, tag)) {
                addFunction(index, tag, name);
            }
            break;
        case SymTagEnum:
            if (!symbolStack->findEqualSymbol(index, tag)) {
                addEnum(index, tag, name);
            }
            break;
        case SymTagUDT:
            if (!symbolStack->findEqualSymbol(index, tag)) {
                addUDT(index, tag, name);
            }
            break;
        case SymTagData:
            addData(index, tag, name);
            break;
        case SymTagBaseClass:
            symbolStack->baseClass = index;
            break;
        case SymTagTypedef:
            //addTypedef(index, name);
            break;
        case SymTagVTable:
            addVTable(index, tag, name);
            break;

        case SymTagExe:
        case SymTagFuncDebugStart:
        case SymTagFuncDebugEnd:
        case SymTagHeapAllocationSite:
        case SymTagCallSite:
        case SymTagCallee:
        case SymTagPublicSymbol:
            // ignore
            break;

        case SymTagLabel:
        case SymTagBlock:
        case SymTagFunctionType:
        case SymTagBaseType:
        case SymTagPointerType:
        case SymTagArrayType:
        case SymTagCompiland:
        case SymTagCompilandDetails:
        case SymTagCompilandEnv:
        case SymTagAnnotation:
        case SymTagFriend:
        case SymTagFunctionArgType:
        case SymTagUsingNamespace:
        case SymTagVTableShape:
        case SymTagCustom:
        case SymTagThunk:
        case SymTagCustomType:
        case SymTagManagedType:
        case SymTagDimension:
        case SymTagInlineSite:
        case SymTagBaseInterface:
        case SymTagVectorType:
        case SymTagMatrixType:
        case SymTagHLSLType:
        case SymTagCaller:
        case SymTagExport:
        case SymTagCoffGroup:
        case SymTagMax:
            printf("[! TAG %d]\n", tag);
            break;
        default : assert(false);
    }

    LocalFree(wName);
}

BOOL CALLBACK enumProc(SYMBOL_INFO* info, ULONG size, void* param)
{
    addSymbol(info->Index);
    return TRUE;
}

void parseDbgHelp(const char* pdbFileName)
{
    handle = GetCurrentProcess();

    DWORD options = SymGetOptions();
    options &= ~SYMOPT_DEFERRED_LOADS;
    options |= SYMOPT_LOAD_LINES;
    options |= SYMOPT_IGNORE_NT_SYMPATH;
    options |= SYMOPT_UNDNAME;
    options |= SYMOPT_DEBUG;
    SymSetOptions(options);

    assert(SymInitialize(handle, NULL, FALSE) != FALSE);

    address = SymLoadModule64(handle, NULL, pdbFileName, NULL, address, 0x7fffffff);
    
    assert(address);

    Symbol* mainSymbol = new Symbol(Symbol::TYPE_MAIN, 0, SymTagNull, "MAIN"); 

    pushSymbol(mainSymbol);
    SymEnumTypes(handle, address, enumProc, NULL);
    SymEnumSymbols(handle, address, "*", enumProc, NULL);
    popSymbol();

    mainSymbol->serialize();

    SymCleanup(handle);

}

void parseDia(const char* pdbFileName)
{
    //
}


int main(int argc, char **argv)
{
    if (argc < 2) {
        return -1;
    }

    parseDbgHelp(argv[1]);
    //parseDia(argv[1]);

    return 0;
}