// gcc -O0 -g self -o sake -Wall -Wextra -pedantic -Wno-unused-function

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEALLOC_QUOTA 0x200000

typedef enum PARSE { PARSE_SKIP = 0, PARSE_MODIFY = 1 } PARSE;
typedef enum FIL { FIL_DISCARD = 0, FIL_KEEP = 1 } FIL;
typedef enum OFILE { OFILE_ERR = 0, OFILE_OUT = 1 } OFILE;

typedef enum POS { POS_BEG, POS_END } POS;
typedef enum TYPE { TYPE_STR, TYPE_HLIST, TYPE_VLIST } TYPE;
typedef enum SYM {
    SYM_GRAM_BEG,
    SYM_TERM_BEG,

    SYM_UNOP_BEG,
    SYM_HASH,
    SYM_AT,
    SYM_LESS,
    SYM_UNOP_END,

    SYM_PAREN_BEG,
    SYM_L_CBRACK,
    SYM_L_SBRACK,
    SYM_L_RBRACK,
    SYM_TERM_END,

    SYM_R_SBRACK,
    SYM_R_CBRACK,
    SYM_R_RBRACK,
    SYM_PAREN_END,

    SYM_BINOP_BEG,
    SYM_PLUS,
    SYM_SUB,
    SYM_MUL,
    SYM_DIV,
    SYM_MOD,
    SYM_BINOP_END,

    SYM_EQ,
    SYM_GRAM_END,

    SYM_ESCAPE,
    SYM_SPACE,
    SYM_TAB,
    SYM_NLINE,
    SYM_UP,

    SYM_SEMICOL,
    SYM_QUOT,
    SYM_LARROW,
    SYM_RARROW,
    SYM_QCOL,
} SYM;

typedef struct Str {
    size_t len;
    short hash;
    char *data;
} Str;

typedef struct Hlist {
    size_t len;
    struct Str *data;
} Hlist;

typedef struct Vlist {
    size_t len;
    struct Hlist *data;
} Vlist;

typedef struct Var {
    enum TYPE type;
    union {
        Str *str;
        Hlist *hlist;
        Vlist *vlist;
        void *anon;
    } val;
} Var;

typedef struct Arr {
    size_t alloc;
    size_t len;
    void **data;
} Arr;

typedef struct Map {
    struct Arr namearr;
    struct Var *node;
} Map;

typedef struct QuotaAlloc {
    struct Arr delay;
    size_t store;
} QuotaAlloc;

extern char *__progname;
static char *plainmk;
static char *copymk;
static const char *fname = "m.sk";
static size_t flen;
static struct Arr quotarr;
static struct Arr tokarr;
static struct Map aliasmap;
static struct QuotaAlloc squalo;
static const unsigned char escape[1 << 8][2] = {
    { 0x0 },  { 0x1 },  { 0x2 },  { 0x3 },  { 0x4 },  { 0x5 },  { 0x6 },
    { 0x7 },  { 0x8 },  { 0x9 },  { 0xa },  { 0xb },  { 0xc },  { 0xd },
    { 0xe },  { 0xf },  { 0x10 }, { 0x11 }, { 0x12 }, { 0x13 }, { 0x14 },
    { 0x15 }, { 0x16 }, { 0x17 }, { 0x18 }, { 0x19 }, { 0x1a }, { 0x1b },
    { 0x1c }, { 0x1d }, { 0x1e }, { 0x1f }, { 0x20 }, { 0x21 }, { 0x22 },
    { 0x23 }, { 0x24 }, { 0x25 }, { 0x26 }, { 0x27 }, { 0x28 }, { 0x29 },
    { 0x2a }, { 0x2b }, { 0x2c }, { 0x2d }, { 0x2e }, { 0x2f }, { 0x30 },
    { 0x31 }, { 0x32 }, { 0x33 }, { 0x34 }, { 0x35 }, { 0x36 }, { 0x37 },
    { 0x38 }, { 0x39 }, { 0x3a }, { 0x3b }, { 0x3c }, { 0x3d }, { 0x3e },
    { 0x3f }, { 0x40 }, { 0x41 }, { 0x42 }, { 0x43 }, { 0x44 }, { 0x45 },
    { 0x46 }, { 0x47 }, { 0x48 }, { 0x49 }, { 0x4a }, { 0x4b }, { 0x4c },
    { 0x4d }, { 0x4e }, { 0x4f }, { 0x50 }, { 0x51 }, { 0x52 }, { 0x53 },
    { 0x54 }, { 0x55 }, { 0x56 }, { 0x57 }, { 0x58 }, { 0x59 }, { 0x5a },
    { 0x5b }, { 0x5c }, { 0x5d }, { 0x5e }, { 0x5f }, { 0x60 }, { 0x61 },
    { 0x62 }, { 0x63 }, { 0x64 }, { 0x65 }, { 0x66 }, { 0x67 }, { 0x68 },
    { 0x69 }, { 0x6a }, { 0x6b }, { 0x6c }, { 0x6d }, { 0x6e }, { 0x6f },
    { 0x70 }, { 0x71 }, { 0x72 }, { 0x73 }, { 0x74 }, { 0x75 }, { 0x76 },
    { 0x77 }, { 0x78 }, { 0x79 }, { 0x7a }, { 0x7b }, { 0x7c }, { 0x7d },
    { 0x7e }, { 0x7f }, { 0x80 }, { 0x81 }, { 0x82 }, { 0x83 }, { 0x84 },
    { 0x85 }, { 0x86 }, { 0x87 }, { 0x88 }, { 0x89 }, { 0x8a }, { 0x8b },
    { 0x8c }, { 0x8d }, { 0x8e }, { 0x8f }, { 0x90 }, { 0x91 }, { 0x92 },
    { 0x93 }, { 0x94 }, { 0x95 }, { 0x96 }, { 0x97 }, { 0x98 }, { 0x99 },
    { 0x9a }, { 0x9b }, { 0x9c }, { 0x9d }, { 0x9e }, { 0x9f }, { 0xa0 },
    { 0xa1 }, { 0xa2 }, { 0xa3 }, { 0xa4 }, { 0xa5 }, { 0xa6 }, { 0xa7 },
    { 0xa8 }, { 0xa9 }, { 0xaa }, { 0xab }, { 0xac }, { 0xad }, { 0xae },
    { 0xaf }, { 0xb0 }, { 0xb1 }, { 0xb2 }, { 0xb3 }, { 0xb4 }, { 0xb5 },
    { 0xb6 }, { 0xb7 }, { 0xb8 }, { 0xb9 }, { 0xba }, { 0xbb }, { 0xbc },
    { 0xbd }, { 0xbe }, { 0xbf }, { 0xc0 }, { 0xc1 }, { 0xc2 }, { 0xc3 },
    { 0xc4 }, { 0xc5 }, { 0xc6 }, { 0xc7 }, { 0xc8 }, { 0xc9 }, { 0xca },
    { 0xcb }, { 0xcc }, { 0xcd }, { 0xce }, { 0xcf }, { 0xd0 }, { 0xd1 },
    { 0xd2 }, { 0xd3 }, { 0xd4 }, { 0xd5 }, { 0xd6 }, { 0xd7 }, { 0xd8 },
    { 0xd9 }, { 0xda }, { 0xdb }, { 0xdc }, { 0xdd }, { 0xde }, { 0xdf },
    { 0xe0 }, { 0xe1 }, { 0xe2 }, { 0xe3 }, { 0xe4 }, { 0xe5 }, { 0xe6 },
    { 0xe7 }, { 0xe8 }, { 0xe9 }, { 0xea }, { 0xeb }, { 0xec }, { 0xed },
    { 0xee }, { 0xef }, { 0xf0 }, { 0xf1 }, { 0xf2 }, { 0xf3 }, { 0xf4 },
    { 0xf5 }, { 0xf6 }, { 0xf7 }, { 0xf8 }, { 0xf9 }, { 0xfa }, { 0xfb },
    { 0xfc }, { 0xfd }, { 0xfe }, { 0xff },
};

static const char litts[][3] = {
            [SYM_R_CBRACK] = "}", [SYM_L_CBRACK] = "{", [SYM_R_SBRACK] = "]",
            [SYM_L_SBRACK] = "[", [SYM_R_RBRACK] = ")", [SYM_L_RBRACK] = "(",
            [SYM_PLUS] = "+",     [SYM_SUB] = "-",      [SYM_MUL] = "*",
            [SYM_DIV] = "/",      [SYM_MOD] = "%",      [SYM_EQ] = "=",
            [SYM_HASH] = "#",     [SYM_AT] = "@",

            [SYM_SPACE] = " ",    [SYM_NLINE] = "\n",   [SYM_TAB] = "\t",
            [SYM_QUOT] = "\'",    [SYM_ESCAPE] = "\\",  [SYM_UP] = "^",
            [SYM_LESS] = "<",     [SYM_SEMICOL] = ";",  [SYM_LARROW] = "<-",
            [SYM_RARROW] = "->",  [SYM_QCOL] = "::",
};

static const char *symmap[1 << 16] = {
            ['}'] = litts[SYM_R_CBRACK], ['{'] = litts[SYM_L_CBRACK],
            [']'] = litts[SYM_R_SBRACK], ['['] = litts[SYM_L_SBRACK],
            [')'] = litts[SYM_R_RBRACK], ['('] = litts[SYM_L_RBRACK],
            ['+'] = litts[SYM_PLUS],     ['-'] = litts[SYM_SUB],
            ['#'] = litts[SYM_HASH],     ['@'] = litts[SYM_AT],
            ['/'] = litts[SYM_DIV],      ['%'] = litts[SYM_MOD],
            ['*'] = litts[SYM_MUL],      ['='] = litts[SYM_EQ],

            ['^'] = litts[SYM_UP],       ['<'] = litts[SYM_LESS],
            [';'] = litts[SYM_SEMICOL],  [' '] = litts[SYM_SPACE],
            ['\t'] = litts[SYM_TAB],     ['\\'] = litts[SYM_ESCAPE],
            ['\''] = litts[SYM_QUOT],
};

#define chrbeg(arr) ((char **)((arr)->data))
#define valbeg(arr) ((Var *)((arr)))
#define chrend(arr) ((char **)((arr)->data) + (arr)->len)
#define valend(arr) ((Var *)((arr)->data) + (arr)->len)
#define printarr(arr, fmt, ...)                                               \
    do {                                                                      \
        for (size_t i = 0; i < (arr)->len; ++i) {                             \
            printf(fmt, chrbeg(arr)[i], ##__VA_ARGS__);                       \
        }                                                                     \
    } while (0)

static void addusrcmds(char **, size_t);
static void initparse();
static char *itertokm(char *, int);
static char *readall(const char *);
static void sigerrn(size_t, char *);
static void showerrn(size_t, char *);
static char *advance(char *, int);
static int isnotsigil(char *);
static int isgrammar(char *);
static int isbinaryop(char *);
static int isunaryop(char *);
static int isparen(char *);
static int isterm(char *);
static void printtok(char **, char **);
static void *memown(void *, size_t);
static Str *copystr(struct Str *);
static Hlist *copyhlist(struct Hlist *);
static Vlist *copyvlist(struct Vlist *);
static void copyval(struct Var *, struct Var *);
static void freestr(struct Str *);
static void freehlist(struct Hlist *);
static void freevlist(struct Vlist *);
static void freeval(struct Var *);
static void printstr(FILE *, struct Str *);
static void printhlist(FILE *, struct Hlist *);
static void printvlist(FILE *, struct Vlist *);
static void printval(struct Var *, char *, enum OFILE);
static void valfromterm(struct Var *, char *);
static Str *emptystr();
static void pushlist(void *, void *, size_t);
static Hlist *emptyhlist();
static Vlist *emptyvlist();
static void hashval(struct Var *);
static void exec(struct Var *, char **);
static void evalmk(char **, char **);
static void evalstmnt(char **, char **);
static char **evalexpr(struct Var *, char **, char **);
static char **evalterm(struct Var *, char **, char **);
static char **evalbinaryop(struct Var *, char **, char **);
static char **evalrbrack(struct Var *, char **, char **);
static char **evalsbrack(struct Var *, char **, char **);
static char **evalcbrack(struct Var *, char **, char **);
static char **evalunaryop(struct Var *, char **, char **);
static void initarr(struct Arr *, size_t);
static void pusharr(struct Arr *, void *);
static void reallocarr(struct Arr *, size_t);
static void shrinkarr(struct Arr *);
static void copymkarr(struct Arr *, struct Arr *);
static void sortstrarr(Arr *);
static char **searcharr(char **, Arr *);
static int sortstr(const void *, const void *);
static void uniqarr(struct Arr *);
static void mapfromarr(struct Map *, struct Arr *);
static void *alloc(size_t);
static void reallocptr(void *, size_t, size_t);
static struct Str *strfromhlist(struct Hlist *);
static struct Str *strfromvlist(struct Vlist *);
static struct Vlist *vlistfromhlist(struct Hlist *);
static struct Hlist *hlistfromstr(struct Str *);
static struct Vlist *vlistfromstr(struct Str *);
static struct Hlist *hlistfromvlist(struct Vlist *);
static void convert(struct Var *, enum TYPE);
static void appendstr(struct Str *, struct Str *);
static void prepend(struct Str *, struct Str *);
static struct Str *addstrstr(struct Str *, struct Str *);
static struct Hlist *addstrhlist(struct Str *, struct Hlist *);
static struct Vlist *addstrvlist(struct Str *, struct Vlist *);
static struct Hlist *addhlisthlist(struct Hlist *, struct Hlist *);
static struct Vlist *addhlistvlist(struct Hlist *, struct Vlist *);
static struct Vlist *addvlisthlist(struct Vlist *, struct Hlist *);
static struct Vlist *addvlistvlist(struct Vlist *, struct Vlist *);
static struct Hlist *addhliststr(struct Hlist *, struct Str *);
static struct Vlist *addvliststr(struct Vlist *, struct Str *);
static struct Str *concatstr(struct Str *, struct Str *);
static struct Hlist *concathlist(struct Hlist *, struct Hlist *);
static struct Vlist *concatvlist(struct Vlist *, struct Vlist *);
static void execbinaryop(struct Var *, struct Var *, char *);
static void addval(struct Var *, struct Var *);
static int matchstrstr(struct Str *, struct Str *, enum POS, enum FIL);
static struct Hlist *
filthlist(struct Hlist *, struct Str *, enum POS, enum FIL);
static struct Vlist *
filtvlist(struct Vlist *, struct Str *, enum POS, enum FIL);
static void subval(struct Var *, struct Var *);
static struct Hlist *subhliststr(struct Hlist *f, struct Str *, enum POS);
static struct Vlist *subvliststr(struct Vlist *f, struct Str *, enum POS);
static void filtval(struct Var *, struct Var *, char *);
static struct Hlist *atstr(struct Str *, char **);
static int cmpstr(const void *, const void *);
static void atval(struct Var *, char **);
static void freemem(void *, size_t);
static void print_help(void);

int
main(int argc, char *argv[]) {
    char *tok;
    int c;

    while ((c = getopt(argc, argv, "hi:")) != -1) {
        if (c == 'i') {
            fname = optarg;
        } else if (c == 'h') {
            print_help();
            return 0;
        }
    }
    plainmk = readall(fname);
    addusrcmds(argv + optind, argc - optind);
    initparse();
    tok = itertokm(plainmk, PARSE_MODIFY);
    while ((tok = itertokm(NULL, PARSE_MODIFY)) != NULL) {
        pusharr(&tokarr, tok);
    }
    if (tokarr.len == 0) {
        return 0;
    } else if (chrbeg(&tokarr)[tokarr.len - 1] != litts[SYM_SEMICOL]) {
        sigerrn(tokarr.len - 1, "missing terminating semicolon");
    }
    shrinkarr(&quotarr);
    shrinkarr(&tokarr);
    sortstrarr(&quotarr);
    mapfromarr(&aliasmap, &tokarr);

    evalmk(chrbeg(&tokarr), chrend(&tokarr));
    return 0;
}

static void
print_help(void) {
    fprintf(stderr,
            "%s: [cmd] [-i filename] [-h]"
            "\n\tcmd<string>: execute command from the loaded script"
            "\n\t-i filename<string>: script file to load"
            "\n\t-h: print this message"
            "\n",
            __progname);
}

static void
addusrcmds(char **cmdbeg, size_t size) {
    size_t cmdslen = 0;
    char *beg;
    size_t len;
    size_t i;

    if (!flen) {
        errx(1, "malformed file");
    }
    for (i = 0; i < size; ++i) {
        cmdslen += strlen(cmdbeg[i]) + 1;
    }
    if (cmdslen == 0) {
        return;
    }
    reallocptr(&plainmk, flen + cmdslen, 1);
    beg = plainmk + strlen(plainmk);
    for (i = 0; i < size; ++i) {
        len = strlen(cmdbeg[i]);
        memcpy(beg, cmdbeg[i], len);
        beg += len;
        *beg++ = ';';
    }
    *beg = '\0';
}

static void
initparse(void) {

    if ((copymk = strdup(plainmk)) == NULL) {
        err(1, "alloc");
    }
    initarr(&squalo.delay, 1024);
    initarr(&tokarr, 1024);
    initarr(&quotarr, 128);
}

static char *
itertokm(char *str, int m) {
    static char *tok = NULL;
    short ch;
    uint16_t mch;
    char *curr;
    int skipcomm;

    if (str) {
        tok = str;
        return NULL;
    }
    do {
        skipcomm = 0;
        while (*tok == ' ' || *tok == '\t' || *tok == '\n') {
            tok = advance(tok, m);
        }
        if (*tok == '^') {
            skipcomm = 1;
            do {
                tok = advance(tok, m);
            } while (*tok && *tok != ';');
            if (*tok != ';' && m) {
                sigerrn(tokarr.len, "non closed comment");
            }
            tok = advance(tok, m);
            curr = tok;
        }
    } while (skipcomm);
    switch (*tok) {
    case '\0':
        if (m == 0) {
            return tok;
        }
        return NULL;
    case '\'':
        tok = advance(tok, m);
        curr = tok;
        while (*tok != '\'') {
            if (*tok++ == '\0' && m) {
                sigerrn(tokarr.len, "non closed litteral");
            }
        }
        pusharr(&quotarr, curr);
        tok = advance(tok, m);
        break;
    case '\\':
        tok = advance(tok, m);
        if (*tok == '\0' && m) {
            sigerrn(tokarr.len, "missing character to escape");
        }
        curr = m ? (char *)escape[(short)*tok] : tok;
        tok = advance(tok, m);
        break;
    default:
        ch = (short)*tok;
        if (symmap[ch]) {
            mch = *(uint16_t *)tok;
            if (symmap[mch]) {
                curr = m ? (char *)symmap[mch] : tok;
                tok = advance(tok, m);
                tok = advance(tok, m);
                break;
            } else {
                curr = m ? (char *)symmap[ch] : tok;
                tok = advance(tok, m);
                break;
            }
        }
        curr = tok;
        do {
            tok++;
        } while (*tok && symmap[(short)*tok] == NULL);
        break;
    }
    return curr;
}

static char *
readall(const char *fname) {
    FILE *f;

    if ((f = fopen(fname, "r")) == NULL) {
        err(1, "fopen %s", fname);
    }
    if (fseek(f, 0, SEEK_END) == -1) {
        err(1, "fseek");
    }
    if ((flen = ftell(f)) == (size_t)-1) {
        err(1, "ftell");
    }
    if (fseek(f, 0, SEEK_SET) == -1) {
        err(1, "fseek");
    }
    if ((plainmk = malloc(flen + 1)) == NULL) {
        err(1, "alloc");
    }
    if (fread(plainmk, 1, flen, f) != flen) {
        err(1, "fread");
    }
    plainmk[flen] = '\0';
    fclose(f);
    return plainmk;
}

static void
sigerrn(size_t n, char *msg) {
    showerrn(n, msg);
    exit(EXIT_FAILURE);
}

static void
showerrn(size_t n, char *msg) {
    char *errp = itertokm(copymk, PARSE_SKIP);
    size_t nlines = 1;
    size_t pos;
    size_t i;
    char *errend;
    char *errbeg;
    char *beg;
    char cont;

    for (i = 0; i <= n; ++i) {
        errp = itertokm(NULL, PARSE_SKIP);
    }
    errend = errp;
    errbeg = errp;
    for (beg = copymk; beg != errp; ++beg) {
        if (*beg == '\n') {
            ++nlines;
        }
    }
    while (*errbeg != '\n' && errbeg != copymk) {
        --errbeg;
    }
    while (*errend != '\n' && *errend != '\0') {
        ++errend;
    }
    cont = *errend == '\0' ? ' ' : ':';
    *errend = '\0';
    pos = errp - errbeg;
    fprintf(stderr,
            "error: %s:\n"
            "  %s:%lu:%lu:\n"
            "  │%s\n"
            "  %c%*c\n",
            msg, fname, nlines, pos, errbeg + 1, cont, (int)pos, '~');
}

static char *
advance(char *tok, int m) {
    if (*tok == '\0') {
        return tok;
    }
    if (m) {
        *tok = '\0';
    }
    return tok + 1;
}

static int
isnotsigil(char *t) {
    return t >= plainmk && t < plainmk + flen;
}

static int
isgrammar(char *t) {
    return t > litts[SYM_GRAM_BEG] && t < litts[SYM_GRAM_END];
}

static int
isbinaryop(char *t) {
    return t > litts[SYM_BINOP_BEG] && t < litts[SYM_BINOP_END];
}

static int
isunaryop(char *t) {
    return t > litts[SYM_UNOP_BEG] && t < litts[SYM_UNOP_END];
}

static int
isparen(char *t) {
    return t > litts[SYM_PAREN_BEG] && t < litts[SYM_PAREN_END];
}

static int
isterm(char *t) {
    return (t > litts[SYM_TERM_BEG] && t < litts[SYM_TERM_END]) ||
           !isgrammar(t);
}

static void
printtok(char **beg, char **end) {
    while (beg != end) {
        if (isnotsigil(*beg)) {
            fprintf(stderr, "%s :<V> ", strlen(*beg) ? *beg : "''");
        } else if (isparen(*beg)) {
            fprintf(stderr, "%s :<B> ", *beg);
        } else {
            fprintf(stderr, "%s :<O> ", *beg);
        }
        beg++;
    }
    printf("\n");
}

static void *
memown(void *p, size_t s) {
    void *res = alloc(s);

    memcpy(res, p, s);
    return res;
}

static Str *
copystr(struct Str *str) {
    struct Str *res = alloc(sizeof(Str));

    res->hash = str->hash;
    res->len = str->len;
    res->data = memown(str->data, str->len);
    return res;
}

static Hlist *
copyhlist(struct Hlist *hl) {
    struct Hlist *res = alloc(sizeof(Hlist));
    struct Str *strv;
    size_t i;

    res->len = hl->len;
    res->data = memown(hl->data, sizeof(Str) * hl->len);
    strv = res->data;
    for (i = 0; i < res->len; ++i) {
        strv[i].data = memown(strv[i].data, strv[i].len);
    }
    return res;
}

static Vlist *
copyvlist(struct Vlist *vl) {
    struct Vlist *res = alloc(sizeof(Vlist));
    struct Hlist *hlv;
    struct Str *strv;
    size_t i;
    size_t j;

    res->len = vl->len;
    res->data = NULL;
    res->data = memown(vl->data, vl->len * sizeof(Hlist));
    hlv = res->data;
    for (i = 0; i < vl->len; ++i) {
        hlv[i].data = memown(hlv[i].data, sizeof(Str) * hlv[i].len);
        strv = hlv[i].data;
        for (j = 0; j < vl->data[i].len; ++j) {
            strv[j].data = memown(strv[j].data, strv[j].len);
        }
    }
    return res;
}

static void
copyval(struct Var *dst, struct Var *v) {
    switch (dst->type = v->type) {
    case TYPE_STR:
        dst->val.str = copystr(v->val.str);
        break;
    case TYPE_HLIST:
        dst->val.hlist = copyhlist(v->val.hlist);
        break;
    case TYPE_VLIST:
        dst->val.vlist = copyvlist(v->val.vlist);
        break;
    }
}

static void
freestr(struct Str *str) {
    freemem(str->data, str->len);
}

static void
freehlist(struct Hlist *hl) {
    size_t i;

    for (i = 0; i < hl->len; ++i) {
        freemem(hl->data[i].data, hl->data[i].len);
    }
    freemem(hl->data, sizeof(Str) * hl->len);
}

static void
freevlist(struct Vlist *vl) {
    size_t i;
    size_t j;

    for (i = 0; i < vl->len; ++i) {
        for (j = 0; j < vl->data[i].len; ++j) {
            freemem(vl->data[i].data[j].data, vl->data[i].data[j].len);
        }
        freemem(vl->data[i].data, sizeof(Str) * vl->data[i].len);
    }
    freemem(vl->data, vl->len * sizeof(Hlist));
}

static void
freeval(struct Var *dst) {
    switch (dst->type) {
    case TYPE_STR:
        freestr(dst->val.str);
        break;
    case TYPE_HLIST:
        freehlist(dst->val.hlist);
        break;
    case TYPE_VLIST:
        freevlist(dst->val.vlist);
        break;
    }
    freemem(dst->val.anon, sizeof(Hlist));
}

static void
printstr(FILE *out, struct Str *str) {
    if (str->hash) {
        fputc('#', out);
    }
    fprintf(out, "\"%s\"", str->data);
}

static void
printhlist(FILE *out, struct Hlist *l) {
    size_t i;

    fputc('[', out);
    for (i = 0; i < l->len; ++i) {
        printstr(out, l->data + i);
        fprintf(out, ", ");
    }
    fputc(']', out);
}

static void
printvlist(FILE *out, struct Vlist *l) {
    size_t i;

    fprintf(out, "{\n");
    for (i = 0; i < l->len; ++i) {
        fprintf(out, "  ");
        printhlist(out, l->data + i);
        fprintf(out, ",\n");
    }
    fprintf(out, "}\n");
}

static void
printval(struct Var *dst, char *str, enum OFILE out) {
    FILE *ofile = out ? stdout : stderr;
    if (str) {
        printf("%s : ", str);
    }
    switch (dst->type) {
    case TYPE_STR:
        printstr(ofile, dst->val.str);
        break;
    case TYPE_HLIST:
        printhlist(ofile, dst->val.hlist);
        break;
    case TYPE_VLIST:
        printvlist(ofile, dst->val.vlist);
        break;
    }
    fputc('\n', ofile);
}

static void
valfromterm(struct Var *val, char *beg) {
    struct Arr *names = &aliasmap.namearr;
    char **aliasname;
    struct Var aliasval;
    size_t len;
    struct Str *ownstr;

    assert(!isgrammar(beg));

    aliasname = searcharr(&beg, names);
    aliasval = aliasmap.node[aliasname - chrbeg(names)];

    if (aliasval.val.anon) {
        copyval(val, &aliasval);
    } else {
        val->type = TYPE_STR;
        ownstr = val->val.str = alloc(sizeof(Str));
        len = ownstr->len = strlen(beg) + 1;
        ownstr->hash = 0;
        ownstr->data = alloc(len);
        memcpy(ownstr->data, beg, len);
    }
}

static Str *
emptystr(void) {
    struct Str *res = alloc(sizeof(Str));

    res->hash = 0;
    res->len = 1;
    res->data = alloc(1);
    res->data[0] = '\0';

    return res;
}

static void
pushlist(void *l, void *e, size_t s) {
    struct Hlist *v = l;
    size_t len = v->len;

    v->len = len + 1;
    reallocptr(&v->data, s, len);
    memcpy(v->data + len, e, s);
}

static Hlist *
emptyhlist(void) {
    struct Hlist *res = alloc(sizeof(Hlist));

    res->len = 0;
    res->data = NULL;

    return res;
}

static Vlist *
emptyvlist(void) {
    struct Vlist *res = alloc(sizeof(Vlist));

    res->len = 0;
    res->data = NULL;

    return res;
}

static void
hashval(struct Var *res) {
    struct Hlist *hlv;
    struct Str *strv;
    size_t i;
    size_t j;
    size_t vlen;
    size_t hlen;

    switch (res->type) {
    case TYPE_STR:
        res->val.str->hash = 1;
        break;
    case TYPE_HLIST:
        strv = res->val.hlist->data;
        hlen = res->val.hlist->len;
        for (i = 0; i < hlen; ++i) {
            strv[i].hash = 1;
        }
        break;
    case TYPE_VLIST:
        hlv = res->val.vlist->data;
        vlen = res->val.vlist->len;
        for (j = 0; j < vlen; ++j) {
            strv = hlv[j].data;
            hlen = hlv[j].len;
            for (i = 0; i < hlen; ++i) {
                strv[i].hash = 1;
            }
        }
        break;
    }
}

static void
exec(struct Var *expr, char **cmd) {
    char **argv = NULL;
    struct Str *strv;
    struct Hlist *hlp;
    struct Vlist *vl;
    size_t size = 0;
    size_t nspown = 0;

    int result;
    size_t i;
    size_t j;
    pid_t pid;

    switch (expr->type) {
    case TYPE_STR:
        strv = expr->val.str;
        if (strv->len == 0) {
            break;
        }
        size = 2;
        reallocptr(&argv, 2, sizeof(char *));
        argv[0] = strv->data;
        argv[1] = NULL;
        if ((pid = fork()) < 0) {
            err(1, "fork");
        } else if (pid == 0) {
            execvp(argv[0], argv);
        } else {
            if (wait(&result) == -1) {
                err(1, "wait");
            }
            if (result) {
                sigerrn(cmd - chrbeg(&tokarr), "executed command failed");
            }
            break;
        }
        break;
    case TYPE_HLIST:
        hlp = expr->val.hlist;
        if (hlp->len == 0) {
            break;
        }
        size = hlp->len + 1;
        reallocptr(&argv, hlp->len + 1, sizeof(char *));
        for (i = 0; i < hlp->len; ++i) {
            argv[i] = hlp->data[i].data;
        }
        argv[hlp->len] = NULL;
        if ((pid = fork()) < 0) {
            err(1, "fork");
        } else if (pid == 0) {
            execvp(argv[0], argv);
        } else {
            if (wait(&result) == -1) {
                err(1, "wait");
            }
            if (result) {
                sigerrn(cmd - chrbeg(&tokarr), "failed");
            }
            break;
        }
        break;
    case TYPE_VLIST:
        vl = expr->val.vlist;
        if (vl->len == 0) {
            break;
        }
        for (j = 0; j < vl->len; ++j) {
            hlp = vl->data + j;
            if (hlp->len == 0) {
                continue;
            }
            if (size < hlp->len + 1) {
                reallocptr(&argv, hlp->len + 1, sizeof(char *));
                size = hlp->len + 1;
            }
            for (i = 0; i < hlp->len; ++i) {
                argv[i] = hlp->data[i].data;
            }
            argv[hlp->len] = NULL;
            if ((pid = fork()) < 0) {
                err(1, "fork");
            } else if (pid == 0) {
                execvp(argv[0], argv);
            } else {
                ++nspown;
                continue;
            }
        }
        for (i = 0; i < nspown; ++i) {
            if (wait(&result) == -1) {
                err(1, "wait");
            }
            if (result) {
                sigerrn(cmd - chrbeg(&tokarr), "failed");
            }
        }
        break;
    }
    freeval(expr);
    freemem(argv, size * sizeof(char *));
}

static void
evalmk(char **toks, char **tokend) {
    char **curr = toks;
    char **begstat;

    while (curr < tokend) {
        begstat = curr;
        while (*curr != litts[SYM_SEMICOL]) {
            ++curr;
        }
#if DEBUG
        printtok(begstat, curr);
#endif
        evalstmnt(begstat, curr);
        ++curr;
    }
}

static void
evalstmnt(char **beg, char **end) {
    struct Var expr;
    struct Var *aliasval;
    char **i;
    char **alias;

    if (beg == end) {
        return;
    }
    if (end - beg > 2 && beg[1] == litts[SYM_EQ]) {
        if (isgrammar(*beg)) {
            sigerrn(beg - chrbeg(&tokarr), "aliasing base symbol");
        }
        if (searcharr(beg, &quotarr)) {
            sigerrn(beg - chrbeg(&tokarr), "aliasing litteral");
        }
        for (i = beg + 2; i < end; ++i) {
            if (*i == litts[SYM_EQ]) {
                sigerrn(i - chrbeg(&tokarr), "assign in expression");
            }
        }
        evalexpr(&expr, beg + 2, end);
        if ((alias = searcharr(beg, &aliasmap.namearr)) == NULL) {
            errx(1, "BUG: non parsed alias, %s", *beg);
        }
        aliasval = &aliasmap.node[alias - chrbeg(&aliasmap.namearr)];
        if (aliasval->val.anon) {
            freeval(aliasval);
        }
        memcpy(aliasval, &expr, sizeof(Var));
#if DEBUG
        printval(&expr, *alias, OFILE_ERR);
#endif
        return;
    } else {
        evalexpr(&expr, beg, end);
        exec(&expr, beg);
    }
}

static char **
evalexpr(struct Var *res, char **beg, char **end) {
    if (beg == end) {
        sigerrn(beg - chrbeg(&tokarr), "no expression to evaluate");
    }
    if (isbinaryop(*beg)) {
        sigerrn(beg - chrbeg(&tokarr), "missing left operand");
    }
    if ((beg = evalterm(res, beg, end)) != end) {
        sigerrn(beg - chrbeg(&tokarr), "malformed expression");
    }
    return beg;
}

static char **
evalterm(struct Var *res, char **beg, char **end) {

    assert(beg != end);

    if (!isterm(*beg)) {
        sigerrn(beg - chrbeg(&tokarr), "malformed expression");
    }
    if (isunaryop(*beg)) {
        beg = evalunaryop(res, beg, end);
    } else if (*beg == litts[SYM_L_SBRACK]) {
        beg = evalsbrack(res, beg, end);
    } else if (*beg == litts[SYM_L_CBRACK]) {
        beg = evalcbrack(res, beg, end);
    } else if (*beg == litts[SYM_L_RBRACK]) {
        beg = evalrbrack(res, beg, end);
    } else {
        valfromterm(res, *beg++);
    }
    while (isbinaryop(*beg) && beg != end) {
        beg = evalbinaryop(res, beg, end);
    }
    return beg;
}

static char **
evalbinaryop(struct Var *res, char **beg, char **end) {
    struct Var rhs;
    char *op = *beg;

    assert(isbinaryop(op));

    if (++beg == end) {
        sigerrn(beg - chrbeg(&tokarr), "missing right operand");
    }
    if (isbinaryop(*beg)) {
        sigerrn(beg - 1 - chrbeg(&tokarr), "missing right operand");
    }
    if (isgrammar(*beg)) {
        beg = evalterm(&rhs, beg, end);
    } else {
        valfromterm(&rhs, *beg++);
    }
    execbinaryop(res, &rhs, op);
    return beg;
}

static char **
evalrbrack(struct Var *res, char **beg, char **end) {
    size_t nest = 1;
    char **subend;

    assert(*beg == litts[SYM_L_RBRACK]);

    subend = ++beg;
    while (subend != end) {
        if (*subend == litts[SYM_L_RBRACK]) {
            ++nest;
        } else if (*subend == litts[SYM_R_RBRACK] && --nest == 0) {
            break;
        }
        ++subend;
    }
    if (*subend != litts[SYM_R_RBRACK]) {
        sigerrn(beg - chrbeg(&tokarr), "missing closing paren");
    }
    if (subend - beg == 0) {
        res->type = TYPE_STR;
        res->val.str = emptystr();
        beg = subend;
    } else if ((beg = evalterm(res, beg, subend)) != subend) {
        sigerrn(beg - chrbeg(&tokarr), "incomplete expression");
    }
    return beg + 1;
}

static char **
evalsbrack(struct Var *res, char **beg, char **end) {
    size_t nest = 1;
    char **subend;
    struct Var ele;

    assert(*beg == litts[SYM_L_SBRACK]);

    subend = ++beg;
    while (subend != end) {
        if (*subend == litts[SYM_L_SBRACK]) {
            ++nest;
        } else if (*subend == litts[SYM_R_SBRACK] && --nest == 0) {
            break;
        }
        ++subend;
    }
    if (*subend != litts[SYM_R_SBRACK]) {
        sigerrn(beg - chrbeg(&tokarr), "missing closing bracket");
    }
    res->type = TYPE_HLIST;
    res->val.hlist = emptyhlist();

    while (beg != subend) {
        beg = evalterm(&ele, beg, subend);
        convert(&ele, TYPE_HLIST);
        res->val.hlist = concathlist(res->val.hlist, ele.val.hlist);
    }
    return beg + 1;
}

static char **
evalcbrack(struct Var *res, char **beg, char **end) {
    size_t nest = 1;
    char **subend;

    assert(*beg == litts[SYM_L_CBRACK]);

    subend = ++beg;
    while (subend != end && *subend) {
        if (*subend == litts[SYM_L_CBRACK]) {
            ++nest;
        } else if (*subend == litts[SYM_R_CBRACK] && --nest == 0) {
            break;
        }
        ++subend;
    }
    if (*subend != litts[SYM_R_CBRACK]) {
        sigerrn(beg - chrbeg(&tokarr), "missing closing bracket");
    }
    res->type = TYPE_VLIST;
    res->val.vlist = emptyvlist();
    struct Var ele;
    while (beg != subend) {
        beg = evalterm(&ele, beg, subend);
        convert(&ele, TYPE_VLIST);
        res->val.vlist = concatvlist(res->val.vlist, ele.val.vlist);
    }
    return beg + 1;
}

static char **
evalunaryop(struct Var *res, char **beg, char **end) {
    char *op = *beg;
    char **cmd;

    assert(isunaryop(op));

    if ((cmd = ++beg) == end) {
        sigerrn(beg - chrbeg(&tokarr), "missing argument");
    }
    if (isgrammar(*beg)) {
        beg = evalterm(res, beg, end);
    } else {
        valfromterm(res, *beg++);
    }

    if (op == litts[SYM_HASH]) {
        hashval(res);
    } else if (op == litts[SYM_LESS]) {
        printval(res, NULL, OFILE_OUT);
    } else if (op == litts[SYM_AT]) {
        atval(res, cmd);
    } else {
        assert("BUG: unimplemented");
    }
    return beg;
}

static void
initarr(struct Arr *arr, size_t all) {
    arr->data = NULL;
    reallocptr(&arr->data, all, sizeof(char *));
    arr->alloc = all;
    arr->len = 0;
}

static void
pusharr(struct Arr *arr, void *d) {
    reallocarr(arr, 1);
    arr->data[arr->len++] = d;
}

static void
reallocarr(struct Arr *arr, size_t n) {
    if (arr->len == arr->alloc) {
        arr->alloc += arr->alloc / 2 + n;
        reallocptr(&arr->data, arr->alloc, sizeof(char *));
    }
}

static void
shrinkarr(struct Arr *arr) {
    reallocptr(&arr->data, arr->len, sizeof(char *));
    arr->alloc = arr->len;
}

static void
copymkarr(struct Arr *dst, struct Arr *src) {
    if (dst->alloc < src->len) {
        reallocptr(&dst->data, src->len, sizeof(char *));
        dst->alloc = src->len;
    }
    memcpy(dst->data, src->data, src->len * sizeof(char *));
    dst->len = src->len;
}

static void
sortstrarr(Arr *arr) {
    qsort(arr->data, arr->len, sizeof(char *), sortstr);
}

static char **
searcharr(char **k, Arr *arr) {
    return bsearch(k, arr->data, arr->len, sizeof(char *), sortstr);
}

static int
sortstr(const void *f, const void *s) {
    return strcmp(*(char **)f, *(char **)s);
}

static void
uniqarr(struct Arr *arr) {
    char **beg = chrbeg(arr);
    char **end = chrend(arr);
    char **dup = beg;

    while (dup != end) {
        while (strcmp(*dup, *beg) == 0) {
            if (++dup == end) {
                return;
            }
        }
        if (++beg == end) {
            break;
        }
        *beg = *dup;
    }
    arr->len = beg - chrbeg(arr);
    shrinkarr(arr);
}

static void
mapfromarr(struct Map *map, struct Arr *arr) {
    initarr(&map->namearr, 0);
    copymkarr(&map->namearr, arr);
    sortstrarr(&map->namearr);
    uniqarr(&map->namearr);
    if ((map->node = calloc(map->namearr.len, sizeof(Var))) == NULL) {
        err(1, "alloc");
    }
}

static void *
alloc(size_t s) {
    void *res;

    if ((res = malloc(s)) == NULL) {
        err(1, "alloc");
    }
    return res;
}

static void
reallocptr(void *p, size_t nmemb, size_t size) {
    void **ptr = p;
    size_t nsize = nmemb * size;

    if (nmemb && size && SIZE_MAX / nmemb < size) {
        errno = ENOMEM;
        err(1, "alloc");
    }
    if ((*ptr = realloc(*ptr, nsize)) == NULL && nsize) {
        err(1, "alloc");
    }
}

static struct Str *
strfromhlist(struct Hlist *hl) {
    size_t len = 0;
    struct Str *str;
    char *beg;
    size_t i;

    for (i = 0; i < hl->len; ++i) {
        len += hl->data[i].len;
    }
    len = len - hl->len + 1;
    str = alloc(sizeof(Str));
    str->len = len;
    str->hash = 0;
    beg = str->data = alloc(len);
    for (i = 0; i < hl->len; ++i) {
        if (hl->data[i].len) {
            memcpy(beg, hl->data[i].data, hl->data[i].len - 1);
            beg += hl->data[i].len - 1;
        }
    }
    *beg = '\0';
    freehlist(hl);
    freemem(hl, sizeof(Hlist));
    return str;
}

static struct Str *
strfromvlist(struct Vlist *vl) {
    struct Hlist *curr;
    struct Str *str;
    char *beg;
    size_t len = 0;
    size_t i;
    size_t j;

    for (j = 0; j < vl->len; ++j) {
        curr = vl->data + j;
        for (i = 0; i < curr->len; ++i) {
            len += curr->data[i].len;
        }
        len = len - curr->len + 1;
    }
    str = alloc(sizeof(Str));
    str->len = len;
    str->hash = 0;
    beg = str->data = alloc(len);
    for (j = 0; j < vl->len; ++j) {
        curr = vl->data + j;
        for (i = 0; i < curr->len; ++i) {
            if (curr->data[i].len) {
                memcpy(beg, curr->data[i].data, curr->data[i].len - 1);
                beg += curr->data[i].len - 1;
            }
        }
    }
    *beg = '\0';
    freevlist(vl);
    freemem(vl, sizeof(Vlist));
    return str;
}

static struct Vlist *
vlistfromhlist(struct Hlist *hl) {
    struct Str *strbeg = hl->data;
    struct Hlist *hlv = alloc(hl->len * sizeof(Hlist));
    struct Vlist *vl = (struct Vlist *)hl;
    size_t i;

    for (i = 0; i < hl->len; ++i) {
        hlv[i].len = 1;
        hlv[i].data = memown(strbeg + i, sizeof(Str));
    }
    vl->data = hlv;
    freemem(strbeg, hl->len * sizeof(Str));
    return vl;
}

static struct Hlist *
hlistfromstr(struct Str *s) {
    struct Hlist *hl = alloc(sizeof(Hlist));

    hl->len = 1;
    hl->data = s;

    return hl;
}

static struct Vlist *
vlistfromstr(struct Str *s) {
    struct Vlist *vl = alloc(sizeof(Vlist));

    vl->len = 1;
    vl->data = alloc(sizeof(Hlist));
    vl->data[0].len = 1;
    vl->data[0].data = s;
    return vl;
}

static struct Hlist *
hlistfromvlist(struct Vlist *vl) {
    size_t vlen = vl->len;
    size_t len = 0;
    struct Hlist *hlv = vl->data;
    struct Hlist *hl = (struct Hlist *)vl;
    struct Str *beg;
    size_t i;

    for (i = 0; i < vlen; ++i) {
        len += hlv[i].len;
    }
    hl->len = len;
    hl->data = alloc(sizeof(Str) * len);
    beg = hl->data;

    for (i = 0; i < vlen; ++i) {
        memcpy(beg, hlv[i].data, sizeof(Str) * hlv[i].len);
        beg += hlv[i].len;
    }
    for (i = 0; i < vlen; ++i) {
        freemem(hlv[i].data, hlv[i].len * sizeof(Str));
    }
    freemem(hlv, sizeof(Hlist));
    return hl;
}

static void
convert(struct Var *from, enum TYPE totype) {
    if (from->type == totype) {
        return;
    }
    switch (from->type) {
    case TYPE_STR:
        if (totype == TYPE_HLIST) {
            from->val.hlist = hlistfromstr(from->val.str);
        } else if (totype == TYPE_VLIST) {
            from->val.vlist = vlistfromstr(from->val.str);
        } else {
            assert("BUG: unreacheble");
        }
        break;
    case TYPE_HLIST:
        if (totype == TYPE_STR) {
            from->val.str = strfromhlist(from->val.hlist);
        } else if (totype == TYPE_VLIST) {
            from->val.vlist = vlistfromhlist(from->val.hlist);
        } else {
            assert("BUG: unreacheble");
        }
        break;
    case TYPE_VLIST:
        if (totype == TYPE_STR) {
            from->val.str = strfromvlist(from->val.vlist);
        } else if (totype == TYPE_HLIST) {
            from->val.hlist = hlistfromvlist(from->val.vlist);
        } else {
            assert("BUG: unreacheble");
        }
        break;
    }
    from->type = totype;
}

static void
appendstr(struct Str *f, struct Str *s) {
    f->hash &= s->hash;
    if (s->len == 0) {
        return;
    }
    if (f->len == 0) {
        reallocptr(&f->data, s->len, 1);
        memcpy(f->data, s->data, s->len);
        f->len = s->len;
        return;
    }
    reallocptr(&f->data, f->len + s->len - 1, 1);
    memcpy(f->data + f->len - 1, s->data, s->len);
    f->len += s->len - 1;
}

static void
prepend(struct Str *f, struct Str *s) {
    f->hash &= s->hash;
    if (s->len == 0) {
        return;
    }
    if (f->len == 0) {
        reallocptr(&f->data, s->len, 1);
        memcpy(f->data, s->data, s->len);
        f->len = s->len;
        return;
    }
    reallocptr(&f->data, f->len - 1 + s->len, 1);
    memmove(f->data + s->len - 1, f->data, f->len);
    memcpy(f->data, s->data, s->len - 1);
    f->len += s->len - 1;
}

static struct Str *
addstrstr(struct Str *f, struct Str *s) {
    appendstr(f, s);
    freestr(s);
    freemem(s, sizeof(Str));
    return f;
}

static struct Hlist *
addstrhlist(struct Str *f, struct Hlist *s) {
    struct Str *str = s->data;

    if (s->len == 0) {
        s->data = f;
        s->len = 1;
        freemem(f, sizeof(Str));
        return s;
    }

    prepend(str, f);
    freemem(f->data, f->len);
    freemem(f, sizeof(Str));
    return s;
}

static struct Vlist *
addstrvlist(struct Str *f, struct Vlist *s) {
    struct Str *str;
    struct Hlist *hlv = s->data;
    size_t len = s->len;
    size_t i;

    if (f->len == 0) {
        freemem(f, sizeof(Str));
        return s;
    }
    for (i = 0; i < len; ++i) {
        str = (hlv + i)->data;
        str->hash &= f->hash;
        if (str->len == 0) {
            reallocptr(&str->data, f->len, 1);
            memcpy(str->data, f->data, f->len);
            str->len = f->len;
            continue;
        }
        reallocptr(&str->data, str->len + f->len - 1, 1);
        memmove(str->data + f->len - 1, str->data, str->len);
        memcpy(str->data, f->data, f->len - 1);
        str->len += f->len - 1;
    }
    freemem(f->data, f->len);
    freemem(f, sizeof(Str));
    return s;
}

static struct Hlist *
addhlisthlist(struct Hlist *f, struct Hlist *s) {
    size_t len = f->len + s->len;

    if (f->len == 0) {
        freemem(f, sizeof(Hlist));
        return s;
    }
    reallocptr(&f->data, len, sizeof(Str));
    memcpy(f->data + f->len, s->data, s->len * sizeof(Str));
    f->len = len;
    freemem(s->data, s->len * sizeof(Str));
    freemem(s, sizeof(Hlist));
    return f;
}

static struct Vlist *
addhlistvlist(struct Hlist *f, struct Vlist *s) {
    size_t i;
    size_t j;
    struct Hlist *hlv;

    if (s->len == 0) {
        s->len = 1;
        s->data = f;
        return s;
    }
    hlv = s->data;
    for (i = 0; i < s->len; ++i) {
        reallocptr(&hlv[i].data, hlv[i].len + f->len, sizeof(Str));
        memmove(hlv[i].data + f->len, hlv[i].data, f->len * sizeof(Str));
        memcpy(hlv[i].data, f->data, f->len * sizeof(Str));
        for (j = 0; j < f->len; ++j) {
            hlv[i].data[j].data = alloc(hlv[i].data[j].len);
            memcpy(hlv[i].data[j].data, f->data[j].data, hlv[i].data[j].len);
        }
        hlv[i].len += f->len;
    }
    freehlist(f);
    freemem(f, sizeof(Hlist));
    return s;
}

static struct Vlist *
addvlisthlist(struct Vlist *s, struct Hlist *f) {
    struct Hlist *hlv;
    struct Str *strp;
    size_t i;
    size_t j;
    size_t nlen;

    if (s->len == 0) {
        s->len = 1;
        s->data = f;
        return s;
    }
    hlv = s->data;
    for (i = 0; i < s->len; ++i) {
        nlen = hlv[i].len + f->len;
        reallocptr(&hlv[i].data, nlen, sizeof(Str));
        strp = hlv[i].data + hlv[i].len;
        memcpy(strp, f->data, f->len * sizeof(Str));
        for (j = 0; j < f->len; ++j) {
            strp[j].data = memown(f->data[j].data, f->data[j].len);
        }
        hlv[i].len = nlen;
    }
    freehlist(f);
    freemem(f, sizeof(Hlist));
    return s;
}

static struct Vlist *
addvlistvlist(struct Vlist *f, struct Vlist *s) {
    struct Hlist *fhlv;
    struct Hlist *shlv;
    struct Str *strv;
    size_t i;
    size_t minlen;
    size_t nhlen;
    size_t vdiff;

    if (f->len == 0) {
        freemem(f, sizeof(Vlist));
        return s;
    }
    if (s->len == 0) {
        freemem(s, sizeof(Vlist));
        return f;
    }
    if (f->len < s->len) {
        minlen = f->len;
        reallocptr(&f->data, s->len, sizeof(Hlist));
        vdiff = s->len - minlen;
        memcpy(f->data + minlen, s->data + minlen, vdiff * sizeof(Hlist));
    } else {
        minlen = s->len;
    }
    fhlv = f->data;
    shlv = s->data;
    for (i = 0; i < minlen; ++i) {
        nhlen = shlv[i].len + fhlv[i].len;
        reallocptr(&fhlv[i].data, nhlen, sizeof(Str));
        strv = fhlv[i].data + fhlv[i].len;
        memcpy(strv, shlv[i].data, shlv[i].len * sizeof(Str));
        fhlv[i].len = nhlen;
    }
    if (f->len == minlen) {
        f->len = s->len;
    }
    for (i = 0; i < minlen; ++i) {
        freemem(shlv[i].data, shlv[i].len * sizeof(Str));
    }
    freemem(s->data, s->len * sizeof(Hlist));
    freemem(s, sizeof(Vlist));
    return f;
}

static struct Hlist *
addhliststr(struct Hlist *f, struct Str *s) {
    struct Str *str;

    if (f->len == 0) {
        f->data = s;
        f->len = 1;
        return f;
    }
    str = &f->data[f->len - 1];
    appendstr(str, s);
    freestr(s);
    freemem(s, sizeof(Str));
    return f;
}

static struct Vlist *
addvliststr(struct Vlist *f, struct Str *s) {
    struct Hlist *hlv;
    size_t vlen;
    size_t hlen;
    size_t i;

    if (f->len == 0) {
        hlv = f->data = emptyhlist();
        hlv->len = 1;
        hlv->data = s;
        return f;
    }
    hlv = f->data;
    vlen = f->len;
    for (i = 0; i < vlen; ++i) {
        if ((hlen = hlv[i].len) == 0) {
            hlv[i].len = 1;
            hlv[i].data = copystr(s);
            continue;
        }
        appendstr(&hlv[i].data[hlen - 1], s);
    }
    freestr(s);
    freemem(s, sizeof(Str));
    return f;
}

static struct Str *
concatstr(struct Str *f, struct Str *s) {
    return addstrstr(f, s);
}

static struct Hlist *
concathlist(struct Hlist *f, struct Hlist *s) {
    return addhlisthlist(f, s);
}

static struct Vlist *
concatvlist(struct Vlist *f, struct Vlist *s) {
    size_t len = f->len + s->len;

    reallocptr(&f->data, len, sizeof(Hlist));
    memcpy(f->data + f->len, s->data, s->len * sizeof(Hlist));
    f->len = len;
    freemem(s->data, s->len * sizeof(Hlist));
    freemem(s, sizeof(Vlist));
    return f;
}

static void
execbinaryop(struct Var *f, struct Var *s, char *op) {

    assert(isbinaryop(op));

    if (op == litts[SYM_PLUS]) {
        addval(f, s);
        return;
    } else if (op == litts[SYM_SUB]) {
        subval(f, s);
        return;
    } else if (op == litts[SYM_MOD] || op == litts[SYM_DIV]) {
        filtval(f, s, op);
        return;
    }
    errx(1, "unimplemented, %s", op);
}

static void
addval(struct Var *f, struct Var *s) {
    switch (f->type) {
    case TYPE_STR:
        switch (s->type) {
        case TYPE_STR:
            f->type = TYPE_STR;
            f->val.str = addstrstr(f->val.str, s->val.str);
            return;
        case TYPE_HLIST:
            f->type = TYPE_HLIST;
            f->val.hlist = addstrhlist(f->val.str, s->val.hlist);
            return;
        case TYPE_VLIST:
            f->type = TYPE_VLIST;
            f->val.vlist = addstrvlist(f->val.str, s->val.vlist);
            return;
        }
    case TYPE_HLIST:
        switch (s->type) {
        case TYPE_STR:
            f->type = TYPE_HLIST;
            f->val.hlist = addhliststr(f->val.hlist, s->val.str);
            return;
        case TYPE_HLIST:
            f->type = TYPE_HLIST;
            f->val.hlist = addhlisthlist(f->val.hlist, s->val.hlist);
            return;
        case TYPE_VLIST:
            f->type = TYPE_VLIST;
            f->val.vlist = addhlistvlist(f->val.hlist, s->val.vlist);
            return;
        }
    case TYPE_VLIST:
        switch (s->type) {
        case TYPE_STR:
            f->type = TYPE_VLIST;
            f->val.vlist = addvliststr(f->val.vlist, s->val.str);
            return;
        case TYPE_HLIST:
            f->type = TYPE_VLIST;
            f->val.vlist = addvlisthlist(f->val.vlist, s->val.hlist);
            return;
        case TYPE_VLIST:
            f->type = TYPE_VLIST;
            f->val.vlist = addvlistvlist(f->val.vlist, s->val.vlist);
            return;
        }
    }
}

static int
matchstrstr(struct Str *f, struct Str *s, enum POS pos, enum FIL fil) {
    if (s->len == 0) {
        return fil;
    }
    if (f->len < s->len) {
        return !fil;
    }
    if (pos == POS_BEG) {
        return memcmp(f->data, s->data, s->len - 1) == 0 ? fil : !fil;
    }
    return memcmp(f->data + f->len - s->len, s->data, s->len - 1) == 0 ? fil
                                                                       : !fil;
}

static struct Hlist *
filthlist(struct Hlist *f, struct Str *s, enum POS pos, enum FIL fil) {
    struct Str *strv = f->data;
    size_t len = f->len;
    size_t i;

    if (f->len == 0) {
        return f;
    }
    for (i = 0; i < f->len; ++i) {
        if (matchstrstr(f->data + i, s, pos, fil)) {
            memcpy(strv++, f->data + i, sizeof(Str));
        } else {
            freestr(f->data + i);
            --len;
        }
    }
    reallocptr(&f->data, len, sizeof(Str));
    f->len = len;
    return f;
}

static struct Hlist *
subhliststr(struct Hlist *f, struct Str *s, enum POS pos) {
    struct Str *strv = f->data;
    size_t len;
    size_t i;

    if (f->len == 0 || s->len == 0) {
        return f;
    }
    for (i = 0; i < f->len; ++i) {
        if (matchstrstr(f->data + i, s, pos, FIL_KEEP)) {
            len = strv[i].len - s->len + 1;
            if (pos == POS_END) {
                strv[i].data[len - 1] = '\0';
            } else {
                memmove(strv[i].data, strv[i].data + s->len - 1, len);
                strv[i].data[len - 1] = '\0';
            }
            reallocptr(&strv[i].data, len, 1);
            strv[i].len = len;
        }
    }
    return f;
}

static struct Vlist *
subvliststr(struct Vlist *f, struct Str *s, enum POS pos) {
    size_t i;

    if (f->len == 0 || s->len == 0) {
        return f;
    }
    for (i = 0; i < f->len; ++i) {
        subhliststr(f->data + i, s, pos);
    }
    return f;
}

static void
subval(struct Var *f, struct Var *s) {
    enum POS pos;
    struct Str *strdel;

    switch (f->type) {
    case TYPE_STR:
        switch (s->type) {
        case TYPE_STR:
            errx(1, "unimplemented: %d", __LINE__);
        case TYPE_HLIST:
            pos = POS_BEG;
            strdel = f->val.str;
            f->type = TYPE_HLIST;
            f->val.hlist = subhliststr(s->val.hlist, f->val.str, pos);
            break;
        case TYPE_VLIST:
            pos = POS_BEG;
            strdel = f->val.str;
            f->type = TYPE_VLIST;
            f->val.vlist = subvliststr(s->val.vlist, f->val.str, pos);
            break;
        }
        freestr(strdel);
        freemem(strdel, sizeof(Str));
        break;
    case TYPE_HLIST:
        switch (s->type) {
        case TYPE_STR:
            pos = POS_END;
            f->type = TYPE_HLIST;
            f->val.hlist = subhliststr(f->val.hlist, s->val.str, pos);
            freeval(s);
            return;
        case TYPE_HLIST:
        case TYPE_VLIST:
            errx(1, "unimplemented: %d", __LINE__);
        }
    case TYPE_VLIST:
        switch (s->type) {
        case TYPE_STR:
            pos = POS_END;
            f->type = TYPE_VLIST;
            f->val.vlist = subvliststr(f->val.vlist, s->val.str, pos);
            freeval(s);
            return;
        case TYPE_HLIST:
        case TYPE_VLIST:
            errx(1, "unimplemented: %d", __LINE__);
        }
    }
}

static struct Vlist *
filtvlist(struct Vlist *f, struct Str *s, enum POS pos, enum FIL fil) {
    struct Hlist *hlv = f->data;
    size_t len = f->len;
    size_t i;

    if (f->len == 0) {
        return f;
    }
    for (i = 0; i < f->len; ++i) {
        filthlist(f->data + i, s, pos, fil);
        if (f->data[i].len != 0) {
            memcpy(hlv++, f->data + i, sizeof(Hlist));
        } else {
            --len;
        }
    }
    reallocptr(&f->data, len, sizeof(Hlist));
    f->len = len;
    return f;
}

static void
filtval(struct Var *f, struct Var *s, char *op) {
    enum FIL fil = (op == litts[SYM_MOD]) ? FIL_DISCARD : FIL_KEEP;
    enum POS pos;
    struct Str *strdel;

    switch (f->type) {
    case TYPE_STR:
        switch (s->type) {
        case TYPE_STR:
            errx(1, "unimplemented: %d", __LINE__);
        case TYPE_HLIST:
            pos = POS_BEG;
            strdel = f->val.str;
            f->type = TYPE_HLIST;
            f->val.hlist = filthlist(s->val.hlist, f->val.str, pos, fil);
            break;
        case TYPE_VLIST:
            strdel = f->val.str;
            pos = POS_BEG;
            f->type = TYPE_VLIST;
            f->val.vlist = filtvlist(s->val.vlist, f->val.str, pos, fil);
            break;
        }
        freestr(strdel);
        freemem(strdel, sizeof(Str));
        break;
    case TYPE_HLIST:
        switch (s->type) {
        case TYPE_STR:
            pos = POS_END;
            f->type = TYPE_HLIST;
            f->val.hlist = filthlist(f->val.hlist, s->val.str, pos, fil);
            freeval(s);
            return;
        case TYPE_HLIST:
        case TYPE_VLIST:
            errx(1, "unimplemented: %d", __LINE__);
        }
        break;
    case TYPE_VLIST:
        switch (s->type) {
        case TYPE_STR:
            pos = POS_END;
            f->type = TYPE_VLIST;
            f->val.vlist = filtvlist(f->val.vlist, s->val.str, pos, fil);
            freeval(s);
            return;
        case TYPE_HLIST:
        case TYPE_VLIST:
            errx(1, "unimplemented: %d", __LINE__);
        }
    }
}

static struct Hlist *
atstr(struct Str *dname, char **cmd) {
    struct Hlist *files = emptyhlist();
    struct Str *strv = alloc(64 * sizeof(Str));
    size_t len = 0;
    struct dirent *dirp;
    DIR *dir;

    if (dname->len < 2) {
        sigerrn(cmd - chrbeg(&tokarr), "empty directory name");
    }
    if ((dir = opendir(dname->data)) == NULL) {
        err(1, "opendir");
    }
    while ((dirp = readdir(dir)) != NULL) {
        strv[len].hash = 1;
        strv[len].len = strlen(dirp->d_name) + 1;
        strv[len].data = memown(dirp->d_name, strv[len].len);
        if (((++len) % 64) == 0) {
            reallocptr(&strv, len + 64, sizeof(Str));
        }
    }
    reallocptr(&strv, len, sizeof(Str));
    qsort(strv, len, sizeof(Str), cmpstr);
    files->len = len;
    files->data = strv;
    closedir(dir);
    freestr(dname);
    freemem(dname, sizeof(Str));

    return files;
}

static int
cmpstr(const void *f, const void *s) {
    struct Str const *fs = f;
    struct Str const *ss = s;

    return strcmp(fs->data, ss->data);
}

static void
atval(struct Var *v, char **cmd) {
    switch (v->type) {
    case TYPE_STR:
        v->type = TYPE_HLIST;
        v->val.hlist = atstr(v->val.str, cmd);
        break;
    case TYPE_HLIST:
    case TYPE_VLIST:
        errx(1, "unimplemented: %d", __LINE__);
    }
}

static void
freemem(void *p, size_t size) {
    size_t i;

    if (DEALLOC_QUOTA == 0) {
        free(p);
        return;
    }
    if (size > DEALLOC_QUOTA) {
        free(p);
        return;
    }
    if (squalo.store + size > DEALLOC_QUOTA) {
        for (i = 0; i < squalo.delay.len; ++i) {
            free(squalo.delay.data[i]);
        }
        squalo.delay.len = 0;
        squalo.store = 0;
    }
    pusharr(&squalo.delay, p);
    squalo.store += size;
}

