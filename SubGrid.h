#ifndef SUBGRID_H
#define SUBGRID_H

#include "Common.h"

class SubGrid {
private:
    Symbol board[3][3];
    Symbol winner;
    int moveCount;

public:
    SubGrid();
    bool play(int r, int c, Symbol s);
    bool isFull() const;
    Symbol getWinner() const { return winner; }
    Symbol getCell(int r, int c) const { return board[r][c]; }
    void checkWinner();
};

#endif // SUBGRID_H
