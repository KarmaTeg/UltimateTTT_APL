#include "main.h"
#include "GameBoard.h"
#include "GameRules.h"
#include "MyIA.h"
#include <iostream>

int main()
{
    game.initialize(10, Level::MEDIUM_2, Mode::ARENA, false, "APL");

    while (!game.isAllGameFinish())
    {
        // New board at the start of each new game
        GameState state;

        while (!game.isFinish())  // repeat until each single game is done
        {
            // Get the opponent's move
            // The engine fills iaGameMove with where the opponent just played
            // If the opponent hasn't moved yet (we go first), row stays -1
            GameMove iaGameMove;
            game.getMove(iaGameMove);

            if (iaGameMove.row != -1) {
                // Convert from flat (0-8) to our internal format
                Move iaMove = Move::fromGameMove(iaGameMove);
                // Update our internal board so we know where they played
                GameRules::playMove(state, iaMove);
            }

            // Check if game ended after the opponent's move
            if (game.isFinish()) break;

            // Compute our best move
            // MyIA looks at the current state and returns the best move
            Move myMove = MyIA::getMove(state);

            // Update our internal board with our own move
            GameRules::playMove(state, myMove);

            // Send our move to the engine
            // Convert from our internal format back to flat (0-8)
            GameMove myGameMove = myMove.toGameMove();
            game.setMove(myGameMove);
        }

        // Print result of this game
        Winner w = game.getWinner();
        if      (w == Winner::PLAYER)        std::cout << ">>> YourPseudo won\n";
        else if (w == Winner::IA)            std::cout << ">>> IA won\n";
        else if (w == Winner::IA_AND_PLAYER) std::cout << ">>> Draw\n";
    }

    return 0;
}
