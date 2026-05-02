#include "RandomPlayer.h"
#include <vector>
#include <cstdlib>
#include <ctime>

Move RandomPlayer::chooseMove(const UltimateBoard& board) {
    std::vector<Move> possibleMoves = board.getValidMoves();
    if (possibleMoves.empty()) return Move(); // Retourne un coup vide si aucun coup n'est possible

    int index = std::rand() % possibleMoves.size();
    return possibleMoves[index];
}
