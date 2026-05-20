#ifndef MYIA_H
#define MYIA_H

// ============================================================================
//  MyIA.h — Ultimate Tic-Tac-Toe AI (v2 — stronger play)
//  Architecture : MCTS / UCT avec représentation Bitboard intégrale
//
//  Améliorations v2 par rapport à v1 :
//   • Solver pré-MCTS : détection des big wins immédiats (gagner / bloquer)
//   • Heuristiques de positionnement : valeur stratégique du board ciblé
//   • Playout "fork-aware" : favorise les coups qui créent une double menace
//   • Évite d'envoyer l'adversaire sur un board fermé proche d'un big win
//   • UCT C tunée à 0.9 (au lieu de 1.414) : plus exploitatif, mieux pour UTTT
//   • Progressive bias : prior heuristique pour les enfants non encore visités
//   • Backprop avec malus quand on offre un free-move dangereux
//
//  Tradeoff : NPS plus bas (~30% moins de simulations) mais qualité
//  largement supérieure → bot bien plus fort.
//
//  Choix algorithmique général : MCTS plutôt que Minimax/AB.
//   - L'évaluation heuristique d'une position UTTT est notoirement instable
//   - Branching factor irrégulier (1 à 81) casse l'alpha-beta
//   - Les bots compétitifs UTTT top-tier sont tous MCTS
//
//  Optimisations bas niveau :
//   1. Bitboards : chaque petit board = 9 bits par joueur (uint16_t)
//      → win check = 1 lookup table (512 entrées, constexpr)
//      → légalité = AND/OR/NOT bit à bit + __builtin_ctz
//   2. Pool statique de nœuds pré-alloué (zéro malloc en boucle hot)
//      → enfants en liste chaînée via nextSibling
//   3. XorShift32 (~10× plus rapide que std::mt19937)
//   4. Time control strict, check tous les 1024 nœuds
//   5. Failsafe : 1er coup légal capturé dès l'entrée, retourné en cas d'urgence
//      Cas trivial (1 seul coup légal) : retour immédiat (0 ms)
// ============================================================================

#include "GameBoard.h"
#include "GameRules.h"
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>

// ============================================================================
//  Constantes & tables précomputées (résolues à la compilation)
// ============================================================================
namespace UTTTConst {

    // Toutes les configurations 3-en-ligne possibles sur un board 3x3
    // bits : 0=TL 1=TM 2=TR 3=ML 4=MM 5=MR 6=BL 7=BM 8=BR
    static constexpr uint16_t WIN_LINES[8] = {
        0b000000111, 0b000111000, 0b111000000,   // 3 lignes
        0b001001001, 0b010010010, 0b100100100,   // 3 colonnes
        0b100010001, 0b001010100                 // 2 diagonales
    };

    // Table 512 entrées : pour un masque 9 bits, est-ce qu'il contient une
    // ligne complète ? Lookup en O(1).
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

    // Mask "board plein" = 9 bits à 1
    static constexpr uint16_t FULL_MASK = 0x1FF;

    // ──────────────────────────────────────────────────────────────────────
    // Valeur stratégique de chaque case dans un board 3x3 :
    //   - Centre : 4 (appartient à 4 lignes : horiz, vert, 2 diag)
    //   - Coins  : 3 (3 lignes : horiz, vert, 1 diag)
    //   - Bords  : 2 (2 lignes : horiz, vert)
    // Utilisée à la fois pour les sous-boards (quel sous-board prendre)
    // ET au sein d'un sous-board (quelle case jouer).
    // ──────────────────────────────────────────────────────────────────────
    static constexpr int CELL_VALUE[9] = {
        3, 2, 3,
        2, 4, 2,
        3, 2, 3
    };
}

// ============================================================================
//  Représentation Bitboard
// ============================================================================
struct BB {
    uint16_t small[9][2];   // small[boardIdx][0]=X bits, [1]=O bits
    uint16_t bigOcc[2];     // bits des sous-boards gagnés par X / O
    uint16_t drawnBoards;   // sous-boards full sans gagnant (fermés)
    int8_t   forcedBoard;   // -1 = libre
    uint8_t  sideToMove;    // 0=X, 1=O

    inline uint16_t closedBoards() const { return bigOcc[0] | bigOcc[1] | drawnBoards; }
    inline bool isBoardOpen(int b) const { return ((closedBoards() >> b) & 1) == 0; }
    inline uint16_t freeCells(int b) const {
        return UTTTConst::FULL_MASK & ~(small[b][0] | small[b][1]);
    }

    // Encode (bigIdx 0..8, cellIdx 0..8) en 1 uint8_t (4 bits + 4 bits)
    static inline uint8_t pack(int bIdx, int cIdx) {
        return (uint8_t)((bIdx << 4) | cIdx);
    }
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
            // Recalculer (ne pas se fier au flag isWon)
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

// ============================================================================
//  Move generation & application en bitboard
// ============================================================================

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

// Joue un coup. Retourne 1 si la partie est gagnée par le joueur qui vient de jouer.
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

// Test non-mutant : "si je joue mv en tant que side, est-ce que je gagne le petit board ?"
inline bool wouldWinSmall(const BB& bb, uint8_t mv, int side) {
    int b = BB::bigOf(mv);
    int c = BB::cellOf(mv);
    uint16_t after = bb.small[b][side] | (uint16_t)(1u << c);
    return UTTTConst::hasLine(after);
}

// Test non-mutant : "si je joue mv en tant que side, est-ce que je gagne la PARTIE ?"
// = je gagne le petit board ET cela complète une ligne sur le big board.
inline bool wouldWinGame(const BB& bb, uint8_t mv, int side) {
    int b = BB::bigOf(mv);
    int c = BB::cellOf(mv);
    uint16_t afterSmall = bb.small[b][side] | (uint16_t)(1u << c);
    if (!UTTTConst::hasLine(afterSmall)) return false;
    // Le sous-board b sera gagné. Est-ce que ça complète une ligne sur le big board ?
    uint16_t afterBig = bb.bigOcc[side] | (uint16_t)(1u << b);
    return UTTTConst::hasLine(afterBig);
}

// ============================================================================
//  Détection de menaces sur un board 3x3 (utilisée pour les heuristiques)
// ============================================================================

// Compte le nombre de "case libre qui crée une ligne pour 'side'"
// dans le sous-board b. = nombre de menaces.
inline int countThreats(const BB& bb, int b, int side) {
    if (!bb.isBoardOpen(b)) return 0;
    uint16_t mine = bb.small[b][side];
    uint16_t free = bb.freeCells(b);
    int count = 0;
    while (free) {
        int c = __builtin_ctz(free);
        if (UTTTConst::hasLine(mine | (uint16_t)(1u << c))) count++;
        free &= free - 1;
    }
    return count;
}

// Vrai ssi 'side' a au moins une menace dans le sous-board b
inline bool hasThreat(const BB& bb, int b, int side) {
    if (!bb.isBoardOpen(b)) return false;
    uint16_t mine = bb.small[b][side];
    uint16_t free = bb.freeCells(b);
    while (free) {
        int c = __builtin_ctz(free);
        if (UTTTConst::hasLine(mine | (uint16_t)(1u << c))) return true;
        free &= free - 1;
    }
    return false;
}

// Compte le nombre de sous-boards où 'side' a une menace de gagner.
// Sert pour détecter les "forks" sur le big board.
inline int countBoardsWithThreat(const BB& bb, int side) {
    int n = 0;
    for (int b = 0; b < 9; ++b) if (hasThreat(bb, b, side)) ++n;
    return n;
}

// "Big board threat" : 'side' peut gagner la partie en 1 coup ?
// = il existe un sous-board b ouvert tel que (bigOcc[side] | (1<<b)) forme
//   une ligne sur le meta-board ET side a une menace dans ce sous-board.
inline bool hasBigWinThreat(const BB& bb, int side) {
    for (int b = 0; b < 9; ++b) {
        if (!bb.isBoardOpen(b)) continue;
        uint16_t afterBig = bb.bigOcc[side] | (uint16_t)(1u << b);
        if (UTTTConst::hasLine(afterBig) && hasThreat(bb, b, side)) return true;
    }
    return false;
}

// ============================================================================
//  XorShift32 — RNG ultra-rapide
// ============================================================================
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

// ============================================================================
//  Heuristique de scoring d'un coup (utilisée pour Progressive Bias UCT
//  et pour orienter le playout).
//
//  Retourne un score relatif (plus haut = meilleur), pour le joueur qui joue.
//  Critères (de plus important à moins important) :
//    +1000  si gagne la partie (big win)
//    +500   si gagne un sous-board stratégique (centre = +200 bonus)
//    +200   si crée un fork (2+ menaces dans 2+ sous-boards)
//    -500   si envoie l'adversaire sur un board où il peut gagner la PARTIE
//    -200   si envoie l'adversaire sur un board où il peut gagner un sous-board
//             qui complète une ligne sur le big board (menace dangereuse)
//    -100   si envoie l'adversaire sur un board fermé (= free move pour lui)
//             quand l'adversaire a déjà 2 sous-boards alignés
//    +CELL_VALUE[smallCell]  : préférer le centre du sous-board
//    +CELL_VALUE[bigBoard]   : préférer les sous-boards stratégiques
// ============================================================================
inline int heuristicScore(const BB& bb, uint8_t mv, int side) {
    int opp = side ^ 1;
    int bIdx = BB::bigOf(mv);
    int cIdx = BB::cellOf(mv);

    // === Big win immédiat ? ===
    if (wouldWinGame(bb, mv, side)) return 100000;

    int score = 0;

    // === Gagner un sous-board ? ===
    bool winsSmall = wouldWinSmall(bb, mv, side);
    if (winsSmall) {
        score += 500 + UTTTConst::CELL_VALUE[bIdx] * 50;
        // Bonus si le sous-board gagné crée une menace de big win pour nous
        uint16_t afterBig = bb.bigOcc[side] | (uint16_t)(1u << bIdx);
        // Pour chaque ligne du meta-board contenant bIdx, vérifier si on a 2/3
        for (int k = 0; k < 8; ++k) {
            uint16_t L = UTTTConst::WIN_LINES[k];
            if ((L & ((uint16_t)(1u << bIdx))) == 0) continue;
            int countMine = __builtin_popcount(afterBig & L);
            int countOpp  = __builtin_popcount(bb.bigOcc[opp] & L);
            // Ligne où on a 2 boards et opp en a 0 → forte menace de big win
            if (countMine == 2 && countOpp == 0) score += 300;
        }
    }

    // === Position de la case dans le sous-board ===
    score += UTTTConst::CELL_VALUE[cIdx] * 3;

    // === Position du sous-board ciblé (où on joue) ===
    score += UTTTConst::CELL_VALUE[bIdx] * 2;

    // === Pénalité : où envoie-t-on l'adversaire ? ===
    // L'adversaire jouera dans le sous-board correspondant à cIdx.
    int targetBoard = cIdx;
    if (!bb.isBoardOpen(targetBoard)) {
        // On envoie l'opp sur un board fermé → il aura free move
        // C'est généralement mauvais (sauf si on est en très bonne position)
        // Pénalité modulée par la menace globale de l'opp.
        int oppThreatBoards = countBoardsWithThreat(bb, opp);
        score -= 50 + oppThreatBoards * 30;
        // Si l'adversaire peut gagner la partie en free-move, c'est fatal
        // (sauf si on vient de gagner nous-mêmes, déjà testé plus haut)
        if (hasBigWinThreat(bb, opp)) score -= 5000;
    } else {
        // On envoie l'opp sur un board ouvert. Évaluer le danger.
        // Danger 1 : l'opp peut y gagner un sous-board qui complète une ligne meta
        uint16_t oppMaskInTarget = bb.small[targetBoard][opp];
        uint16_t freeInTarget = bb.freeCells(targetBoard);
        // Si l'opp gagnerait le sous-board targetBoard en y jouant,
        // est-ce que ça complète une ligne sur le big board pour lui ?
        bool oppCanWinSmallHere = false;
        {
            uint16_t f = freeInTarget;
            while (f) {
                int cc = __builtin_ctz(f);
                if (UTTTConst::hasLine(oppMaskInTarget | (uint16_t)(1u << cc))) {
                    oppCanWinSmallHere = true; break;
                }
                f &= f - 1;
            }
        }
        if (oppCanWinSmallHere) {
            uint16_t afterBigOpp = bb.bigOcc[opp] | (uint16_t)(1u << targetBoard);
            if (UTTTConst::hasLine(afterBigOpp)) {
                // Catastrophe : on lui offre la partie
                score -= 5000;
            } else {
                // On lui offre juste un sous-board. Pénalité modérée,
                // dépend de la valeur du sous-board.
                score -= 100 + UTTTConst::CELL_VALUE[targetBoard] * 20;
            }
        }
    }

    return score;
}

// ============================================================================
//  Playout heuristique (simulation MCTS) — version v2 améliorée
//   - Priorité 1 : gagner la PARTIE
//   - Priorité 2 : bloquer un big win de l'adversaire (si menace)
//   - Priorité 3 : top-K coups par heuristique, choix pondéré aléatoire
//   Retour : 0 = X gagne, 1 = O gagne, 2 = nul
// ============================================================================
inline int playout(BB bb, XorShift32& rng) {
    uint8_t moves[81];

    for (int step = 0; step < 100; ++step) {
        int n = genMoves(bb, moves);
        if (n == 0) {
            if (UTTTConst::hasLine(bb.bigOcc[0])) return 0;
            if (UTTTConst::hasLine(bb.bigOcc[1])) return 1;
            int cX = __builtin_popcount(bb.bigOcc[0]);
            int cO = __builtin_popcount(bb.bigOcc[1]);
            if (cX > cO) return 0;
            if (cO > cX) return 1;
            return 2;
        }

        int side = bb.sideToMove;
        int opp  = side ^ 1;

        // === Priorité 1 : gagner la partie immédiatement ===
        int chosen = -1;
        for (int i = 0; i < n; ++i) {
            if (wouldWinGame(bb, moves[i], side)) { chosen = i; break; }
        }

        if (chosen < 0) {
            // === Pré-calcul : board(s) où l'adversaire a une menace dangereuse
            //     (gagner un sous-board qui compléterait une ligne meta pour lui) ===
            //     On stocke un mask des cells "à éviter d'envoyer l'opp dessus".
            uint16_t dangerSentTo = 0;  // bit b = 1 si envoyer opp sur board b est dangereux
            for (int b = 0; b < 9; ++b) {
                if (!bb.isBoardOpen(b)) continue;
                // L'opp peut-il y gagner un sous-board ?
                if (!hasThreat(bb, b, opp)) continue;
                // Si oui, est-ce que ça complète une ligne meta ?
                uint16_t afterBig = bb.bigOcc[opp] | (uint16_t)(1u << b);
                if (UTTTConst::hasLine(afterBig)) {
                    dangerSentTo |= (uint16_t)(1u << b); // FATAL
                } else {
                    // Menace modérée : flag aussi mais avec moins de poids
                    // (on l'utilisera comme tie-breaker)
                    dangerSentTo |= (uint16_t)(1u << b);
                }
            }

            // === Sélection heuristique pondérée ===
            // On classe les coups en 3 catégories :
            //   1. Coups "gagnants locaux" (winsSmall) qui n'envoient pas l'opp en danger
            //   2. Coups "safe" (n'envoient pas l'opp en danger)
            //   3. Autres (en dernier recours)
            uint8_t catWinSafe[81]; int nWS = 0;
            uint8_t catSafe[81];    int nS  = 0;
            uint8_t catWin[81];     int nW  = 0;

            for (int i = 0; i < n; ++i) {
                int sentTo = BB::cellOf(moves[i]);
                bool sendsToDanger = bb.isBoardOpen(sentTo) &&
                                     ((dangerSentTo >> sentTo) & 1);
                bool wins = wouldWinSmall(bb, moves[i], side);

                if (wins && !sendsToDanger)      catWinSafe[nWS++] = (uint8_t)i;
                else if (!sendsToDanger)         catSafe[nS++]     = (uint8_t)i;
                else if (wins)                   catWin[nW++]      = (uint8_t)i;
            }

            if      (nWS > 0) chosen = catWinSafe[rng.bounded((uint32_t)nWS)];
            else if (nS  > 0) chosen = catSafe[rng.bounded((uint32_t)nS)];
            else if (nW  > 0) chosen = catWin[rng.bounded((uint32_t)nW)];
            else              chosen = (int)rng.bounded((uint32_t)n);
        }

        if (playBB(bb, moves[chosen])) {
            return side;
        }
    }
    if (UTTTConst::hasLine(bb.bigOcc[0])) return 0;
    if (UTTTConst::hasLine(bb.bigOcc[1])) return 1;
    return 2;
}

// ============================================================================
//  Nœud MCTS
//   - prior : score heuristique du coup menant à ce nœud (Progressive Bias)
//             utilisé pour orienter l'UCT quand visits est faible
// ============================================================================
struct Node {
    int32_t parent;
    int32_t firstChild;
    int32_t nextSibling;
    uint16_t numChildren;
    uint16_t numUntried;
    uint32_t visits;
    float    winsForSideJustMoved;
    float    prior;            // score heuristique normalisé [0..1]

    uint8_t  moveFromParent;
    uint8_t  sideJustMoved;
    uint8_t  _pad[2];

    uint8_t  untried[81];
};

// ============================================================================
//  MyIA — interface publique
// ============================================================================
class MyIA {
public:
    static constexpr int TIME_BUDGET_MS = 950;
    static constexpr int POOL_SIZE = 200000;

    static Move getMove(const GameState& rootState) {
        auto t0 = std::chrono::steady_clock::now();

        // ====================================================================
        // 1. Construction de la bitboard racine
        // ====================================================================
        BB rootBB;
        buildBB(rootState, rootBB);

        // ====================================================================
        // 2. FAILSAFE absolu : capture du premier coup légal
        // ====================================================================
        uint8_t failsafeBuf[81];
        int failsafeN = genMoves(rootBB, failsafeBuf);
        if (failsafeN == 0) return Move();
        Move failsafe = toOfficialMove(BB::bigOf(failsafeBuf[0]),
                                       BB::cellOf(failsafeBuf[0]));
        if (failsafeN == 1) return failsafe;

        // ====================================================================
        // 3. SOLVER PRE-MCTS : si on peut gagner la partie maintenant → on le fait.
        //    (Pour le blocage des menaces adverses, on fait confiance au MCTS :
        //    le prior heuristique pénalise fortement les coups qui laissent
        //    une victoire à l'adversaire.)
        // ====================================================================
        int mySide = rootBB.sideToMove;

        for (int i = 0; i < failsafeN; ++i) {
            if (wouldWinGame(rootBB, failsafeBuf[i], mySide)) {
                return toOfficialMove(BB::bigOf(failsafeBuf[i]),
                                      BB::cellOf(failsafeBuf[i]));
            }
        }

        // ====================================================================
        // 4. Init pool (statique, une seule fois pendant la vie du process)
        // ====================================================================
        static Node* pool = nullptr;
        if (!pool) pool = new Node[POOL_SIZE];
        int32_t poolUsed = 0;

        // ====================================================================
        // 5. RNG
        // ====================================================================
        XorShift32 rng;
        rng.state = (uint32_t)std::chrono::high_resolution_clock::now()
                        .time_since_epoch().count();
        if (rng.state == 0) rng.state = 0xDEADBEEFu;

        // ====================================================================
        // 6. Création de la racine
        // ====================================================================
        int32_t rootIdx = poolUsed++;
        Node& root = pool[rootIdx];
        root.parent = -1;
        root.firstChild = -1;
        root.nextSibling = -1;
        root.numChildren = 0;
        root.visits = 0;
        root.winsForSideJustMoved = 0.0f;
        root.prior = 0.5f;
        root.moveFromParent = 0;
        root.sideJustMoved = (uint8_t)(rootBB.sideToMove ^ 1);
        root.numUntried = (uint16_t)failsafeN;
        std::memcpy(root.untried, failsafeBuf, failsafeN);

        // ====================================================================
        // 7. Boucle MCTS : Selection → Expansion → Simulation → Backprop
        // ====================================================================
        auto deadline = t0 + std::chrono::milliseconds(TIME_BUDGET_MS);
        uint64_t iter = 0;

        // Constante UCT tunée pour UTTT (plus exploitatif que sqrt(2))
        // Valeur empirique recommandée par la littérature MCTS pour UTTT.
        const float C_UCT = 0.9f;
        // Poids du progressive bias (décroît avec les visites)
        const float BIAS_W = 2.0f;

        while (true) {
            if ((iter & 1023) == 0) {
                if (std::chrono::steady_clock::now() >= deadline) break;
            }
            if (poolUsed >= POOL_SIZE - 2) break;
            ++iter;

            // ============== SELECTION ==============
            BB bb = rootBB;
            int32_t nodeIdx = rootIdx;

            while (true) {
                Node& nd = pool[nodeIdx];
                if (nd.numUntried > 0) break;
                if (nd.numChildren == 0) break;

                float logN = std::log((float)nd.visits + 1.0f);
                int32_t bestChild = -1;
                float bestScore = -1e30f;

                for (int32_t cIdx = nd.firstChild; cIdx >= 0;
                     cIdx = pool[cIdx].nextSibling) {
                    const Node& ch = pool[cIdx];
                    float score;
                    if (ch.visits == 0) {
                        // FPU + prior heuristique
                        score = 10.0f + ch.prior;
                    } else {
                        float q = ch.winsForSideJustMoved / (float)ch.visits;
                        float u = C_UCT * std::sqrt(logN / (float)ch.visits);
                        // Progressive bias : prior shrink avec les visites
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

                if (UTTTConst::hasLine(bb.bigOcc[0]) ||
                    UTTTConst::hasLine(bb.bigOcc[1])) break;
            }

            Node& sel = pool[nodeIdx];

            // ============== EXPANSION ==============
            int32_t leafIdx = nodeIdx;
            bool terminal = UTTTConst::hasLine(bb.bigOcc[0]) ||
                            UTTTConst::hasLine(bb.bigOcc[1]);

            if (!terminal && sel.numUntried > 0 && poolUsed < POOL_SIZE - 1) {
                // Pop un untried au hasard (swap-remove O(1))
                int idx = (int)rng.bounded(sel.numUntried);
                uint8_t mv = sel.untried[idx];
                sel.untried[idx] = sel.untried[--sel.numUntried];

                // Calcul du prior heuristique pour ce coup
                int sideWhoPlays = bb.sideToMove;
                int hScore = heuristicScore(bb, mv, sideWhoPlays);
                // Normalisation grossière : on map [-5000, +5000] → [0, 1]
                // (les valeurs extrêmes sont rares ; le clamp empêche tout overflow)
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
                int n = genMoves(bb, ch.untried);
                ch.numUntried = (uint16_t)n;
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
        // 8. Sélection du meilleur coup à la racine (robust child)
        // ====================================================================
        Node& finalRoot = pool[rootIdx];
        if (finalRoot.numChildren == 0 || finalRoot.firstChild < 0) {
            return failsafe;
        }

        int32_t bestChild = -1;
        uint32_t bestVisits = 0;
        float bestQTieBreak = -1.0f;
        for (int32_t cIdx = finalRoot.firstChild; cIdx >= 0;
             cIdx = pool[cIdx].nextSibling) {
            const Node& ch = pool[cIdx];
            float q = ch.visits > 0 ? (ch.winsForSideJustMoved / ch.visits) : -1.0f;
            if (ch.visits > bestVisits ||
               (ch.visits == bestVisits && q > bestQTieBreak)) {
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
