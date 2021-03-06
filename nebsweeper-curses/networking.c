/*
 * NebSweeper Curses - A server for NebSweeper
 * NebSweeper Curses Copyright (C) 2010, 2011 Flávio Zavan
 *
 * This file is part of NebSweeper Curses.
 *
 * NebSweeper Curses is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NebSweeper Curses is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NebSweeper Curses.  If not, see <http://www.gnu.org/licenses/>.
 *
 * flavio [AT] nebososo [DOT] com
 * http://www.nebososo.com
*/

#include <stdlib.h>
#include <curses.h>

#ifdef __APPLE__
    #include <unistd.h>
    #include <select.h>
#endif

#ifdef WIN32

#define _WIN32_WINNT 0x501

    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <sys/types.h>
    #include <sys/unistd.h>
    #include <netdb.h>
#endif

#include "defs.h"
#include "screen.h"
#include "networking.h"

int startSocket(int *sock, const char *host, unsigned short port){

    unsigned char i;
    struct addrinfo *result;
    struct sockaddr_in sAddr;
    
    //Initialize the socket
    if((*sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        return 1;
    }

    //Resolve
    if(getaddrinfo(host, NULL, NULL, &result)){
        return 2;
    }

    //Copy the ip to sAddr
    sAddr.sin_addr.s_addr =
        ((struct sockaddr_in *) result->ai_addr)->sin_addr.s_addr;
    //Free the memory
    freeaddrinfo(result);

    //Set the rest of the structure
    sAddr.sin_family = AF_INET;
    sAddr.sin_port = htons(port);
    for(i = 0; i < sizeof(sAddr.sin_zero); i++){
        sAddr.sin_zero[i] = 0;
    }

    //Connect
    if(connect(*sock, (struct sockaddr *) &sAddr, sizeof(sAddr))){
        return 3;
    }

    return 0;
}

int exchangeBasics(int sock, unsigned char *players, unsigned char *bombs,
    unsigned char *mines, unsigned char *id, char *name, struct data **pData){

    //Return 0 for failure, or the number of players including you

    char buffer[17];
    unsigned char i, n, t, p, len;

    //Measure the name and copy to the buffer
    for(i = 0; name[i]; i++){
        buffer[i+1] = name[i];
    }
    buffer[0] = i;

    //Send the name
    if(send(sock, buffer, i + 1, 0) <= 0){
        return 0;
    }

    //Receive data
    for(i = 0; i < 6; i += len){
        len = recv(sock, buffer + i * sizeof(char), 6 - i, 0);
        if(len <= 0){
            return 0;
        }
    }

    //Set the data
    *players = buffer[1];
    *mines = buffer[2];
    *bombs = buffer[3];
    *id = buffer[4];
    p = buffer[5];

    //Allocate the structs
    if(!(*pData = (struct data *) malloc(sizeof(struct data) * *players))){
        return 0;
    }

    //Reset the stats, score and the null byte
    for(i = 0; i < *players; i++){
        (*pData)[i].stats = 0;
        (*pData)[i].score = 0;
        (*pData)[i].name[16] = 0;
    }

    //Receive names
    //i = 1 because p includes you
    for(i = 1; i < p; i++){
        //Get the length
        len = recv(sock, buffer, 1, 0);
        if(len <= 0){
            return 0;
        }
        //Get the data
        for(n = 0; n < buffer[0]; n += len){
            len = recv(sock, buffer + (1 + n) * sizeof(char),
                buffer[0] - n, 0);
            if(len <= 0){
                return 0;
            }
        }
        t = buffer[1];
        //Copy name
        for(n = 2; n < buffer[0] + 1; n++){
            (*pData)[t].name[n - 2] = buffer[n];
        }
        (*pData)[t].name[n - 2] = 0;

        //Update the stats
        (*pData)[t].stats = 1;
    }

    return p;
}

int game(WINDOW *left, WINDOW *bottom, WINDOW *textBox, WINDOW *middle,
    int sock, unsigned char id, unsigned char players,
    unsigned char bombs, unsigned char mines, struct data *pData,
    unsigned char field[16][16]){
    
    unsigned char i, toReceive, toDiscard, chatLen, y, x;
    unsigned char msgLen, len, isChatting, turn, bombing;
    //255-64
    char buffer[191];
    //65 to fit a 0 at the end before printing
    char chatBuffer[65];

    fd_set set;

    //Initialize what we have to
    isChatting = 0;
    toReceive = 0;
    toDiscard = 0;
    chatLen = 2;
    bombing = 0;
    msgLen = 0;
    turn = players + 1;
    chatBuffer[1] = 't';
    y = 7;
    x = 7;

    //Windows' timeval
#ifdef WIN32
    struct timeval timeVal;
    timeVal.tv_sec = 0;
    timeVal.tv_usec = 250;
#endif

    //Main loop
    while(mines){
        //Build the set
        FD_ZERO(&set);
#ifndef WIN32
        FD_SET(fileno(stdin), &set);
#endif
        FD_SET(sock, &set);

        //Wait for something to happen
#ifdef WIN32
        if(select(FD_SETSIZE, &set, NULL, NULL, &timeVal) < 0){
#else
        if(select(FD_SETSIZE, &set, NULL, NULL, NULL) < 0){
#endif
            return 1;
        }

        //Read the keyboard first
#ifndef WIN32
        if(FD_ISSET(fileno(stdin), &set)){
#endif
            //Send the message if the handler says it's OK
            switch(keyboardHandler(middle, textBox, &chatLen, &isChatting,
                bombs, &bombing, &y, &x, id, turn, field, chatBuffer)){

                //Chat
                case 1:
                    //Set the size of the message and send it
                    chatBuffer[0] = chatLen - 1;
                    send(sock, chatBuffer, chatLen, 0);
                    //Cleanup
                    isChatting = 0;
                    mvwhline(textBox, 0, 0, ' ', COLS-18);
                    chatLen = 2;
                    wrefresh(textBox);
                    break;

                //Sweep
                case 2:
                    buffer[0] = 2;
                    buffer[1] = 's';
                    buffer[2] = (y << 4) | (x & 15);
                    send(sock, buffer, 3, 0);
                    break;

                //Bomb
                case 3:
                    buffer[0] = 2;
                    buffer[1] = 'b';
                    buffer[2] = (y << 4) | (x & 15);
                    send(sock, buffer, 3, 0);
                    bombs--;
                    updateBombs(left, id, bombs);
                    break;
            }
#ifndef WIN32
        }
#endif

        //Receive data
        //Read the keyboard first
        if(FD_ISSET(sock, &set)){
            //Receive if we can
            if(toReceive){
                len = recv(sock, buffer + (msgLen - toReceive) * sizeof(char),
                    toReceive, 0);

                if(len > 0){
                    toReceive -= len;
                    if(toReceive){
                        continue;
                    }
                }
            }
            //Discard if we have to
            else if(toDiscard){
                len = recv(sock, buffer, toDiscard, 0);
                if(len > 0){
                    toDiscard -= len;
                    continue;
                }
            }
            //Get the length of the new message
            else {
                len = recv(sock, buffer, 1, 0);
                //Get the length if we didn't lose connection
                if(len > 0){
                    //Adjust the values
                    if((unsigned char) buffer[0] > 64){
                        msgLen = 64;
                        toReceive = 64;
                        toDiscard = (unsigned char) buffer[0] - 64;
                    }
                    else {
                        msgLen = (unsigned char) buffer[0];
                        toReceive = (unsigned char) buffer[0];
                    }
                    continue;
                }
            }
            //Lost connection
            if(len <= 0){
                return 1;
            }

            //If we got down here, we're ready to parse

            //Chat
            if(buffer[0] == 't'){
                addLine(bottom, msgLen, buffer, pData);
            }
            //New player
            else if(buffer[0] == 'c'){
                //Copy name
                for(i = 2; i < msgLen; i++){
                    //Make it 7-bit
                    if(buffer[i] < 32){
                        buffer[i] = '?';
                    }
                    pData[(unsigned char) buffer[1]].name[i - 2] = buffer[i];
                }

                //Null terminate
                pData[(unsigned char) buffer[1]].name[i-2] = 0;

                //Update the stats
                pData[(unsigned char) buffer[1]].stats = 1;

                //Add player to the left panel
                addPlayer(left, bottom, (unsigned char) buffer[1], pData);
            }
            //Player left
            else if(buffer[0] == 'l'){
                //Update the stats
                pData[(unsigned char) buffer[1]].stats = 0;

                //Remove from the window
                removePlayer(left, bottom, (unsigned char) buffer[1], pData);
            }
            //New turn
            else if(buffer[0] == 'v'){
                //Update the stats
                turn = buffer[1];

                //Remove from the window
                tellTurn(bottom, turn, pData);

                //Draw the cursor if we're playing now
                if(turn == id){
                    bombing = 0;
                    startCursor(middle, &y, &x, bombing, field);
                }
                //Hide cursor
                else {
                    setValue(middle, y, x, field[y][x] & 15,
                        (field[y][x] & 240) >> 4);
                }
            }
            //Player won
            else if(buffer[0] == 'w'){
                return 0;
            }
            //Reveal
            else if(buffer[0] == 'r'){
                for(i = 1; i < msgLen; i += 2){
                    //Update the matrix
                    field[(buffer[i] & 240) >> 4][buffer[i] & 15] =
                        buffer[i+1];

                    //Update the screen
                    setValue(middle, (buffer[i] & 240) >> 4, buffer[i] & 15,
                        buffer[i+1], turn);

                    //Set the player and update the score
                    if(buffer[i+1] > 8){
                        field[(buffer[i] & 240) >> 4][buffer[i] & 15] |=
                            turn << 4;

                        pData[turn].score++;

                        updateMines(left, --mines);
                    }
                }
                //Update the score on the screen
                updateScore(left, turn, pData);
            }
        }
    }

    return 0;
}
