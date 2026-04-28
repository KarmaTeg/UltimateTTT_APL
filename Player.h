#ifndef PLAYER_H
#define PLAYER_H

#include "UltimateBoard.h"
#include <string>

class Player {
protected:
    std::string name;
    Symbol symbol;

public:
    Player(std::string n, Symbol s) : name(n), symbol(s) {}
    virtual ~Player() = default;
    virtual Move chooseMove(const UltimateBoard& board) = 0;
    Symbol getSymbol() const { return symbol; }
    std::string getName() const { return name; }
};

#endif // PLAYER_H
