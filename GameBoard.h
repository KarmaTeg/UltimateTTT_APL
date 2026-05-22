#ifndef GAMEBOARD_H
#define GAMEBOARD_H

#include "main.h"

// Who owns a cell : X, O, or nobody
enum class Player {
    NONE,
    X,
    O
};

// One small 3x3 board
struct SmallBoard {
    Player cells[3][3];                               //The type of each cell is Player so it can only contain Player::NONE, Player::X or Player::O
    bool isWon;
    Player winner;

    SmallBoard() {                                                      //The constructor runs automatically the moment the SmallBoard is created
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                cells[r][c] = Player::NONE;                             //sets every cell to NONE at the start
        isWon = false;                                                  //checks if this small board is finished without rechecking all 9 cells every time
        winner = Player::NONE;
    }
};

// The full game : everything needed to make a decision
struct GameState {
    SmallBoard boards[3][3];    // The 9 small boards
    Player bigBoard[3][3];      // tracks who won each small board
    Player currentPlayer;
    bool isBoardForced;         // returns true if next player must play in a forced board
    int forcedRow;              // which small board the next player must play in
    int forcedCol;

    GameState() {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                bigBoard[r][c] = Player::NONE;
        currentPlayer = Player::X;
        isBoardForced = false;
        forcedRow = -1;
        forcedCol = -1;
    }
};

// Where on the board a player plays
struct Move {
    int bigRow, bigCol;                                        //coordinates of the move in the big board
    int smallRow, smallCol;                                    //coordinates of the move in the small board

    // Default constructor that creates an "invalid" move
    Move() {
        bigRow = -1;
        bigCol = -1;
        smallRow = -1;
        smallCol = -1;
    }

    // Normal constructor that creates a move with real coordinates
    Move(int br, int bc, int sr, int sc) {
        bigRow = br;
        bigCol = bc;
        smallRow = sr;
        smallCol = sc;
    }

    // Is this move valid (meaning was it actually set)?
    bool isValid() const {
        return bigRow != -1;
    }

    // Converts our move(flat 0-8 coordinates)
    GameMove toGameMove() const {               //Conversion to (0-8,0-8) coordinates for the setMove()
        GameMove gm;
        gm.row = bigRow * 3 + smallRow;
        gm.col = bigCol * 3 + smallCol;
        return gm;
    }

    // Converts from GameMove to our move                         //Conversion from (0-8,0-8) coordinates from the getMove()
    static Move fromGameMove(const GameMove& gm) {                //static mreans that the function belongs to the type not to the instance
        return Move(gm.row / 3, gm.col / 3,                       //as the instance does't exist yet, the whole goal is to get that instance, move
                    gm.row % 3, gm.col % 3);
    }
};

#endif // GAMEBOARD_H

