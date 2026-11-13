#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _MSC_VER
#define FSEEK64 _fseeki64
#define FTELL64 _ftelli64
#else
#define FSEEK64 fseeko
#define FTELL64 ftello
#endif

static bool readWholeFile(const char* path, std::vector<uint8_t>& buf) {
    FILE* f = fopen(path, "rb");                 // binary mode: no CRLF translation
    if (!f) return false;
    FSEEK64(f, 0, SEEK_END);
    long long size = FTELL64(f);
    FSEEK64(f, 0, SEEK_SET);
    buf.resize((size_t)size);
    size_t got = size ? fread(buf.data(), 1, (size_t)size, f) : 0;
    fclose(f);
    return got == (size_t)size;
}

static std::string outputName(const std::string& in) {
    size_t slash = in.find_last_of("/\\");
    size_t dot = in.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return in + ".zip301";
    return in.substr(0, dot) + ".zip301";
}

// Compute Huffman code lengths for symbols with nonzero frequency.
static void huffmanLengths(const uint64_t freq[256], int len[256]) {
    struct Node { uint64_t w; int left, right; };
    std::vector<Node> nodes;
    nodes.reserve(512);
    std::vector<int> heap;                       // min-heap of node indices by weight
    for (int s = 0; s < 256; s++) {
        len[s] = 0;
        if (freq[s]) { nodes.push_back({freq[s], -1 - s, 0}); heap.push_back((int)nodes.size() - 1); }
    }
    if (heap.empty()) return;
    if (heap.size() == 1) { len[-1 - nodes[heap[0]].left] = 1; return; }

    auto cmp = [&](int a, int b) { return nodes[a].w > nodes[b].w; };
    std::make_heap(heap.begin(), heap.end(), cmp);
    while (heap.size() > 1) {
        std::pop_heap(heap.begin(), heap.end(), cmp); int a = heap.back(); heap.pop_back();
        std::pop_heap(heap.begin(), heap.end(), cmp); int b = heap.back(); heap.pop_back();
        nodes.push_back({nodes[a].w + nodes[b].w, a, b});
        heap.push_back((int)nodes.size() - 1);
        std::push_heap(heap.begin(), heap.end(), cmp);
    }
    // Iterative DFS to assign depths.
    std::vector<std::pair<int, int>> stack;
    stack.push_back({heap[0], 0});
    while (!stack.empty()) {
        int n = stack.back().first, d = stack.back().second; stack.pop_back();
        if (nodes[n].left < 0) { len[-1 - nodes[n].left] = d; continue; }
        stack.push_back({nodes[n].left, d + 1});
        stack.push_back({nodes[n].right, d + 1});
    }
}

// Canonical code assignment: shorter codes first, ties broken by symbol value.
static void canonicalCodes(const int len[256], uint64_t code[256]) {
    std::vector<int> syms;
    for (int s = 0; s < 256; s++) if (len[s]) syms.push_back(s);
    std::sort(syms.begin(), syms.end(), [&](int a, int b) {
        return len[a] != len[b] ? len[a] < len[b] : a < b; });
    uint64_t c = 0; int prevLen = 0;
    for (int s : syms) {
        c <<= (len[s] - prevLen);
        code[s] = c;
        prevLen = len[s];
        c++;
    }
}

static const char* symbolName(int s, char tmp[2]) {
    switch (s) {
        case ' ':  return "space";
        case '\t': return "tab";
        case '\n': return "newline";
        case '\r': return "return";
    }
    tmp[0] = (char)s; tmp[1] = 0;
    return tmp;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: zip301 <file.txt>\n"); return 1; }
    std::vector<uint8_t> in;
    if (!readWholeFile(argv[1], in)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const size_t n = in.size();
    const uint8_t* p = in.data();

    // Frequency count, 4 tables to avoid store-to-load stalls on repeated bytes.
    static uint64_t f4[4][256];
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        f4[0][p[i]]++; f4[1][p[i + 1]]++; f4[2][p[i + 2]]++; f4[3][p[i + 3]]++;
    }
    for (; i < n; i++) f4[0][p[i]]++;
    uint64_t freq[256];
    for (int s = 0; s < 256; s++) freq[s] = f4[0][s] + f4[1][s] + f4[2][s] + f4[3][s];

    int len[256];
    uint64_t code[256] = {0};
    huffmanLengths(freq, len);
    canonicalCodes(len, code);

    uint64_t totalBits = 0;
    for (int s = 0; s < 256; s++) totalBits += freq[s] * (uint64_t)len[s];

    // Header.
    std::string header;
    header.reserve(256 * 48);
    char tmp[2];
    for (int s = 0; s < 256; s++) {
        if (!len[s]) continue;
        for (int b = len[s] - 1; b >= 0; b--) header.push_back(((code[s] >> b) & 1) ? '1' : '0');
        header.push_back(' ');
        if (s == 0) header.push_back('\0');      // raw NUL byte (strlen would drop it)
        else header += symbolName(s, tmp);
        header.push_back('\n');
    }
    header += "*****\n";
    header += std::to_string(totalBits);
    header.push_back('\n');

    // Body.
    size_t bodyBytes = (size_t)((totalBits + 7) / 8);
    std::vector<uint8_t> out(header.size() + bodyBytes + 8);
    memcpy(out.data(), header.data(), header.size());
    uint8_t* w = out.data() + header.size();

    // Codes longer than 32 bits are possible in theory on huge skewed files;
    // split them so the accumulator never needs more than 63 bits.
    bool longCodes = false;
    for (int s = 0; s < 256; s++) if (len[s] > 32) longCodes = true;

    uint64_t acc = 0; int nb = 0;
    auto put = [&](uint64_t c, int l) {
        acc = (acc << l) | c; nb += l;
        if (nb >= 32) {
            nb -= 32;
            uint32_t v = (uint32_t)(acc >> nb);
            w[0] = (uint8_t)(v >> 24); w[1] = (uint8_t)(v >> 16);
            w[2] = (uint8_t)(v >> 8);  w[3] = (uint8_t)v;
            w += 4;
        }
    };
    if (!longCodes) {
        for (size_t k = 0; k < n; k++) put(code[p[k]], len[p[k]]);
    } else {
        for (size_t k = 0; k < n; k++) {
            int l = len[p[k]]; uint64_t c = code[p[k]];
            if (l > 32) { put(c >> 32, l - 32); put(c & 0xFFFFFFFFull, 32); }
            else put(c, l);
        }
    }
    // Flush remaining bits, zero-padded on the right.
    while (nb > 0) {
        if (nb >= 8) { nb -= 8; *w++ = (uint8_t)(acc >> nb); }
        else { *w++ = (uint8_t)(acc << (8 - nb)); nb = 0; }
    }

    std::string outName = outputName(argv[1]);
    FILE* fo = fopen(outName.c_str(), "wb");
    if (!fo) { fprintf(stderr, "cannot write %s\n", outName.c_str()); return 1; }
    fwrite(out.data(), 1, header.size() + bodyBytes, fo);
    fclose(fo);
    return 0;
}
