#ifndef MYIA_H
#define MYIA_H
#include "GameBoard.h"
#include "GameRules.h"

// Standard C++ libraries we need: math, time measurement, fixed-size integers, and memory functions
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>

//Putting all constants inside a "namespace" so they don't conflict with the other parts of code
namespace UTTTConst {
    // The 8 possible winning lines on a 3x3 board, stored as 9-bit numbers (one bit per cell)
    static constexpr uint16_t WIN_LINES[8] = {
        0b000000111, 0b000111000, 0b111000000,  // The 3 horizontal lines
        0b001001001, 0b010010010, 0b100100100,  // The 3 vertical lines
        0b100010001, 0b001010100                // The 2 diagonals
    };

    // For every possible board state (512 = 2^9 combinations),
    // we pre-calculate if it contains a winning line. This makes win-detection faster
    struct WinTable {
        bool data[512];
        constexpr WinTable() : data{} {
            // Every possible 9 bit pattern.
            for (int m = 0; m < 512; ++m) {
                bool w = false;
                // Checking if any of the 8 winning lines is fully present in this pattern.
                for (int k = 0; k < 8; ++k)
                    if ((m & WIN_LINES[k]) == (int)WIN_LINES[k]) { w = true; break; }
                data[m] = w;
            }
        }
    };
    // Creating one instance of this table that will be used by the whole program :
    static constexpr WinTable WIN_TABLE{};
    // A helper function: given a board (9-bit mask), tells us instantly if it has a winning line
    inline bool hasLine(uint16_t mask) { return WIN_TABLE.data[mask & 0x1FF]; }
    // FULL_MASK = 0b111111111 = all 9 cells filled.
    static constexpr uint16_t FULL_MASK = 0x1FF;

    // How much each cell on a 3x3 board is "worth" strategically
    // The center (4) is the best, the corners are good (3), the edges are weaker (2).
    static constexpr int CELL_VALUE[9] = {
        3, 2, 3,
        2, 4, 2,
        3, 2, 3
    };
}

// BB for "BitBoard", to store the whole game state using bits
struct BB {
    uint16_t small[9][2];   // For each of the 9 small boards, the cells taken by player 0 and player 1
    uint16_t bigOcc[2];     // Which of the 9 big-board squares have been won by each player
    uint16_t drawnBoards;   // Which small boards ended in a draw
    int8_t   forcedBoard;   // The board where the next player MUST play (-1 means free choice)
    uint8_t  sideToMove;    // Whose turn is it: 0 or 1

    // Returns all boards that can no longer be played in (won or drawn)
    inline uint16_t closedBoards() const { return bigOcc[0] | bigOcc[1] | drawnBoards; }
    // Tells if a given small board (index b) is still playable
    inline bool isBoardOpen(int b) const { return ((closedBoards() >> b) & 1) == 0; }
    // Returns the empty cells of a given small board
    inline uint16_t freeCells(int b) const {
        return UTTTConst::FULL_MASK & ~(small[b][0] | small[b][1]);
    }

    // A move is packed into 1 byte, the 4 high bits are the board index, the 4 low bits are the cell.
    static inline uint8_t pack(int bIdx, int cIdx) { return (uint8_t)((bIdx << 4) | cIdx); }
    // Extracts the board index from a packed move
    static inline int bigOf(uint8_t mv)  { return (mv >> 4) & 0xF; }
    // Extracts the cell index from a packed move
    static inline int cellOf(uint8_t mv) { return mv & 0xF; }
};

// Converting the "normal" game state into our fast BitBoard representation.
inline void buildBB(const GameState& s, BB& bb) {
    // Setting everything to 0
    std::memset(&bb, 0, sizeof(bb));
    // Going through the 3x3 grid of small boards
    for (int br = 0; br < 3; ++br) {
        for (int bc = 0; bc < 3; ++bc) {
            int b = br * 3 + bc;
            const SmallBoard& sb = s.boards[br][bc];
            // then through each cell inside the small board
            for (int sr = 0; sr < 3; ++sr) {
                for (int sc = 0; sc < 3; ++sc) {
                    int cell = sr * 3 + sc;
                    Player p = sb.cells[sr][sc];
                    // We set the right bit in the bitmask depending on who owns the cell
                    if      (p == Player::X) bb.small[b][0] |= (uint16_t)(1u << cell);
                    else if (p == Player::O) bb.small[b][1] |= (uint16_t)(1u << cell);
                }
            }
            // After filling the cells, we check if a small board is won, lost, or drawn
            if      (UTTTConst::hasLine(bb.small[b][0])) bb.bigOcc[0] |= (uint16_t)(1u << b);
            else if (UTTTConst::hasLine(bb.small[b][1])) bb.bigOcc[1] |= (uint16_t)(1u << b);
            else if ((bb.small[b][0] | bb.small[b][1]) == UTTTConst::FULL_MASK)
                bb.drawnBoards |= (uint16_t)(1u << b);
        }
    }
    // We store whose turn it is (0 for X, 1 for O).
    bb.sideToMove = (s.currentPlayer == Player::X) ? 0 : 1;
    // If the previous move forces us into a specific board
    if (s.isBoardForced) {
        int fb = s.forcedRow * 3 + s.forcedCol;
        // But if that board is already finished, then the player can play anywhere :
        bb.forcedBoard = bb.isBoardOpen(fb) ? (int8_t)fb : (int8_t)-1;
    } else {
        bb.forcedBoard = -1;  // -1 for free choice
    }
}

// Converts a (boardIndex, cellIndex) pair back into the official Move format used by the game.
inline Move toOfficialMove(int bIdx, int cIdx) {
    return Move(bIdx / 3, bIdx % 3, cIdx / 3, cIdx % 3);
}

// Generates the list of every legal move from the current position
// The result is written into the "out" array, and the function returns how many moves were found
inline int genMoves(const BB& bb, uint8_t* out) {
    int n = 0;
    // Case 1: we are forced into a specific board
    if (bb.forcedBoard >= 0) {
        int b = bb.forcedBoard;
        uint16_t free = bb.freeCells(b);
        // We pick out each free cell one by one.
        while (free) {
            int c = __builtin_ctz(free);  // ctz = "count trailing zeros" it finds the lowest set bit
            out[n++] = BB::pack(b, c);
            free &= free - 1;  // to clear the lowest set bit.
        }
    } else {
        // Case 2: free choice which means that we list every cell of every open board
        uint16_t closed = bb.closedBoards();
        for (int b = 0; b < 9; ++b) {
            if ((closed >> b) & 1) continue;  // Skip boards that are already finished
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

// Plays a move on a BitBoard and returns 1 if this move wins the whole game, 0 otherwise
inline int playBB(BB& bb, uint8_t mv) {
    int b = BB::bigOf(mv);    // Which small board
    int c = BB::cellOf(mv);   // Which cell inside it
    int s = bb.sideToMove;    // Who is playing
    uint16_t cellBit = (uint16_t)(1u << c);
    bb.small[b][s] |= cellBit; // We mark the cell as taken

    int gameWon = 0;
    // If the player just completed a line in this small board :
    if (UTTTConst::hasLine(bb.small[b][s])) {
        bb.bigOcc[s] |= (uint16_t)(1u << b);  // then they win that small board
        // And if they also have a line of small-board victories, they win the whole game
        if (UTTTConst::hasLine(bb.bigOcc[s])) gameWon = 1;
    } else if ((bb.small[b][0] | bb.small[b][1]) == UTTTConst::FULL_MASK) {
        // Otherwise, if the small board is  completely full with no winner, it's a draw
        bb.drawnBoards |= (uint16_t)(1u << b);
    }

    // The next player will be forced to play in the small board matching the cell we just chose
    // Unless that board is already finished, in which case they can play anywhere
    bb.forcedBoard = bb.isBoardOpen(c) ? (int8_t)c : (int8_t)-1;
    // Switching to the other player (XOR with 1 flips 0->1 and 1->0).
    bb.sideToMove ^= 1;
    return gameWon;
}

// Tests "what if I played this move?" does it win the small board?
inline bool wouldWinSmall(const BB& bb, uint8_t mv, int side) {
    int b = BB::bigOf(mv);
    int c = BB::cellOf(mv);
    uint16_t after = bb.small[b][side] | (uint16_t)(1u << c);
    return UTTTConst::hasLine(after);
}

// Tests "what if I played this move?": does it win the WHOLE game?
inline bool wouldWinGame(const BB& bb, uint8_t mv, int side) {
    int b = BB::bigOf(mv);
    int c = BB::cellOf(mv);
    uint16_t afterSmall = bb.small[b][side] | (uint16_t)(1u << c);
    // Must first win the small board, then check if it completes a big-board line
    if (!UTTTConst::hasLine(afterSmall)) return false;
    uint16_t afterBig = bb.bigOcc[side] | (uint16_t)(1u << b);
    return UTTTConst::hasLine(afterBig);
}

// A very fast random number generator (XorShift). We need millions of random numbers per move
struct XorShift32 {
    uint32_t state;
    // Returns the next random number using a few bit operations
    inline uint32_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }
    // Returns a random number between 0 and n-1
    inline uint32_t bounded(uint32_t n) { return next() % n; }
};

// Gives a "quality score" to a move, used to bias the tree search toward promising moves
inline int heuristicScore(const BB& bb, uint8_t mv, int side) {
    // Winning the whole game is by far the best move so the biggest score
    if (wouldWinGame(bb, mv, side)) return 10000;

    int bIdx = BB::bigOf(mv);
    int cIdx = BB::cellOf(mv);
    int score = 0;

    // Winning a small board is good (gives us a square on the big board)
    if (wouldWinSmall(bb, mv, side)) score += 500;
    // Better cells inside a small board score higher
    score += UTTTConst::CELL_VALUE[cIdx] * 3;
    // Playing in important small boards (corners/center) is also valued
    score += UTTTConst::CELL_VALUE[bIdx] * 2;

    // Penalty when sending the opponent to an open board where they can play anything
    if (!bb.isBoardOpen(cIdx)) score -= 100;

    return score;
}

// A "playout" simulates a random game until the end, to estimate who's winning
// Returns 0 if player 0 wins, 1 if player 1 wins, 2 if it's a draw

inline int playout(BB bb, XorShift32& rng) {
    uint8_t moves[81];
    // We allow up to 100 moves (a UTTT game can't have more legal moves than this).
    for (int step = 0; step < 100; ++step) {
        int n = genMoves(bb, moves);
        if (n == 0) break;  // No legal moves left => game over

        int side = bb.sideToMove;
        int chosen = -1;

        // Priority 1: if a move wins the game right now
        for (int i = 0; i < n; ++i) {
            if (wouldWinGame(bb, moves[i], side)) { chosen = i; break; }
        }

        // Otherwise, pick a completely random move
        if (chosen < 0) {
            chosen = (int)rng.bounded((uint32_t)n);
        }

        // Plays the chosen move, if it wins the game returns who won
        if (playBB(bb, moves[chosen])) return side;
    }

    // If the loop ends without a winner, check the final state:
    if (UTTTConst::hasLine(bb.bigOcc[0])) return 0;
    if (UTTTConst::hasLine(bb.bigOcc[1])) return 1;

    // No big-board line for anyone => count how many small boards each side won
    int cX = __builtin_popcount(bb.bigOcc[0]);
    int cO = __builtin_popcount(bb.bigOcc[1]);
    if (cX > cO) return 0;
    if (cO > cX) return 1;
    return 2;  // Equal count means draw
}

// A "Node" represents one position in our search tree
// We keep it as small as possible (only 24 bytes) for speed.
struct Node {
    int32_t  parent;                  // Index of the parent node in the pool (-1 = root)
    int32_t  firstChild;              // Index of the first child (start of the children list)
    int32_t  nextSibling;             // Index of the next sibling (links children together)
    uint32_t visits;                  // How many times this node was visited
    float    winsForSideJustMoved;    // Total score won for the player who just moved here
    float    prior;                   // A starting estimate of this move's quality (from heuristic)

    uint8_t  moveFromParent;          // The move that led from parent to this node
    uint8_t  sideJustMoved;           // Which player made that move
    uint8_t  numChildren;             // How many children this node currently has
    uint8_t  maxChildren;             // Maximum number of children (total legal moves available)
};

// The main AI class. It uses Monte Carlo Tree Search (MCTS)
class MyIA {
public:
    static constexpr int TIME_BUDGET_MS = 250;       // Maximum thinking time in milliseconds
    static constexpr int POOL_SIZE = 50000000;       // Pre-allocated pool of nodes (50 million)
    // Augment? car les nodes sont plus petits

    // The main function: given a game state, return the move the AI wants to play
    static Move getMove(const GameState& rootState) {
        // Remember the start time so we can stop when our time budget runs out
        auto t0 = std::chrono::steady_clock::now();

        // Convert the game state into our fast BitBoard representation
        BB rootBB;
        buildBB(rootState, rootBB);

        // Generate all legal moves; if there's only one, no need to think
        uint8_t failsafeBuf[81];
        int failsafeN = genMoves(rootBB, failsafeBuf);
        if (failsafeN == 0) return Move();  // Should never happen, a precaution
        // We keep a "failsafe" move ready, in case anything goes wrong
        Move failsafe = toOfficialMove(BB::bigOf(failsafeBuf[0]), BB::cellOf(failsafeBuf[0]));
        if (failsafeN == 1) return failsafe;

        // Quick check if any available move wins the game right away
        int mySide = rootBB.sideToMove;
        for (int i = 0; i < failsafeN; ++i) {
            if (wouldWinGame(rootBB, failsafeBuf[i], mySide)) {
                return toOfficialMove(BB::bigOf(failsafeBuf[i]), BB::cellOf(failsafeBuf[i]));
            }
        }

        // We allocate one big block of memory for all our tree nodes (done only once)
        static Node* pool = nullptr;
        if (!pool) pool = new Node[POOL_SIZE];
        int32_t poolUsed = 0;  // How many nodes we've used so far

        // Initialize the random number generator using the current time as a seed
        XorShift32 rng;
        rng.state = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        if (rng.state == 0) rng.state = 0xDEADBEEFu;  // XorShift can't have a seed of 0

        // Create the root of the search tree
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
        root.sideJustMoved = (uint8_t)(rootBB.sideToMove ^ 1);  // The "previous" player

        // The point in time after which we must stop searching
        auto deadline = t0 + std::chrono::milliseconds(TIME_BUDGET_MS);
        uint64_t iter = 0;

        // MCTS tuning constants.
        const float C_UCT = 0.9f;   // Controls exploration vs exploitation in the UCT formula
        const float BIAS_W = 2.0f;  // How much the heuristic prior influences early choice.


        // Main MCTS loop: keep doing iterations until time runs out.

        while (true) {
            // To save time we only check the clock every 2048 iterations
            if ((iter & 2047) == 0) { // Check de temps moins fr?quent (plus rapide)
                if (std::chrono::steady_clock::now() >= deadline) break;
            }
            // Stop if our memory pool is almost full
            if (poolUsed >= POOL_SIZE - 2) break;
            ++iter;

            // Walk down the tree, always picking the most promising child
            BB bb = rootBB;
            int32_t nodeIdx = rootIdx;

            while (true) {
                Node& nd = pool[nodeIdx];
                if (nd.numChildren < nd.maxChildren) break; // This node still has unexplored moves => stop here
                if (nd.numChildren == 0) break; // Terminal node, no moves possible.

                // UCT formula: balances "exploit known good moves" and "explore new ones".
                float logN = std::log((float)nd.visits + 1.0f);
                int32_t bestChild = -1;
                float bestScore = -1e30f;

                // Loop through all children to find the one with the best UCT score
                for (int32_t cIdx = nd.firstChild; cIdx >= 0; cIdx = pool[cIdx].nextSibling) {
                    const Node& ch = pool[cIdx];
                    float score;
                    if (ch.visits == 0) {
                        // Unvisited children get a very high score so we visit them at least once
                        score = 10.0f + ch.prior;
                    } else {
                        float q = ch.winsForSideJustMoved / (float)ch.visits;       // Win rate (exploitation)
                        float u = C_UCT * std::sqrt(logN / (float)ch.visits);       // Exploration bonus
                        float bias = BIAS_W * ch.prior / (1.0f + (float)ch.visits); // Heuristic bonus
                        score = q + u + bias;
                    }
                    if (score > bestScore) {
                        bestScore = score;
                        bestChild = cIdx;
                    }
                }
                if (bestChild < 0) break;
                // Move down into the best child and play its move on our temporary board
                nodeIdx = bestChild;
                playBB(bb, pool[nodeIdx].moveFromParent);

                // If the game ended during selection we stop walking down
                if (UTTTConst::hasLine(bb.bigOcc[0]) || UTTTConst::hasLine(bb.bigOcc[1])) break;
            }

            Node& sel = pool[nodeIdx];

            // ============== EXPANSION ==============
            // Add a new child node for an unexplored move
            int32_t leafIdx = nodeIdx;
            bool terminal = UTTTConst::hasLine(bb.bigOcc[0]) || UTTTConst::hasLine(bb.bigOcc[1]);

            if (!terminal && sel.numChildren < sel.maxChildren && poolUsed < POOL_SIZE - 1) {
                // Generate all legal moves at this position
                uint8_t moves[81];
                int n = genMoves(bb, moves);

                // Remove from "moves" the ones that are already children (already explored)

                for (int32_t cIdx = sel.firstChild; cIdx >= 0; cIdx = pool[cIdx].nextSibling) {
                    uint8_t cmv = pool[cIdx].moveFromParent;
                    for (int i = 0; i < n; ++i) {
                        if (moves[i] == cmv) {
                            moves[i] = moves[--n];  // Fast removal that swaps with last and shrink
                            break;
                        }
                    }
                }

                // Picking a random unexplored move
                uint8_t mv = moves[rng.bounded((uint32_t)n)];

                // Computing its prior (initial quality estimate) using the heuristic
                int sideWhoPlays = bb.sideToMove;
                int hScore = heuristicScore(bb, mv, sideWhoPlays);
                float prior = 0.5f + (float)hScore / 10000.0f;
                // Clamp the prior between 0 and 1
                if (prior < 0.0f) prior = 0.0f;
                if (prior > 1.0f) prior = 1.0f;

                // Allocate a new node for this child
                int32_t childIdx = poolUsed++;
                Node& ch = pool[childIdx];
                ch.parent = nodeIdx;
                ch.firstChild = -1;
                // Insert the new child at the front of the parent's children list
                ch.nextSibling = sel.firstChild;
                sel.firstChild = childIdx;
                ch.numChildren = 0;
                ch.visits = 0;
                ch.winsForSideJustMoved = 0.0f;
                ch.prior = prior;
                ch.moveFromParent = mv;
                ch.sideJustMoved = (uint8_t)sideWhoPlays;

                // Actually play the move on the temporary board
                playBB(bb, mv);

                // Count how many legal moves are available from this new position
                uint8_t tmpMoves[81];
                ch.maxChildren = (uint8_t)genMoves(bb, tmpMoves);

                sel.numChildren++;
                leafIdx = childIdx;  // This is our leaf where the simulation will start from
            }

            // ============== SIMULATION ==============
            // From the leaf, simulate a random game to the end to see who likely wins
            int simResult;
            if      (UTTTConst::hasLine(bb.bigOcc[0])) simResult = 0;  // Player 0 already won.
            else if (UTTTConst::hasLine(bb.bigOcc[1])) simResult = 1;  // Player 1 already won.
            else {
                uint8_t tmp[81];
                int nn = genMoves(bb, tmp);
                if (nn == 0) {
                    // No moves left: decide by counting won small boards :
                    int cX = __builtin_popcount(bb.bigOcc[0]);
                    int cO = __builtin_popcount(bb.bigOcc[1]);
                    simResult = (cX > cO) ? 0 : (cO > cX) ? 1 : 2;
                } else {
                    // Run the random playout until the game ends
                    simResult = playout(bb, rng);
                }
            }

            // ============== BACKPROPAGATION ==============
            // Walk back up from the leaf to the root, updating stats along the way
            int32_t cur = leafIdx;
            while (cur >= 0) {
                Node& nd = pool[cur];
                nd.visits++;
                int side = nd.sideJustMoved;
                // Add a win, a draw (half a point), or nothing to this node's score
                if      (simResult == side) nd.winsForSideJustMoved += 1.0f;
                else if (simResult == 2)    nd.winsForSideJustMoved += 0.5f;
                cur = nd.parent;
            }
        }

        // ====================================================================
        // Sélection du meilleur coup
        // ====================================================================

        // After all iterations, picking the child of the root with the most visits
        // Most visits = the move MCTS spent the most time confirming is good

        Node& finalRoot = pool[rootIdx];
        if (finalRoot.numChildren == 0 || finalRoot.firstChild < 0) {
            return failsafe;
        }

        int32_t bestChild = -1;
        uint32_t bestVisits = 0;
        float bestQTieBreak = -1.0f;
        // Iterates through the root's children
        for (int32_t cIdx = finalRoot.firstChild; cIdx >= 0; cIdx = pool[cIdx].nextSibling) {
            const Node& ch = pool[cIdx];
            float q = ch.visits > 0 ? (ch.winsForSideJustMoved / ch.visits) : -1.0f;
            // Picking the most visited child; if two are tied, prefer the one with a better win rate
            if (ch.visits > bestVisits || (ch.visits == bestVisits && q > bestQTieBreak)) {
                bestVisits = ch.visits;
                bestQTieBreak = q;
                bestChild = cIdx;
            }
        }
        if (bestChild < 0) return failsafe;

        // Convert the chosen move back to the official Move format and return it.
        uint8_t mv = pool[bestChild].moveFromParent;
        return toOfficialMove(BB::bigOf(mv), BB::cellOf(mv));
    }
};

#endif // MYIA_H
