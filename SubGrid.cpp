#include "SubGrid.h"

SubGrid::SubGrid() : winner(Symbol::NONE), moveCount(0) {
    for(int i=0; i<3; ++i)
        for(int j=0; j<3; ++j)
            board[i][j] = Symbol::NONE;
}

bool SubGrid::play(int r, int c, Symbol s) {
    if (board[r][c] == Symbol::NONE && winner == Symbol::NONE) {
        board[r][c] = s;
        moveCount++;
        checkWinner();
        return true;
    }
    return false;
}

bool SubGrid::isFull() const {
    return moveCount >= 9;
}

void SubGrid::checkWinner() {
    // Vérification des Lignes et Colonnes
    for (int i = 0; i < 3; i++) {
        if (board[i][0] != Symbol::NONE && board[i][0] == board[i][1] && board[i][1] == board[i][2])
            winner = board[i][0];
        if (board[0][i] != Symbol::NONE && board[0][i] == board[1][i] && board[1][i] == board[2][i])
            winner = board[0][i];
    }
    // Vérification des Diagonales
    if (board[0][0] != Symbol::NONE && board[0][0] == board[1][1] && board[1][1] == board[2][2])
        winner = board[0][0];
    if (board[0][2] != Symbol::NONE && board[0][2] == board[1][1] && board[1][1] == board[2][0])
        winner = board[0][2];

    // Egalité si pleine sans vainqueur
    if (winner == Symbol::NONE && isFull()) {
        winner = Symbol::DRAW;
    }
}
