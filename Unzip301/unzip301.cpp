// unzip301.cpp - decompressor for the ZIP301 format (CS 301 final project)
//
// Speed notes: whole file read with one fread, 64-bit bit reader,
// 12-bit lookup table that decodes most symbols in one step (a tree walk
// handles only codes longer than 12 bits), output buffered and written in
// large chunks. Works with any valid prefix code, not just canonical ones,
// so it accepts files from the reference Zip301.

#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _MSC_VER
#define FSEEK64 _fseeki64
#define FTELL64 _ftelli64
#else
#define FSEEK64 fseeko
#define FTELL64 ftello
#endif

static bool readWholeFile(const char* path, std::vector<uint8_t>& buf) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    FSEEK64(f, 0, SEEK_END);
    long long size = FTELL64(f);
    FSEEK64(f, 0, SEEK_SET);
    buf.resize((size_t)size + 16, 0);            // zero padding so the bit reader can over-read
    size_t got = size ? fread(buf.data(), 1, (size_t)size, f) : 0;
    fclose(f);
    buf.resize((size_t)size);
    buf.reserve((size_t)size + 16);
    return got == (size_t)size;
}

static std::string outputName(const std::string& in) {
    size_t slash = in.find_last_of("/\\");
    size_t dot = in.find_last_of('.');
    std::string base = (dot == std::string::npos || (slash != std::string::npos && dot < slash))
                       ? in : in.substr(0, dot);
    return base + "2.txt";
}

static int parseName(const char* s, size_t l) {
    if (l == 1) return (unsigned char)s[0];
    std::string n(s, l);
    if (n == "space") return ' ';
    if (n == "tab") return '\t';
    if (n == "newline") return '\n';
    if (n == "return" || n == "carriage" || n == "cr") return '\r';
    return -1;
}

static const int TB = 12;                        // lookup table bits

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: unzip301 <file.zip301>\n"); return 1; }
    std::vector<uint8_t> in;
    if (!readWholeFile(argv[1], in)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const size_t n = in.size();
    const char* p = (const char*)in.data();

    // Tree: node 0 is root. child[node][bit]; leaf when sym >= 0.
    std::vector<int> child0(1, -1), child1(1, -1), sym(1, -1);
    int maxLen = 0, minLen = 64;

    size_t pos = 0;
    for (;;) {
        if (pos >= n) { fprintf(stderr, "bad file: no *****\n"); return 1; }
        size_t eol = pos;
        while (eol < n && p[eol] != '\n') eol++;
        size_t lineEnd = eol;
        if (lineEnd - pos >= 5 && memcmp(p + pos, "*****", 5) == 0) { pos = eol + 1; break; }
        if (lineEnd == pos) { pos = eol + 1; continue; }
        size_t sp = pos;
        while (sp < lineEnd && p[sp] != ' ') sp++;
        // Tolerate CRLF header lines, but keep a raw '\r' that is the symbol itself.
        if (lineEnd - sp - 1 > 1 && p[lineEnd - 1] == '\r') lineEnd--;
        int s = parseName(p + sp + 1, lineEnd - sp - 1);
        if (s < 0) { fprintf(stderr, "unknown symbol name in header\n"); return 1; }
        int node = 0, l = (int)(sp - pos);
        for (size_t k = pos; k < sp; k++) {
            std::vector<int>& ch = (p[k] == '1') ? child1 : child0;
            if (ch[node] < 0) {
                ch[node] = (int)sym.size();
                child0.push_back(-1); child1.push_back(-1); sym.push_back(-1);
            }
            node = ((p[k] == '1') ? child1 : child0)[node];
        }
        sym[node] = s;
        if (l > maxLen) maxLen = l;
        if (l < minLen) minLen = l;
        pos = eol + 1;
    }
    // Bit count line.
    uint64_t totalBits = 0;
    while (pos < n && (p[pos] < '0' || p[pos] > '9')) pos++;
    while (pos < n && p[pos] >= '0' && p[pos] <= '9') totalBits = totalBits * 10 + (p[pos++] - '0');
    while (pos < n && p[pos] != '\n') pos++;
    pos++;                                       // data starts right after '\n'

    std::string outName = outputName(argv[1]);
    FILE* fo = fopen(outName.c_str(), "wb");
    if (!fo) { fprintf(stderr, "cannot write %s\n", outName.c_str()); return 1; }
    if (totalBits == 0 || sym.size() == 1) { fclose(fo); return 0; }

    // Build lookup table: entry = (symbol | length << 8) for codes <= TB bits,
    // or (node index | 0x80000000) to continue walking the tree after TB bits.
    std::vector<uint32_t> table(1u << TB);
    for (uint32_t idx = 0; idx < (1u << TB); idx++) {
        int node = 0, depth = 0;
        while (depth < TB && sym[node] < 0) {
            int bit = (idx >> (TB - 1 - depth)) & 1;
            int nx = bit ? child1[node] : child0[node];
            if (nx < 0) break;                   // unused prefix (only in incomplete codes)
            node = nx; depth++;
        }
        if (sym[node] >= 0) table[idx] = (uint32_t)sym[node] | ((uint32_t)depth << 8);
        else table[idx] = 0x80000000u | (uint32_t)node;
    }

    const uint8_t* data = in.data() + pos;
    const uint8_t* dataEnd = in.data() + n;

    const size_t OUTCHUNK = 1 << 22;
    std::vector<uint8_t> outBuf(OUTCHUNK + 64);
    uint8_t* o = outBuf.data();
    uint8_t* oLimit = outBuf.data() + OUTCHUNK;

    // Bit reader: 'bits' holds 'cnt' valid bits left-aligned.
    uint64_t bits = 0; int cnt = 0;
    const uint8_t* rp = data;
    auto refill = [&]() {
        while (cnt <= 56) {
            uint64_t b = (rp < dataEnd) ? *rp : 0;
            rp++;
            bits |= b << (56 - cnt);
            cnt += 8;
        }
    };

    uint64_t consumed = 0;
    while (consumed < totalBits) {
        refill();
        // Decode several symbols per refill: each step uses at most TB bits from the table.
        while (cnt >= TB && consumed < totalBits) {
            uint32_t e = table[bits >> (64 - TB)];
            if (!(e & 0x80000000u)) {
                int l = (int)(e >> 8);
                *o++ = (uint8_t)e;
                bits <<= l; cnt -= l; consumed += l;
            } else {
                // Long code: skip TB bits and walk the tree.
                int node = (int)(e & 0x7FFFFFFFu);
                bits <<= TB; cnt -= TB; consumed += TB;
                while (sym[node] < 0) {
                    if (cnt == 0) refill();
                    int bit = (int)(bits >> 63);
                    bits <<= 1; cnt--; consumed++;
                    node = bit ? child1[node] : child0[node];
                    if (node < 0) { fprintf(stderr, "corrupt data\n"); fclose(fo); return 1; }
                }
                *o++ = (uint8_t)sym[node];
            }
            if (o >= oLimit) { fwrite(outBuf.data(), 1, o - outBuf.data(), fo); o = outBuf.data(); }
        }
    }
    fwrite(outBuf.data(), 1, o - outBuf.data(), fo);
    fclose(fo);
    return 0;
}
