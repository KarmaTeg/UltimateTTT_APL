#ifndef MYIA_H
#define MYIA_H

#include "GameBoard.h"
#include "GameRules.h"
#include <cmath>
#include <chrono>
#include <cstdint>
#include <vector>

// 1. Structure de liste statique ultra-rapide
struct FastMoveList {
    Move moves[81];
    int count = 0;

    inline void add(int br, int bc, int sr, int sc) {
        moves[count].bigRow = br;
        moves[count].bigCol = bc;
        moves[count].smallRow = sr;
        moves[count].smallCol = sc;
        count++;
    }
};

// 2. Utilitaires globaux optimisés
inline void generateLegalMoves(const GameState& state, FastMoveList& list) {
    list.count = 0;
    if (state.isBoardForced) {
        const SmallBoard& board = state.boards[state.forcedRow][state.forcedCol];
        for (int sr = 0; sr < 3; sr++)
            for (int sc = 0; sc < 3; sc++)
                if (board.cells[sr][sc] == Player::NONE)
                    list.add(state.forcedRow, state.forcedCol, sr, sc);
    } else {
        for (int br = 0; br < 3; br++) {
            for (int bc = 0; bc < 3; bc++) {
                if (!state.boards[br][bc].isWon && GameRules::isSmallBoardPlayable(state.boards[br][bc])) {
                    for (int sr = 0; sr < 3; sr++) {
                        for (int sc = 0; sc < 3; sc++) {
                            if (state.boards[br][bc].cells[sr][sc] == Player::NONE) {
                                list.add(br, bc, sr, sc);
                            }
                        }
                    }
                }
            }
        }
    }
}

// Vérification de victoire sans copie (In-Place)
inline bool fastCheckSmallWin(const GameState& state, const Move& m, Player p) {
    const SmallBoard& sb = state.boards[m.bigRow][m.bigCol];
    int r = m.smallRow;
    int c = m.smallCol;

    // Ligne
    if ((c == 0 || sb.cells[r][0] == p) && (c == 1 || sb.cells[r][1] == p) && (c == 2 || sb.cells[r][2] == p)) return true;
    // Colonne
    if ((r == 0 || sb.cells[0][c] == p) && (r == 1 || sb.cells[1][c] == p) && (r == 2 || sb.cells[2][c] == p)) return true;
    // Diagonales
    if (r == c) {
        if ((r==0 && c==0 || sb.cells[0][0] == p) && (r==1 && c==1 || sb.cells[1][1] == p) && (r==2 && c==2 || sb.cells[2][2] == p)) return true;
    }
    if (r + c == 2) {
        if ((r==0 && c==2 || sb.cells[0][2] == p) && (r==1 && c==1 || sb.cells[1][1] == p) && (r==2 && c==0 || sb.cells[2][0] == p)) return true;
    }
    return false;
}

// Encodage/Décodage pour compacter la mémoire de l'arbre MCTS
inline uint8_t encodeMove(const Move& m) {
    return (m.bigRow * 3 + m.bigCol) * 9 + (m.smallRow * 3 + m.smallCol);
}
inline Move decodeMove(uint8_t code) {
    int big = code / 9;
    int small = code % 9;
    return Move(big / 3, big % 3, small / 3, small % 3);
}

// 3. Nœud MCTS allégé (Pas de GameState stocké, seulement l'essentiel)
struct MCTSNode {
    MCTSNode* parent;
    MCTSNode* children[81];

    int visits;
    double wins;

    Move move;
    Player playerJustMoved;

    uint8_t untriedMoves[81];
    uint8_t numUntried;
    uint8_t numChildren;

    void init(const GameState& state, Move m, MCTSNode* p, Player pMoved) {
        move = m;
        parent = p;
        playerJustMoved = pMoved;
        visits = 0;
        wins = 0.0;
        numChildren = 0;

        FastMoveList list;
        generateLegalMoves(state, list);
        numUntried = list.count;
        for (int i = 0; i < list.count; ++i) {
            untriedMoves[i] = encodeMove(list.moves[i]);
        }
    }

    inline bool isTerminal() const {
        return numUntried == 0 && numChildren == 0;
    }

    inline bool isFullyExpanded() const {
        return numUntried == 0;
    }

    MCTSNode* getBestUCTChild(double c = 1.41421356) const {
        MCTSNode* bestChild = nullptr;
        double bestValue = -1e9;
        double logVisits = std::log(this->visits);

        for (int i = 0; i < numChildren; ++i) {
            MCTSNode* child = children[i];
            if (child->visits == 0) return child;

            double uctValue = (child->wins / child->visits) +
                              c * std::sqrt(logVisits / child->visits);

            if (uctValue > bestValue) {
                bestValue = uctValue;
                bestChild = child;
            }
        }
        return bestChild;
    }

    MCTSNode* getMostVisitedChild() const {
        MCTSNode* bestChild = nullptr;
        int maxVisits = -1;

        for (int i = 0; i < numChildren; ++i) {
            if (children[i]->visits > maxVisits) {
                maxVisits = children[i]->visits;
                bestChild = children[i];
            }
        }
        return bestChild;
    }

    void backpropagate(Player winner) {
        MCTSNode* current = this;
        while (current != nullptr) {
            current->visits++;
            if (winner == current->playerJustMoved) {
                current->wins += 1.0;
            } else if (winner == Player::NONE) {
                current->wins += 0.5; // Nul
            }
            current = current->parent;
        }
    }
};

// 4. Classe Principale IA
class MyIA {
public:
    static inline uint32_t xorshift32(uint32_t& state) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // Phase de Simulation (Rollout) : Heavy Playout heuristique
    static Player simulate(GameState state, uint32_t& rng_state) {
        FastMoveList list;
        while (true) {
            generateLegalMoves(state, list);
            if (list.count == 0) {
                return GameRules::getWinner(state); // Retourne le gagnant final ou NONE pour Draw
            }

            Player me = state.currentPlayer;
            Player opp = (me == Player::X) ? Player::O : Player::X;
            int bestMoveIdx = -1;
            int fallbackMoveIdx = -1;

            int safeMoves[81];
            int numSafe = 0;

            for(int i = 0; i < list.count; ++i) {
                const Move& m = list.moves[i];

                // 1. Gagner la petite grille (Priorité 1)
                if (fastCheckSmallWin(state, m, me)) {
                    bestMoveIdx = i; break;
                }

                // 2. Bloquer l'adversaire (Priorité 2)
                if (fallbackMoveIdx == -1 && fastCheckSmallWin(state, m, opp)) {
                    fallbackMoveIdx = i;
                }

                // 3. Coups Sûrs : Ne pas envoyer l'adversaire sur une grille injouable !
                if (GameRules::isSmallBoardPlayable(state.boards[m.smallRow][m.smallCol])) {
                    safeMoves[numSafe++] = i;
                }
            }

            // Choix du coup selon la priorité stratégique
            if (bestMoveIdx == -1) {
                if (fallbackMoveIdx != -1) {
                    bestMoveIdx = fallbackMoveIdx;
                } else if (numSafe > 0) {
                    bestMoveIdx = safeMoves[xorshift32(rng_state) % numSafe]; // Aléatoire "sûr"
                } else {
                    bestMoveIdx = xorshift32(rng_state) % list.count; // Aléatoire pur par défaut
                }
            }

            GameRules::playMove(state, list.moves[bestMoveIdx]);

            // Si le mouvement que l'on vient de faire nous a fait gagner la partie
            if (GameRules::checkBigWin(state.bigBoard, me)) {
                return me;
            }
        }
    }

    static Move getMove(const GameState& rootState) {
        auto startTime = std::chrono::steady_clock::now();
        const int timeLimitMs = 950; // Laisser un petit buffer par rapport à la seconde max

        uint32_t rng_state = 123456789 ^ (uint32_t)std::chrono::system_clock::now().time_since_epoch().count();
        if (rng_state == 0) rng_state = 1; // Sécurité anti-lock du Xorshift

        // Object Pool statique avec limitation pour ne jamais péter la mémoire
        // Capacité de 300 000 noeuds (prend environ ~230 Mo de RAM)
        static std::vector<MCTSNode> nodePool;
        if (nodePool.empty()) {
            nodePool.resize(300000);
        }

        size_t nodeCount = 0;
        int iterations = 0;

        Player rootPlayerJustMoved = (rootState.currentPlayer == Player::X) ? Player::O : Player::X;

        MCTSNode* root = &nodePool[nodeCount++];
        root->init(rootState, Move(), nullptr, rootPlayerJustMoved);

        while (true) {
            // Vérifier le chronomètre tous les 128 tours d'arbre pour ne pas ralentir l'algo
            if ((++iterations & 127) == 0) {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
                if (elapsedTime >= timeLimitMs) break;
            }

            // Arrêt forcé si le Pool de RAM est presque plein
            if (nodeCount >= nodePool.size() - 2) break;

            MCTSNode* node = root;
            GameState state = rootState;

            // 1. Sélection (Descente de l'arbre)
            while (node->isFullyExpanded() && !node->isTerminal()) {
                node = node->getBestUCTChild();
                GameRules::playMove(state, node->move);
            }

            // 2. Expansion (Ajout d'un nouveau coup dans l'arbre)
            if (!node->isFullyExpanded()) {
                int index = xorshift32(rng_state) % node->numUntried;
                Move moveToTry = decodeMove(node->untriedMoves[index]);

                // Swap avec le dernier élément pour retirer en O(1)
                node->numUntried--;
                node->untriedMoves[index] = node->untriedMoves[node->numUntried];

                GameRules::playMove(state, moveToTry);

                MCTSNode* childNode = &nodePool[nodeCount++];
                Player nextPlayerJustMoved = (state.currentPlayer == Player::X) ? Player::O : Player::X;
                childNode->init(state, moveToTry, node, nextPlayerJustMoved);

                node->children[node->numChildren++] = childNode;
                node = childNode;
            }

            // 3. Simulation
            Player winner = simulate(state, rng_state);

            // 4. Rétropropagation
            node->backpropagate(winner);
        }

        if (root->numChildren == 0) {
            // Failsafe s'il y a eu un bug (très peu probable)
            FastMoveList failsafe;
            generateLegalMoves(rootState, failsafe);
            if (failsafe.count > 0) return failsafe.moves[0];
            return Move();
        }

        // Renvoie le coup le plus robuste (plus visité)
        return root->getMostVisitedChild()->move;
    }
};

#endif // MYIA_H
