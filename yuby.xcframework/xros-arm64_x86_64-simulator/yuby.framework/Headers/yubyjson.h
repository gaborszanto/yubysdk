#ifndef yubyjsonheader
#define yubyjsonheader
#ifndef NOSTDLIB
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if _WIN32
    #define stringdup _strdup
#else
    #define stringdup strdup
#endif
#endif
#include <stdarg.h>

namespace yuby {
    
struct json {
    json *next, *prev, *firstChild;
    char *key;
    union { char *string; long long int i; double d; int b; } value;
    enum jtype { Null = 0, Bool = 1, Int = 2, Double = 3, String = 4, Array = 5, Object = 6 } type;
    
    void dealloc() {
        json *c = this, *_next;
        while (c) {
            _next = c->next;
            if (c->firstChild) c->firstChild->dealloc();
            if ((c->type == String) && c->value.string) free(c->value.string);
            if (c->key) free(c->key);
            free(c);
            c = _next;
        };
    }
    static json *parse(const char *value, const char **returnParseEnd = 0, bool requireNullTerminated = false) {
        if (!value) return NULL;
        json *root = newItem();
        if (!root) return NULL;
        const char *end = root->parseValue(skip(value));
        if (!end) { root->dealloc(); return NULL; }
        if (requireNullTerminated) {
            end = skip(end);
            if (*end) { root->dealloc(); return NULL; }
        };
        if (returnParseEnd) *returnParseEnd = end;
        return root;
    }
    static void minify(char *read) {
        char *write = read;
        while (*read) {
            if (*read == ' ') read++;
            else if (*read == '\t') read++;
            else if (*read == '\r') read++;
            else if (*read == '\n') read++;
            else if (*read == '/' && read[1] == '/') { while (*read && *read != '\n') read++; }
            else if (*read == '/' && read[1] == '*') { while (*read && !(*read == '*' && read[1] == '/')) read++; read += 2; }
            else if (*read == '\"') {
                *write++ = *read++;
                while (*read && *read != '\"') { if (*read == '\\') *write++ = *read++; *write++ = *read++; }
                *write++ = *read++;
            } else *write++ = *read++;
        };
        *write = 0;
    }
#ifndef NOSTDLIB
    char *print(bool formatted = true) { printInfo pi; return pi.print(this, formatted); }
#endif
    json *duplicate(bool recursively) {
        json *newitem = (json *)malloc(sizeof(json));
        if (!newitem) return NULL; else memcpy(newitem, this, sizeof(json));
        if ((this->type == String) && this->value.string) {
            newitem->value.string = stringdup(this->value.string);
            if (!newitem->value.string) { newitem->dealloc(); return NULL; };
        };
        if (this->key) {
            newitem->key = stringdup(this->key);
            if (!newitem->key) { newitem->dealloc(); return NULL; };
        };
        if (!recursively) return newitem;
        json *child = this->firstChild, *_prev = NULL, *newchild;
        while (child) {
            newchild = child->duplicate(true);
            if (!newchild) { newitem->dealloc(); return NULL; };
            if (_prev) { _prev->next = newchild; newchild->prev = _prev; } else newitem->firstChild = newchild;
            _prev = newchild;
            child = child->next;
        };
        return newitem;
    }
    
    json *atIndex(int index) { json *item = this->firstChild; while (item && (index > 0)) { index--; item = item->next; }; return item; }
    int getArraySize() { int count = 0; json *item = this->firstChild; while (item) { count++; item = item->next; }; return count; }
    void addToArray(json *item) {
        if (!item) return;
        json *last = this->firstChild;
        while (last && last->next) last = last->next;
        if (!last) this->firstChild = item;
        else { last->next = item; item->prev = last; }
    }
    void replaceInArray(int index, json *newitem) { json *olditem = this->atIndex(index); if (olditem) replaceItem(olditem, newitem); else newitem->dealloc(); }
    json *detachFromArray(int index) { json *item = this->atIndex(index); return item ? detachItem(item) : NULL; }
    void deleteFromArray(int index) { json *item = this->detachFromArray(index); if (item) item->dealloc(); }

#define ATKEY json *object = this->firstChild; while (object) { if (samekey(object->key, _key)) break; object = object->next; };
    json *atKey(const char *_key) { ATKEY return object; }
    json *atKeyWithType(const char *_key, jtype _type) { ATKEY return (!object || (object->type != _type) || ((_type == String) && !object->value.string)) ? NULL : object; }
    json *nullAtKey(const char *_key) { ATKEY return (!object || (object->type != Null)) ? NULL : object; }
    json *boolAtKey(const char *_key) { ATKEY return (!object || (object->type != Bool)) ? NULL : object; }
    json *intAtKey(const char *_key) { ATKEY return (!object || (object->type != Int)) ? NULL : object; }
    json *doubleAtKey(const char *_key) { ATKEY return (!object || (object->type != Double)) ? NULL : object; }
    json *stringAtKey(const char *_key) { ATKEY return (!object || (object->type != String) || !object->value.string) ? NULL : object; }
    json *arrayAtKey(const char *_key) { ATKEY return (!object || (object->type != Array)) ? NULL : object; }
    json *objectAtKey(const char *_key) { ATKEY return (!object || (object->type != Object)) ? NULL : object; }
#undef ATKEY
#define ATKEY \
json *item = this->firstChild, *object = NULL; \
while (item) { if (samekey(item->key, _key)) { object = item; break; } else item = item->next; }; \
if (!object) return NULL; \
const char *arg; va_list args; va_start(args, _key); \
while (object) { \
    arg = va_arg(args, const char *); if (!arg) break; item = object->firstChild; object = NULL; \
    while (item) { if (samekey(item->key, arg)) { object = item; break; } else item = item->next; }; \
} \
va_end(args);
    json *atKeyRecursive(const char *_key, ...) { ATKEY return object; }
    json *atKeyWithTypeRecursive(jtype _type, const char *_key, ...) { ATKEY return (!object || (object->type != _type) || ((_type == String) && !object->value.string)) ? NULL : object; }
    json *nullAtKeyRecursive(const char *_key, ...) { ATKEY return (!object || (object->type != Null)) ? NULL : object; }
    json *boolAtKeyRecursive(const char *_key, ...) { ATKEY return (!object || (object->type != Bool)) ? NULL : object; }
    json *intAtKeyRecursive(const char *_key, ...) { ATKEY return (!object || (object->type != Int)) ? NULL : object; }
    json *doubleAtKeyRecursive(const char *_key, ...) { ATKEY return (!object || (object->type != Double)) ? NULL : object; }
    json *stringAtKeyRecursive(const char *_key, ...) { ATKEY return (!object || (object->type != String) || !object->value.string) ? NULL : object; }
    json *arrayAtKeyRecursive(const char *_key, ...) { ATKEY return (!object || (object->type != Array)) ? NULL : object; }
    json *objectAtKeyRecursive(const char *_key, ...) { ATKEY return (!object || (object->type != Object)) ? NULL : object; }
#undef ATKEYRECURSIVE
    
    void addToObject(const char *name, json *item) {
        if (!item) return; else if (item->key) free(item->key);
        item->key = stringdup(name);
        this->addToArray(item);
    }
    void replaceInObject(const char *_key, json *newitem) {
        json *olditem = this->atKey(_key);
        if (olditem) {
            if (newitem->key) free(newitem->key);
            newitem->key = stringdup(_key);
            replaceItem(olditem, newitem);
        } else newitem->dealloc();
    }
    json *detachFromObject(const char *_key) { json *item = this->atKey(_key); return item ? detachItem(item) : NULL; }
    void deleteFromObject(const char *_key) { json *item = this->detachFromObject(_key); if (item) item->dealloc(); }
    
    static json *createNull() { json *item = newItem(Null); return item; }
    static json *createBool(bool b) { json *item = newItem(Bool); if (item) item->value.b = b ? 1 : 0; return item; }
    static json *createInteger(long long value) { json *item = newItem(Int); if (item) item->value.i = value; return item; }
    static json *createDouble(double value) { json *item = newItem(Double); if (item) item->value.d = value; return item; }
    static json *createString(const char *string) { json *item = newItem(String); if (item) item->value.string = string ? stringdup(string) : NULL; return item; }
    static json *createArray(void) { return newItem(Array); }
    static json *createObject(void) { return newItem(Object); }
    static json *createIntArray(const int *numbers, int count) {
        json *array = createArray(), *prev = NULL, *item;
        if (!array) return NULL;
        for (int n = 0; n < count; n++) {
            item = json::createInteger(numbers[n]);
            if (!item) break; else if (prev) prev->next = item; else array->firstChild = item;
            item->prev = prev;
            prev = item;
        }
        return array;
    }
    static json *createLongLongIntArray(const long long int *numbers, int count) {
        json *array = createArray(), *prev = NULL, *item;
        if (!array) return NULL;
        for (int n = 0; n < count; n++) {
            item = json::createInteger(numbers[n]);
            if (!item) break; else if (prev) prev->next = item; else array->firstChild = item;
            item->prev = prev;
            prev = item;
        }
        return array;
    }
    static json *createFloatArray(const float *numbers, int count) {
        json *array = createArray(), *prev = NULL, *item;
        if (!array) return NULL;
        for (int n = 0; n < count; n++) {
            item = json::createDouble(numbers[n]);
            if (!item) break; else if (prev) prev->next = item; else array->firstChild = item;
            item->prev = prev;
            prev = item;
        }
        return array;
    }
    static json *createDoubleArray(const double *numbers, int count) {
        json *array = createArray(), *prev = NULL, *item;
        if (!array) return NULL;
        for (int n = 0; n < count; n++) {
            item = json::createDouble(numbers[n]);
            if (!item) break; else if (prev) prev->next = item; else array->firstChild = item;
            item->prev = prev;
            prev = item;
        }
        return array;
    }
    static json *createStringArray(const char **strings, int count) {
        json *array = createArray(), *prev = NULL, *item;
        if (!array) return NULL;
        for (int n = 0; n < count; n++) {
            item = json::createString(strings[n]);
            if (!item) break; else if (prev) prev->next = item; else array->firstChild = item;
            item->prev = prev;
            prev = item;
        }
        return array;
    }
    
private:
    static inline bool samekey(const char *a, const char *b) {
        if (!a || !b) return false;
        const unsigned char *aa = (const unsigned char *)a, *bb = (const unsigned char *)b;
        unsigned char ca, cb;
        while (true) {
            ca = *aa++; cb = *bb++;
            if ((ca >= 'A') && (ca <= 'Z')) ca += 32;
            if ((cb >= 'A') && (cb <= 'Z')) cb += 32;
            if (ca != cb) return false; else if (ca == 0) return true;
        }
    }
    
    static inline json *newItem(jtype type = json::Null) {
        json *node = (json *)malloc(sizeof(json));
        if (node) {
#ifndef NOSTDLIB
            memset(node, 0, sizeof(json));
#else
            node->next = node->prev = node->firstChild = NULL; node->key = NULL; node->value.i = 0;
#endif
            node->type = type;
        }
        return node;
    }
    
    json *detachItem(json *item) {
        if (item->prev) item->prev->next = item->next;
        if (item->next) item->next->prev = item->prev;
        if (item == firstChild) firstChild = item->next;
        item->prev = item->next = NULL;
        return item;
    }
    
    void replaceItem(json *olditem, json *newitem) {
        newitem->next = olditem->next;
        newitem->prev = olditem->prev;
        if (newitem->next) newitem->next->prev = newitem;
        if (olditem == firstChild) firstChild = newitem; else newitem->prev->next = newitem;
        olditem->next = olditem->prev = NULL;
        olditem->dealloc();
    }
    
    static inline const char *skip(const char *in) { while (in && *in && (unsigned char)*in <= 32) in++; return in; }
    
    const char *parseValue(const char *v) {
        if (!v) return NULL;
        if ((v[0] == 'n') && (v[1] == 'u') && (v[2] == 'l') && (v[3] == 'l')) { type = json::Null; return v + 4; };
        if ((v[0] == 'f') && (v[1] == 'a') && (v[2] == 'l') && (v[3] == 's') && (v[4] == 'e')) { type = json::Bool; value.b = 0; return v + 5; };
        if ((v[0] == 't') && (v[1] == 'r') && (v[2] == 'u') && (v[3] == 'e')) { type = json::Bool; value.b = 1; return v + 4; };
        if (*v == '\"') return parseString(v);
        if (*v == '-' || (*v >= '0' && *v <= '9')) return parseNumber(v);
        if (*v == '[') return parseArray(v);
        if (*v == '{') return parseObject(v);
        return NULL;
    }
    
    const char *parseArray(const char *v) {
        if (*v != '[') return NULL; else type = json::Array;
        v = skip(v + 1);
        if (*v == ']') return v + 1;
        json *child = firstChild = newItem();
        if (!child) return NULL; else v = skip(child->parseValue(skip(v)));
        if (!v) return NULL;
        while (*v == ',')    {
            child->next = newItem();
            if (!child->next) return NULL; else child->next->prev = child;
            child = child->next;
            v = skip(child->parseValue(skip(v + 1)));
            if (!v) return NULL;
        };
        if (*v == ']') return v + 1; else return NULL;
    }
    
    const char *parseObject(const char *v) {
        if (*v != '{') return NULL; else type = json::Object;
        v = skip(v + 1);
        if (*v == '}') return v + 1;
        json *child = firstChild = newItem(), *i, *s;
        if (!child) return NULL; else v = skip(child->parseString(skip(v)));
        if (!v) return NULL; else child->key = child->value.string;
        child->value.string = NULL;
        if (*v != ':') return NULL; else v = skip(child->parseValue(skip(v + 1)));
        if (!v) return NULL;
        while (*v == ',') {
            i = newItem();
            if (!i) return NULL; else v = skip(i->parseString(skip(v + 1)));
            if (!v || !i->value.string) { i->dealloc(); return NULL; } else i->key = i->value.string;
            i->value.string = NULL;
            if (*v != ':') { i->dealloc(); return NULL; } else v = skip(i->parseValue(skip(v + 1)));
            if (!v) { i->dealloc(); return NULL; } else s = firstChild;
            while (s) if (samekey(s->key, i->key)) break; else s = s->next;
            if (s) i->dealloc(); // existing key
            else { child->next = i; i->prev = child; child = i; }
        };
        return (*v == '}') ? v + 1 : NULL;
    }
    
    static unsigned parseHex4(const char *str) {
        unsigned n = 0; char c = *str++;
        if (c >= '0' && c <= '9') n += c - '0'; else if (c >= 'A' && c <= 'F') n += 10 + c - 'A'; else if (c >= 'a' && c <= 'f') n += 10 + c - 'a'; else return 0;
        n = n << 4; c = *str++;
        if (c >= '0' && c <= '9') n += c - '0'; else if (c >= 'A' && c <= 'F') n += 10 + c - 'A'; else if (c >= 'a' && c <= 'f') n += 10 + c - 'a'; else return 0;
        n = n << 4; c = *str++;
        if (c >= '0' && c <= '9') n += c - '0'; else if (c >= 'A' && c <= 'F') n += 10 + c - 'A'; else if (c >= 'a' && c <= 'f') n += 10 + c - 'a'; else return 0;
        n = n << 4; c = *str;
        if (c >= '0' && c <= '9') n += c - '0'; else if (c >= 'A' && c <= 'F') n += 10 + c - 'A'; else if (c >= 'a' && c <= 'f') n += 10 + c - 'a'; else return 0;
        return n;
    }
    
    const char *parseString(const char *str) {
        static const unsigned char firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
        if (*str != '\"') return NULL;
        const char *read = str + 1;
        int len = 0;
        while (*read != '\"' && *read && ++len) if (*read++ == '\\') read++;
        value.string = (char *)malloc(len + 1);
        if (!value.string) return NULL; else read = str + 1;
        char *write = value.string;
        while (*read != '\"' && *read) {
            if (*read != '\\') { *write++ = *read++; continue; } else read++;
            switch (*read) {
                case 'b': *write++ = '\b'; break;
                case 'f': *write++ = '\f'; break;
                case 'n': *write++ = '\n'; break;
                case 'r': *write++ = '\r'; break;
                case 't': *write++ = '\t'; break;
                case 'u': {
                    unsigned uc = parseHex4(read + 1);
                    read += 4;
                    if ((uc >= 0xDC00 && uc <= 0xDFFF) || uc == 0) break;
                    if (uc >= 0xD800 && uc <= 0xDBFF) {
                        if (read[1] != '\\' || read[2] != 'u') break;
                        unsigned uc2 = parseHex4(read + 3);
                        read += 6;
                        if (uc2 < 0xDC00 || uc2 > 0xDFFF) break;
                        uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
                    };
                    if (uc < 0x80) len = 1; else if (uc < 0x800) len = 2; else if (uc < 0x10000) len = 3; else len = 4;
                    write += len;
                    switch (len) {
                        case 4: *--write = ((uc | 0x80) & 0xBF); uc >>= 6;
    #if __cplusplus >= 201703L
    [[fallthrough]];
    #endif
                        case 3: *--write = ((uc | 0x80) & 0xBF); uc >>= 6;
    #if __cplusplus >= 201703L
    [[fallthrough]];
    #endif
                        case 2: *--write = ((uc | 0x80) & 0xBF); uc >>= 6;
    #if __cplusplus >= 201703L
    [[fallthrough]];
    #endif
                        case 1: *--write = (char)(uc | firstByteMark[len]);
                    };
                    write += len;
                }; break;
                default: *write++ = *read; break;
            };
            read++;
        };
        *write = 0;
        if (*read == '\"') read++;
        type = json::String;
        return read;
    }
    
    static inline double getFraction(const char *digits, int numDigits, int leadingZeros) {
        static const double mul[17] = { 0.1, 0.01, 0.001, 0.0001, 0.00001, 0.000001, 0.0000001, 0.00000001, 0.000000001, 0.0000000001, 0.00000000001, 0.000000000001, 0.0000000000001, 0.00000000000001, 0.000000000000001, 0.0000000000000001, 0.00000000000000001 };
        numDigits += leadingZeros;
        if (numDigits == 0) return 0; else if (numDigits > 17) numDigits = 17;
        double d = 0;
        for (int n = leadingZeros; n < numDigits; n++) d += *digits++ * mul[n];
        return d;
    }

    const char *parseNumber(const char *str) {
    #define MAXDIGITS 44 // largest integer: 19, largest fraction: 17, sign, point, e, e num (max 36 back, so 2 chars) -> max 44 characters
    #define POINTPOSMNOTFOUND 2147483647
        type = json::Int;
        char digits[MAXDIGITS], c = *str;
        int numDigits = 0, pointPos = POINTPOSMNOTFOUND;
        digits[0] = digits[1] = digits[2] = digits[3] = 0; // happy code analyzer
        bool negative;
        if (c == '-') { negative = true; str++; } else negative = false;
        c = *str++;
        if (c == '0') { digits[numDigits++] = 0; c = *str++; }
        else {
            while ((c >= '0') && (c <= '9') && (numDigits < MAXDIGITS)) { digits[numDigits++] = c - '0'; c = *str++; }
            if (numDigits >= MAXDIGITS) {
                while ((c >= '0') && (c <= '9')) c = *str++;
                value.i = negative ? (-1-0x7fffffffffffffff) : (0x7fffffffffffffff); // INT64_MIN / INT64_MAX;
                return str - 1;
            }
        }
        if (c == '.') {
            pointPos = numDigits;
            c = *str++;
            while ((c >= '0') && (c <= '9') && (numDigits < MAXDIGITS)) { digits[numDigits++] = c - '0'; c = *str++; }
            if (numDigits >= MAXDIGITS) {
                while ((c >= '0') && (c <= '9')) c = *str++;
                value.d = negative ? 2.22507385850720138309e-308 : 1.79769313486231570815e+308; // DBL_MIN / DBL_MAX
                return str - 1;
            }
        }
        if ((c == 'e') || (c == 'E')) {
            if (pointPos == POINTPOSMNOTFOUND) pointPos = numDigits;
            c = *str++;
            bool negativePointPos = false;
            if (c == '-') { negativePointPos = true; c = *str++; } else if (c == '+') c = *str++;
            int pointPosAdd = 0;
            while ((c >= '0') && (c <= '9')) { pointPosAdd = pointPosAdd * 10 + (c - '0'); c = *str++; }
            if (negativePointPos) pointPos -= pointPosAdd; else pointPos += pointPosAdd;
        }
        if (pointPos == POINTPOSMNOTFOUND) {
            unsigned long long int i = 0;
            for (int n = 0; n < numDigits; n++) i = i * 10 + digits[n];
            value.i = negative ? -((long long int)i) : i;
        } else if (pointPos < 0) {
            double d = getFraction(digits, numDigits, -pointPos);
            value.d = negative ? -d : d;
            type = json::Double;
        } else if (pointPos >= numDigits) {
            unsigned long long int i = 0;
            int n = 0;
            for (; n < numDigits; n++) i = i * 10 + digits[n];
            for (; n < pointPos; n++) i = i * 10;
            value.i = negative ? -((long long int)i) : i;
        } else {
            int n = numDigits - 1;
            while (n >= pointPos) if (digits[n] != 0) break; else n--;
            if (n < pointPos) {
                unsigned long long int i = 0;
                for (n = 0; n < pointPos; n++) i = i * 10 + digits[n];
                value.i = negative ? -((long long int)i) : i;
            } else {
                unsigned long long int i = 0;
                for (n = 0; n < pointPos; n++) i = i * 10 + digits[n];
                double d = i + getFraction(digits + pointPos, numDigits - pointPos, 0);
                value.d = negative ? -d : d;
                type = json::Double;
            }
        }
        return str - 1;
    #undef MAXDIGITS
    #undef POINTPOSMNOTFOUND
    }
    
    #ifndef NOSTDLIB
    struct printInfo {
    public:
        char *print(json *item, bool formatted) {
            sizeBytes = 4096;
            str = write = (char *)malloc(sizeBytes);
            if (!str) return NULL; else if (printValue(item, 0, formatted, false)) { *write = 0; return str; } else free(str);
            return NULL;
        }
    private:
        char *str, *write;
        int sizeBytes;
        
        inline bool grow(int numBytes) {
            int written = int(write - str), size = written + numBytes + 1;
            if (size <= sizeBytes) return true; else while (sizeBytes < size) sizeBytes += 4096;
            char *old = str;
            str = (char *)realloc(str, sizeBytes);
            if (!str && old) free(old); else write = str + written;
            return str != NULL;
        }
        
        inline void printDepth(int depth) { memset(write, '\t', depth); write += depth; }

        bool printArray(json *array, int depth, bool formatted, bool afterName) {
            if (!grow(2 + depth)) return false; else if (formatted && !afterName) printDepth(depth);
            *write++ = '[';
            if (!array->firstChild) { *write++ = ']'; return true; }
            json *child = array->firstChild;
            bool simpleArray = true;
            while (child) {
                if ((child->type == json::Array) || (child->type == json::Object)) { simpleArray = false; break; }
                child = child->next;
            }
            if (formatted) *write++ = simpleArray ? ' ' : '\n';
            child = array->firstChild;
            while (child) {
                if (!printValue(child, depth + 1, formatted, false) || !grow(2)) return false; else if (child->next) *write++ = ',';
                if (formatted) *write++ = simpleArray ? ' ' : '\n';
                child = child->next;
            }
            if (!grow(1 + depth)) return false; else if (formatted && !simpleArray) printDepth(depth);
            *write++ = ']';
            return true;
        }

        bool printObject(json *object, int depth, bool formatted, bool afterName) {
            if (!grow(2 + depth)) return false; else if (formatted && !afterName) printDepth(depth);
            *write++ = '{';
            if (!object->firstChild) { *write++ = '}'; return true; }
            if (formatted) *write++ = '\n';
            json *child = object->firstChild;
            while (child) {
                if (!grow(depth + 1)) return false; else if (formatted) printDepth(depth + 1);
                if (!printString(child->key) || !grow(2)) return false; else *write++ = ':';
                if (formatted) *write++ = ' ';
                if (!printValue(child, depth + 1, formatted, true) || !grow(2)) return false; else if (child->next) *write++ = ',';
                if (formatted) *write++ = '\n';
                child = child->next;
            }
            if (!grow(1 + depth)) return false; else if (formatted) printDepth(depth);
            *write++ = '}';
            return true;
        }

        bool printValue(json *item, int depth, bool formatted, bool afterName) {
            if (!item) return false;
            switch (item->type)    {
                case json::Null: return printString(NULL);
                case json::Bool: if (!grow(5)) return false;
                    if (item->value.b) { memcpy(write, "true", 4); write += 4; } else { memcpy(write, "false", 5); write += 5; }
                    return true;
                case json::Int: if (!grow(32)) return false; else {
                    stringprint(write, 32, "%lld", item->value.i);
                    for (int n = 0; n < 32; n++) if (*write == 0) break; else write++;
                } return true;
                case json::Double: if (!grow(64)) return false; else {
                    double a = item->value.d; if (a < 0) a = -a;
                    stringprint(write, 64, a < 1.0e-6 || a > 1.0e9 ? "%e" : "%f", item->value.d);
                    for (int n = 0; n < 64; n++) if (*write == 0) break; else write++;
                } return true;
                case json::String: return printString(item->value.string);
                case json::Array: return printArray(item, depth, formatted, afterName);
                case json::Object: return printObject(item, depth, formatted, afterName);
                default: return false;
            };
        }
        
        bool printString(const char *s) {
            if (!s) { if (!grow(4)) return false; memcpy(write, "null", 4); write += 4; return true; }
            const unsigned char *read = (const unsigned char *)s;
            int len = 0;
            unsigned char token;
            while ((token = *read++)) switch (token) {
                case '\\':
                case '\"':
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t': len += 2; break;
                default: if ((token < 32) || (token > 127)) len += 6; else len++;
            }
            if (!grow(len + 3)) return false;
            unsigned char *w = (unsigned char *)write;
            read = (const unsigned char *)s;
            const unsigned char *zero = (const unsigned char *)(s + strlen(s));
            *w++ = '\"';
            while (*read) {
                token = *read;
                if (token > 127) {
                    if (token >= 192) {
                        size_t remaining = zero - read;
                        if ((token <= 223) && (remaining >= 2)) { w[0] = read[0]; w[1] = read[1]; w += 2; read += 2; continue; }
                        else if ((token <= 239) && (remaining >= 3)) { w[0] = read[0]; w[1] = read[1]; w[2] = read[2]; w += 3; read += 3; continue; }
                        else if ((token <= 247) && (remaining >= 4)) { w[0] = read[0]; w[1] = read[1]; w[2] = read[2]; w[3] = read[3]; w += 4; read += 4; continue; }
                    }
                    w[0] = 192 | ((token >> 6) & 3); w[1] = 128 | (token & 63); w += 2; read++;
                } else if ((token > 31) && (token != '\"') && (token != '\\')) { *w++ = token; read++; }
                else {
                    *w++ = '\\';
                    switch (token = *read++) {
                        case '\\': *w++ = '\\'; break;
                        case '\"': *w++ = '\"'; break;
                        case '\b': *w++ = 'b'; break;
                        case '\f': *w++ = 'f'; break;
                        case '\n': *w++ = 'n'; break;
                        case '\r': *w++ = 'r'; break;
                        case '\t': *w++ = 't'; break;
                        default: stringprint((char *)w, len, "u%04x", token); w += 5; break;
                    }
                }
            }
            *w++ = '\"';
            write = (char *)w;
            return true;
        }
        
        static int stringprint(char *s, int n, const char *format, ...) {
            va_list args;
            va_start(args, format);
            #if _WIN32
            int r = _vsnprintf_s_l(s, n, n - 1, format, NULL, args);
            #else
            int r = vsnprintf(s, n, format, args);
            #endif
            va_end(args);
            return r;
        }
    };
    #endif
};

}

#undef stringdup
#endif
