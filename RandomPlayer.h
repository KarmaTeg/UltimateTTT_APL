#ifndef RANDOMPLAYER_H
#define RANDOMPLAYER_H

#include "Player.h"

class RandomPlayer : public Player {
public:
    RandomPlayer(std::string n, Symbol s) : Player(n, s) {}
    virtual Move chooseMove(const UltimateBoard& board) override;
};

#endif // RANDOMPLAYER_H
