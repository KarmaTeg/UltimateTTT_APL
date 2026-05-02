#ifndef ULTIMATEBOARD_H
#define ULTIMATEBOARD_H

#include "SubGrid.h"
#include "Move.h"
#include <vector>

class UltimateBoard {
private:
    SubGrid grids[3][3];
    int nextMacroR, nextMacroC; // -1 si le joueur peut jouer n'importe où
    Symbol globalWinner;

public:
    UltimateBoard();
    bool isValidMove(const Move& m) const;
    bool makeMove(const Move& m, Symbol s);
    std::vector<Move> getValidMoves() const;

    Symbol checkGlobalWinner();
    int getNextMacroR() const { return nextMacroR; }
    int getNextMacroC() const { return nextMacroC; }
    Symbol getGlobalWinner() const { return globalWinner; }
};

#endif // ULTIMATEBOARD_H
