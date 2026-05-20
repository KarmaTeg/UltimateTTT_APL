#ifndef MYIA_H
#define MYIA_H

// ============================================================================
//  MyIA.h — Ultimate Tic-Tac-Toe AI (v3 — Hyper-Optimized MCTS)
//  Améliorations v3 (Focus sur la vitesse brute et le cache CPU) :
//   • Node structure allégée à 24 octets (Cache-friendly, tient dans 1 ligne L1)
//   • Suppression du tableau untried[81] par nœud (gain de RAM massif)
//   • Playout "Lightweight" : focus uniquement sur l'Immediate Win (ultra-rapide)
//   • Heuristique simplifiée : évaluations purement arithmétiques
//   • Génération dynamique des coups non-explorés à l'expansion
// ============================================================================

#include "GameBoard.h"
#include "GameRules.h"
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace UTTTConst {
    static constexpr uint16_t WIN_LINES[8] = {
        0b000000111, 0b000111000, 0b111000000,
        0b001001001, 0b010010010, 0b100100100,
        0b100010001, 0b001010100
    };

    struct WinTable {
        bool data[512];
        constexpr WinTable() : data{} {
            for (int m = 0; m < 512; ++m) {
                bool w = false;
                for (int k = 0; k < 8; ++k)
                    if ((m & WIN_LINES[k]) == (int)WIN_LINES[k]) { w = true; break; }
                data[m] = w;
            }
        }
    };
    static constexpr WinTable WIN_TABLE{};
    inline bool hasLine(uint16_t mask) { return WIN_TABLE.data[mask & 0x1FF]; }
    static constexpr uint16_t FULL_MASK = 0x1FF;

    static constexpr int CELL_VALUE[9] = {
        3, 2, 3,
        2, 4, 2,
        3, 2, 3
    };
}

struct BB {
    uint16_t small[9][2];
    uint16_t bigOcc[2];
    uint16_t drawnBoards;
    int8_t   forcedBoard;
    uint8_t  sideToMove;

    inline uint16_t closedBoards() const { return bigOcc[0] | bigOcc[1] | drawnBoards; }
    inline bool isBoardOpen(int b) const { return ((closedBoards() >> b) & 1) == 0; }
    inline uint16_t freeCells(int b) const {
        return UTTTConst::FULL_MASK & ~(small[b][0] | small[b][1]);
    }

    static inline uint8_t pack(int bIdx, int cIdx) { return (uint8_t)((bIdx << 4) | cIdx); }
    static inline int bigOf(uint8_t mv)  { return (mv >> 4) & 0xF; }
    static inline int cellOf(uint8_t mv) { return mv & 0xF; }
};

inline void buildBB(const GameState& s, BB& bb) {
    std::memset(&bb, 0, sizeof(bb));
    for (int br = 0; br < 3; ++br) {
        for (int bc = 0; bc < 3; ++bc) {
            int b = br * 3 + bc;
            const SmallBoard& sb = s.boards[br][bc];
            for (int sr = 0; sr < 3; ++sr) {
                for (int sc = 0; sc < 3; ++sc) {
                    int cell = sr * 3 + sc;
                    Player p = sb.cells[sr][sc];
                    if      (p == Player::X) bb.small[b][0] |= (uint16_t)(1u << cell);
                    else if (p == Player::O) bb.small[b][1] |= (uint16_t)(1u << cell);
                }
            }
            if      (UTTTConst::hasLine(bb.small[b][0])) bb.bigOcc[0] |= (uint16_t)(1u << b);
            else if (UTTTConst::hasLine(bb.small[b][1])) bb.bigOcc[1] |= (uint16_t)(1u << b);
            else if ((bb.small[b][0] | bb.small[b][1]) == UTTTConst::FULL_MASK)
                bb.drawnBoards |= (uint16_t)(1u << b);
        }
    }
    bb.sideToMove = (s.currentPlayer == Player::X) ? 0 : 1;
    if (s.isBoardForced) {
        int fb = s.forcedRow * 3 + s.forcedCol;
        bb.forcedBoard = bb.isBoardOpen(fb) ? (int8_t)fb : (int8_t)-1;
    } else {
        bb.forcedBoard = -1;
    }
}

inline Move toOfficialMove(int bIdx, int cIdx) {
    return Move(bIdx / 3, bIdx % 3, cIdx / 3, cIdx % 3);
}

inline int genMoves(const BB& bb, uint8_t* out) {
    int n = 0;
    if (bb.forcedBoard >= 0) {
        int b = bb.forcedBoard;
        uint16_t free = bb.freeCells(b);
        while (free) {
            int c = __builtin_ctz(free);
            out[n++] = BB::pack(b, c);
            free &= free - 1;
        }
    } else {
        uint16_t closed = bb.closedBoards();
        for (int b = 0; b < 9; ++b) {
            if ((closed >> b) & 1) continue;
            uint16_t free = bb.freeCells(b);
            while (free) {
                int c = __builtin_ctz(free);
                out[n++] = BB::pack(b, c);
                free &= free - 1;
            }
        }
    }
    return n;
}

inline int playBB(BB& bb, uint8_t mv) {
    int b = BB::bigOf(mv);
    int c = BB::cellOf(mv);
    int s = bb.sideToMove;
    uint16_t cellBit = (uint16_t)(1u << c);
    bb.small[b][s] |= cellBit;

    int gameWon = 0;
    if (UTTTConst::hasLine(bb.small[b][s])) {
        bb.bigOcc[s] |= (uint16_t)(1u << b);
        if (UTTTConst::hasLine(bb.bigOcc[s])) gameWon = 1;
    } else if ((bb.small[b][0] | bb.small[b][1]) == UTTTConst::FULL_MASK) {
        bb.drawnBoards |= (uint16_t)(1u << b);
    }

    bb.forcedBoard = bb.isBoardOpen(c) ? (int8_t)c : (int8_t)-1;
    bb.sideToMove ^= 1;
    return gameWon;
}

inline bool wouldWinSmall(const BB& bb, uint8_t mv, int side) {
    int b = BB::bigOf(mv);
    int c = BB::cellOf(mv);
    uint16_t after = bb.small[b][side] | (uint16_t)(1u << c);
    return UTTTConst::hasLine(after);
}

inline bool wouldWinGame(const BB& bb, uint8_t mv, int side) {
    int b = BB::bigOf(mv);
    int c = BB::cellOf(mv);
    uint16_t afterSmall = bb.small[b][side] | (uint16_t)(1u << c);
    if (!UTTTConst::hasLine(afterSmall)) return false;
    uint16_t afterBig = bb.bigOcc[side] | (uint16_t)(1u << b);
    return UTTTConst::hasLine(afterBig);
}

struct XorShift32 {
    uint32_t state;
    inline uint32_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }
    inline uint32_t bounded(uint32_t n) { return next() % n; }
};

inline int heuristicScore(const BB& bb, uint8_t mv, int side) {
    if (wouldWinGame(bb, mv, side)) return 10000;

    int bIdx = BB::bigOf(mv);
    int cIdx = BB::cellOf(mv);
    int score = 0;

    if (wouldWinSmall(bb, mv, side)) score += 500;
    score += UTTTConst::CELL_VALUE[cIdx] * 3;
    score += UTTTConst::CELL_VALUE[bIdx] * 2;

    if (!bb.isBoardOpen(cIdx)) score -= 100;

    return score;
}

// ============================================================================
// Playout Lightweight : Seulement les victoires immédiates + Random complet
// ============================================================================
inline int playout(BB bb, XorShift32& rng) {
    uint8_t moves[81];
    for (int step = 0; step < 100; ++step) {
        int n = genMoves(bb, moves);
        if (n == 0) break;

        int side = bb.sideToMove;
        int chosen = -1;

        // Priorité 1 unique : gagner si on peut
        for (int i = 0; i < n; ++i) {
            if (wouldWinGame(bb, moves[i], side)) { chosen = i; break; }
        }

        if (chosen < 0) {
            chosen = (int)rng.bounded((uint32_t)n);
        }

        if (playBB(bb, moves[chosen])) return side;
    }

    if (UTTTConst::hasLine(bb.bigOcc[0])) return 0;
    if (UTTTConst::hasLine(bb.bigOcc[1])) return 1;

    int cX = __builtin_popcount(bb.bigOcc[0]);
    int cO = __builtin_popcount(bb.bigOcc[1]);
    if (cX > cO) return 0;
    if (cO > cX) return 1;
    return 2;
}

// ============================================================================
// Nœud structure allégée - 24 octets max !
// ============================================================================
struct Node {
    int32_t  parent;
    int32_t  firstChild;
    int32_t  nextSibling;
    uint32_t visits;
    float    winsForSideJustMoved;
    float    prior;

    uint8_t  moveFromParent;
    uint8_t  sideJustMoved;
    uint8_t  numChildren;
    uint8_t  maxChildren;
};

class MyIA {
public:
    static constexpr int TIME_BUDGET_MS = 200;
    static constexpr int POOL_SIZE = 500000; // Augmenté car les nodes sont plus petits

    static Move getMove(const GameState& rootState) {
        auto t0 = std::chrono::steady_clock::now();

        BB rootBB;
        buildBB(rootState, rootBB);

        uint8_t failsafeBuf[81];
        int failsafeN = genMoves(rootBB, failsafeBuf);
        if (failsafeN == 0) return Move();
        Move failsafe = toOfficialMove(BB::bigOf(failsafeBuf[0]), BB::cellOf(failsafeBuf[0]));
        if (failsafeN == 1) return failsafe;

        int mySide = rootBB.sideToMove;
        for (int i = 0; i < failsafeN; ++i) {
            if (wouldWinGame(rootBB, failsafeBuf[i], mySide)) {
                return toOfficialMove(BB::bigOf(failsafeBuf[i]), BB::cellOf(failsafeBuf[i]));
            }
        }

        static Node* pool = nullptr;
        if (!pool) pool = new Node[POOL_SIZE];
        int32_t poolUsed = 0;

        XorShift32 rng;
        rng.state = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        if (rng.state == 0) rng.state = 0xDEADBEEFu;

        int32_t rootIdx = poolUsed++;
        Node& root = pool[rootIdx];
        root.parent = -1;
        root.firstChild = -1;
        root.nextSibling = -1;
        root.numChildren = 0;
        root.maxChildren = (uint8_t)failsafeN;
        root.visits = 0;
        root.winsForSideJustMoved = 0.0f;
        root.prior = 0.5f;
        root.moveFromParent = 0;
        root.sideJustMoved = (uint8_t)(rootBB.sideToMove ^ 1);

        auto deadline = t0 + std::chrono::milliseconds(TIME_BUDGET_MS);
        uint64_t iter = 0;

        const float C_UCT = 0.9f;
        const float BIAS_W = 2.0f;

        while (true) {
            if ((iter & 2047) == 0) { // Check de temps moins fréquent (plus rapide)
                if (std::chrono::steady_clock::now() >= deadline) break;
            }
            if (poolUsed >= POOL_SIZE - 2) break;
            ++iter;

            // ============== SELECTION ==============
            BB bb = rootBB;
            int32_t nodeIdx = rootIdx;

            while (true) {
                Node& nd = pool[nodeIdx];
                if (nd.numChildren < nd.maxChildren) break; // Pas totalement étendu
                if (nd.numChildren == 0) break; // Terminal

                float logN = std::log((float)nd.visits + 1.0f);
                int32_t bestChild = -1;
                float bestScore = -1e30f;

                for (int32_t cIdx = nd.firstChild; cIdx >= 0; cIdx = pool[cIdx].nextSibling) {
                    const Node& ch = pool[cIdx];
                    float score;
                    if (ch.visits == 0) {
                        score = 10.0f + ch.prior;
                    } else {
                        float q = ch.winsForSideJustMoved / (float)ch.visits;
                        float u = C_UCT * std::sqrt(logN / (float)ch.visits);
                        float bias = BIAS_W * ch.prior / (1.0f + (float)ch.visits);
                        score = q + u + bias;
                    }
                    if (score > bestScore) {
                        bestScore = score;
                        bestChild = cIdx;
                    }
                }
                if (bestChild < 0) break;
                nodeIdx = bestChild;
                playBB(bb, pool[nodeIdx].moveFromParent);

                if (UTTTConst::hasLine(bb.bigOcc[0]) || UTTTConst::hasLine(bb.bigOcc[1])) break;
            }

            Node& sel = pool[nodeIdx];

            // ============== EXPANSION ==============
            int32_t leafIdx = nodeIdx;
            bool terminal = UTTTConst::hasLine(bb.bigOcc[0]) || UTTTConst::hasLine(bb.bigOcc[1]);

            if (!terminal && sel.numChildren < sel.maxChildren && poolUsed < POOL_SIZE - 1) {
                uint8_t moves[81];
                int n = genMoves(bb, moves);

                // Soustraction O(1) des enfants existants
                for (int32_t cIdx = sel.firstChild; cIdx >= 0; cIdx = pool[cIdx].nextSibling) {
                    uint8_t cmv = pool[cIdx].moveFromParent;
                    for (int i = 0; i < n; ++i) {
                        if (moves[i] == cmv) {
                            moves[i] = moves[--n];
                            break;
                        }
                    }
                }

                uint8_t mv = moves[rng.bounded((uint32_t)n)];

                int sideWhoPlays = bb.sideToMove;
                int hScore = heuristicScore(bb, mv, sideWhoPlays);
                float prior = 0.5f + (float)hScore / 10000.0f;
                if (prior < 0.0f) prior = 0.0f;
                if (prior > 1.0f) prior = 1.0f;

                int32_t childIdx = poolUsed++;
                Node& ch = pool[childIdx];
                ch.parent = nodeIdx;
                ch.firstChild = -1;
                ch.nextSibling = sel.firstChild;
                sel.firstChild = childIdx;
                ch.numChildren = 0;
                ch.visits = 0;
                ch.winsForSideJustMoved = 0.0f;
                ch.prior = prior;
                ch.moveFromParent = mv;
                ch.sideJustMoved = (uint8_t)sideWhoPlays;

                playBB(bb, mv);

                uint8_t tmpMoves[81];
                ch.maxChildren = (uint8_t)genMoves(bb, tmpMoves);

                sel.numChildren++;
                leafIdx = childIdx;
            }

            // ============== SIMULATION ==============
            int simResult;
            if      (UTTTConst::hasLine(bb.bigOcc[0])) simResult = 0;
            else if (UTTTConst::hasLine(bb.bigOcc[1])) simResult = 1;
            else {
                uint8_t tmp[81];
                int nn = genMoves(bb, tmp);
                if (nn == 0) {
                    int cX = __builtin_popcount(bb.bigOcc[0]);
                    int cO = __builtin_popcount(bb.bigOcc[1]);
                    simResult = (cX > cO) ? 0 : (cO > cX) ? 1 : 2;
                } else {
                    simResult = playout(bb, rng);
                }
            }

            // ============== BACKPROPAGATION ==============
            int32_t cur = leafIdx;
            while (cur >= 0) {
                Node& nd = pool[cur];
                nd.visits++;
                int side = nd.sideJustMoved;
                if      (simResult == side) nd.winsForSideJustMoved += 1.0f;
                else if (simResult == 2)    nd.winsForSideJustMoved += 0.5f;
                cur = nd.parent;
            }
        }

        // ====================================================================
        // Sélection du meilleur coup
        // ====================================================================
        Node& finalRoot = pool[rootIdx];
        if (finalRoot.numChildren == 0 || finalRoot.firstChild < 0) {
            return failsafe;
        }

        int32_t bestChild = -1;
        uint32_t bestVisits = 0;
        float bestQTieBreak = -1.0f;
        for (int32_t cIdx = finalRoot.firstChild; cIdx >= 0; cIdx = pool[cIdx].nextSibling) {
            const Node& ch = pool[cIdx];
            float q = ch.visits > 0 ? (ch.winsForSideJustMoved / ch.visits) : -1.0f;
            if (ch.visits > bestVisits || (ch.visits == bestVisits && q > bestQTieBreak)) {
                bestVisits = ch.visits;
                bestQTieBreak = q;
                bestChild = cIdx;
            }
        }
        if (bestChild < 0) return failsafe;

        uint8_t mv = pool[bestChild].moveFromParent;
        return toOfficialMove(BB::bigOf(mv), BB::cellOf(mv));
    }
};

#endif // MYIA_H
