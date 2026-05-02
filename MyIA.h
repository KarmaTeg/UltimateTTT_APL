#ifndef MYIA_H
#define MYIA_H

#include "GameBoard.h"
#include "GameRules.h"
#include <vector>
#include <cstdlib>


// Pour l'instant l'IA choisit les coups au hasard, il n'y a aucune stratégie

class MyIA {
public:

    // The function that returns the best move for now randomly
    static Move getMove(const GameState& state) {
    std::vector<Move> legalMoves = GameRules::getLegalMoves(state);

    // Safety check — should never happen because should have been finished but checks if the list is empty
    if (legalMoves.empty())
        return Move();  // returns an invalid move with -1 coordinates

    int randomIndex = rand() % legalMoves.size();                                      //legalMoves.size returns how many moves are in the list to
    return legalMoves[randomIndex];                                                    //get a random valid index into that list
}

};

#endif // MYIA_H


