#ifndef GAMERULES_H
#define GAMERULES_H

#include "GameBoard.h"
#include <vector>

class GameRules {
public:

    // Did a player win a small board ?
    static bool checkSmallWin(const SmallBoard& board, Player player) {       //static says that no instance is needed, it can be called as GameRules::checkSmallWin(...)
    // Check 3 rows
    for (int r = 0; r < 3; r++)
        if (board.cells[r][0] == player &&                                    //board.cell because board is a structure and we need the instances to get to the coordiantes
            board.cells[r][1] == player &&
            board.cells[r][2] == player)
            return true;

    // Check 3 columns
    for (int c = 0; c < 3; c++)
        if (board.cells[0][c] == player &&
            board.cells[1][c] == player &&
            board.cells[2][c] == player)
            return true;

    // Check diagonal top-left to bottom-right
    if (board.cells[0][0] == player &&
        board.cells[1][1] == player &&
        board.cells[2][2] == player)
        return true;

    // Check diagonal top-right to bottom-left
    if (board.cells[0][2] == player &&
        board.cells[1][1] == player &&
        board.cells[2][0] == player)
        return true;

    // None of the 8 lines matched
    return false;
}



    // Did a player win the big board?
    static bool checkBigWin(const Player bigBoard[3][3], Player player) {
    // Check 3 rows
    for (int r = 0; r < 3; r++)
        if (bigBoard[r][0] == player &&                                          //no need for .cell, bigBoard is already a plain 2D array
            bigBoard[r][1] == player &&
            bigBoard[r][2] == player)
            return true;

    // Check 3 columns
    for (int c = 0; c < 3; c++)
        if (bigBoard[0][c] == player &&
            bigBoard[1][c] == player &&
            bigBoard[2][c] == player)
            return true;

    // Check diagonal top-left to bottom-right
    if (bigBoard[0][0] == player &&
        bigBoard[1][1] == player &&
        bigBoard[2][2] == player)
        return true;

    // Check diagonal top-right to bottom-left
    if (bigBoard[0][2] == player &&
        bigBoard[1][1] == player &&
        bigBoard[2][0] == player)
        return true;

    return false;
}


    // Play a move and update the game state
    static void playMove(GameState& state, const Move& move) {
    // 1. Placing the piece
    state.boards[move.bigRow][move.bigCol].cells[move.smallRow][move.smallCol] = state.currentPlayer;

    // 2. Checking if that small board is won
    SmallBoard& sb = state.boards[move.bigRow][move.bigCol];                  //& means modifying sb modifies the actual board.
    if (checkSmallWin(sb, state.currentPlayer)) {                             //sb is a shortcut reference
        sb.isWon = true;
        sb.winner = state.currentPlayer;
        state.bigBoard[move.bigRow][move.bigCol] = state.currentPlayer;
    }

    // 3. Calculating the next forced board
    int nextRow = move.smallRow;
    int nextCol = move.smallCol;

    if (isSmallBoardPlayable(state.boards[nextRow][nextCol])) {
        state.isBoardForced = true;
        state.forcedRow = nextRow;
        state.forcedCol = nextCol;
    } else {
        // That board is already won or full which means free choice
        state.isBoardForced = false;
        state.forcedRow = -1;
        state.forcedCol = -1;
    }

    // 4. Switch the current player
    if (state.currentPlayer == Player::X)
        state.currentPlayer = Player::O;
    else
        state.currentPlayer = Player::X;
}


    // Get all legal moves in the current state
    //check if a board is playable
    static bool isSmallBoardPlayable(const SmallBoard& board) {
        if (board.isWon) return false;

        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                if (board.cells[r][c] == Player::NONE)
                    return true;
        return false;
    }
    //Collecting the moves

    static std::vector<Move> getLegalMoves(const GameState& state) {                      //Initialization
        std::vector<Move> moves;

        if (state.isBoardForced) {
            // if forced then only look in that one small board
            const SmallBoard& board = state.boards[state.forcedRow][state.forcedCol];    //creates a reference to a specific small board
            for (int sr = 0; sr < 3; sr++)
                for (int sc = 0; sc < 3; sc++)
                    if (board.cells[sr][sc] == Player::NONE)
                        moves.push_back(Move(state.forcedRow, state.forcedCol, sr, sc));  //adds the move to the end of that list.
        } else {
            // if free then look in all playable small boards
            for (int br = 0; br < 3; br++)
                for (int bc = 0; bc < 3; bc++)
                    if (isSmallBoardPlayable(state.boards[br][bc]))                        //check if it's playable
                        for (int sr = 0; sr < 3; sr++)
                            for (int sc = 0; sc < 3; sc++)
                                if (state.boards[br][bc].cells[sr][sc] == Player::NONE)
                                    moves.push_back(Move(br, bc, sr, sc));
        }

        return moves;
    }




    // Is the game finished?
    static bool isGameOver(const GameState& state) {
        if (getWinner(state) != Player::NONE) return true;                      //if someone won the big board
        if (getLegalMoves(state).empty()) return true;                          //if no moves are left
        return false;
    }


    // Who won the game? Returns Player::NONE if nobody yet
    static Player getWinner(const GameState& state) {
    if (checkBigWin(state.bigBoard, Player::X)) return Player::X;
    if (checkBigWin(state.bigBoard, Player::O)) return Player::O;
    return Player::NONE;
}

};

#endif // GAMERULES_H


