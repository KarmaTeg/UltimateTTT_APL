#ifndef MYIA_H
#define MYIA_H

#include "GameBoard.h"
#include "GameRules.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cmath>

// =====================================================================
//  MyIA - Ultimate Tic-Tac-Toe AI
// =====================================================================
//  Algorithme : MCTS (Monte Carlo Tree Search) + UCT
//
//  Pourquoi MCTS plutot que Minimax + AlphaBeta ?
//  ----------------------------------------------
//  - UTTT a un branching factor eleve (jusqu'a 81 coups au debut).
//  - L'evaluation heuristique statique est notoirement faible en UTTT
//    (gagner un small board peut etre mauvais selon ou ca envoie l'adv).
//  - MCTS est "anytime" : on peut couper proprement a tout moment et
//    recuperer le meilleur coup trouve jusque-la -> failsafe parfait.
//
//  Optimisations principales :
//  ---------------------------
//  1. Bitboards 16-bit par small board + LUT de victoire (512 entrees)
//  2. Pool de noeuds pre-alloue (ZERO allocation pendant la recherche)
//  3. XorShift32 pour les playouts (3-4x plus rapide que mt19937)
//  4. UCT avec FPU (First Play Urgency) sur enfants non visites
//  5. Detection de victoire immediate / blocage avant MCTS
//  6. Playouts semi-aleatoires (capture des coups gagnants evidents)
//  7. Budget temps strict, verifie toutes les 1024 iterations
// =====================================================================


// ---------------------------------------------------------------------
//  PARAMETRES
// ---------------------------------------------------------------------
static constexpr int    TIME_BUDGET_MS      = 90;
static constexpr int    PANIC_THRESHOLD_MS  = 2;
static constexpr int    NODE_POOL_SIZE      = 250000;
static constexpr int    MAX_MOVES           = 81;
static constexpr double UCT_C               = 1.41421356237;
static constexpr int    TIMEOUT_CHECK_FREQ  = 1024;


// ---------------------------------------------------------------------
//  XorShift32 - RNG ultra-rapide pour les playouts
// ---------------------------------------------------------------------
struct XorShift32 {
    uint32_t state;
    XorShift32(uint32_t seed = 2463534242u) : state(seed ? seed : 1u) {}
    inline uint32_t next() {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    inline uint32_t nextBounded(uint32_t n) { return next() % n; }
};


// ---------------------------------------------------------------------
//  BITBOARD - tables et utilitaires
// ---------------------------------------------------------------------
static constexpr uint16_t WIN_PATTERNS[8] = {
    0b000000111, 0b000111000, 0b111000000,
    0b001001001, 0b010010010, 0b100100100,
    0b100010001, 0b001010100,
};

struct WinLUT {
    bool data[512];
    constexpr WinLUT() : data{} {
        for (int m = 0; m < 512; ++m) {
            bool w = false;
            for (int p = 0; p < 8; ++p)
                if ((m & WIN_PATTERNS[p]) == WIN_PATTERNS[p]) { w = true; break; }
            data[m] = w;
        }
    }
};
static constexpr WinLUT WIN_LUT = WinLUT();

inline bool isWonMask(uint16_t mask) { return WIN_LUT.data[mask & 0x1FF]; }

inline int popcount9(uint16_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcount(x);
#else
    int c = 0; while (x) { x &= x - 1; ++c; } return c;
#endif
}

inline int ctz9(uint16_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(x);
#else
    int c = 0; while (!(x & 1)) { x >>= 1; ++c; } return c;
#endif
}


// ---------------------------------------------------------------------
//  CompactBoard - representation bitboard dense
// ---------------------------------------------------------------------
struct CompactBoard {
    uint16_t smallX[9];
    uint16_t smallO[9];
    uint16_t bigX;
    uint16_t bigO;
    uint16_t bigClosed;
    int8_t   nextBoard;
    uint8_t  toMove;

    void fromGameState(const GameState& gs) {
        for (int b = 0; b < 9; ++b) {
            int br = b / 3, bc = b % 3;
            smallX[b] = 0; smallO[b] = 0;
            const SmallBoard& sb = gs.boards[br][bc];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) {
                    int bit = r * 3 + c;
                    if (sb.cells[r][c] == Player::X) smallX[b] |= (1u << bit);
                    else if (sb.cells[r][c] == Player::O) smallO[b] |= (1u << bit);
                }
        }
        bigX = 0; bigO = 0; bigClosed = 0;
        for (int br = 0; br < 3; ++br)
            for (int bc = 0; bc < 3; ++bc) {
                int b = br * 3 + bc;
                Player w = gs.bigBoard[br][bc];
                if (w == Player::X) { bigX |= (1u << b); bigClosed |= (1u << b); }
                else if (w == Player::O) { bigO |= (1u << b); bigClosed |= (1u << b); }
                else if ((smallX[b] | smallO[b]) == 0x1FF) bigClosed |= (1u << b);
            }
        toMove = (gs.currentPlayer == Player::X) ? 0 : 1;
        if (gs.isBoardForced) {
            int idx = gs.forcedRow * 3 + gs.forcedCol;
            nextBoard = (bigClosed & (1u << idx)) ? -1 : (int8_t)idx;
        } else nextBoard = -1;
    }

    inline int generateMoves(uint8_t* buf) const {
        int n = 0;
        if (nextBoard != -1) {
            uint16_t occ = smallX[nextBoard] | smallO[nextBoard];
            uint16_t free_mask = (~occ) & 0x1FF;
            while (free_mask) {
                int cell = ctz9(free_mask);
                buf[n++] = (uint8_t)((nextBoard << 4) | cell);
                free_mask &= free_mask - 1;
            }
        } else {
            uint16_t open_boards = (~bigClosed) & 0x1FF;
            while (open_boards) {
                int b = ctz9(open_boards);
                uint16_t occ = smallX[b] | smallO[b];
                uint16_t free_mask = (~occ) & 0x1FF;
                while (free_mask) {
                    int cell = ctz9(free_mask);
                    buf[n++] = (uint8_t)((b << 4) | cell);
                    free_mask &= free_mask - 1;
                }
                open_boards &= open_boards - 1;
            }
        }
        return n;
    }

    // Joue un coup. Retourne 0 = pas fini, 1 = X gagne, 2 = O gagne, 3 = draw.
    inline int playMove(uint8_t encoded) {
        int b    = encoded >> 4;
        int cell = encoded & 0x0F;
        uint16_t bit = (uint16_t)(1u << cell);

        if (toMove == 0) smallX[b] |= bit;
        else             smallO[b] |= bit;

        uint16_t myMask = (toMove == 0) ? smallX[b] : smallO[b];
        uint16_t bbit   = (uint16_t)(1u << b);
        if (isWonMask(myMask)) {
            if (toMove == 0) bigX |= bbit; else bigO |= bbit;
            bigClosed |= bbit;
            uint16_t bigMask = (toMove == 0) ? bigX : bigO;
            if (isWonMask(bigMask)) return (toMove == 0) ? 1 : 2;
        } else if ((smallX[b] | smallO[b]) == 0x1FF) {
            bigClosed |= bbit;
        }

        nextBoard = (bigClosed & (1u << cell)) ? -1 : (int8_t)cell;

        if (bigClosed == 0x1FF) {
            int cntX = popcount9(bigX);
            int cntO = popcount9(bigO);
            if (cntX > cntO) return 1;
            if (cntO > cntX) return 2;
            return 3;
        }

        toMove ^= 1;
        return 0;
    }
};


// ---------------------------------------------------------------------
//  Node - noeud MCTS
// ---------------------------------------------------------------------
struct Node {
    int32_t  firstChild;
    int16_t  childCount;
    uint8_t  move;
    uint8_t  toMoveAtNode;
    uint32_t visits;
    double   wins;
};


// ---------------------------------------------------------------------
//  MyIA
// ---------------------------------------------------------------------
class MyIA {
public:
    static Move getMove(const GameState& state) {
        auto t0 = std::chrono::steady_clock::now();
        auto deadline = t0 + std::chrono::milliseconds(TIME_BUDGET_MS);

        CompactBoard board;
        board.fromGameState(state);

        uint8_t moves[MAX_MOVES];
        int nMoves = board.generateMoves(moves);

        if (nMoves == 0) return Move();
        if (nMoves == 1) return decodeMove(moves[0]);
        if (TIME_BUDGET_MS <= PANIC_THRESHOLD_MS) return decodeMove(moves[0]);

        // === Heuristique 1 : coup gagnant immediat global ===
        for (int i = 0; i < nMoves; ++i) {
            CompactBoard tmp = board;
            int res = tmp.playMove(moves[i]);
            if ((board.toMove == 0 && res == 1) ||
                (board.toMove == 1 && res == 2))
                return decodeMove(moves[i]);
        }

        // === Heuristique 2 : coup safe force (quand quasi tous perdent) ===
        int forcedSafe = findForcedSafeMove(board, moves, nMoves);
        if (forcedSafe >= 0) return decodeMove(moves[forcedSafe]);

        return runMCTS(board, moves, nMoves, deadline);
    }

private:
    static Node       s_pool[NODE_POOL_SIZE];
    static int        s_poolUsed;
    static XorShift32 s_rng;

    static Move decodeMove(uint8_t encoded) {
        int b = encoded >> 4, cell = encoded & 0x0F;
        return Move(b / 3, b % 3, cell / 3, cell % 3);
    }

    static int findForcedSafeMove(const CompactBoard& board,
                                  const uint8_t* moves, int nMoves) {
        int safeMove = -1;
        int countLoss = 0;
        for (int i = 0; i < nMoves; ++i) {
            CompactBoard afterOurs = board;
            int res = afterOurs.playMove(moves[i]);
            if (res != 0) {
                if ((board.toMove == 0 && res == 1) ||
                    (board.toMove == 1 && res == 2)) return i;
                if (safeMove < 0) safeMove = i;
                continue;
            }
            uint8_t advMoves[MAX_MOVES];
            int nAdv = afterOurs.generateMoves(advMoves);
            bool advWins = false;
            for (int j = 0; j < nAdv; ++j) {
                CompactBoard afterAdv = afterOurs;
                int r2 = afterAdv.playMove(advMoves[j]);
                if ((afterOurs.toMove == 0 && r2 == 1) ||
                    (afterOurs.toMove == 1 && r2 == 2)) { advWins = true; break; }
            }
            if (advWins) countLoss++;
            else if (safeMove < 0) safeMove = i;
        }
        if (safeMove >= 0 && countLoss >= nMoves - 1) return safeMove;
        return -1;
    }

    static Move runMCTS(const CompactBoard& root,
                        const uint8_t* rootMoves, int nRootMoves,
                        std::chrono::steady_clock::time_point deadline) {
        s_poolUsed = 0;
        int32_t rootIdx = allocNode();
        Node& rootNode = s_pool[rootIdx];
        rootNode.firstChild   = -1;
        rootNode.childCount   = 0;
        rootNode.move         = 0xFF;
        rootNode.toMoveAtNode = root.toMove;
        rootNode.visits       = 0;
        rootNode.wins         = 0.0;

        expandNode(rootIdx, root, rootMoves, nRootMoves);
        if (s_pool[rootIdx].firstChild == -1)
            return decodeMove(rootMoves[0]);

        int iteration = 0;
        while (true) {
            if ((iteration & (TIMEOUT_CHECK_FREQ - 1)) == 0) {
                if (std::chrono::steady_clock::now() >= deadline) break;
                if (s_poolUsed >= NODE_POOL_SIZE - MAX_MOVES - 2) break;
            }
            ++iteration;

            CompactBoard sim = root;
            mctsIteration(rootIdx, sim);
        }

        const Node& rn = s_pool[rootIdx];
        int bestIdx = -1;
        uint32_t bestVisits = 0;
        double bestRatio = -1.0;
        for (int i = 0; i < rn.childCount; ++i) {
            const Node& ch = s_pool[rn.firstChild + i];
            double r = ch.visits > 0 ? ch.wins / (double)ch.visits : -1.0;
            if (ch.visits > bestVisits ||
                (ch.visits == bestVisits && r > bestRatio)) {
                bestVisits = ch.visits;
                bestRatio  = r;
                bestIdx = i;
            }
        }
        if (bestIdx < 0) return decodeMove(rootMoves[0]);
        return decodeMove(s_pool[rn.firstChild + bestIdx].move);
    }

    static inline int32_t allocNode() {
        if (s_poolUsed >= NODE_POOL_SIZE) return -1;
        return s_poolUsed++;
    }

    static void expandNode(int32_t nodeIdx, const CompactBoard& board,
                           const uint8_t* moves, int nMoves) {
        if (nMoves == 0) return;
        if (s_poolUsed + nMoves > NODE_POOL_SIZE) return;
        int32_t first = s_poolUsed;
        s_poolUsed += nMoves;

        Node& n = s_pool[nodeIdx];
        n.firstChild = first;
        n.childCount = (int16_t)nMoves;
        for (int i = 0; i < nMoves; ++i) {
            Node& c = s_pool[first + i];
            c.firstChild   = -1;
            c.childCount   = 0;
            c.move         = moves[i];
            c.toMoveAtNode = board.toMove ^ 1;
            c.visits       = 0;
            c.wins         = 0.0;
        }
    }

    static void mctsIteration(int32_t rootIdx, CompactBoard& board) {
        int32_t path[MAX_MOVES + 2];
        int pathLen = 0;
        path[pathLen++] = rootIdx;

        int32_t curIdx = rootIdx;
        int terminal = 0;

        // SELECTION (UCT)
        while (true) {
            Node& cur = s_pool[curIdx];
            if (cur.childCount == 0) break;
            int32_t bestChild = -1;
            double  bestScore = -1.0;
            double  logN = std::log((double)(cur.visits + 1));
            for (int i = 0; i < cur.childCount; ++i) {
                Node& ch = s_pool[cur.firstChild + i];
                double score;
                if (ch.visits == 0) {
                    score = 1e6 + (double)(s_rng.next() & 0xFFFF);
                } else {
                    double exploit = ch.wins / (double)ch.visits;
                    double explore = UCT_C * std::sqrt(logN / (double)ch.visits);
                    score = exploit + explore;
                }
                if (score > bestScore) { bestScore = score; bestChild = cur.firstChild + i; }
            }
            if (bestChild < 0) break;
            terminal = board.playMove(s_pool[bestChild].move);
            curIdx = bestChild;
            path[pathLen++] = curIdx;
            if (terminal != 0) break;
        }

        // EXPANSION
        if (terminal == 0) {
            Node& cur = s_pool[curIdx];
            if (cur.visits > 0 && cur.childCount == 0) {
                uint8_t moves[MAX_MOVES];
                int nMoves = board.generateMoves(moves);
                if (nMoves > 0) {
                    expandNode(curIdx, board, moves, nMoves);
                    Node& cur2 = s_pool[curIdx];
                    if (cur2.childCount > 0) {
                        int pick = (int)s_rng.nextBounded((uint32_t)cur2.childCount);
                        int32_t childIdx = cur2.firstChild + pick;
                        terminal = board.playMove(s_pool[childIdx].move);
                        curIdx = childIdx;
                        path[pathLen++] = curIdx;
                    }
                }
            }
        }

        // SIMULATION
        if (terminal == 0) terminal = playout(board);

        // BACKPROPAGATION
        double rewardX, rewardO;
        if      (terminal == 1) { rewardX = 1.0; rewardO = 0.0; }
        else if (terminal == 2) { rewardX = 0.0; rewardO = 1.0; }
        else                    { rewardX = 0.5; rewardO = 0.5; }

        for (int i = 0; i < pathLen; ++i) {
            Node& n = s_pool[path[i]];
            n.visits += 1;
            uint8_t parentToMove = (i == 0)
                ? n.toMoveAtNode
                : s_pool[path[i - 1]].toMoveAtNode;
            n.wins += (parentToMove == 0) ? rewardX : rewardO;
        }
    }

    static int playout(CompactBoard board) {
        uint8_t moves[MAX_MOVES];
        for (int step = 0; step < 200; ++step) {
            int nMoves = board.generateMoves(moves);
            if (nMoves == 0) {
                int cntX = popcount9(board.bigX);
                int cntO = popcount9(board.bigO);
                if (cntX > cntO) return 1;
                if (cntO > cntX) return 2;
                return 3;
            }

            int chosen = -1;
            // Heuristique de capture sur peu de coups
            if (nMoves <= 10) {
                for (int i = 0; i < nMoves; ++i) {
                    int b = moves[i] >> 4;
                    int cell = moves[i] & 0x0F;
                    uint16_t bit = (uint16_t)(1u << cell);
                    uint16_t newSmall = ((board.toMove == 0) ? board.smallX[b] : board.smallO[b]) | bit;
                    if (isWonMask(newSmall)) {
                        uint16_t newBig = ((board.toMove == 0) ? board.bigX : board.bigO) | (1u << b);
                        if (isWonMask(newBig)) { chosen = i; break; }
                        if (chosen < 0) chosen = i;
                    }
                }
            }
            if (chosen < 0) chosen = (int)s_rng.nextBounded((uint32_t)nMoves);

            int res = board.playMove(moves[chosen]);
            if (res != 0) return res;
        }
        return 3;
    }
};

// Definitions statiques (inline -> header-only)
inline Node       MyIA::s_pool[NODE_POOL_SIZE];
inline int        MyIA::s_poolUsed = 0;
inline XorShift32 MyIA::s_rng(0xDEADBEEF);

#endif // MYIA_H
